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
- **Hand-purchase gate (the SOLE coverage requirement).** The **top row is the hands** — climbability = *that row* has ≥ `ClimbTopRowMinHits` (default **1**) valid cells. There is **no torso/grip coverage-ratio gate** any more (the old `MinClimbCoverageRatio` was removed): the hands hold you on; the torso and legs are free to miss. A single centre handhold is enough to grab on and cling idle. Rationale for dropping the ratio: over the whole grip band it both under- and over-fired — a whole-top-row miss still passed at 0.6, while a good one-handhold ledge failed.
- **Move threshold (hysteresis on the same row).** Traversing needs ≥ `ClimbTopRowMinHitsToMove` (default **2**) top-row cells — enough room to shuffle the hands. With exactly 1 you cling but the climb mode zeros the along-wall intent (`CanMoveWhileClimbing()` → false); escape is jump-off or the surface changing. Idle needs 1, moving needs 2 — that split *is* the two thresholds. **`GetClimbMoveSpeedFraction()` also returns 0 when locked**, so the anim holds (playrate 0) and the hang idle↔move blend collapses to the dead-hang idle — a single handhold reads as a relaxed cling, not a frozen mid-pull.
- **Consistency gate:** every valid normal within ~35° of the average normal — rejects corners and lumpy geometry. (With coverage gone, this + the hand rule are the whole climbability test.)
- Output the **dominant plane** = averaged valid normal + averaged valid point (drives the mode), and keep the per-sample results for animation.

## Design stance: forgiving + snapping
- Thresholds lean lenient; the character snaps to the dominant plane rather than conforming exactly. Tighten only if players climb things they shouldn't.

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
- **Pre-IK caveat:** the idle↔move arm extend/bend is exactly where hand IK (milestone 5) matters — with the grip pinned, bending the arms should raise the *body* toward a fixed hold; pre-IK it drifts the hands a little across that transition, so keep it quick.

## Hang move — directional coverage (experiments; parked)
Pack has only **idle (dead-hang) + 2 horizontal (L/R)** hang-move clips — no vertical/diagonal. Attempts to cover all 8 directions from these:
- **2 real clips + braced-upper fallback for the other 6** — rejected: seams/pops at the boundary, and the braced-upper is exactly the off-looking pose.
- **BS1D of the 2 L/R moves** — the center (average of two *sideways* clips) splays; two horizontal clips can't average into a vertical pull. Putting the **idle clip at the BS centre** fixes the odd centre (down then reads as idle-extended = lowering; straight-up is the lone compromise).
- **BS2D (hang row at Y=0, braced climb at Y=±1)** — the mid-blend (half-pull arms + half-dangle legs) reads better than full dead-hang, but the half-pull **shortens the vertical stride** → vertical moves slower. Per-region bias (arms sample full vertical, legs sample horizontal/0 — two **synced** BS evals + a leg mask, or legs→CR) restores the stride but still can't conjure a vertical *pull* pose.

**Root finding.** A vertical hang-pull **cannot be synthesised from horizontal clips** — it's missing *content*, not a blend-config problem. Off-the-shelf **8-directional hang-climb clips essentially don't exist**: real games do braced climb + ledge-hang + Uncharted-style ledge *jumps*, not sustained omnidirectional pure-hang climb (a short-character, BOTW-ish mechanic). Agentic/AI authoring is out for now (coding agents can't author motion; AI mocap is rough and needs retarget + cleanup).

**Conclusion (shipping).** Live with **braced-climb upper body + hanging lower body**. Scope the hang to **idle (dead-hang) + horizontal shimmy + down (idle-extended = lowering)**. **Vertical-up is transient** — it resolves to **braced** (foot probes re-acquire footing → per-foot IK plants) or to **mantle**, so a sustained vertical-pull clip isn't actually needed. Optional later polish: a **procedural arm-pull** (up-intent → bend the arms, body rises via the FBIK drop) or **one bespoke vertical-pull clip** (author / video-mocap) if the pack author delivers.

## Wall contact — standoff controller, not a constant press
- Keeping the capsule on the wall is a **regulated pull to a target standoff**, not a constant `-WallNormal · speed` press. Each tick the mode measures the perpendicular gap from the capsule centre to the dominant **plane** (`dot(centre − planePoint, planeNormal)`) and moves toward `ClimbWallStandoff` (0 = auto = capsule radius, so the body just touches), clamped by `ClimbIntoWallSpeed`. It eases to a stop as it arrives and nudges back **out** if it has sunk past the standoff.
- **Why (the hang bug it fixes).** A constant press has nothing to brace against on a **hang** — the body is over a void, only the hands are on wall — so it crept the capsule *into* the surface, which (a) pushed the detection ray origins (at the capsule centre) *inside* the wall so the rows read empty → a **gradual fall**, and (b) slid the body down. Symptoms were "auto-falls with the top rows still green" and "sinks in + slides down"; `ClimbIntoWallSpeed = 0` stopped the fall but left a gap (nothing pulls you in) — the tell that the press was doing double duty. Measuring to the **infinite plane** keeps the standoff valid even though the wall only physically exists up at the hands, so the hang holds at the right distance without being bulldozed through. (This is why the trace-origin-backoff idea was dropped — regulating the press removes the root cause instead of hardening detection against it.)

## Mode tags — `Mover.Climb.*`
- Every climb-family mode carries **one child of `Mover.Climb`** in its constructor (`GameplayTags.AddTag`), the same mechanism as Mover's own `Mover.IsOnGround` / `Mover.IsInAir`: `Mover.Climb.Sustained` (sustained climb), `.MantleUp`, `.MantleDown`, `.CornerTurn`. Declared in `RogueSharedGameplayTags`.
- **Queries:** `IsInClimbState()` = `HasGameplayTag(Mover.Climb, non-exact)` — the parent matches any child, so a future climb mode joins by adding its own child tag and no caller changes. `IsClimbing()` / `IsMantling()` / `IsMantlingDown()` / `IsCornerTurning()` = their child tag, exact. Same sync-state buffer as `GetMovementModeName()`, so no tick of disagreement. (Mode *names* still key registration and transitions.)
- **Scope decision: tags for MODE only.** Discussed tagging everything (lower-body / IK states) for a fully tag-driven AnimBP; rejected for now. Tags are booleans that don't enforce exclusivity; per-limb IK flags must travel with their data (the world-origin grip bug was a flag/data split); continuous values (alphas, intent, playhead) can't be tags. Coarse body states (e.g. `Mover.Climb.Hanging` as a Mover external tag) get a tag only once a *gameplay* consumer needs one (stamina, ability blocking). Note `UMoverComponent::HasGameplayTag` is not `BlueprintThreadSafe`; if the AnimBP moves to thread-safe update, feed it a game-thread snapshot (a tag container + plain data — container `HasTag` *is* thread-safe), Lyra-style.

## Data split: sim vs anim
- **Sim** (climb mode / sync state): minimal — dominant normal + point only.
- **Anim** (component, game-thread, like `CurrentWallHits`): full per-sample grid + the `IsLowerBodyHanging()` / `CanMoveWhileClimbing()` bools, the per-foot `LeftFootProbe`/`RightFootProbe` (`bHanging` + foothold `TargetLocation`/`TargetNormal`), and the legacy `LowerBodySupport`/Upper/Left/Right scalars.

## Per-limb refinement — IK on top (Control Rig)
- The body state is coarse; individual hands/feet are resolved by **IK, not by more states or finer coverage**.
- **Foot probe — done (C++, detection):** `ProbeFoot` sphere-sweeps from the **animated foot bone** toward the dominant plane (origin backed off the wall so it starts in free space), within `FootProbeReach`; a climbable same-face hit within reach ⇒ foothold (`bHasTarget` + point/normal), else `bHanging`. Runs each pre-sim tick while climbing (`RefreshFootProbes`), reading `foot_l`/`foot_r`. Debug-draws the sweep (green plant / red hang). Following the live foot is what lets the plant/hang track motion. Feet only depend on the grid for the **plane**, not the leg rows.
- **Release hysteresis (Schmitt trigger) — done.** First pass at the edge flicker: a foot that was already **planted** searches wider (radius + reach both grow by `FootProbeReleaseHysteresis`), so it takes a clearly-gone foothold to flip to hanging — harder to release than to acquire. `RefreshFootProbes` carries each foot's previous `bHanging` into `ProbeFoot` for this. Damps *noise*, but see next.
- **The flicker is a FEEDBACK LOOP, not noise (key insight).** The probe rides the **animated foot**, whose pose depends on the plant/hang decision it's driving: the climb pose puts the foot further out (over the void) than the hang/tuck pose, so plant→foot-out→miss→hang→foot-in→hit→plant… a limit cycle. Hysteresis on the probe *extent* can't fix it because the *measured position itself* crosses the boundary each flip. Two real fixes: (a) decouple the probe origin from the pose (decide from a body/capsule anchor, snap only the IK target to the hit), or (b) break the cycle's return edge.
- **Idle latch — done (chosen fix).** The cycle is only *visible* at idle (motion masks it), and a foot dangling near a surface reads far better than one pawing the air. So while the player isn't driving movement (`GetClimbMoveSpeedFraction() ≤ FootPlantIdleInputDeadzone`), a **hanging foot is not allowed to re-plant** (`RefreshFootProbes` forces it back to `bHanging`). That removes the hang→plant edge, making hang an *absorbing* state at idle → it converges to the stationary dangle instead of cycling. Applied to the **status** (bool), not the AnimBP blend alpha, so state and presentation don't diverge and the upcoming foot-lock/IK reads a stable decision. Moving re-enables normal re-planting (where the loop is hidden). Fuller fix if it ever shows *during* motion: the capsule-anchor decouple, or foot-lock (release on hip-to-target distance) with the plant IK.
- **Actuators + hand grips — superseded by the stance pins below.** The body-anchored hand grips (`RefreshHandGrips` / `LeftHandGrip` / `RightHandGrip`, with `HandGripStanceHalfWidth`/`HandGripAnchorHeight`) are **retired**: a hand now aims at its baked landing on the destination clip (predictive placement), and the idle FBIK anchoring is just the hand pins on the idle clip's grips. The old gotcha — *never publish a zero-vector target; put the pin weight on a flag, not the target* — is baked into `FClimbLimbPin` (it freezes its last `Location` when unpinned, so a blend-out never targets world origin). See **Limb IK — stance pins**.

## Limb IK — stance pins (Control Rig)

Per-limb world-locked contact pins the Control Rig plants hands/feet onto. **C++ owns *where* and *how much*; the CR applies it.** One `FClimbLimbPin` per limb (`LeftFootPin`/`RightFootPin`/`LeftHandPin`/`RightHandPin`), refreshed each pre-sim tick by `RefreshLimbPins`. Two resolvers: **predictive** for the sustained climb (`ResolveLimbPinPredictive`), **reactive** for corner turns (`ResolveLimbPin` — the corner clips carry no baked contacts).

- **What the CR reads.** `Weight` (0..1 — the single on/off, `>0` = pinned) drives the effector's position (+rotation) alpha; `LocalLocation`/`LocalNormal` are the target in **mesh-component space** (feed these — a world position flings the rig). `Location`/`Normal` are the raw world pin (debug + the break test).
- **Stance vs swing.** An authored per-clip float curve per limb (`Stance_FootL/R`, `Stance_HandL/R`) = *holding* (IK on) vs *swinging* (free, follows the clip). It is also the IK blend profile. A limb with no curve stays dormant — opt-in, one limb at a time.
- **Contact point = probe point = IK point.** Aim the baked contact bone, the socket knobs and the CR effector tip at the real contact (a palm, `ball_l`), not the wrist/ankle, so `LimbPinSurfaceOffset` — the "lip" standoff pushed out along the normal — reads the same on every limb. Applied **once at consume** (into `LocalLocation`, from the raw on-surface pin), so it can't accumulate on a held pin.
- **Target spring.** The world target is eased by a critically-damped spring (`bUseLimbIKSpring`, `LimbIKSpringStiffness`) — zero lag on a held pin (its target doesn't move), absorbs target *jumps*. Past `LimbIKSpringSnapDistance` it snaps instead (a stale/far target) — except an idle-retreat move, which always eases.

### Sustained climb — predictive placement
- **Bake (offline).** `URogueClimbContactBaker` (editor Animation Modifier) auto-detects each plant at the stance curve's rising crossing of `PlantDetectThreshold` — two per limb on the two-strike clips; a hold that wraps the loop seam is handled circularly — and stores the contact bone's **component-space** landing in `URogueClimbContactMetaData`. It also stamps a monotonic `ClimbPhase` 0→1 curve, so the runtime can locate itself in the clip (a stance value repeats; a phase value doesn't).
- **Destination clip.** Moving = the dominant blendspace sample (`GetDominantClimbClip` queries `ClimbBlendSpace` with the input the component itself drives — note the axis swap: BS input = `(Intent.Y, Intent.X)`). Idle = `ClimbIdleClip`, baked like the move clips (flat stance → one landing per limb = the authored idle grips).
- **Per limb, per tick.** Phase from the live `ClimbPhase` curve → `W` = the **destination clip's own** stance curve at that phase (never the blended pose — crossfades smear it) → plant = nearest in phase, circularly (`FindNearestPlant`). While unlocked, the target = `MeshXform · Landing` traced onto the wall (`ResolvePlantTarget`), re-resolved every frame so the body's travel is folded in — no fall-short. `W ≥ PredictiveLockStanceValue` → world-lock; the **reach leash** then keeps the held pin within each limb's own reach, dragging it along the wall only if the body would leave it behind (see *Stopping well*, layer 5). Weight = `W`, smoothed (`PredictiveWeightInterpSpeed`) so a playhead jump eases in instead of snapping. A trace miss (over a void) aims at the predicted anchor itself.
- **Release / stop handling** — what happens when the player lets go (the staged retreat, the re-grip swing + peel, the weight shift, the reach leash + reach cap, the bad-segment skip) and the direction-change blend now live in their own section: **Stopping well — the idle retreat** below.
- **"The transition-to decides."** Code state picks the regime (moving / idle / corner — discrete, doesn't crossfade); the destination clip's own curve supplies the clean signal; inertialization smooths the pose. Applies to every future state boundary.

### Corner turn — reactive pins
- Each limb probes **both faces** from its live bone and pins to the nearest valid hit, so it auto-selects the face it's on as the turn carries it across. The corner clip's stance curves gate pin/release (`LimbStanceThreshold`). Acquire at the hit, then **hold rigidly — never re-probe a held pin** (one-way ratchet: re-probing reads the limb the IK already moved, and the pin creeps). The reach leash drags a held pin along its face to keep it within reach; one it can't keep there holds (the reach cap bends it) until the corner clip's stance ends. Weight = the stance curve.
- **Void fallbacks.** Arm over a void → edge-search along the segment from the intended (void) grip back to the last grip (two samples, nearest-to-void wins) → that hit, else grace-hold the last grip, else play the clip. Leg → stays unpinned (the foot probe's `bHanging`, debounced by `FootHangCooldownSeconds`, drives the hang/tuck).

- **Why it stays drift-free** — four rules, each closing a feedback/accumulation path: (1) never re-probe a held pin; (2) break off a body point, not the IK'd limb; (3) apply the surface offset once at consume, from the raw pin; (4) predictive targets come from the **baked anchor** resolved against the mesh root — feedback-free — never from the live, IK'd limb.

*History:* the sustained climb first ran **reactive** pins too — acquire at the live limb on the stance edge, an idle state-hold with a settle window (`IdlePinSettleDelay`), an intent fade near release (`LimbPinRelaxBand`), roll-back release + re-acquire with a reuse tolerance (`LimbPinReuseDistance`), an enter-climb grace, and a load-bearing-hand exemption. Predictive placement replaced all of it (removed 2026-11-01); the reactive resolver survives only for corner turns.

## Stopping well — the idle retreat (showcase)

> **The problem.** The climb runs on in-place clips: the body follows each clip's `ClimbDistance` curve, and predictive IK world-pins every hand and foot on the holds the clip is reaching for. When the player lets go, the animation blends to the idle cling - but the limbs are still pinned on the last strike's holds, which rarely match the idle grips, and in the middle of a reach the torso is ahead of the root (`ClimbDistance` only credits travel during the pull). Left alone, a stop produces a run of bad poses: a relaxed torso with limbs stretched to far holds, limbs sliding or snapping back all at once ("floating"), arms locked straight, and pops whenever the targets swap.
>
> **The goal.** Make every stop read like a climber settling into a rest - re-grip deliberately, one support at a time, with the body answering each move - **without** auto-completing moves the player didn't ask for, **without** a full-body solver (the rig is four two-bone IKs), and **without** anything that can drift.

*[Capture: the same mid-strike stop with every layer off, then on.]*

### The layers, in the order they act

| # | Layer | Answers | Where |
|---|---|---|---|
| 1 | Bad-segment skip | "Don't stop *here*." | strike markers + `ResolveClimbMoveInput` |
| 2 | Staged, sequenced retreat | "Which limb moves, and when?" | `BuildIdleRetreatPlan` / `AdvanceIdleRetreat` |
| 3 | Re-grip swing + effector peel | "How does a limb move from one grip to another?" | `EvaluateRegripSwing` |
| 4 | Weight shift (pelvis, hip-in, chest) | "How does the body answer?" | `RefreshClimbWeightShift` → Control Rig |
| 5 | Reach leash + reach cap | "What if the body would leave a limb out of reach?" | `LeashLimbTargetsWithinReach`, `CapToLimbReach` |
| 6 | Clip-switch blend + re-grip | "And when the player changes direction instead?" | `RefreshLimbPins` + the predictive resolver |

**Two questions, kept apart.** Every pinned limb answers two separate questions. *Where should it be?* - decided by **events** (a clip switch, committing to idle). *Can it reach where it is?* - a condition that can go wrong at any moment (the body keeps travelling, the blend differs from the dominant clip, the idle clip pulls the torso back). Mixing them produced a growing pile of special cases (an over-extension release while moving, a slide at idle, a body-travel break distance); separated, the whole policy is four lines:

| When | What the limb does |
|---|---|
| Clip switch, and the new clip has the limb in stance more than `LimbRegripMinDistance` away | **Arcs** to the new plant - all limbs at once (layer 6) |
| Idle committed | **Schedule**: arcs to the idle grips, one support at a time (layers 2-4) |
| Otherwise (same clip, idle→move, corner turn) | **Holds**; the clip decides when it swings and releases |
| Always, on top of the above | **Reach leash**: a pinned or fading target never sits past the limit - it's dragged along the wall by just the excess (layer 5) |

All of it is presentation on top of the existing pins: the capsule, the Mover sim and the pin placement rules are untouched (one exception - the skip holds the input for a moment), so the drift-free rules of *Limb IK — stance pins* still hold.

### 1. Bad-segment skip — don't stop in a stretch that can't look good
**What you see:** releasing in the few frames where a stop always looks wrong (mid-reach) plays on to the end of that short stretch, then stops as usual. Releasing anywhere else stops at once.

- **Bad-segment skip (strike markers) — a stop can't land in a stretch that looks bad.** A clip may carry short `StrikeCommit` → `StrikeEnd` marker pairs (sync markers or notifies; names `ClimbStrikeCommitMarkerName` / `ClimbStrikeEndMarkerName`) around only the stretches where stopping produces a bad pose - e.g. the reach, where the in-place torso leads the root and a stop strands the just-planted limbs far from it. A release inside a segment keeps feeding the last input (`ResolveClimbMoveInput`, called from the player's `ProduceInput`) until the clip crosses the segment's end marker, then stops as usual (blend to idle + retreat). It's a **skip, not an auto-complete**: the end pose is wherever the segment ends, not necessarily a full strike, and segments are kept as short as possible so the forwarding isn't felt. Untagged clips, and everything outside a segment, stop on release as normal.
  - **Decision:** one query on the dominant clip at the live `ClimbPhase` - is the most recent strike marker before the playhead (circular, so a segment may wrap the loop seam) a `StrikeCommit`? It both starts the skip (on release) and ends it (crossing `StrikeEnd` flips the answer). Holding new input in any direction passes straight through and cancels it.
  - **Authoring:** alternate Commit, End (two Commits in a row run on to the next End). A clip with commits but no `StrikeEnd` anywhere never skips - it would otherwise latch the input forever - and logs a warning.
  - **Wiring:** `bCommitClimbStrikes` toggles it; `IsClimbStrikeCommitting()` is true while skipping. The AnimBP's climb move→idle rule must use `GetClimbMoveSpeedFraction()` (it sees the held input), not raw keys or velocity. Debug: `[ClimbStrike]` logs each release's decision ("finishing the strike" / "before the commit marker, going idle" / "no strike markers, going idle") and the segment end ("reached the strike end").
  - *History:* built first as **strike commit — finish the pull** (a commit marker per strike, so a release past it finished the whole strike into a four-point hold near idle). Dropped as unwanted auto-completion, then repurposed as these short skip segments once a mid-reach stop proved to be the case that breaks the retreat. Identifiers keep the original "strike / commit" names so tuned values and authored markers survive.

### 2. Hold, then sequence — one support at a time
**What you see:** nothing lets go the instant the player stops (so a quick resume is seamless); then the limbs re-grip toward the idle pose one diagonal at a time, the worst-placed first, and the body never hangs on fewer than two diagonal holds. Contralateral pairs (hand + opposite foot) are the point: a diagonal support resists the "barn door" swing that a same-side pair would let the body make.

- **Idle — staged, sequenced retreat.** On move→idle nothing releases: gripping limbs **hold** their move grips (a quick resume is seamless), and a limb caught mid-swing — already unpinned — approaches its idle grip at once. After `IdleRetreatDelaySeconds` of committed idle the sequencer (`BuildIdleRetreatPlan` / `AdvanceIdleRetreat`) retreats the still-gripping limbs: those displaced more than `LimbRegripMinDistance` → a **contralateral pair** (LH+RF / RH+LF) when both of a diagonal must move, else singles → most-displaced step first (a pair counts its sum) → each step waits for its limbs to arrive (spring within `RegripArrivalTolerance`). Never both hands or both feet at once, so a planted diagonal always remains. In a pair the hand leads (`bIdleRetreatPairHandLeads`) and the follower trails by `IdleRetreatPairStagger`.

**Schedule at rest, respond while moving.** The sequencer runs **only for the retreat to idle**. At rest the character is stable, the viewer studies the pose, and the player has handed over control, so floating is most visible and scheduling costs nothing - and it's the system's job to avoid it. Out-of-reach limbs never break it: the leash drags them along the wall while they stay pinned, and the plan waits until the dragging stops (layer 5). While moving the input is the authority: every policy change (a clip switch, a re-grip) applies **at once** to all limbs it concerns, because lag between stick and body reads as unresponsive, motion masks a brief float, and a float caused by a fast input change is the player's choice.

### 3. The step — lift, travel, place, with a peel
**What you see:** a limb peels off its hold - the edge nearest the body first, fingertips/toes last - lifts, crosses and comes straight down onto the new grip, instead of sliding along the wall. Short re-grips are quick little hops; long ones are slower, higher arcs.

- **Re-grip swing — lift, travel, place.** A re-gripping limb doesn't slide along the wall: it travels a **cubic Bézier** (`EvaluateRegripSwing`) — `P0` = where it was released, `P1 = P0 + N·h`, `P2 = target + N·h`, `P3` = the target — so it peels straight off its grip, crosses, and comes straight down onto the new grip (the procedural step of spider/creature rigs). Duration = distance ÷ `RegripSwingSpeed` (≥ `RegripSwingMinDuration`); peak lift = distance × `RegripSwingLiftRatio` (≤ `RegripSwingMaxLift`; handles scaled by 4/3 since a cubic's midpoint reaches ¾ of its handle height). Timing = smoothstep, or the optional `RegripSwingProfile` curve. During a swing the arc **replaces** the target spring (which would lag and flatten it); the spring is parked on the arc so it takes over at rest when the swing lands, and landing is the sequencer's arrival. `bRegripArc` off = the old spring slide.
  - **Effector peel.** During the swing the published normal (what the CR orients the hand/foot from) leans toward the in-plane "away from the body" direction by `RegripHandPeelAngle` / `RegripFootPeelAngle` × `4UV` — the arc's lift normalised to 1 at its peak — so it's flush on both grips and most tilted at the top. With the palm/sole laid across the normal, the edge nearest the body comes off first and the far edge (fingertips/toes) leaves last and lands first. The direction is fixed per swing from the body, not the travel, so a limb peels the same way whichever way it moves. C++ only; negative angle flips it.

**One motion for every re-grip.** The arc isn't only the idle retreat: every time a LOCKED limb moves to a new hold it travels this same swing - the idle sequencer, the clip-switch re-grip (layer 6) and the leash's last resort (a limb it can't keep on its face, layer 5). All go through `BeginRegripSwing`: unpin, re-lock in the same tick, and the arc starts from the point the rig was last shown. That's why it's the *re-grip* swing (settings `Mover|Climbing|LimbIK|RegripSwing`) - it started life as the idle retreat's swing and kept that name until it served every re-grip.

*[Capture: a close-up of one hand's retreat, arc off vs on, peel 0 vs default.]*

### 4. The body answers — weight shift
**What you see:** just before a limb lifts, the hips move onto the holds that stay (and press into the wall when a hand lets go); the chest tilts toward the supporting hand and leans back so the arms stay straight; when the limb lands the body settles back. A diagonal pair barely moves the body - the support diagonal already runs through it.

- **Weight shift — the body moves onto the remaining holds.** The CR is per-limb two-bone IK (no full-body solver), so without this nothing moves the body when a limb lets go. A limb is **unloaded** (`IsLimbUnloaded`) from its sequencer step's start until it lands — *pinned ≠ loaded*: during the lead-in it's still held, but the body has stopped leaning on it. `RefreshClimbWeightShift` (end of `RefreshLimbPins`):
  - **Shift** = (centroid of the loaded grips − centroid of all four) × `WeightShiftLateralGain` sideways / `WeightShiftVerticalGain` up-down, in the wall frame (WallUp = gravity flattened onto the wall), capped at `WeightShiftMaxDistance` (two-bone IK can't stretch). A released limb counts from where it left (its pin is already re-locked at the idle grip). Single limb → toward the other three; a diagonal pair → ≈ 0 (the support diagonal runs through the centre — the anti-"barn door" reason for contralateral pairs). All four loaded → exactly 0, recomputed every tick, so nothing accumulates.
  - **Hip-in** while a hand is unloaded: `−N · WeightShiftHipInDepth`, faded to 0 as the wall leans back into a slab (`WeightShiftHipInSlabFadeAngle`; climbers stand off a slab). Not under the along-wall cap.
  - **Timing**: the lift waits `WeightShiftLead` after the unload, so the body visibly moves first; `0` = lift at once (shift during the swing). Smoothed with `FMath::CriticallyDampedSmoothing` (`WeightShiftSmoothingTime`), no overshoot. Speed-capped at `WeightShiftMaxSpeed` (the per-tick move and the smoother's rate): crossed limbs put the load centroid far across, and the next step can swing it to the other side, which the smoothing alone would cover in a blink.
  - **Chest** (`ClimbChestRotation`), read back off the *smoothed* shift so it moves with the body: (a) tilt — the chest's top toward the sideways shift, `WeightShiftChestTiltPerCm`; (b) lean back by `atan(hip-in / WeightShiftShoulderHeight)` × `WeightShiftChestLeanScale`, which keeps the shoulders where they were so the planted arms are unaffected. Twist deliberately left out.
  - **Published** in mesh space: `ClimbBodyShift` (add to `pelvis`, global) and `ClimbChestRotation` (split across the spine chain, global), both applied in the CR **before** the limb IK so the world-locked limbs bend to absorb it. Presentation only — the pins resolve from the mesh root, not the pelvis, so the drift-free rules hold. Idle retreat only (move clips author their own weight shift). Settings are `EditAnywhere` for live PIE tuning.

*[Capture: a single-hand retreat, `bIdleWeightShift` off vs on, debug draw showing the centroids.]*

### 5. Never out of reach — the reach leash and the reach cap
**What you see:** when the body moves away from a limb that's holding on - a stop as the idle clip pulls the torso back, a direction change, the blend leaning another way - the hand or foot is dragged along the wall just enough to stay within reach, instead of locking straight or letting go; a limb fading out after a clip switch is dragged the same way, so it fades with the body. Anything else that's briefly out of reach bends softly toward its target.

- **Reach leash — a limit, not a move.** `LeashLimbTargetsWithinReach`, every climbing tick (moving, idle and the corner turn), before the idle block and the resolvers. For each **held** target - pinned, or fading out on its frozen grip after a clip switch (row "IK on → IK off" in layer 6); not one approaching a plant (re-resolved from the live body every tick) or on a re-grip arc - it measures root → IK target (the pin pushed out by `LimbPinSurfaceOffset`; root = `Left/RightHandRootBoneName` = upperarm, `Left/RightFootRootBoneName` = thigh, last evaluated pose, so the weight shift counts) against `LimbReachLeashRatio` × the limb's straight reach (reference-pose chain length root → contact tip - hands: `Left/RightHandSocketName`, the palm sockets; feet: `Left/RightFootReachTipName` = ball - × mesh scale, cached per mesh asset). One ratio for every limb, but each limb's own length - hands and feet stop at their own stretch.
  - **The drag.** The reachable part of the target's plane is a disc: centred on the root's projection, radius √(L² − h²), h = the root's distance from the plane. Past the limit the target moves to the disc's nearest point - along the face, the shortest way back into reach, by **exactly the excess** - then re-traces onto the real surface there (`ProbeContactToward`: a bumpy face is followed; past a wall's edge or into a corner there's no same-face surface, so a limb is never dragged off its face). No state, no speed, no hysteresis: the pin moves only while, and as fast as, the body pulls it - it reads as the body dragging a hand that won't let go.
  - **The spring moves with it.** The target spring absorbs *jumps*; the leash is a continuous constraint, so the spring state is translated by the same delta. Otherwise the spring's lag (~0.2 s at stiffness 120) would trail the shown limb behind the dragged target, back out of reach.
  - **Last resort.** A *pinned* limb it can't keep on its face within reach (the face has no reachable point left - the body turned away from it or pushed off - or that point is off the surface): the climb lets go and **re-grips** on the arc onto this tick's plant (the resolver re-locks it right after; out of any retreat plan, `DropLimbFromRetreatPlan`); the corner turn **holds** - a reactive re-acquire would land on the same unreachable face - and the reach cap bends it until the corner clip's stance ends.
  - **At idle** this is what fixes the classic stop: the idle clip reclaims the torso while the limbs are still on their move grips, often leaving *both hands* out of reach. The leash drags both at once - neither leaves the wall, so "never both hands off" holds - and the retreat plan is built only once no pinned target was dragged that tick, so it measures from settled grips and keeps its one-support-at-a-time order. A not-yet-released limb the weight shift pulls out of reach mid-sequence is dragged, never released out of turn.
  - `bLimbReachLeash` off = held pins stay wherever they are (the cap still bends them). Debug: a magenta line root → dragged target (plus the probe's magenta hit) while it drags; a red root → target line and `[ClimbReach] XX can't stay on its face within reach (N% of its reach) - re-grips` for the last resort.
- **Reach cap (visual, every IK target).** The leash covers held targets; the cap is the visual net for the rest - a target approaching a plant, a clip-switch ease sliding through unreachable points, the start of a re-grip arc, a corner pin the leash couldn't keep in reach, the spring's few frames of lag. `CapToLimbReach` (in the component-space conversion, after the spring): the target handed to the rig is pulled toward the limb's root so its distance never exceeds `LimbReachLimit` x reach - untouched below `LimbReachSoftStart` x reach, then `d' = s + (M - s)(1 - e^(-(d - s)/(M - s)))` (slope 1 at the start, asymptotic to M; M <= s = hard cap). An out-of-reach target reads as "reaching toward it, still bent" and the cap fades by itself as the target returns in reach - no weight change, no hysteresis. Purely visual: pins and the spring keep the REAL target. Keep `LimbReachLeashRatio` ≤ `LimbReachSoftStart` and the cap never touches a held pin. A re-grip swing starts from the last SHOWN point (`FClimbLimbPin::DisplayedWorld`). Elbow bend vs ratio (equal segments): 2·acos(r) - 16° at 0.99, 36° at 0.95, 51° at 0.9, 74° at 0.8. Known gap: the intentionally straight dead hang gets bent (hands short of holds) - to be special-cased later. Debug: yellow line = the unreachable part (shown point -> real target).
- Settings: `Mover|Climbing|LimbIK|Reach`.

### 6. Direction changes — blend, don't pop
**What you see:** pressing a new direction mid-strike (W then D) hands each limb over to the new clip's holds - planted limbs step across on the arc, the rest blend - instead of snapping. All at once, following the input (see *Schedule at rest, respond while moving*, layer 2).

**Per-limb policy on a clip switch.** "IK on" = the clip has the limb in stance (pinned or approaching a plant); "IK off" = it has it mid-swing (the clip drives it, IK weight 0).

| # | Old clip | New clip | What the code does | What you see |
|---|---|---|---|---|
| 1a | IK on, **locked** | IK on | **Re-grip** (`RegripLockedLimbsOnClipSwitch`): if the new clip's plant is farther than `LimbRegripMinDistance` (8 cm - shared with the idle retreat), the limb lets go and the re-grip arc carries it onto the new plant, re-locked the same tick; within the distance it keeps its hold. Once per limb per plant. | The planted limb **steps across** to where the new clip wants it (or stays put if that's nearly the same spot). |
| 1b | IK on, **approaching** | IK on | Its target switches to the new clip's landing; `bClipSwitchEase` lets the spring ease it past the teleport guard until it arrives (within 1 cm); locks there if the new stance is high enough. | The limb **glides** from where it was heading to the new hold. |
| 2 | IK on | IK off | Release, but inside the clip-switch blend window the weight **eases out** instead of dropping to 0, the target frozen on the old hold meanwhile - dragged by the reach leash if the body would leave it out of reach. | The limb **lets go gradually** into the new clip's swing, following the body. |
| 3 | IK off | IK on | Approach the new clip's plant: the target jumps there unseen (weight 0), then the weight **eases in** at the window's rate; may lock at once. | The limb **blends from the new clip's pose onto its new hold**. |
| 4 | IK off | IK off | Nothing - weight stays 0. | The limb follows the clips (the blendspace's own crossfade). |

Timing: the IK switches at the raw clip change (the component reads the unsmoothed input), while the pose crossfades over the blendspace's own smoothing. The IK's blend window is **read off the blendspace asset** (`GetClimbClipSwitchBlendTime`), so the two can't drift apart: Sample Smoothing's Weight Speed if set (`1 / speed`, or `~0.87 / speed` with its ease-in/out "Smoothing" on - the engine's critically damped weight spring, ~95% there), else the axes' Input Interpolation time, else 0 (the pose pops, so does the IK). `bClipSwitchBlend` off = the old instant switch. Settings: `Mover|Climbing|LimbIK|ClipSwitch`. Debug: `[ClimbIK] blendspace clip switch: A -> B (limbs blend over N s, from the blendspace)` and `[ClimbIK] XX re-grips onto <clip>'s plant (N cm away)`.

- **Blendspace clip switch — blend, don't pop.** A direction change mid-strike (W then D: Up -> Up-Right) flips the dominant clip, so every limb's destination landing changes at once. Two things used to pop it: (1) the target spring's teleport guard - cross-clip landings are easily > `LimbIKSpringSnapDistance` apart, so the target snapped; (2) release zeroed the weight in one tick - when the new clip says the limb is mid-swing, a limb held at full weight dropped straight to FK. Within ONE clip both are correct and invisible (plant-to-plant jumps happen in the swing at weight 0; an authored stance ramp is ~0 before release), so the fix keys off the switch itself: `RefreshLimbPins` compares the dominant clip with the last MOVING tick's (`LastDominantClimbClip`, cleared at idle / off the sustained climb, so idle<->move edges don't count). On a change: LOCKED limbs the new clip also plants elsewhere re-grip on the arc (case 1a in the table above), and every pin gets `bClipSwitchEase` (its target eases through the spring past the teleport guard until it catches up within 1 cm), and a window as long as the pose's crossfade opens (read off the blendspace) in which the resolver eases the weight - in and OUT (release fades instead of zeroing; the frozen Location keeps it aimed where it was) - at a rate covering ~95% in that time. `bClipSwitchBlend` off = the old instant switch. Debug: `[ClimbIK] blendspace clip switch: A -> B`.

### What didn't work (and why it shaped the rest)
- **Releasing all four limbs at once on committed idle** (the first version) - several limbs moving with nothing anchoring the body read as floating. → the sequencer (layer 2).
- **Finishing the whole strike on release** ("strike commit") - it fixed the pose but auto-completed moves the player had abandoned. → repurposed as short skip segments (layer 1).
- **Holding the move pose until the retreat finished** (frozen blendspace) - it froze the extended torso over a capsule that hadn't caught up; worse than blending back. Removed.
- **Moving the capsule under the body on a stop** (idle settle: for ~0.5 s after a stop the capsule followed the animated pelvis, so the torso stayed put while the pose blended to idle) - physically motivated (the in-place torso leads the root mid-reach; the root is bookkeeping, the mass is where the torso is), but the capsule glide read oddly, and it was the one piece of the stop handling that changed the simulation. Removed; the reach leash (layer 5) handles the stretched limbs it was meant to fix, and the stop handling is presentation-only again.
- **Releasing a limb that went out of reach** (the over-extension release: let go and re-grip on the arc, idle and moving) - at a stop both hands often went out of reach at once, and releasing both floated; while moving it added a hop just before the clip's own swing, and "once per plant" left a limb stretched when its new plant was out of reach too. Then **sliding** it back within reach at a fixed speed (idle only) - fixed the stop, but a fixed speed can't keep up with a body that keeps moving. → the reach leash (layer 5): a limit instead of a move, the same everywhere.
- **A body-travel break distance** (`LimbPinBreakDistance`, capsule travel since the grab) - one centimetre value for hands and feet, measured from the capsule rather than the limb; hands broke far more often than feet. → the leash's per-limb reach ratio.
- **Fading the IK to the clip's own pose while a target is out of reach** - rejected at design time: the clip's limb mid-strike belongs to another motion (it would jump elsewhere, then back), and it costs two transitions plus hysteresis. → the reach cap (layer 5), which is continuous and stateless.

### Debugging and tuning
- **One switch:** `game.climbing.DebugDraw` (on by default).
  - cyan / green spheres - a pin approaching / locked (the LOGICAL pin);
  - white sphere (size = IK weight) + white line to the pin - the target the rig is ACTUALLY given (after the spring, a swing and the reach cap); a green pin the limb hasn't reached yet is the weight fading in, the spring easing, a swing in flight, or the cap;
  - orange - the retreat plan (move grip → idle grip, step numbers, 3 s), the swing's arc point and peeled normal;
  - amber / white points - the load-bearing / all-four centroids; amber line + sphere - the body shift;
  - magenta line - the reach leash, taut (limb root → dragged target) while it drags; red line - a held limb it couldn't keep in reach (2 s on a climb re-grip; continuous while a corner pin holds); yellow line - the part of a target the reach cap is holding back;
  - logs: `[ClimbRetreat]` (plan), `[ClimbReach]` (a leash re-grip), `[ClimbStrike]` (every release's skip decision), `[ClimbIK]` (clip switches, re-grips).
- **Settings:** `Mover|Climbing|LimbIK|IdleRetreat`, `…|RegripSwing`, `…|Reach`, `…|ClipSwitch`, `…|Placement` (`LimbRegripMinDistance`), `…|Smoothing`, and `Mover|Climbing|WeightShift`. Every layer has an off switch for A/B comparison, and the newer settings are `EditAnywhere` for live tuning in PIE.

### Known limitations
- A leash drag is a hand or foot sliding on the wall: invisible over a few centimetres, but a long drag would read as skating. Not expected - clip switches arc, and clips swing limbs on schedule - but it's the thing to watch.
- The reach cap also bends poses that are straight on purpose (the dead hang) - special case pending.
- The leash and the reach cap read last frame's pose (one frame behind - a couple of centimetres at climb speed; the cap absorbs it).
- The weight shift runs during the idle retreat only - the move clips author their own.

### Capture checklist (showcase)
- Mid-strike stop: every layer off vs on.
- Re-grip swing: `bRegripArc` off vs on; peel angles 0 vs default.
- Weight shift: `bIdleWeightShift` off vs on, with the debug centroids visible.
- Mid-reach stop with and without a skip segment (`bCommitClimbStrikes`).
- W → D: `bClipSwitchBlend` off vs on; `bRegripOnClipSwitch` off vs on (planted limbs keep their old holds vs step across).
- A stop that strands both hands: `bLimbReachLeash` / `bCapLimbReach` off vs on (straight arms or a double release vs both hands dragged, then the one-at-a-time retreat).
- One pass with the debug draw on, to show the machinery.

## Procedural tuck — floating-limb fallback
- When a foot's probe returns `bHanging` (no reachable target) → blend that foot's IK weight to 0 **and** apply a small **additive/procedural tuck** (pull the foot toward the body/wall) so it reads as dangling/reaching, not frozen over open air. IK-weight-0 alone isn't enough — the FK pose is the braced "planted on air" pose (image 4); the tuck is the half that actually kills the artifact.
- The tuck is cheap (a procedural offset, not a full solve), so it's the first actuator to wire on top of the probe — before the full two-bone plant IK.
- This covers **single-foot** misses; a whole-lower-body void (both feet `bHanging`) is caught earlier by the **gross hang** (dead-hang upper body), not by FK-fallback.

## Target pipeline (Mover-native)
`Climb mode (state + GetPredictedTrajectory)` → `PoseSearch / Motion Matching picks clip` → `Motion Warping aligns root to target` → `Control Rig IK refines limbs` → `procedural tuck fallback`.

## Animation ↔ movement sync (foot-lock + surge)

**Problem.** A climb clip's motion is a burst rhythm — *reach (body ~still) → pull (body surges) → reach…*. Moving the capsule at a smooth, near-constant velocity against it makes gripped hands/feet slide on the wall. Fix: **the anim drives the body speed** — the body follows the clip, not the other way round.

### Distance curve — the body follows the anim

**Reasoning chain (the part to remember):** a gripped hand stays world-still (no skate) only if the body advances exactly as far as the planted contact slides back through the in-place clip. The clip's **root-motion sibling *is* that travel** — so author it as a curve and move the body by it, and the slide cancels at *every instant*, not just on average.

Each clip carries a monotonic **`ClimbDistance`** float curve: cumulative along-wall travel in cm, 0 at frame 0 → the stride at the end (flat = hold/reach, steep = pull). Bake it from the root-motion sibling's root path with **`URogueClimbDistanceBaker`** (editor Animation Modifier), then hand-tune keys. Each pre-sim tick:

```
d_now  = blended ClimbDistance            // anim-instance curve read, same path as the stance curves
Δd     = d_now − d_prev                   // + stride ONLY if it dropped ~a full stride in one tick (loop-seam wrap)
v_body = Δd / dt                          // along-wall MAGNITUDE; DIRECTION stays the solver's
```

Riding on the anim, it is:

- **phase-locked** — the body's pull lands on the anim's pull; no separate sim phase to drift,
- **auto-shaped** — the clip's asymmetric two pulses (big, then small ~⅓) are in the curve, no hand-tuned surge,
- **playrate-proof** — the curve advances by `playrate · dt`, so body speed scales with playrate automatically → **playrate IS the climb-speed knob**. No reference speed, no `V_clip`, nothing to keep consistent,
- **blendspace-correct** — direction curves blend, so diagonals stay consistent (a curveless direction dilutes the blend → *every* clip needs the curve).

```
playrate = GetClimbMoveSpeedFraction() · scale    // scale = free motion-speed multiplier (e.g. 1.5 = 50% faster climb)
```

**Blended-curve gotchas (both shipped).** Differencing a *blended* curve is fragile — crossfades and per-direction phase offsets make it non-monotonic:

- **Wrap detection needs a real jump.** Only a one-tick drop of more than half a stride is a loop-seam wrap; a gradual decrease (crossfade to idle, diagonal phase mismatch) is *zero* advance. An "any negative = wrap" rule added a full stride every tick of the move→idle blend-out → the body lunged forward a long way after release.
- **Input gate.** At push ≤ `FootPlantIdleInputDeadzone` (the limb-IK idle threshold) the curve stops driving and the solver's `ClimbDeceleration` brakes the body to a stop. The curve keeps genuinely advancing while the move clip blends out — ungated, the body glides on with no input.

**`ClimbMaxSpeed` is only a safety cap** on a spiking blended curve (fast direction change, authoring error). It caps the *instantaneous* speed — the pull peak, not the average — so keep it above `peak ClimbDistance slope × max playrate`, or it clips real pulls and the planted limbs skate.

**Deadlock caveat (still applies).** Playrate must come from the *steady* push, never from the body speed — else a flat hold ⇒ speed 0 ⇒ playrate 0 freezes the anim at the reach:

```
playrate = GetClimbMoveSpeedFraction() · scale   // steady round push amount, NOT |GetClimbMoveIntent()|
v_body   = from ClimbDistance
```

*History:* this replaced (2026-10-31) a speed-led model — body at `fraction · ClimbMaxSpeed`, playrate synced to it via `scale = ClimbMaxSpeed / V_clip` (matched only the loop average), plus a per-clip cadence surge curve ÷ `ClimbCadenceReferenceSpeed` (to match the instant). Three numbers had to stay consistent; the distance curve inverts the authority so only the curve has to be right.

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

`|GetClimbMoveIntent()|` would be `√2` on a diagonal → plays 1.41× too fast — and since the body follows the anim, it would *move* 1.41× too fast too. The square remap is for the blendspace *axes* only.

### Code touchpoints
- `RefreshClimbDistanceSpeed()` (pre-sim) — reads `ClimbDistanceCurveName` off the anim instance; wrap detection + input gate → `GetClimbAlongWallSpeed()`.
- `RogueClimbMode::GenerateMove` — `LinearVelocity = solverDir · min(v_body, ClimbMaxSpeed)` (along-wall only; into-wall standoff unscaled). Invalid (idle / curve missing) → the solver's own velocity stands (brakes with no input).
- `URogueClimbDistanceBaker` (editor module) — bakes `ClimbDistance` onto the in-place clip from its root-motion sibling's root path (`AxisWeights`, `DistanceScale` to trim).
- `GetClimbMoveIntent()` — round intent (`ComputeRoundClimbWallIntent`) → `CircleToSquare` → blendspace **axes**.
- `GetClimbMoveSpeedFraction()` — `min(1, |round intent|)` → blendspace **playrate**.
- Playrate wiring + per-clip curves are AnimBP / asset-side.

### Why scalar, not full root motion
The clip drives along-wall *magnitude* only; the mode keeps *direction* (wall plane) + player control. Full root-motion climb fights Mover (fixed-tick + rollback + looping blendspace) and arbitrary geometry for a rhythm the distance curve already gives (it is, in effect, scalar root motion). Determinism is a *networking* concern, moot for single-player / Standalone. Root motion + motion warping is reserved for **discrete traversals** (mantle, corner turns) on known geometry.

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

**Clip requirements:** real root motion incl. the ~90° root **yaw** (retarget recipe in the mantle's *Root motion* note); **Force Root Lock ON** (played in a Sequence Evaluator — only that flag keeps the mesh on the capsule outside montages); Root Motion Root Lock = Ref Pose (the default — the capsule math assumes it); the `CornerRest` notify on the 4-limb frame.

**AnimBP:** a corner state entered on `IsCornerTurning()` → Sequence Evaluator (Sequence = `GetCornerTurnAnimation()`, Explicit Time = `GetCornerTurnPlayhead()`) → hang legs layered by `IsLowerBodyHanging()` (thigh branch filters, mesh-space rotation) → masked `idle_additive` (spine-up) ramped by `IsCornerTurnHolding()`. Clip frame 0 matches the climb idle, so entry/exit blends are near-free.

**Hang state through the turn — freeze, don't latch.** The foot probes self-gate on `IsClimbing()`, so in the turn's own mode they reset `IsLowerBodyHanging()` to false → the AnimBP's smoothed hang alpha decayed mid-turn and re-rose on hand-off. A separate latched `IsCornerTurnLowerBodyHanging()` was the first fix, but two hang bools means two smoothed alphas that reset on every climb↔corner switch. Now `RefreshFootProbes` simply **returns early while corner-turning**, freezing the per-foot results and the gross hang at their pre-turn values (the probes can't run meaningfully mid-turn anyway: one target plane while the body straddles two, from swinging foot bones). So `IsLowerBodyHanging()` is the single hang source for climb **and** corner; `IsCornerTurnLowerBodyHanging()` remains only as an equivalent for existing AnimBP nodes. Corollary: the idle foot latch carries over the hand-off — a turn that *settles* onto the new face (stick released) keeps hanging feet hanging until the player moves, even if the new face has footholds.

**Debug (`game.climbing.DebugDraw`) during the turn:** cyan Mid / green End targets, silver = where the *unwarped* clip would end each strike (the silver→target gap is the warp correction — tune `CornerInner/OuterEndOffset` and Mid to shrink it), yellow = current capsule target (magenta while holding at the rest).

## Tuning knobs
`MinHorizontalDegreesToStartClimbing`, grid dimensions & footprint (width/height/reach), `ClimbTopRowMinHits` (grab/cling) + `ClimbTopRowMinHitsToMove` (traverse), `ClimbLowerBodyRowCount` (leg-footing band), normal-consistency angle, per-foot IK search radius + interp/hysteresis, tuck offset. (Hang-blend smoothing lives in the AnimBP now, not a C++ knob.)

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

## Status & roadmap

**Done**
1. **Sustained climb** — detection sweep → 3×5 coverage grid → dominant-plane orient; custom `URogueClimbMode` + contextual entry/exit transition.
2. **Climb anim sync** — 8-dir blendspace, distance-curve speed (body follows the anim via per-clip `ClimbDistance`, baked from the root-motion siblings), round→square input remap.
3. **Mantle top-out** — first authored traversal: two-phase motion-warped root-motion montage → Walking; established the reusable traversal skeleton.

**Next — big milestones, roughly ordered**
4. **Lower-body hang blend** — *C++/detection done* (gross `IsLowerBodyHanging()` now = BOTH feet without a foothold, from the per-foot probes; hand-purchase gate replaced the coverage ratio; `CanMoveWhileClimbing()` locks traversal to a single handhold; `GetClimbMoveSpeedFraction()` gated to 0 when locked). **Remaining (AnimBP):** `FInterpTo` the hang bool into an alpha, then blend per the *Hang anim recipe* above — `Blend(braced, Blend(hang_idle, climb_move ⊕ dangle_legs, speedFraction), hangAlpha)` — plus a subtle dead-hang sway.
5. **Per-foot / per-hand IK (Control Rig)** — *done and in use*: predictive placement for the sustained climb (offline contact baker + `ClimbPhase`, destination-clip stance weight, approach→lock, target spring), the full stop/release handling (**Stopping well** — bad-segment skip, staged contralateral retreat, Bézier swing + peel, weight shift with hip-in and chest, reach leash + reach cap, clip-switch blend), and reactive two-face pins for corner turns; the Control Rig consumes `Weight` + `LocalLocation`/`LocalNormal` and `ClimbBodyShift` / `ClimbChestRotation`. Full design in **Limb IK — stance pins** and **Stopping well — the idle retreat** above. **Remaining:** a dead-hang special case for the reach cap; the per-foot **procedural tuck** on `bHanging`. (Sync markers for phase-aligned in-between blends are deferred: climbing is 8-directional for now, so the blendspace sits on one clip except while changing direction.)
6. **Corner turns** — *C++ done*: probe + debug draw (*Corner turns — probe*) and the input-driven turn (*Corner turns — input-driven turn*: stick-driven playhead, settle-to-rest, deterministic per-strike warp onto Mid/End, plane seeding). **Remaining:** clip setup (root yaw check, Force Root Lock, `CornerRest` notify, assign the 4 clips), AnimBP corner state (Sequence Evaluator + hang-legs layer + masked breathing additive), then in-editor tuning (commit fractions, Mid/End offsets via the silver ghosts).
7. **Climb feel polish** — wall-distance vs into-wall bias; **edge-clamp** (don't walk off a side edge — a cheap safety add-on).
8. **Uneven surfaces** — climb across surface intersections / non-planar geometry.

**Later polish** — ledge shimmy; stamina + UI.

## TODO

Confirmed: a Blend Space Player that isn't in a sync group ticks as a single-animation context, and in that case it syncs its samples by marker automatically once they have markers. So authoring these markers also gives you the phase-aligned diagonal blends that were still pending.

## (1) Strike commit: done

Your choices are implemented as you specified. I haven't compiled it; the header changed, so do a full rebuild.

**How it works:**
- **Decision:** `IsPastClimbStrikeCommit()` takes the dominant clip and the live `ClimbPhase`, and asks whether the most recent strike marker before the playhead is a `StrikeCommit`. The check wraps around the loop. That single check both **starts** the commit (on release) and **ends** it, because crossing `StrikeEnd` flips the answer.
- **Mechanism:** the game keeps holding the stick for you. `ProduceInput` now gets its climb input from `MoverComp->ResolveClimbMoveInput(raw)`. That passes the raw input through and remembers it. On a release past a commit marker, it keeps returning the last input until the strike end, then clears it for good, so nothing can re-commit while idle. The anim, the `ClimbDistance` body speed, the IK and the retreat timer all read input, so they finish the strike together with no special cases. After the strike end it's the normal idle + retreat.
- Release before a commit marker behaves as before. A clip with no markers never commits. `bCommitClimbStrikes` toggles the whole thing for A/B testing.

**Your authoring, on each of the 8 clips:** add sync markers `StrikeCommit` → `StrikeEnd` for each strike, so four per clip. Use the same names in the same order on every clip. Strike 2's `StrikeEnd` can sit on the last frame or on frame 0; the loop handling covers both.

**One AnimBP check:** the climb move→idle transition must use `GetClimbMoveSpeedFraction()`, not raw key state. Otherwise the anim leaves the move state while the body is still finishing. With the debug cvar on, you'll see `[ClimbStrike] released past the commit marker…` and `…reached the strike end` in the log.

## (2) Spider-style stepping: real projects

- **[PhilS94 – Unity Procedural IK Wall-Walking Spider](https://github.com/PhilS94/Unity-Procedural-IK-Wall-Walking-Spider)** ([showcase video](https://www.youtube.com/watch?v=4Ff9XeACUH4)). The closest match to climbing: it walks on walls, around corners and on ceilings. A leg steps when its IK can no longer reach. The target is predicted from an anchor plus the body's expected travel, then raycast onto the surface. A step manager decides which legs may step: a queue, or an "alternating tetrapod gait". The README doesn't describe the arc itself; that's in the code.
- **[80.lv – IK-driven procedural spider in Godot 4.5](https://80.lv/articles/ik-driven-procedural-spider-locomotion-in-godot-4-5)**. A raycast step sensor, a destination predicted from velocity, and the foot moves there in an arc. Legs move in **diagonal pairs, one pair at a time**.
- **[mmmeri/ue-procedural-limbs](https://github.com/mmmeri/ue-procedural-limbs)**. Unreal (UE4, Blueprints). A per-limb `LimbAnimator` plus a `LimbCoordinator` that stops neighbouring limbs moving at once, and the body leans on springs in response. It includes a GIF demo, and it's the closest to your engine.
- **[80.lv – procedural spider animation in UE5](https://80.lv/articles/tutorial-procedural-spider-animation-in-unreal-engine-5)**. A video tutorial over an hour long for multi-legged walks in UE5.
- **[Robin Six – procedurally animated spider](https://sixrobin.itch.io/proceduralspider)**. A small, readable project, inspired by Codeer's well-known clips.
- **Talks:** David Rosen's GDC 2014 ["An Indie Approach to Procedural Animation"](https://discussions.unity.com/t/an-indie-approach-to-procedural-animation-gdc-video-talk/538228) (Overgrowth), which covers lively motion from very few keyframes. "The Rain World Animation Process" (GDC 2016, [background](https://unity.com/en/blog/exploring-procedural-design-rain-world)), about limbs as points driven by code.

**What to look for in them:**
- **The step itself.** Watch how quickly the foot lifts, how high the arc goes, and whether it sets down softly.
- **Coordination.** It matches what you designed independently: diagonal pairs, one group at a time, a manager deciding who moves. Our sequencer is that manager.
- **The difference from our case.** They step continuously while walking. We only need the arc for IK-driven retreats, because the clips already handle the swings during movement.
- **The body's response.** In the better ones the body shifts onto the supporting legs as one lifts. That's the weight-shift layer on our list.

None of these pages states a step-arc formula outright, so for the exact curve you'd read the code. The Bézier lift-travel-place path from my last message is the standard choice. Tell me if it matches what you see in these and I'll build it.

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

- Mantle down — **C++ implemented & compiling** (`URogueMantleDownMode` / `URogueMantleDownTransition` / `RefreshMantleDownProbe` / `FRogueTraversalInputs`). Remaining: montage + warp windows, `IA_MantleDown` + F binding, tilt event tag, and in-editor validation. See *Mantle down — implementation* above.
