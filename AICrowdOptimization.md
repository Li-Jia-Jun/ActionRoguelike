# AI Crowd Optimization — A Profiling Case Study

How the CPU cost of a large AI crowd (~575–1000 combat AI, all on screen) was taken from
**~60 ms/frame down to ~25 ms**, entirely on the game thread, by profiling first and letting the
data pick every optimization.

The companion reference for the LOD system itself is [SignificanceManager.md](SignificanceManager.md).
The techniques live in [`ARogueAICharacter::SignificanceLODChanged`](Source/ActionRoguelike/AI/RogueAICharacter.cpp)
and [`URogueSignificanceManager`](Source/ActionRoguelike/Significance/RogueSignificanceManager.cpp).

---

## The scenario

- **~575–1000 skeletal-mesh combat AI**, spawned in `TestEnemyCount` map.
- **Worst case:** a flying/top-down camera in the middle of the crowd, so nearly every agent is
  on screen. This defeats visibility culling *and* screen-size heuristics — the two things most
  built-in optimizations rely on — so it stresses the CPU game logic directly.
- **Target:** this is a demo project for practicing optimization; ~25 ms (≈40 fps) with 1000
  interactive AI was the acceptance bar, with the goal of a clean, data-driven methodology.

---

## Results

| Milestone | Frame time | What changed |
|---|---:|---|
| Baseline | ~60 ms | Mesh LODs only; full AI logic per agent |
| Significance bucketing + LOD reactions | ~40 → 25 ms | Per-tier kinematic-body gating, movement mode/tick throttle, BT throttle |
| Movement-smoothness tuning | 25 → 28 ms | Raised LOD2 movement tick to 0.1 s to kill positional popping (deliberate trade) |
| Significance-driven URO | 28 → 25 ms | Animation update rate keyed to significance LOD, not screen size |
| CMC avoidance + AIController tick | 25 → ~23 ms | Avoidance off / NavWalking for far tiers; throttle the AIController's own tick |

Final: **~23–25 ms, game-thread bound**, with ~15 ms of GPU headroom to spare.

---

## Methodology: measure the limiter, not a guess

The single most valuable habit in this exercise was **identifying the bottleneck before touching
code** — and re-checking after every change.

### 1. `stat unit` — which thread is the wall?

A frame is a pipeline: **Game → Render → GPU**, each ~a frame apart, and frame time is gated by
the *slowest* stage.

- `Game ≈ Frame` and `Game > Draw/GPU` → **game-thread (CPU) bound**. That was us the whole time
  (`Game` 25–30 ms vs `GPU` 10–15 ms).
- A **render thread that waits more than it works** is a downstream stage *starved* by the game
  thread — independent confirmation of CPU-bound. If the GPU were the wall, `Frame ≈ GPU`.

### 2. Unreal Insights — the Timers table

Don't eyeball the timeline; select a stable frame range and read the aggregated **Timers** table:
- Sort by **Inclusive** time → hottest *subtree* (which system/tick group).
- Sort by **Exclusive (self)** time → hottest *function*.

This is what localized the cost to `TG_PrePhysics` (the pre-physics tick group = per-actor and
per-component ticks), and then to the exact components inside it.

### 3. Count ≠ cost

A profiler showing "1000 calls to X" is not the same as X being expensive. `UpdateKinematicBonesToAnim`
appeared 1000×/frame even after it was gated — because the callers still *invoke* it; it just
**early-outs**. Always read the *inclusive time*, not the call count.

### 4. Verify a hypothesis cheaply before committing

Console vars and toggles confirm a theory in seconds. `a.URO.ForceAnimRate 4` proved the URO
pipeline worked and that the *screen-size heuristic* was the only thing suppressing it — before any
code was written.

---

## The optimizations, by system

All per-agent reactions are driven by one signal: the significance LOD tier, applied in
`SignificanceLODChanged`. Nearer/more-significant agents keep full quality; far ones degrade.

### Significance system (the backbone)
- A per-world `USignificanceManager` subclass buckets agents by significance rank (see
  [SignificanceManager.md](SignificanceManager.md)).
- **Two-phase update** to spread cost: a heavy *calculate + bucket* pass every N ticks, and a
  light *apply LOD changes* pass on the intervening ticks, draining a cursor-based queue with a
  per-frame change cap and a freeze window to prevent thrash.
- Driven from a custom `UGameViewportClient::Tick` (the engine does not tick the significance
  manager for you).

### Physics-body sync — the biggest single win
- `UpdateKinematicBonesToAnim` pushes each animated pose into the mesh's physics-asset bodies
  **every frame**. Unnecessary for agents that only ragdoll on death.
- Fix: `KinematicBonesUpdateType = SkipAllBones` for far tiers → the function early-outs.
  Re-enabled + `SetSimulatePhysics(true)` on death for the ragdoll.

### Movement (CharacterMovementComponent)
- **Tick interval** scaled per tier (0 → 0.08 → 0.15 s). Fewer full-rate updates.
- **Movement mode**: near = `MOVE_Walking` (accurate floor sweeps, seen up close);
  far = `MOVE_NavWalking` (skips the sweeps, cheap).
- **RVO avoidance** off for far tiers. Avoidance is steering, *not* collision — capsules still
  block each other, so far agents bump/jostle instead of flowing smoothly. Invisible at distance.
- **Popping trade-off**: throttling movement tick makes position advance in discrete jumps
  (0.5 s × speed = a visible teleport). Raising the far tier to 0.1 s removed most of it for ~3 ms.
  The *proper* fix (not needed here) is to interpolate the mesh visually between sim ticks.

### Animation (URO)
- **Key gotcha**: default URO picks its rate from **screen size**, so in a top-down crowd where
  everything is large on screen it never throttles — enabling URO alone did *nothing*.
- Fix: switch URO to **LOD-map mode** (`bShouldUseLodMap` + `LODToFrameSkipMap`) via the
  `OnAnimUpdateRateParamsCreated` delegate, and force the mesh LOD per tier with `SetForcedLOD`.
  Now animation evaluation rate follows *significance*, not screen size. Bonus: forced LOD also
  cuts mesh triangles per tier.
- `bSkipKinematicUpdateWhenInterpolating` + `bSkipBoundsUpdateWhenInterpolating` so URO-interpolated
  frames skip the physics-body sync and bounds recompute too. (These do nothing until URO actually
  interpolates — which it wasn't, until the LOD-map fix.)

### AI logic
- BehaviorTree component tick interval scaled per tier (0 → 0.25 → 0.5 s).
- The **AIController's own actor tick** was unthrottled (all 575 ticking every frame — ~2.9 ms).
  Throttled via `SetActorTickInterval` per tier.

---

## Dead ends (the most useful part)

Three confident guesses that the profile *disproved* — the reason "measure first" matters:

- **Health-bar widgets.** A per-AI screen-space `WidgetComponent` is a classic crowd cost, so it
  looked like an obvious target. The `TG_PrePhysics` breakdown showed **zero** widget-tick cost —
  it simply wasn't a factor here. Dropped.
- **Multithreaded animation update.** Enabling it gave *no* frame-time change. Not because anim was
  cheap, but because the game thread **dispatches the anim tasks and then blocks waiting** for them
  inside the same tick group (`WaitForTasks`) — there was no independent work to overlap. Moving
  work off the game thread only helps if the game thread had something else to do.
- **"It must be the GPU now."** After big CPU cuts, a GPU-bound assumption was tempting. `stat unit`
  said otherwise: `Game ≈ Frame`, GPU at half that. Still CPU-bound.

Lesson: a hypothesis is worth one profile check, not one refactor.

---

## Where it landed, and what's next

At ~25 ms the remaining `TG_PrePhysics` cost is the **per-mesh anim completion**
(`FParallelAnimationCompletionTask`) that runs every frame regardless of URO — this is the
**actor-model floor**. Going lower is no longer a `SignificanceLODChanged` knob; it's structural:

- **MassEntity / MassAI** — data-oriented, no per-entity actor overhead. Ideal when *most* agents
  are dormant/far/simple, with actor promotion for the few near the player. The catch is
  interactivity: GAS, montages, per-bone hits, and this project's `ActionSystem` all expect
  `AActor`s, so fully interactive combat enemies want to be actors anyway. Best fit is a hybrid
  (ambient Mass horde + actor combatants), not a wholesale rewrite.
- **Vertex Animation Textures + instanced meshes** for distant crowd rendering (a GPU-side lever;
  not this project's current bottleneck).

For interactive combat AI that fights the player, casts abilities, and plays effects, the
**actor + significance-LOD** architecture used here is the appropriate choice.

---

## Takeaways

1. **Find the limiting *thread* first** (`stat unit`), then the limiting *system* (Insights Timers,
   inclusive → exclusive), then the limiting *function*. Never optimize a stage that isn't the wall.
2. **One signal, many reactions.** A single significance LOD drove physics-body sync, movement mode,
   tick rates, avoidance, animation rate, and mesh LOD — cohesive and tunable from developer settings.
3. **Count ≠ cost. On-by-default ≠ working.** (URO looked "on" but wasn't throttling.)
4. **Every guess gets one profile check before any code.** The dead ends saved more time than the wins.
