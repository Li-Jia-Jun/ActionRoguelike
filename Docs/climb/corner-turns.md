# Climbing — Corner turns

Turning around inner (concave) and outer (convex) corners: the probe that finds the corner and the two turn poses, the input-driven turn that scrubs the clip with the stick, and why it uses its own warp instead of Motion Warping. Part of the [Climbing docs](README.md).

## Corner turns — probe (C++ done; turn itself pending)

Detection + the two turn transforms, published per side (`LeftCorner`/`RightCorner`, `GetClimbCorner(bRight)`), mode-agnostic so either turn design can consume it. `RefreshCornerProbes` runs both sides every pre-sim tick while climbing (stick-independent, like the mantle-down affordance).

**Hand row only.** The probe runs at **one height: the coverage grid's top (hand) row** (`ClimbGridTopOffset` + the climb up-bias along the wall) — no separate height knob, so probe and climb always agree on where the hands are. *Why:* a valid climb only needs the hand row (the hand-purchase gate), so a valid turn doesn't need more either. The first version also probed the torso and required both heights to agree (notch rejection); that **failed every corner while hanging**, where both faces exist only up at the hands — the lower rays found nothing, the edge test read the void as "face ended", the wrap ray missed. It also covers the opposite case (old face braced, new face only has the hand band). Trade-off accepted: no multi-height notch rejection; the End-pose hand-row replay (below) still rejects a side face too small to climb.

**Classification is the ray, not a later test.** At the hand row:
- **Inner** — a side ray along `±WallRight`, in front of the face, reach `CapR + CornerInnerArmDistance`. A climbable hit facing back at us = concave corner; any other hit = obstruction (also rules out an outer corner on that side).
- **Outer** — edge test: a forward ray at `CornerOuterArmDistance` laterally; if the *same* face is still there, no corner. Otherwise a **wrap ray** (the mantle-down backward-ray trick turned 90°): start past the edge and `CornerOuterWrapDepth` *behind* the face plane, fire back toward the body; the climbable hit is the side face, normal pointing the way we were moving.
- Turn angle `acos(N1·N2)` must sit in `[CornerMinTurnDegrees, CornerMaxTurnDegrees]` (60–120). Below ~35° the sustained climb's normal smoothing already follows the bend; 35–60 is an open gap.

**Corner frame — no edge scan.** The corner is the exact intersection line of the two planes (`FMath::IntersectPlanes2`), oriented up and slid to the character's height. Both poses are built off it, same formula for both types:
```
Away2 = ⊥ to the line, in face 2, pointing away from the corner   (toward +N1 inner, −N1 outer)
End   = C + N2·standoff + Away2·EndOffset,   rot = MakeFromZX(WallUp2, −N2)   // climb's own pose → seamless hand-off
Mid   = C + Bisector·r,   r = inner ? standoff / cos(θ/2) : standoff          // inner: off BOTH faces; outer: orbit the edge
```
Mid = strike 1 (round the corner), End = strike 2 (onto the new face) — matching the clips' two strikes, and the two warp targets later. `standoff` = capsule radius by default (same rule as `ClimbWallStandoff`).

**Validation (always-succeed):** both poses fit (skin-shrunk overlap), and the coverage grid's **hand row is replayed from the End pose** against face 2, requiring the **move** threshold — so the climb grid is guaranteed to accept the face at hand-off and a turn never strands you in a cling.

**Debug (`game.climbing.DebugDraw`):** inner ray yellow/red, edge test orange (edge) / silver (face continues), wrap ray yellow/red, white corner line, Mid ghost cyan, End ghost green (available) / orange (new face lacks hands) / red (blocked), hand-row replay green/red, label `R OUTER 90 deg`.

**Why the turn will be its own movement mode** (whatever its input model): mid-turn the climb grid is invalid *by construction* (it straddles two faces → hand purchase / consistency fail → the climb transition's exit-to-Falling would fire). Same reason the mantle is a distinct mode. A mode can still read input every tick, so "mode" does not mean "non-interactive".

## Corner turns — input-driven turn (C++ done; clips + AnimBP pending)

**Why not BOTW-style.** BOTW (as far as footage shows) has no corner clips: the corner is just a sharp bend in the sustained climb — an *in-place, smooth-paced* cycle, so the body can follow any procedural path and the facing simply turns. Our climb clips are *impulse*-paced (timing baked in → the `ClimbDistance` curve), and the corner clips are **root motion** — they encode the *path* (the arc round the edge) as well as the timing. Swap the path for a procedural one and the hands slide. So an input-driven turn must keep the clip's path and timing coupled.

**The stick drives the clip's PLAYHEAD.** `t += push · CornerTurnPlayRate · dt`; the capsule goes to `Warped(t)` (the clip's own root path) and the AnimBP evaluates the pose at the same `t`. One parameter drives both, so the hands stay phase-locked at **any** push — stopped or reversing included. (This is what Steps A+B approximate for the in-place climb; with root motion it's exact.)

**Settle to the nearest rest.** Three rests: `R0` clip start (= climb idle → finishing here returns to Climbing on the old face), `R1` the 4-limb corner rest (a named notify, `CornerTurnRestNotifyName`, default `CornerRest`), `R2` clip end (→ Climbing on the new face). Released mid-strike, the playhead settles forward if past the strike's commit point (`CornerTurnStrike1/2CommitFraction`), else back. Only `R1` is a corner-specific hold (`IsCornerTurnHolding()` → breathing additive). Rest-vs-motion again: it only ever *stops* in a rest pose.

**Warp = a pure function of `t` (not UE Motion Warping).** The modifier in `RootMotionModifier.cpp` is incremental and forward-only (distributes the remaining correction from `PreviousPosition` to the window end, marks windows done on exit) — it breaks under reverse/settle-back (pausing alone is fine). Instead, per strike (`R0→R1` onto the probe's **Mid**, `R1→R2` onto **End**):
```
Clip(t)  = MeshRel⁻¹ · RootMotion(strikeStart → t) · MeshRel · StrikeStart     // root motion is mesh-space; conjugate by the mesh offset/yaw
loc(t)   = S + d(t) + (Dtarget − Dclip) · α(t),   α = d(t)·Dclip / |Dclip|²      // rank-one skew: corrects in proportion to the clip's own progress
rot(t)   = Slerp(I, Target·ClipEnd⁻¹, β(t)) · Clip(t).rot,   β = clip rotation so far / total
```
Exact at both ends, zero correction while the clip body is still (reach phases), deterministic under scrubbing. No layered move / montage for this traversal.

**Hand-off.** `FinishCornerTurn` **snaps the climb plane** to the face we end on (else the normal smoothing swings the capsule after the clip already turned it, and the first grid stacks on a stale plane → false exit-to-Falling). Collision relaxed in the mode (both poses fit-tested). `ProduceInput` keeps sending raw stick while turning; "right" stays right relative to the character all the way round, so holding the entry direction keeps going. The climb transition's re-grab is guarded by `!IsCornerTurning()`.

**Clip requirements:** real root motion incl. the ~90° root **yaw** (retarget recipe in [the mantle's *Root motion* note](mantle.md#root-motion-the-retarget-fix)); **Force Root Lock ON** (played in a Sequence Evaluator — only that flag keeps the mesh on the capsule outside montages); Root Motion Root Lock = Ref Pose (the default — the capsule math assumes it); the `CornerRest` notify on the 4-limb frame.

**AnimBP:** a corner state entered on `IsCornerTurning()` → Sequence Evaluator (Sequence = `GetCornerTurnAnimation()`, Explicit Time = `GetCornerTurnPlayhead()`) → hang legs layered by `IsLowerBodyHanging()` (thigh branch filters, mesh-space rotation) → masked `idle_additive` (spine-up) ramped by `IsCornerTurnHolding()`. Clip frame 0 matches the climb idle, so entry/exit blends are near-free.

**Hang state through the turn — freeze, don't latch.** The foot probes self-gate on `IsClimbing()`, so in the turn's own mode they reset `IsLowerBodyHanging()` to false → the AnimBP's smoothed hang alpha decayed mid-turn and re-rose on hand-off. A separate latched `IsCornerTurnLowerBodyHanging()` was the first fix, but two hang bools means two smoothed alphas that reset on every climb↔corner switch. Now `RefreshFootProbes` simply **returns early while corner-turning**, freezing the per-foot results and the gross hang at their pre-turn values (the probes can't run meaningfully mid-turn anyway: one target plane while the body straddles two, from swinging foot bones). So `IsLowerBodyHanging()` is the single hang source for climb **and** corner; `IsCornerTurnLowerBodyHanging()` remains only as an equivalent for existing AnimBP nodes. Corollary: the idle foot latch carries over the hand-off — a turn that *settles* onto the new face (stick released) keeps hanging feet hanging until the player moves, even if the new face has footholds.

**Debug (`game.climbing.DebugDraw`) during the turn:** cyan Mid / green End targets, silver = where the *unwarped* clip would end each strike (the silver→target gap is the warp correction — tune `CornerInner/OuterEndOffset` and Mid to shrink it), yellow = current capsule target (magenta while holding at the rest).

## Why writing a custom warping instead of using the built-in motion-warp?

UE's Motion Warping is a per-tick correction. Every tick, SkewWarp re-solves from two things: the clip motion remaining from the previous playhead position to the window end, and where the character actually is now. It then skews that tick's slice so the remaining motion lands on the window's single end target (RootMotionModifier_SkewWarp.cpp:152/153/169). That design suits forward-played montages that can be bumped by collision, network corrections or moving targets. But it could fail in reverse because the warp keeps stepping toward the end target with a freshly solved skew, and the path drifts. The built-in code also don't have mid-reverse handling so a warp window will always skew to its end. 

The corner turn needs the stick to scrub the playhead freely, including pausing, reversing and settling back to rest poses. So it uses a warp that's a fixed function of the playhead instead: pose(t) = clip path(t) + Error · Alpha(t), with Error fixed when the turn starts and Alpha the progress along the clip's own motion. The same t always gives the same pose, so every in-between pose retraces exactly. The correction still only happens while the clip is moving, and the endpoints still land exactly on the targets. What we give up is ad-hoc robustness (reacting to bumps and moving targets) and editor-authored warp windows. Neither matters for a static corner with collision off in single-player. Moving surfaces would keep the same function in the surface's local space.

The custom warp works better in replication. It is still a step function with minimal state (easier to correct), and since the character position is fully determined by `t`, remote character will follow the real path.


| | Built-in Motion Warping | Custom playhead warp |
|---|---|---|
| **Model** | Correction re-solved every tick: remaining clip motion → (target − current position) | Fixed function of the playhead: `pose = Warped(t)` |
| **Authoring** | Notify-state windows in the clip, a modifier type + settings per window, named targets | Strike boundaries from the `CornerRest` notify; skew + slerp in code; correction spread over the whole strike |
| **Playing forward** | ✅ | ✅ |
| **Pausing** | ✅ (zero delta → no motion, no correction) | ✅ |
| **Reversing / settling back** | ❌ Windows only work forward: a window activates only while the previous position is inside it, is removed once past its end or on a jump in position, and records its start position on activation (`RootMotionModifier.cpp:245-276, 313-317`). Scrubbing back into a finished window recreates it from wherever you are → the path drifts | ✅ Retraces exactly |
| **Moving target / bumps** | ✅ Adapts (reads the actual position) | ❌ Ignores them (fine: static corner, collision off) |
| **Mover integration** | Through `FLayeredMove_AnimRootMotion`, which runs on the montage's own clock (forward only) | Direct, in the corner mode |
| **Debug** | `a.MotionWarping.Debug` | Silver "unwarped clip end" ghosts under `game.climbing.DebugDraw` |

**Splitting each clip into two strike clips doesn't change this:** each half is still forward-only under the built-in warp. Splitting only enables the fully built-in pipeline for a *strike-gated* turn (each strike a forward-played montage, no scrubbing or settle-back). With the custom warp, one continuous clip is simpler: a single Sequence Evaluator, and the rest pose is just a time.

**Possible middle ground (not done):** author standard `MotionWarping` notify-state windows on the clips (targets `CornerMid`/`CornerEnd`, SkewWarp settings), and have the code read their times, targets and flags while still evaluating them with the playhead warp. That gives standard authoring plus control over *where* in each strike the correction happens, and the same clips would also work with the built-in warp if played forward as montages.
