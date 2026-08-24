# Climbing — Surface & Animation Design

Design decisions for the Mover-based climbing system, agreed before implementation.
Following Vitor Cantão's tutorial *in concept*, translated from CMC to Mover.

## Detection (done)
- Each pre-sim-tick, a forward capsule sweep (`SweepAndStoreWallHits`) stores `CurrentWallHits` — a cheap "is anything in front?" check.
- `SweepMultiByChannel` returns one hit per *component*, so it is detection only, not coverage.

## Validation — 3×5 grid
- Validate climbability by sampling a **3 wide × 5 tall grid** (hip → reach) of short forward line traces over the climb footprint, not by trusting a single hit or ray.
- Per sample: must hit `ECC_WorldStatic` within reach, and its normal must pass the steepness (near-vertical) + facing (≤ `MinHorizontalDegreesToStartClimbing`) test (`IsSurfaceClimbable`).
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

## Traversals: mantle & corner turns (hybrid)
- Sustained climb stays **procedural** (grid → dominant plane → orient + IK, BOTW-like); mantle and corner turns are **authored, motion-warped root-motion clips** (AC/Uncharted style, matching the anim packs). BOTW-style tutorials only cover sustained climb + a basic top-out, not these authored clips.
- All three share one **skeleton**: grid boundary flag → directed probe finds the target transform → pick clip (chooser or Pose Search) → Motion Warp to target → play as an `AnimRootMotionLayeredMove` (input locked, lip collision relaxed) → hand off to climb/walk.
- **Mantle:** top rows miss → up-and-over probe finds lip + landing → warp + root motion up and over.
- **Corner turns:** a side column misses → side probe classifies **inner** (concave, perpendicular wall also hits) vs **outer** (convex, face wraps away) → matching clip warped to the new face → resume climb on the new dominant normal.
- Reference: UE5 **Game Animation Sample** traversal system (chooser + Motion Warping), not a BOTW tutorial.

## Tuning knobs
`MinHorizontalDegreesToStartClimbing`, grid dimensions & footprint (width/height/reach), `MinCoverageRatio`, normal-consistency angle, hang-blend smoothing rate, per-foot IK search radius + interp/hysteresis, tuck offset.

## Build order
1. **Sustained climb** — coverage grid, state classification, dominant-plane orient + IK (current focus).
2. **Mantle** — first authored traversal; completes the climb-a-wall loop and builds the reusable traversal skeleton.
3. **Corner turns** — inner then outer; same skeleton + corner-classification side probes + authored clips.

Edge-clamp (don't move off a side edge) is a cheap safety add-on to step 1; ledge shimmy and stamina/UI are later polish.
