# Climbing — Hang

When the lower body hangs (no footholds) and how it is animated: the gross hang + per-foot probes, the hang anim recipe, the parked hang-move experiments, and the procedural tuck. Part of the [Climbing docs](README.md).

## State classification — gross hang (both feet) + per-foot probes (done, C++ side)
- Two layers, not one: a **coarse gross hang** (whole lower body → the dead-hang *upper body*) **and** a **per-foot** plant/hang decision (the legs). Still never a per-limb *anim state* — the per-foot part is IK/tuck on top of the two-legged cycle.
- **Gross hang = BOTH feet lack a foothold** (`IsLowerBodyHanging()`), set from the per-foot probes. This is the whole-lower-body-over-void case that warrants the dead-hang arms. **Evolution:** it started as a grid test (bottom-row 2×2 per leg, hang if *either* empty). That over-fired — a single foot over a void (the corner in image 4: left foot planted, right foot floating in braced pose) forced *both* legs to hang off a perfectly good foothold, or, kept braced, left the foot planted in air. So the trigger moved to **both feet** and the single-foot case moved to per-foot IK.
- **Per-foot foothold probe (replaces the grid's leg rows).** Each foot sphere-sweeps from its **animated bone** toward the dominant plane (`ProbeFoot` → `LeftFootProbe`/`RightFootProbe`): a reachable climbable hit ⇒ plant target; none ⇒ that foot hangs/tucks. Because it follows the live foot, the plant/hang (and later the IK snap) can happen **during motion**. It's decoupled from the grid's leg rows but still uses the grid's **plane** as the sweep reference. This is the standard AAA approach (per-hand/foot IK traced to the climb surface); the dead-hang **move** L/R clips stay shelved.
- **Smoothing lives in the AnimBP,** not C++: the mode publishes the raw bools each tick (gross `IsLowerBodyHanging()` + per-foot `bHanging`) and the AnimBP/Control Rig `FInterpTo`s them into blend/IK alphas. So "eases into the dangle" is anim-side blends of sim-side binaries.
- `LowerBodySupport`/`Upper`/`Left`/`Right` fraction scalars are still published (cheap, handy for debug/HUD) but are **not** the hang driver any more — prune once nothing reads them.
- The same grid still yields other states for free: top rows miss → mantle/top-out; a whole side misses → edge/reach (future).

## Hang anim recipe — idle = dead-hang, move = climb + legs (AnimBP)
- The hang's upper body is driven by **motion, not geometry**. Inside the lower-body-hang blend:
  - **Idle** (`GetClimbMoveSpeedFraction()` → 0) = the **dead-hang idle** clip (arms fully extended, weight on the skeleton). A resting hang must relax to straight arms.
  - **Moving** (→ 1) = the **climb move** blendspace upper body ⊕ dangling legs (per-bone), the *arms bent into the pull*.
  - Blend the two by the speed fraction: `HangPose = Blend(hang_idle, climb_move ⊕ dangle_legs, GetClimbMoveSpeedFraction())`, then `Blend(braced, HangPose, smoothed IsLowerBodyHanging())`. Legs dangle at both ends. Effort scales with speed for free, and it stays phase-locked to the playrate (same fraction).
- **Why not a torso-support / "full-hang" state.** Tried classifying a dead-hang from the torso rows missing (rope/pipe). Rejected: the believability axis proved to be **rest-vs-motion**, not wall contact — a *stationary* bent-arm mid-pull is the uncanny pose (a moving one reads as effort), and that's a speed question. The dead-hang **move** L/R clips are shelved; revisit only for a true rope/pipe dead-hang *shimmy*.
- **Pre-IK caveat:** the idle↔move arm extend/bend is exactly where hand IK ([stance pins](limb-ik.md)) matters — with the grip pinned, bending the arms should raise the *body* toward a fixed hold; pre-IK it drifts the hands a little across that transition, so keep it quick.

## Hang move — directional coverage (experiments; parked)
Pack has only **idle (dead-hang) + 2 horizontal (L/R)** hang-move clips — no vertical/diagonal. Attempts to cover all 8 directions from these:
- **2 real clips + braced-upper fallback for the other 6** — rejected: seams/pops at the boundary, and the braced-upper is exactly the off-looking pose.
- **BS1D of the 2 L/R moves** — the center (average of two *sideways* clips) splays; two horizontal clips can't average into a vertical pull. Putting the **idle clip at the BS centre** fixes the odd centre (down then reads as idle-extended = lowering; straight-up is the lone compromise).
- **BS2D (hang row at Y=0, braced climb at Y=±1)** — the mid-blend (half-pull arms + half-dangle legs) reads better than full dead-hang, but the half-pull **shortens the vertical stride** → vertical moves slower. Per-region bias (arms sample full vertical, legs sample horizontal/0 — two **synced** BS evals + a leg mask, or legs→CR) restores the stride but still can't conjure a vertical *pull* pose.

**Root finding.** A vertical hang-pull **cannot be synthesised from horizontal clips** — it's missing *content*, not a blend-config problem. Off-the-shelf **8-directional hang-climb clips essentially don't exist**: real games do braced climb + ledge-hang + Uncharted-style ledge *jumps*, not sustained omnidirectional pure-hang climb (a short-character, BOTW-ish mechanic). Agentic/AI authoring is out for now (coding agents can't author motion; AI mocap is rough and needs retarget + cleanup).

**Conclusion (shipping).** Live with **braced-climb upper body + hanging lower body**. Scope the hang to **idle (dead-hang) + horizontal shimmy + down (idle-extended = lowering)**. **Vertical-up is transient** — it resolves to **braced** (foot probes re-acquire footing → per-foot IK plants) or to **mantle**, so a sustained vertical-pull clip isn't actually needed. Optional later polish: a **procedural arm-pull** (up-intent → bend the arms, body rises via the FBIK drop) or **one bespoke vertical-pull clip** (author / video-mocap) if the pack author delivers.

## Procedural tuck — floating-limb fallback
- When a foot's probe returns `bHanging` (no reachable target) → blend that foot's IK weight to 0 **and** apply a small **additive/procedural tuck** (pull the foot toward the body/wall) so it reads as dangling/reaching, not frozen over open air. IK-weight-0 alone isn't enough — the FK pose is the braced "planted on air" pose (image 4); the tuck is the half that actually kills the artifact.
- The tuck is cheap (a procedural offset, not a full solve), so it's the first actuator to wire on top of the probe — before the full two-bone plant IK.
- This covers **single-foot** misses; a whole-lower-body void (both feet `bHanging`) is caught earlier by the **gross hang** (dead-hang upper body), not by FK-fallback.
