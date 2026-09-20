# Climbing — Surface & Animation Design

Design decisions for the Mover-based climbing system, agreed before implementation.
Following Vitor Cantão's tutorial *in concept*, translated from CMC to Mover.

## Detection (done)
- Each pre-sim-tick, a forward capsule sweep (`SweepAndStoreWallHits`) stores `CurrentWallHits` — a cheap "is anything in front?" check.
- `SweepMultiByChannel` returns one hit per *component*, so it is detection only, not coverage.

## Validation — 3×5 grid
- Validate climbability by sampling a **3 wide × 5 tall grid** (hip → reach) of short forward line traces over the climb footprint, not by trusting a single hit or ray.
- Per sample: must hit `ECC_WorldStatic` within reach, and its normal must pass the steepness (near-vertical) + **horizontal** facing (≤ `MinHorizontalDegreesToStartClimbing`; the forward is flattened, so a wall's tilt doesn't shrink the angle and reject a dead-on climber) test (`IsSurfaceClimbable`).
- **Wall vs walk gate (`IsSurfaceClimbable`, shared by all climbing actions).** A surface is a WALL when it leans ≤ `MaxWallTiltFromVerticalDegrees` (target **±40°**, stored in degrees; internally `|dot(normal, Up)| ≤ sin(that)`) **and** is not walkable (`dot(normal, Up) < MantleMinWalkableDot`). The two thresholds must stay **ordered** — wall lean (sin 40° ≈ 0.64) below the walkable slope (0.7 ≈ 44° from vertical) — with a small dead-band between; that ordering *is* the gate (a wall threshold at/above the walkable one lets flat tops classify as walls). All climb, mantle-up, and mantle-down detection support this ±40° range.
- **Grid basis follows the surface.** The grid stacks along the **wall** while climbing (`WallUp` / `WallRight` from the cached plane) and along **world up** at entry (character upright, no plane cached yet). So on a tilted wall the rows keep a constant standoff from the face instead of drifting off it — a world-up grid would walk the top rows off a steep face (past trace reach) and lose coverage.
- **Coverage gate:** `validSamples / total ≥ MinCoverageRatio` (start forgiving, ~0.6) — this is the real fix for notches / fragmented / gappy surfaces.
- **Consistency gate:** every valid normal within ~35° of the average normal — rejects corners and lumpy geometry.
- Output the **dominant plane** = averaged valid normal + averaged valid point (drives the mode), and keep the per-sample results for animation.

## Design stance: forgiving + snapping
- Thresholds lean lenient; the character snaps to the dominant plane rather than conforming exactly. Tighten only if players climb things they shouldn't.

## State classification — only lower-body hang
- Coverage picks a **coarse body state**, never per-limb states: **braced (no hang)** vs **lower-body hang**. There is intentionally no "left foot / right arm hanging" state.
- `LowerBodySupport` = fraction of the bottom rows that hit → drives the hang blend as a **continuous, time-smoothed** scalar (not a hard bool), so climbing over a lip eases into the dangle.
- The same grid also yields future states for free: top rows miss → mantle/top-out; one side misses → edge/reach.

## Data split: sim vs anim
- **Sim** (climb mode / sync state): minimal — dominant normal + point only.
- **Anim** (component, game-thread, like `CurrentWallHits`): full per-sample grid + `LowerBodySupport` (and Upper/Left/Right) scalars.

## Per-limb refinement — IK on top (Control Rig)
- The body state is coarse; individual hands/feet are resolved by **IK, not by more states or finer coverage**.
- Each foot/hand runs a **dedicated probe** (trace toward the wall along the surface normal), separate from the coverage grid; the grid can *bias* the search toward cells it already marked solid.
- Found a target → Control Rig two-bone IK plants there, searching a small region and snapping to the nearest valid spot (e.g. a notch edge); IK weight + position are `FInterpTo`'d with hysteresis to avoid popping.

## Procedural tuck — floating-limb fallback
- If a limb's probe finds no reachable target → blend that limb's IK weight to 0, so it falls back to the authored FK pose (≈ the wall plane); there is no separate "in-air" clip.
- Add a small **additive/procedural tuck** (pull the foot toward the body/wall) so a floating foot reads as reaching, not frozen over open air.
- This only ever covers small, unnoticeable single-limb misses; a whole-lower-body void is caught earlier by the **hang state**, not by FK-fallback.

## Target pipeline (Mover-native)
`Climb mode (state + GetPredictedTrajectory)` → `PoseSearch / Motion Matching picks clip` → `Motion Warping aligns root to target` → `Control Rig IK refines limbs` → `procedural tuck fallback`.

## Animation ↔ movement sync (foot-lock + surge)

**Problem.** The climb *mode* moves the capsule at a smooth, near-constant velocity, but a climb clip's motion is a burst rhythm — *reach (body ~still) → pull (body surges) → reach…*. Constant body velocity against a bursty animation makes hands/feet slide on the wall. Fixed in two layers.

### Step A — playrate sync (matches the loop *average*)

**Reasoning chain (the part to remember):** we want a gripped hand to stay world-still, or it skates → `v_world = 0`. Its world motion is the body plus the hand-relative motion the clip drives; in an in-place clip the planted hand slides past the root at `-R · V_clip`. For the two to cancel we need `R · V_clip = v_body`. `V_clip` is **fixed** (baked into the clip, can't change at runtime), but `v_body` **scales with input** → so `R` is the only free term, and it must scale with input too.

```
v_world = v_body + v_relative,   v_relative = -R · V_clip     // along the climb axis
v_world = 0            ⇒  R · V_clip = v_body                 // the anim must cancel the body
v_body  = fraction · ClimbMaxSpeed    (scales with input)
V_clip  = const                       (fixed by the clip)
⇒ R = v_body / V_clip = fraction · (ClimbMaxSpeed / V_clip)
        └ input part ┘   └──── scale ────┘
```

So `playrate = GetClimbMoveSpeedFraction() · scale`, with `scale = ClimbMaxSpeed / V_clip` (≈1 when `ClimbMaxSpeed ≈ V_clip`). `V_clip` = contacts' stride ÷ loop time (empirical).

**Limit:** `R` is one scalar but `V_clip` varies within the loop (slow reach, fast pull), so this cancels only the loop *average* — a residual within-cycle slide remains → Step B.

### Step B — velocity surge (matches the *instant*)

Vary body speed with the cycle to track the clip's contact speed:

```
v_body(phase)  = baseSpeed · cadence(phase),   mean(cadence) = 1
cadence(phase) = curve(phase) / ClimbCadenceReferenceSpeed
// baseSpeed  = fraction · ClimbMaxSpeed. It is what gameplay wants: input amount × your tuned cap
```

`curve` = a float curve authored **on each clip** (`MovementSpeed`): ~0 at each reach, peak at each pull. The mode reads the *blended* value each tick and scales along-wall velocity by it. Riding on the anim, it is:

- **phase-locked** — the body's pull lands on the anim's pull; no separate sim phase to drift,
- **auto-shaped** — the clip's asymmetric two pulses (big, then small ~⅓) are baked in, no hand-tuning,
- **blendspace-correct** — direction curves blend, so diagonals stay consistent (a curveless direction dilutes the blend → *every* clip needs the curve).

`mean(cadence) = 1` keeps average speed `= baseSpeed`; the Step-A residual now cancels *every instant*.

**Reference speed.** `MovementSpeed` is absolute cm/s; set `ClimbCadenceReferenceSpeed` = its average (root travel ÷ loop time) to make it mean-1. One shared reference across directions preserves the clips' *relative* speeds.

**Deadlock caveat.** Playrate must advance from a *steady* speed, not the surged one — else `cadence→0 ⇒ speed→0 ⇒ playrate→0` freezes the anim at the reach:

```
playrate = GetClimbMoveSpeedFraction() · scale   // steady round push amount, NOT |GetClimbMoveIntent()|
v_body   = surged (Step B)
```

### Input geometry — round stick → square blendspace

Intent is a **round** unit direction (full diagonal = `(0.707, 0.707)`); the blendspace is a **square** with diagonal clips at corners `(±1, ±1)`. The circle reaches only 70.7% toward a corner, so diagonal clips never fully weight in. `CircleToSquare()` pushes the intent out to the square along the same ray, preserving push amount (swaps L2 for L∞, so `max(|X|,|Y|) == inputLen`):

```
dir = in / |in|
out = dir / max(|dir.x|, |dir.y|) · |in|    // (0.707,0.707)→(1,1);  (1,0)→(1,0);  half stays half
```

Three consumers, kept separate:

```
blendspace AXES     ← GetClimbMoveIntent()         // square  (which clip)
blendspace PLAYRATE ← GetClimbMoveSpeedFraction()  // round scalar = min(1, |round intent|)   (how fast)
physics DIRECTION   ← WallMoveDir (normalized)     // round    (so diagonals aren't √2× faster)
```

`|GetClimbMoveIntent()|` would be `√2` on a diagonal → plays 1.41× too fast (skate), though the body moves at `ClimbMaxSpeed` in every direction. The square remap is *anim-only*.

### Code touchpoints
- `GetClimbCadenceScale()` — reads `ClimbCadenceCurveName` off the mesh, ÷ `ClimbCadenceReferenceSpeed`; returns 1 when absent.
- `RogueClimbMode::GenerateMove` — `LinearVelocity *= GetClimbCadenceScale()` (along-wall only; into-wall bias unscaled).
- `GetClimbMoveIntent()` — round intent (`ComputeRoundClimbWallIntent`) → `CircleToSquare` → blendspace **axes**.
- `GetClimbMoveSpeedFraction()` — `min(1, |round intent|)` → blendspace **playrate**.
- Playrate wiring + per-clip curves are AnimBP / asset-side.

### Why scalar, not full root motion
The clip drives along-wall *magnitude* only; the mode keeps *direction* (wall plane) + player control. Full root-motion climb fights Mover (fixed-tick + rollback + looping blendspace) and arbitrary geometry for a cadence this curve already gives. Determinism is a *networking* concern, moot for single-player / Standalone. Root motion + motion warping is reserved for **discrete traversals** (mantle, corner turns) on known geometry.

## Traversals: mantle & corner turns (hybrid)
- Sustained climb stays **procedural** (grid → dominant plane → orient + IK, BOTW-like); mantle and corner turns are **authored, motion-warped root-motion clips** (AC/Uncharted style, matching the anim packs). BOTW-style tutorials only cover sustained climb + a basic top-out, not these authored clips.
- All three share one **skeleton**: grid boundary flag → directed probe finds the target transform → pick clip (chooser or Pose Search) → Motion Warp to target → play as an `AnimRootMotionLayeredMove` (input locked, lip collision relaxed) → hand off to climb/walk.
- **Mantle:** top rows miss → up-and-over probe finds lip + landing → warp + root motion up and over.
- **Corner turns:** a side column misses → side probe classifies **inner** (concave, perpendicular wall also hits) vs **outer** (convex, face wraps away) → matching clip warped to the new face → resume climb on the new dominant normal.
- Reference: UE5 **Game Animation Sample** traversal system (chooser + Motion Warping), not a BOTW tutorial.

## Mantle — implementation (done)

Top-out from climbing: push **up** at a ledge → an authored **root-motion** montage, **motion-warped** onto the real lip + landing, ending in **Walking** on top. Works on **tilted walls** (leaning toward or away). Built as a distinct `URogueMantleMode` + `URogueMantleTransition`, so `IsClimbing()` is false during it → the climb-exit-to-Falling *fling* can't fire. Motion = a `FLayeredMove_AnimRootMotion` (`OverrideAll`, self-terminating when the montage stops). MotionWarping works on Mover **without CMC**: `UMoverComponent` auto-wires a `UMotionWarpingMoverAdapter` when the actor owns a `UMotionWarpingComponent`. Completion is read from the sync state — `!LayeredMoves.HasMove<FLayeredMove_AnimRootMotion>()` → `GroundMovementModeName` (Walking).

### The probe: wall-relative face, world-space landing

Key to tilted walls: **probe the climb FACE in wall-relative terms, but evaluate the LANDING in world/gravity space** (the character tops out standing upright). One rule separates "face vs top" everywhere — `dot(surfaceNormal, Up) < MantleMinWalkableDot` = wall face, else walkable top. `RefreshMantleProbe` runs each frame while climbing as the cheap **arming gate**:

- **Overhead** — traces **perpendicular into the face** (`-WallNormal`, *not* horizontal) at the top of the ledge band. Only a *steep* hit blocks (face still climbing = tall wall, no lip); empty or a *walkable* hit both mean the face ended into a lip. Perpendicular keeps the clearance a consistent ~capsule-radius across tilts (a horizontal ray's gap varies with tilt and shoots *under* an overhang).
- **Landing** — a down-trace finds the walkable top, then a **downward capsule sweep** (FindFloor-style) drops the real capsule and takes where it *rests*: settles onto a sloped top without the false self-overlap of a static overlap test, still stops on real obstacles.
- **Height gate** — the rest's rise measured **along the wall** (`WallUp`), so the trigger window is a **tilt-invariant along-wall span** ("has the character climbed up to the lip" is an along-wall question; world-up compresses the window on a steep lean).

### Precise lip at commit (decoupled from the landing)

The cheap probe only *arms* the mantle; at commit `ComputePreciseMantleEdge` **binary-searches up the face** (perpendicular rays, a few lateral samples, midpoint-corrected) to localize the **lip** — independent of the landing, so a lip *higher* than the walk surface works. This is the `MantleUp` target. Perf↔precision knobs: `MantleLipScanIterations` (halves the error each step) × `MantleLipLateralSamples`, run once. (Same `Feet` origin — with `GetClimbSampleUpBias` — as the arming gate, or the band misaligns and the search caps below the lip.)

### Steep tilts: feet along the wall, landing inland of the lip (the 30–40° top-out fix)

Supporting walls that lean up to `MaxWallTiltFromVerticalDegrees` (±40°) exposed **three coupled world-up assumptions** in the probe/scan. Each only bites as the tilt grows, and each is a **no-op on a vertical wall** (`WallUp == Up`, `sin 0 = 0`). Symptom before the fix: on 30–40° walls leaning *away*, `ComputePreciseMantleEdge` drew **neither the cyan nor the white sphere** (it returned false) and the character mantled **into the air**.

1. **Feet reference — `WallUp·CapHH`, not `Up·CapHH` (Fix A).** The probe used `Feet = center − Up·CapHH`, but the *climbing* capsule is **tilted to the wall**, so its real bottom is `center − WallUp·CapHH`. World-up put every scan-ray origin at perpendicular distance **`CapR − CapHH·sin(tilt)`** off the face, which **crosses 0 near 40°** (`CapR/CapHH`), so the `−WallNormal` rays start on/behind the face and never register an on-face hit → `LipCount 0` → false → no spheres. `WallUp·CapHH` keeps the origin a constant `CapR` off the face at any tilt. (Applied in **both** `RefreshMantleProbe` and `ComputePreciseMantleEdge`.)

2. **Lip band — the landing is inland of the lip (Fix B, the dominant bug; explains the 30° "sometimes").** The landing (`RestFeet`) is found `Reach` (`≈ CapR + MantleForwardReach`, ~99cm) *horizontally inland* of the lip, and `dot(IntoWall, WallUp) = sin(tilt)`, so **along the wall the landing sits `Reach·sin(tilt)` above the lip**. But the arming gate measures to the landing while the precise scan searches for the *lip* in the same `[Min, Max]` band — so by 40° the lip drops below the `Min` floor and every probe lands above it → false. Fix: **widen the scan bracket by `Reach·sin(tilt)`** in the tilt's direction so it straddles the lip. This is **capsule-size-independent**, which is why it fired even at tilts where the perp bug (A) hadn't yet.

3. **Fallback edge — the literal "in air" (Fix C).** When the scan legitimately fails and `BeginMantle` falls back to `MantleEdgeTransformCache`, its `EdgeUp` carried the same inland offset → the phase-1 `MantleUp` target floated `~Reach·sin(tilt)` **up over the ledge** → mantle into the air. Subtract the inland rise so the fallback edge sits on the lip.

**Unifying rule:** probe the FACE wall-relative (origins at the tilted capsule's real bottom `−WallUp·CapHH`; rays `−WallNormal`), and remember the **landing is inland of the lip by `Reach·sin(tilt)`** — while the landing/fit itself stays world-vertical (the character tops out upright). `B`/`C` use `Reach·sin(tilt)` (exact on a flat top); `B` is robust because it only *widens* the bracket. **Tuning follow-up:** Fix A drops the `Feet` reference by `CapHH·(1 − cos tilt)` (~16cm at 40°), so the `[MantleMinLedgeHeightFromFeet, Max]` window drifts slightly on tilts — nudge `MantleMinLedgeHeightFromFeet` if arming shifts. **Secondary (unfixed, watch):** the *landing* capsule center still assumes the upright blend has completed before the forward warp pins the feet — the same feet→center reconstruction lesson as *Mantle down*; tune via the upright-blend event/`MantleUprightBlendSpeed` if steep tops-out sit high or into the ground.

### Two warp targets — trace the corner, don't cut it

Two named SkewWarp windows so the montage goes up *then* over (a single landing target cuts a diagonal through the wall corner):

```
MantleUp      → the lip      (precise scan)
MantleForward → the landing  (sweep rest, on top)
```

`BeginMantle()` sets both (`AddOrUpdateWarpTargetFromTransform`). **SkewWarp warps the ENDPOINT, not the path** — the in-between is the clip's own root motion skewed, so forward bleeds into the up-window unless the window split is early enough (primary lever; `MantleEdgeUp`/`MantleEdgeBackward` nudge the target). **Overshoot caveat:** a very short window with no speed clamp overshoots (all remaining translation crammed into little time → huge velocity); fix = longer window or clamp.

### Capsule wall-align + event-gated upright blend

During climb the capsule **tilts to lie against the wall** (`URogueClimbMode` orients to `MakeFromZX(WallUp, -WallNormal)`, gated by `bAlignToClimbSurface`), and the detection sweep/grid shift up **along the wall** (`GetClimbSampleUpBias`, not world-up) so they stay on a receding tilted face. The mantle **inherits that tilt** and eases to vertical (`QInterpTo`, `MantleUprightBlendSpeed`) **only when the montage fires the upright-blend event** (`MantleUprightBlendEventTag`) — so the character straightens on the authored frame, not with a snap at entry.

> **Gotcha — custom action system, not stock GAS.** A montage "gameplay event" notify must route through `URogueActionSystemBlueprintLibrary::SendGameplayEventToActor` → `HandleGameplayEvent` → `GameplayEventReceivedDelegate`. Stock UE "Send Gameplay Event to Actor" targets `UAbilitySystemComponent` and does nothing here. Applies to every montage anim event (foot plants, IK windows, …).

### Root motion: the UE4→UE5 retarget drops it (the mesh-drift bug)

**Symptom.** During warping the mesh drifts *out* of the capsule; the detection capsule still lands on target. `HasRootMotion()` reads true and the capsule moves — but the mesh doesn't ride it.

**Root cause.** The pack is UE4 Manny retargeted to UE5 Manny, and a **misconfigured retarget root-motion op** left the *target* root bone flat — the travel baked into the body, *no usable root motion on the target* (the source clip has it; the op just wasn't wired to copy it — see Fix). The capsule only moved because **SkewWarp ADDS translation when the anim supplies none**, so there was nothing for the root-lock to hold the mesh to.

**Tell.** In the IK Retargeter the **"dotted line"** (root trajectory) is *missing*, vs present on a clip with real root motion (e.g. Mover's `VaultOver`).

**Fix (corrected).** IK Retargeter with the **default ops**; on the **Root Motion Op → Motion Source**, set **Root Motion Source = "Copy from Source Root"** and **Target Pelvis = "root"**. That reproduces the source UE4 clip's root trajectory *exactly* — real root motion, no drift. *This supersedes an earlier prescription here* ("Generate from Target Pelvis"; "Copy from Source Root gives a flat line") — the flat line was actually **Target Pelvis not being set to `root`**, not the copy mode itself. This is the confirmed recipe for the mantle-down clip; re-verify the top-out clip against it if its mesh ever drifts.

**Related finding — the root lock is an ANIM-SYSTEM step, not CMC.** The mesh rides the capsule via `FRootMotionReset` in `DecompressPose`, gated by `Montage->HasRootMotion() && RootMotionMode != NoRootMotionExtraction`; CMC only *consumes* the delta. So a CMC-less Mover character keeps the lock **for free** as long as the AnimBP's Root Motion Mode isn't "No Root Motion Extraction" *and* the clip has real root motion. `PushDisableRootMotion` disables only the movement-facing extraction (prevents double-apply), not the visual lock — so `ForceRootLock` / `IgnoreRootMotion` / `RootMotionAttribute` all failed here for the same reason: no real root motion to lock.

### End-pose seam — mantle → combat idle

**Problem.** The mantle clip settles into a tall, arms-down "stand" far from the aim-idle, so handing back to the locomotion state machine *popped* (a visible cross-fade between two far-apart poses).

**Fix — three orthogonal levers together**, each closing a different gap:

```
cut the montage tail   → shrinks the pose distance at the source
Inertialization        → residual cross-fade is velocity-continuous, not a linear lerp
foot IK ease in/out    → pins ground contact across the handoff
                         (inertialization blends the whole pose, not contacts)
```

Inertialization = an `Inertialization` node downstream of the `DefaultSlot` + the montage's **Blend Out → Inertialization** (same overshoot caveat as warping if the pose gap is large and the blend time tiny). Extra levers: montage Blend Out **Trigger Time** earlier so the tall settle overlaps idle; a per-bone **Blend Profile**. This is the **reusable montage→locomotion recipe** for every authored traversal (corner turns next).

## Mantle down — implementation (C++ done; assets + validation pending)

The **inverse** of the top-out: stand at a ledge, press a button, an authored **root-motion** montage carries the character **over the edge and down** into a hang, ending in **Climbing** (not Walking). Reuses the whole traversal skeleton, inverted at both ends: it runs while **Walking** (not climbing) and hands off to **Climbing** (not walking). Built as a sibling `URogueMantleDownMode` + `URogueMantleDownTransition` + `BeginMantleDown()`, sharing the wall-basis helpers. Tilt-aware like the top-out. **Button-triggered, not contextual** — walking off an edge still just falls.

### Detection — Zelda-style "always-succeed" affordance

The probe runs each pre-sim tick **while grounded**, but the affordance is what's gated, not the commit: full validation runs continuously (behind a cheap gate) and publishes `IsMantleDownAvailable()`, so a button press only ever fires when the drop is **already** guaranteed. No dead presses. (`bMantleDownAvailable` is `BlueprintReadOnly`-facing for a HUD prompt later.)

- **Stage 1 — arm (cheap):** a capsule in the **lower front** of the character. Collision-free ⇒ the ground has ended ahead (an edge). This early-out is why running the full check per tick is affordable — it only fires the heavy rays when you're actually at an edge.
- **Stage 2 — validate (the backward-ray trick):** from points **out-front-and-below the lip** (over the drop, *below* the top surface, or they'd graze the lip), fire rays **back toward the wall**. The first **near-vertical** (`IsSurfaceClimbable`) hit is the descendable face; its **normal points back at us** — exactly the plane we seed the climb hand-off with. Averaging the samples gives the wall plane; the topmost hit ≈ the lip; the top↔bottom span confirms a hangable face.
- **Extra gates:** **min face height** (along `WallUp`, so it's a step-reject that's tilt-consistent), a **clearance corridor** (**one** capsule lying *parallel to the wall* from the hang feet up past the lip to a configurable amount above it — the whole swing-over-and-hang volume in a single test, anchored at the hang feet so it doesn't drift with tilt like a standalone box would), and a **loose facing gate** (~25°; motion warping snaps the rest).
- **Walk surface vs wall face kept separate:** the standing/edge geometry is world/gravity space (upright), the hang face is wall-relative (tilted) — the same `dot(normal, Up) < MantleMinWalkableDot` rule that separates face from top in the up-mantle.

### Two warp phases (forward → down) — trace the corner

Matches the clip's root motion (*forward a little, then straight down*) and traces the corner instead of cutting it:

```
MantleDownForward → the lip pivot   (over the edge; upright, facing OUT — the rotation warp snaps the ±25° approach)
MantleDownDown    → the hang        (on the face; wall-tilted, facing IN = MakeFromZX(WallUp, -WallNormal) = climb's pose)
```

Enable **rotation warp on the down window** — that's where the ~180° turn to face the wall + the approach-angle snap resolve.

### Orientation — inverse of the top-out, event-gated

The capsule **holds upright** (the standing entry pose) until the montage fires `MantleDownTiltBlendEventTag`, then **eases into the wall lean** (`QInterpTo`) so the hand-off lands on climb's exact tilt. The ~180° **facing turn rides the clip/warp** (the mode reads the warped root rotation for yaw); the **event gates only the tilt**. Mirror of the top-out's hold-tilt→upright, and it reads the **probe-cached** face normal (`GetMantleDownWallNormal`), not the live grid — the forward sweep loses the face mid-traversal while the character is out over the edge.

### Input — one unified traversal block

A tap (e.g. **F**) sets `FRogueTraversalInputs::bWantsToMantleDown` (one unified Mover input struct — a later dash / corner-turn adds a **bool**, not a new class, mirroring how `FCharacterDefaultInputs` bundles move + jump). The transition fires on grounded + tap + available. (`FRogueClimbInputs`, the old unused single-purpose block, was removed in favor of this.)

### Hand-off to Climbing

On montage completion the mode hands to **Climbing unconditionally** (the probe already guaranteed a climbable face — the always-succeed contract). The grid re-acquires the face as the character settles into the hang (facing in), so `CanClimbNow()` is true; if the grid momentarily can't see it on the exact boundary frame, climb's own exit-to-Falling self-heals rather than us pre-emptively dropping the hang. **The boundary frame is the thing to watch in-editor** (tune via the montage tail if it blips).

### Remaining (asset-side + validation)
- Author the **montage** with the two named SkewWarp windows (`MantleDownForward`, then `MantleDownDown`) + **rotation warp** on the down window; assign it to `MantleDownMontage`.
- Add **`IA_MantleDown`** + an **F** binding in the IMC, assign to the player BP's `Input_MantleDown`.
- Author the **tilt-blend "Send Gameplay Event"** notify and set `MantleDownTiltBlendEventTag` (routed through the action system, per the top-out gotcha).
- The **montage→climb-hang** seam recipe (inertialization + cut tail + foot IK), blending into the climb hang pose instead of idle.
- Verify the clip has **real root motion** (the "dotted line" test in the IK Retargeter; retarget recipe in the top-out's *Root motion* note — Copy from Source Root + Target Pelvis `root`) and validate the whole drop in-editor; tune the probe knobs (`Mover|Climbing|MantleDown`).

## Tuning knobs
`MinHorizontalDegreesToStartClimbing`, grid dimensions & footprint (width/height/reach), `MinCoverageRatio`, normal-consistency angle, hang-blend smoothing rate, per-foot IK search radius + interp/hysteresis, tuck offset.

## Status & roadmap

**Done**
1. **Sustained climb** — detection sweep → 3×5 coverage grid → dominant-plane orient; custom `URogueClimbMode` + contextual entry/exit transition.
2. **Climb anim sync** — 8-dir blendspace, playrate sync (Step A) + per-clip velocity surge (Step B), round→square input remap.
3. **Mantle top-out** — first authored traversal: two-phase motion-warped root-motion montage → Walking; established the reusable traversal skeleton.

**Next — big milestones, roughly ordered**
4. **Lower-body hang blend** — drive the already-computed `LowerBodySupport` scalar into an anim blend so climbing over a lip eases into a dangle (the sim scalar exists; it just isn't wired to anim yet).
5. **Per-foot / per-hand IK (Control Rig)** — dedicated limb probes + two-bone IK plant with interp/hysteresis, plus the floating-limb procedural tuck fallback (design lives in *Per-limb refinement* + *Procedural tuck* above).
6. **Corner turns** — inner then outer; reuse the mantle traversal skeleton + side-probe corner classification (concave vs convex) + authored clips.
7. **Climb feel polish** — wall-distance vs into-wall bias; **edge-clamp** (don't walk off a side edge — a cheap safety add-on).
8. **Uneven surfaces** — climb across surface intersections / non-planar geometry.

**Later polish** — ledge shimmy; stamina + UI.

## TODO

- Precise mantle up (use hand pos as probe)

- Climb on uneven surfaces (especially surface intersections) — milestone 8 above.

- Climb stamina via GAS.

- Camera zooming out when climbing.

- Mantle down — **C++ implemented & compiling** (`URogueMantleDownMode` / `URogueMantleDownTransition` / `RefreshMantleDownProbe` / `FRogueTraversalInputs`). Remaining: montage + warp windows, `IA_MantleDown` + F binding, tilt event tag, and in-editor validation. See *Mantle down — implementation* above.
