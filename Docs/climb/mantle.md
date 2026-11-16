# Climbing — Mantle up & down

A mantle is an authored **root-motion montage**, motion-warped onto probed targets. **Mantle up** carries the climber up and over a ledge into Walking; **mantle down** carries a standing character over an edge and down into a hang, into Climbing. Both share one skeleton:

```
probe → two warp targets (trace the corner, don't cut it) → root motion in its own mode → hand off
```

Both work on walls tilted up to ±40°. The one formula to remember there: the landing is found `Reach` inland of the lip, so **along the wall it sits `Reach·sin(tilt)` from the lip**. Part of the [Climbing docs](README.md).

![Mantle side views: up and over with the overhead ray, landing sweep, lip scan and two warp targets; down with the arm capsule, backward rays, clearance corridor and two warp targets; a tilted wall where the landing sits Reach·sin(tilt) along the wall from the lip](images/mantle.svg)

## Up vs down
| | Mantle up (top-out) | Mantle down |
|---|---|---|
| Trigger | Push **up** at a ledge (contextual) | Tap **F**, only when already validated |
| From → to | Climbing → **Walking** | Walking → **Climbing** |
| Mode / transition | `URogueMantleMode` / `URogueMantleTransition` | `URogueMantleDownMode` / `URogueMantleDownTransition` |
| Warp windows | `MantleUp` → the lip, then `MantleForward` → the landing | `MantleDownForward` → the lip pivot, then `MantleDownDown` → the hang |
| Capsule rotation | Wall tilt → upright, on `MantleUprightBlendEventTag` | Upright → wall tilt, on `MantleDownTiltBlendEventTag` |
| Status | Done | C++ done; assets + validation pending |

## Shared rules
- **Motion:** a `FLayeredMove_AnimRootMotion` (`OverrideAll`) that ends itself when the montage stops.
  - Completion is read from the sync state: `!LayeredMoves.HasMove<FLayeredMove_AnimRootMotion>()` → the next mode.
- **Own mode** (and `Mover.Climb.*` tag), so `IsClimbing()` is false during it and the climb-exit-to-Falling fling can't fire.
- **Motion Warping without CMC:** `UMoverComponent` auto-wires a `UMotionWarpingMoverAdapter` when the actor owns a `UMotionWarpingComponent`.
- **Two SkewWarp windows**, because one landing target cuts a diagonal through the wall corner.
  - SkewWarp warps the **endpoint, not the path**: the in-between is the clip's own root motion, skewed. An early enough window split is the main lever.
  - A very short window with no speed clamp overshoots (all remaining travel crammed into little time). Fix: a longer window or a clamp.
- **Face vs top:** `dot(normal, Up) < MantleMinWalkableDot` = a wall face, else a walkable top. The same rule is used everywhere.
- **Probe the face wall-relative, the landing world-space:**
  - rays into the face go along `−WallNormal`, with origins at the tilted capsule's real bottom (`center − WallUp·CapHH`);
  - the landing and the capsule fit stay world-vertical, because the character stands upright on top.
- **Event-gated rotation:** the capsule keeps its entry rotation until the montage fires its blend event, then eases (`QInterpTo`), so the change lands on the authored frame instead of snapping at entry.
- **Gotcha: custom action system, not stock GAS.** A montage "gameplay event" notify must go through `URogueActionSystemBlueprintLibrary::SendGameplayEventToActor`. Stock "Send Gameplay Event to Actor" targets `UAbilitySystemComponent` and does nothing here.

## Mantle up

### Arming probe (`RefreshMantleProbe`, every climbing tick)
- **Overhead ray:** perpendicular into the face (`−WallNormal`) at the top of the ledge band.
  - A steep hit = the face continues (no lip).
  - Empty, or a walkable hit = the face ended into a lip.
  - Perpendicular, not horizontal: the clearance stays ~one capsule radius at any tilt, and a horizontal ray would shoot under an overhang.
- **Landing:** a down-trace finds the walkable top, then a **downward capsule sweep** (FindFloor-style) takes where the capsule rests.
  - It settles onto a sloped top without a static overlap's false self-hit, and still stops on real obstacles.
- **Height gate:** the rest's rise is measured **along the wall** (`WallUp`), so the trigger window is the same span at any tilt.

### Precise lip at commit (`ComputePreciseMantleEdge`)
- Binary-searches up the face with perpendicular rays, a few lateral samples, midpoint-corrected. This gives the `MantleUp` target.
- Independent of the landing, so a lip **higher** than the walk surface works.
- Knobs: `MantleLipScanIterations` (each halves the error) × `MantleLipLateralSamples`, run once.
- Uses the same `Feet` origin as the arming probe (with `GetClimbSampleUpBias`), or the band misaligns.

### Steep tilts (the 30–40° fix)
Before the fix, on 30–40° walls leaning away, the lip scan failed (no cyan or white sphere) and the character mantled into the air. Three world-up assumptions caused it; each fix is a no-op on a vertical wall.
- **A. Feet = `center − WallUp·CapHH`, not `Up·CapHH`.**
  - With world-up, ray origins sit `CapR − CapHH·sin(tilt)` off the face. That crosses 0 near 40°, so the rays started behind the face.
  - Applied in both the arming probe and the lip scan.
- **B. Widen the lip scan by `Reach·sin(tilt)`** (the main bug, and why 30° failed "sometimes").
  - The landing is `Reach` (≈ `CapR + MantleForwardReach`) inland of the lip, so the scan band, centred on the landing, missed the lip.
  - This one doesn't depend on capsule size; it only widens the bracket, so it's safe.
- **C. Fallback edge:** subtract the same `Reach·sin(tilt)` from `MantleEdgeTransformCache`'s rise, or `MantleUp` floats over the ledge.
- **Follow-ups:**
  - A drops `Feet` by `CapHH·(1 − cos tilt)` (~16 cm at 40°). Nudge `MantleMinLedgeHeightFromFeet` if arming shifts.
  - Watch: the landing capsule centre still assumes the upright blend has finished before the forward warp pins the feet. Tune `MantleUprightBlendSpeed` and the event timing if steep top-outs sit high or sink into the ground.

### Capsule tilt
- While climbing, the capsule lies against the wall (`MakeFromZX(WallUp, −WallNormal)`, `bAlignToClimbSurface`), and the sweep and grid shift **along the wall** (`GetClimbSampleUpBias`).
- The mantle inherits that tilt and eases upright (`MantleUprightBlendSpeed`) only on the event.

## Mantle down

### Detection: always-succeed (`RefreshMantleDownProbe`, every grounded tick)
- The affordance is gated, not the commit: full validation runs continuously and publishes `IsMantleDownAvailable()`, so a press only fires when the drop is already guaranteed. There are no dead presses.
- **Stage 1, arm (cheap):** a capsule at the lower front of the character. Collision-free = the ground ended ahead. This early-out keeps the per-tick cost low.
- **Stage 2, validate:** from points out in front and **below the lip**, fire rays **back toward the wall**.
  - The first near-vertical hit (`IsSurfaceClimbable`) is the face; its normal points back at us, which is the plane the climb hand-off is seeded with.
  - Averaged samples give the wall plane; the topmost hit ≈ the lip; the top-to-bottom span confirms a hangable face.
- **Extra gates:**
  - **Min face height**, measured along `WallUp`.
  - **Clearance corridor:** one capsule lying parallel to the wall, from the hang feet up past the lip. It tests the whole swing-over-and-hang volume at once and is anchored at the hang feet so it doesn't drift with tilt.
  - **Loose facing:** within ~25°; the warp snaps the rest.
- Walking off an edge still just falls; mantle down is button-only.

### Motion
- **Warp:** `MantleDownForward` = the lip pivot (upright, facing out); `MantleDownDown` = the hang (wall-tilted, facing in, the climb's own pose).
  - Enable **rotation warp on the down window**: the ~180° turn to face the wall and the approach-angle snap resolve there.
- **Rotation:** the ~180° facing turn rides the clip and warp. The event gates only the tilt.
  - It reads the probe-cached face normal (`GetMantleDownWallNormal`), because the live grid loses the face while the character is out over the edge.
- **Input:** `FRogueTraversalInputs::bWantsToMantleDown`. It's one unified traversal input struct, so a future dash or corner input adds a bool, not a class.
- **Hand-off:** to Climbing **unconditionally**; the probe already guaranteed a face.
  - If the grid misses it on the boundary frame, climb's own exit-to-Falling recovers. **Watch that frame** in-editor, and tune via the montage tail.

### Remaining
- Montage with the two windows plus rotation warp on the down window → `MantleDownMontage`.
- `IA_MantleDown` + an **F** binding → the player BP's `Input_MantleDown`.
- The tilt-blend event notify → `MantleDownTiltBlendEventTag` (through the action system).
- The montage → climb-hang seam (the end-pose recipe below, blending into the hang).
- Verify real root motion (below), validate in-editor, and tune `Mover|Climbing|MantleDown`.

## Root motion: the retarget fix
- **Symptom:** while warping, the mesh drifts out of the capsule, even though `HasRootMotion()` is true and the capsule lands on target.
- **Cause:** the pack is UE4 Manny retargeted to UE5 Manny, and a misconfigured root-motion op left the target root flat. The capsule only moved because **SkewWarp adds translation when the anim supplies none**.
- **Tell:** the IK Retargeter's **dotted line** (root trajectory) is missing.
- **Fix:** default ops, then on **Root Motion Op → Motion Source**: **Root Motion Source = "Copy from Source Root"**, **Target Pelvis = "root"**.
  - This replaces the earlier "Generate from Target Pelvis" advice. The flat line came from Target Pelvis not being `root`.
  - Confirmed on the mantle-down clip; re-check the top-out clip if its mesh ever drifts.
- **The root lock is an anim-system step, not CMC:** `FRootMotionReset` in `DecompressPose`, gated by `Montage->HasRootMotion()` and the Root Motion Mode.
  - A CMC-less Mover character keeps the lock for free, if the clip has real root motion.
  - That's why `ForceRootLock`, `IgnoreRootMotion` and `RootMotionAttribute` all failed: there was nothing to lock.

## End pose: montage → locomotion
- **Problem:** the mantle ends in a tall arms-down stand far from the aim idle, so handing back to locomotion popped.
- **Fix: three levers together**, each closing a different gap:
  - **cut the montage tail** to shrink the pose distance at the source;
  - **Inertialization** (a node after `DefaultSlot` + the montage's Blend Out = Inertialization) so the remaining blend is velocity-continuous;
  - **ease foot IK in and out** to pin contacts across the hand-off (inertialization blends the whole pose, not contacts).
- **Extra levers:** an earlier Blend Out Trigger Time, or a per-bone Blend Profile.
- This is the reusable montage → locomotion recipe for every authored traversal (corner turns too).
