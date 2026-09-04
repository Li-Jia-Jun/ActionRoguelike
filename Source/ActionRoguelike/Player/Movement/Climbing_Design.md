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

## Tuning knobs
`MinHorizontalDegreesToStartClimbing`, grid dimensions & footprint (width/height/reach), `MinCoverageRatio`, normal-consistency angle, hang-blend smoothing rate, per-foot IK search radius + interp/hysteresis, tuck offset.

## Build order
1. **Sustained climb** — coverage grid, state classification, dominant-plane orient + IK (current focus).
2. **Mantle** — first authored traversal; completes the climb-a-wall loop and builds the reusable traversal skeleton.
3. **Corner turns** — inner then outer; same skeleton + corner-classification side probes + authored clips.

Edge-clamp (don't move off a side edge) is a cheap safety add-on to step 1; ledge shimmy and stamina/UI are later polish.

## New TODO

Hand IK, limit upper body rotation.
