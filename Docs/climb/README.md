# Climbing

Design decisions for the Mover-based climbing system, agreed before implementation.
Following Vitor Cantão's tutorial *in concept*, translated from CMC to Mover.
The code lives in `Source/ActionRoguelike/Player/Movement/` (the climb, mantle and corner modes, `URogueCharacterMoverComponent`, the limb solver and its Control Rig node).

## Docs

| Doc | Covers |
|---|---|
| [Detection & wall contact](detection.md) | the forward sweep, the 3×5 validation grid, the wall-vs-walk gate, the standoff controller |
| [Surface map & warp](surface-warp.md) | the marched surface map, the warp formula, per-joint, reach rule, pins + leash through the map |
| [Hang](hang.md) | gross hang + per-foot probes, the hang anim recipe, hang-move experiments, the procedural tuck |
| [Limb IK](limb-ik.md) | pins (where + how much: predictive placement, IK follow, corner pins, foot probes) and the Climb Limb IK swivel solver |
| [Stopping well: the idle retreat](idle-retreat.md) | bad-segment skip, sequenced retreat, re-grip swing, weight shift, reach leash + cap, clip-switch blend |
| [Animation ↔ movement sync](movement-sync.md) | the distance curve, round → square input, scalar vs full root motion |
| [Mantle up & down](mantle.md) | the shared skeleton, the top-out probe + lip scan + steep tilts, the always-succeed mantle down, the retarget fix, the end-pose seam |
| [Corner turns](corner-turns.md) | the corner probe, the input-driven turn, the custom playhead warp |

## Architecture

### Design stance: forgiving + snapping
- Thresholds lean lenient; the character snaps to the dominant plane rather than conforming exactly. Tighten only if players climb things they shouldn't.

### Target pipeline (Mover-native)
`Climb mode (state + GetPredictedTrajectory)` → `PoseSearch / Motion Matching picks clip` → `Motion Warping aligns root to target` → `Control Rig IK refines limbs` → `procedural tuck fallback`.

### Traversals: mantle & corner turns (hybrid)
- Sustained climb stays **procedural** (grid → dominant plane → orient + IK, BOTW-like); mantle and corner turns are **authored, motion-warped root-motion clips** (AC/Uncharted style, matching the anim packs). BOTW-style tutorials only cover sustained climb + a basic top-out, not these authored clips.
- All three share one **skeleton**: grid boundary flag → directed probe finds the target transform → pick clip (chooser or Pose Search) → Motion Warp to target → play as an `AnimRootMotionLayeredMove` (input locked, lip collision relaxed) → hand off to climb/walk.
- **Mantle:** top rows miss → up-and-over probe finds lip + landing → warp + root motion up and over.
- **Corner turns:** a side column misses → side probe classifies **inner** (concave, perpendicular wall also hits) vs **outer** (convex, face wraps away) → matching clip warped to the new face → resume climb on the new dominant normal.
- Reference: UE5 **Game Animation Sample** traversal system (chooser + Motion Warping), not a BOTW tutorial.

### Mode tags — `Mover.Climb.*`
- Every climb-family mode carries **one child of `Mover.Climb`** in its constructor (`GameplayTags.AddTag`), the same mechanism as Mover's own `Mover.IsOnGround` / `Mover.IsInAir`: `Mover.Climb.Sustained` (sustained climb), `.MantleUp`, `.MantleDown`, `.CornerTurn`. Declared in `RogueSharedGameplayTags`.
- **Queries:** `IsInClimbState()` = `HasGameplayTag(Mover.Climb, non-exact)` — the parent matches any child, so a future climb mode joins by adding its own child tag and no caller changes. `IsClimbing()` / `IsMantling()` / `IsMantlingDown()` / `IsCornerTurning()` = their child tag, exact. Same sync-state buffer as `GetMovementModeName()`, so no tick of disagreement. (Mode *names* still key registration and transitions.)
- **Scope decision: tags for MODE only.** Discussed tagging everything (lower-body / IK states) for a fully tag-driven AnimBP; rejected for now. Tags are booleans that don't enforce exclusivity; per-limb IK flags must travel with their data (the world-origin grip bug was a flag/data split); continuous values (alphas, intent, playhead) can't be tags. Coarse body states (e.g. `Mover.Climb.Hanging` as a Mover external tag) get a tag only once a *gameplay* consumer needs one (stamina, ability blocking). Note `UMoverComponent::HasGameplayTag` is not `BlueprintThreadSafe`; if the AnimBP moves to thread-safe update, feed it a game-thread snapshot (a tag container + plain data — container `HasTag` *is* thread-safe), Lyra-style.

### Data split: sim vs anim
- **Sim** (climb mode / sync state): minimal — dominant normal + point only.
- **Anim** (component, game-thread, like `CurrentWallHits`): full per-sample grid + the `IsLowerBodyHanging()` / `CanMoveWhileClimbing()` bools, the per-foot `LeftFootProbe`/`RightFootProbe` (`bHanging` + foothold `TargetLocation`/`TargetNormal`), the limb pins + weight shift + `ClimbLimbIKPlanes` for the Control Rig, and the legacy `LowerBodySupport`/Upper/Left/Right scalars.

## Tuning knobs
`MinHorizontalDegreesToStartClimbing`, grid dimensions & footprint (width/height/reach), `ClimbTopRowMinHits` (grab/cling) + `ClimbTopRowMinHitsToMove` (traverse), `ClimbLowerBodyRowCount` (leg-footing band), normal-consistency angle, per-foot IK search radius + interp/hysteresis, tuck offset. (Hang-blend smoothing lives in the AnimBP now, not a C++ knob.)

## Status & roadmap

**Done**
1. **Sustained climb** — detection sweep → 3×5 coverage grid → dominant-plane orient; custom `URogueClimbMode` + contextual entry/exit transition.
2. **Climb anim sync** — 8-dir blendspace, distance-curve speed (body follows the anim via per-clip `ClimbDistance`, baked from the root-motion siblings), round→square input remap.
3. **Mantle top-out** — first authored traversal: two-phase motion-warped root-motion montage → Walking; established the reusable traversal skeleton.

**Next — big milestones, roughly ordered**
4. **Lower-body hang blend** — *C++/detection done* (gross `IsLowerBodyHanging()` now = BOTH feet without a foothold, from the per-foot probes; hand-purchase gate replaced the coverage ratio; `CanMoveWhileClimbing()` locks traversal to a single handhold; `GetClimbMoveSpeedFraction()` gated to 0 when locked). **Remaining (AnimBP):** `FInterpTo` the hang bool into an alpha, then blend per the [*Hang anim recipe*](hang.md) — `Blend(braced, Blend(hang_idle, climb_move ⊕ dangle_legs, speedFraction), hangAlpha)` — plus a subtle dead-hang sway.
5. **Per-foot / per-hand IK (Control Rig)** — *done and in use*: predictive placement for the sustained climb (offline contact baker + `ClimbPhase`, destination-clip stance weight, approach→lock, one-rate IK follow), the full stop/release handling ([Stopping well](idle-retreat.md) — bad-segment skip, staged contralateral retreat, Bézier swing + peel, weight shift with hip-in and chest, reach leash + reach cap, clip-switch blend), and reactive two-face pins for corner turns; the Control Rig consumes `Weight` + `LocalLocation`/`LocalNormal` and `ClimbBodyShift` / `ClimbChestRotation`. Full design in [Limb IK](limb-ik.md) and [Stopping well: the idle retreat](idle-retreat.md). **Solver:** the **Climb Limb IK** node (swivel search: FK-hinge reference, fold/reach range, wall + body colliders, side hysteresis, swivel speed cap) replaces Basic IK on all four limbs - in use; see [Limb IK](limb-ik.md#part-2--solver-how-the-limb-bends). **Remaining:** that section's *Next* ((optional) world blockers, one-source body colliders, the reachability-aware leash + drag budget); a dead-hang special case for the reach cap; the per-foot **procedural tuck** on `bHanging`. (Sync markers for phase-aligned in-between blends are deferred: climbing is 8-directional for now, so the blendspace sits on one clip except while changing direction.)
6. **Corner turns** — *C++ done*: probe + debug draw and the input-driven turn ([Corner turns](corner-turns.md): stick-driven playhead, settle-to-rest, deterministic per-strike warp onto Mid/End, plane seeding). **Remaining:** clip setup (root yaw check, Force Root Lock, `CornerRest` notify, assign the 4 clips), AnimBP corner state (Sequence Evaluator + hang-legs layer + masked breathing additive), then in-editor tuning (commit fractions, Mid/End offsets via the silver ghosts).
7. **Climb feel polish** — wall-distance vs into-wall bias; **edge-clamp** (don't walk off a side edge — a cheap safety add-on).
8. **Uneven surfaces** — climb across surface intersections / non-planar geometry.

**Later polish** — ledge shimmy; stamina + UI.

## TODO

- Sync markers for diagnoal blends

- Corner turns fail cases:
  - Tilted walls
  - When climbing diagonally

- Find a better animation package have better hanging: 8 dir move + corner. Candidates:
  - https://www.fab.com/listings/d33b6e39-06fd-4530-be40-96ffcaf22f4b (8 dir move, no corner, no mantle)
  - https://www.fab.com/listings/6463c64d-8e64-43cb-832a-74566ef55198 (2 dir move, corner, air entry, mantle)

- Climb on animated surface (need in-place body swing animation).

- Climb on uneven surfaces (especially surface intersections) — milestone 8 above.

- Upper body misses but still can climb.

- Async probe.

- Climb stamina via GAS.

- Custom anim graph nodes for better performance?

- Camera zooming out when climbing.                     

- Precise mantle up (use hand pos as probe)?

- Mantle down — **C++ implemented & compiling** (`URogueMantleDownMode` / `URogueMantleDownTransition` / `RefreshMantleDownProbe` / `FRogueTraversalInputs`). Remaining: montage + warp windows, `IA_MantleDown` + F binding, tilt event tag, and in-editor validation. See [Mantle up & down](mantle.md#mantle-down).
