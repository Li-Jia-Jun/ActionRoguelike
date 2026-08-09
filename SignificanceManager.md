# Actor Bucketing SignificanceManager

## Why this matters
Engine built-in LODs handle meshes, skeleton and animation tick rate. What's unhandled is the game
logic on the CPU. This project leverages the Significance Manager plugin to achieve a
significance-based bucketing system, allowing AI enemies to LOD their game logic — BT tick, AI
perception, movement, animation update rate, physics-body sync, and more.

For the end-to-end optimization story and the profiling that justified each reaction, see
[AICrowdOptimization.md](AICrowdOptimization.md).

---

## Implementation

### Data flow at a glance

```
URogueGameViewportClient::Tick        (every frame, gathers player viewpoints)
        │  Update(Viewpoints)
        ▼
URogueSignificanceManager::Update
        │  every Nth tick ── UpdateCalculation()   (recompute + bucket + queue changes)
        │  other ticks    ── UpdateLOD()           (drain queue, apply a capped batch)
        ▼
IRogueSignificanceInterface::SignificanceLODChanged(NewLOD)   (per-agent reaction)
        ▼
ARogueAICharacter                     (throttles movement / BT / anim / physics per tier)
```

Key source:
[RogueSignificanceManager](Source/ActionRoguelike/Significance/RogueSignificanceManager.cpp) ·
[RogueSignificanceDeveloperSettings](Source/ActionRoguelike/Significance/RogueSignificanceDeveloperSettings.h) ·
[RogueSignificanceInterface](Source/ActionRoguelike/Significance/RogueSignificanceInterface.h) ·
[RogueGameViewportClient](Source/ActionRoguelike/Significance/RogueGameViewportClient.cpp) ·
[RogueAICharacter](Source/ActionRoguelike/AI/RogueAICharacter.cpp)

---

### 1. The manager: a per-world subclass

`URogueSignificanceManager` extends the engine's `USignificanceManager`. The engine creates **one
instance per game world** and picks the subclass from config:

```ini
; Config/DefaultEngine.ini
[/Script/SignificanceManager.SignificanceManager]
SignificanceManagerClassName=/Script/ActionRoguelike.RogueSignificanceManager
```

Because it is a plain `UObject` (not a subsystem), it has **no `BeginPlay`**. But it *is* created
during world init (before actors begin play) with the world as its outer, so we get a
BeginPlay-equivalent by binding to the world delegate in the constructor — guarded against the CDO,
whose outer is not a world:

```cpp
URogueSignificanceManager::URogueSignificanceManager()
{
    if (HasAnyFlags(RF_ClassDefaultObject)) { return; }      // CDO has no world outer
    if (UWorld* World = GetWorld())
    {
        if (World->HasBegunPlay()) { OnWorldBeginPlay(); }   // late-created safety
        else { World->OnWorldBeginPlay.AddUObject(this, &URogueSignificanceManager::OnWorldBeginPlay); }
    }
}
```

`OnWorldBeginPlay` simply resets the update counter and the pending-change queue. State is
per-world, so a new level gets a fresh manager automatically.

### 2. Extended per-object info

The base plugin stores an `FManagedObjectInfo` per registered object. We extend it to carry LOD
state:

```cpp
struct FExtendedManagedObjectInfo : USignificanceManager::FManagedObjectInfo
{
    int32 LOD;                      // current LOD
    int32 NewLOD;                   // target LOD, applied later by the apply phase
    float LastLODChangeTimeInSeconds;
    bool  bPendingLODChange;        // true while queued (dedup + safe removal)
};
```

`RegisterObject` is overridden to `new` the extended struct and hand it to the protected
`RegisterManagedObject`. Deleting through the base pointer is safe because the base has a virtual
destructor.

### 3. Opt-in: how an agent participates

An agent participates by implementing `IRogueSignificanceInterface` (one method,
`SignificanceLODChanged(int32 NewLOD)`) and carrying a `SignificanceTag`. Registration is
**gated by the developer settings**, so the tag doubles as a per-class opt-in switch — an agent
only registers if its tag is configured:

```cpp
// ARogueAICharacter::RegisterWithSignificanceManager (BeginPlay)
if (SignificanceTag.IsNone()) return;
if (Settings->FindBucketInfo(SignificanceTag) == nullptr) return;   // not opted in
SignificanceManager->RegisterObject(this, SignificanceTag, /*significance fn*/ ...);
```

`EndPlay` unregisters (tracked by a bool so it only fires when actually registered).

#### The significance function convention

Registration supplies a lambda that returns a float per viewpoint. Convention: **higher = more
significant.** We use negative squared distance so *nearer* is higher, and halve it when the agent
was recently rendered so *on-screen* agents rank above off-screen ones at the same distance:

```cpp
float NegDistSq = -FVector::DistSquared(Actor->GetActorLocation(), Viewpoint.GetLocation());
if (Actor->WasRecentlyRendered()) { NegDistSq *= 0.5f; }   // less negative => higher => more significant
return NegDistSq;
```

The base manager evaluates this against each viewpoint and keeps the **max**, then sorts each tag's
array **descending** — so **index 0 is the nearest / most significant** agent. (Squared distance
avoids a `sqrt`; only ordering matters for bucketing.)

### 4. Configuration (developer settings)

[`URogueSignificanceDeveloperSettings`](Source/ActionRoguelike/Significance/RogueSignificanceDeveloperSettings.h)
(a `Config=Game` `UDeveloperSettings`) exposes everything live-tunable:

| Setting | Meaning |
|---|---|
| `SignificanceBuckets` | Per-tag `{ Tag, BucketSizes[] }`. `BucketSizes` are agent counts per LOD, nearest first. |
| `LODChangeFreezeInterval` | Minimum seconds between LOD changes for one agent (anti-thrash). Default 0.5. |
| `MaxLODChangesPerUpdate` | Hard cap on LOD changes applied per apply-frame (spike protection). Default 100. |
| `SignificanceUpdateTickInterval` | Heavy pass runs every N ticks; ≥2 so a cycle always has an apply frame. Default 3. |

`FindBucketInfo(Tag)` is the opt-in lookup used above. Settings are read live each pass (a
`GetDefault<>()` CDO fetch is effectively free), so these can be tuned in PIE without a restart.

### 5. Driving the update

The engine does **not** tick the significance manager. A custom
[`URogueGameViewportClient`](Source/ActionRoguelike/Significance/RogueGameViewportClient.cpp)
(registered via `GameViewportClientClassName` in `DefaultEngine.ini`) calls `Update` every frame,
feeding it the local players' camera viewpoints:

```cpp
void URogueGameViewportClient::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    // gather PlayerController::GetPlayerViewPoint() per local player -> Viewpoints
    SignificanceManager->Update(Viewpoints);
}
```

`Update` is called every tick, but the *heavy* work is throttled internally (next section).

### 6. Two-phase update — amortize across frames

```cpp
void URogueSignificanceManager::Update(...)
{
    ++UpdateCounter;
    const int32 Interval = FMath::Max(2, Settings->SignificanceUpdateTickInterval);
    if (UpdateCounter % Interval == 0) { UpdateCalculation(Viewpoints); }  // heavy
    else                               { UpdateLOD(Viewpoints); }          // light
}
```

- **`UpdateCalculation` (every Nth tick)** — the expensive pass: `Super::Update()` recomputes every
  agent's significance and re-sorts each tag's array, then we walk the sorted array and assign each
  agent a **target** LOD from the bucket sizes. It only *marks* work; it doesn't fire callbacks.
- **`UpdateLOD` (other ticks)** — the cheap pass: apply a bounded batch of the marked changes.

Splitting the passes keeps the heavy recompute and the callback burst off the *same* frame, so peak
frame cost is `max(calc, apply)` rather than their sum. `Interval` is clamped `>= 2` so at least one
apply frame always exists to drain the queue.

### 7. Bucketing — position maps to LOD

In `UpdateCalculation`, for each configured tag we walk the significance-sorted array and assign LOD
by **rank**, using the accumulated bucket sizes:

```
BucketSizes = [50, 150, 300]      (for one tag)
sorted index:   0..49   -> LOD 0   (nearest / most significant)
               50..199  -> LOD 1
              200..499  -> LOD 2
              500..end  -> LOD 2   (overflow clamps to the last/most-culled LOD)
```

Assigning by *rank* rather than absolute distance is what makes this robust in the top-down
worst case: even when every agent is on screen and at similar distance, only the top bucket ever
pays full cost — the rest are degraded by their position in the sorted list.

### 8. The cursor queue — spread cost, stay allocation-free

Marked changes go into `PendingLODChanges` (a `TArray<FExtendedManagedObjectInfo*>`) via
`MarkTargetLOD`, which refreshes the target, dedups (`bPendingLODChange`), and applies the freeze
gate before enqueuing.

`UpdateLOD` drains from a **cursor** rather than removing from the front:

```cpp
while (PendingCursor < PendingLODChanges.Num() && AppliedChanges < MaxLODChanges)
{
    FExtendedManagedObjectInfo* Info = PendingLODChanges[PendingCursor++];
    Info->bPendingLODChange = false;
    if (Info->LOD == Info->NewLOD) { continue; }         // stale (target returned to current)
    Cast<IRogueSignificanceInterface>(Info->GetObject())->SignificanceLODChanged(Info->NewLOD);
    Info->LOD = Info->NewLOD;
    Info->LastLODChangeTimeInSeconds = NowInSeconds;
    ++AppliedChanges;                                    // budget counts real changes only
}
if (PendingCursor >= PendingLODChanges.Num()) { PendingLODChanges.Reset(); PendingCursor = 0; }
```

Why the cursor:
- **O(applied) per frame, no per-item shifting.** Removing from the front of a `TArray` is O(n);
  the cursor avoids it entirely.
- **Cap counts *real* changes** (increment is inside the apply branch), so a queue full of
  already-satisfied entries can't starve the ones that still need work.
- **Lazy compaction.** A fully-drained queue is `Reset()` here (eager). A partially-drained queue is
  compacted once at the top of the next `UpdateCalculation` via `RemoveAt(0, PendingCursor)` — a
  single shift per cycle instead of one per apply frame.
  - This compaction **must** be `RemoveAt(0, cursor)`, *not* `Reset()`: the un-applied tail still has
    `bPendingLODChange == true`, and wiping the array would strand those flags so `MarkTargetLOD`'s
    dedup check would refuse to re-queue them.

Net effect: LOD changes drain FIFO across the frames between heavy passes, bounded per frame by
`MaxLODChangesPerUpdate` — so a churn spike (e.g. a fast camera sweep through the crowd) never
applies hundreds of expensive reactions in a single frame.

### 9. Freeze window & change cap

- **Freeze window** (`LODChangeFreezeInterval`): `MarkTargetLOD` refuses to re-queue an agent whose
  last change was too recent. This kills flicker at bucket boundaries (an agent oscillating between
  two tiers as it straddles an edge). Freeze uses wall-clock `GetTimeSeconds()`, so it is unaffected
  by the update cadence.
- **Change cap** (`MaxLODChangesPerUpdate`): the hard per-frame ceiling on applied changes — the
  spike guarantee. Anything over budget waits for the next apply frame.

### 10. Lifetime safety — no dangling pointers

The base `USignificanceManager::UnregisterObject` **`delete`s** the `FManagedObjectInfo`. If an agent
dies while queued, that pointer would dangle in `PendingLODChanges`. The override removes it from the
queue *before* calling `Super`, fixing up the cursor if the removed entry was before it:

```cpp
if (ExtInfo->bPendingLODChange)
{
    const int32 Found = PendingLODChanges.IndexOfByKey(ExtInfo);
    if (Found != INDEX_NONE)
    {
        PendingLODChanges.RemoveAt(Found, 1, EAllowShrinking::No);
        if (Found < PendingCursor) { --PendingCursor; }
    }
    ExtInfo->bPendingLODChange = false;
}
Super::UnregisterObject(Object);
```

The `bPendingLODChange` guard means the common death path (agent not queued) pays only a flag check.

---

## Per-agent reactions

`SignificanceLODChanged(NewLOD)` on [`ARogueAICharacter`](Source/ActionRoguelike/AI/RogueAICharacter.cpp)
maps the tier to concrete degradations (all measured — see
[AICrowdOptimization.md](AICrowdOptimization.md)):

| Reaction | LOD 0 (near) | Far tiers |
|---|---|---|
| Physics-body sync (`KinematicBonesUpdateType`) | `SkipSimulatingBones` | `SkipAllBones` |
| Movement mode | `MOVE_Walking` | `MOVE_NavWalking` |
| Movement tick interval | every frame | throttled (0.08–0.15 s) |
| RVO avoidance | on | off (capsule collision still blocks) |
| BehaviorTree tick interval | every frame | 0.25–0.5 s |
| AIController tick interval | every frame | 0.15–0.3 s |
| Animation update rate (URO) | every frame | via forced LOD + `LODToFrameSkipMap` |
| Render mesh LOD | 0 | forced to tier (`SetForcedLOD`) |

The forced mesh LOD does double duty: it also feeds URO's LOD-map rate selection, so animation
throttles by *significance* instead of the default screen-size heuristic (which never throttles when
the whole crowd is on screen).

---

## Extending

- **Add a participating class**: implement `IRogueSignificanceInterface`, give it a
  `SignificanceTag`, add a matching entry in `SignificanceBuckets`, and register in `BeginPlay`.
- **Add a tier**: append to a tag's `BucketSizes` and handle the new LOD in `SignificanceLODChanged`.
- **Add a reaction**: extend `SignificanceLODChanged` — the manager already delivers the tier; the
  agent decides what to degrade.
