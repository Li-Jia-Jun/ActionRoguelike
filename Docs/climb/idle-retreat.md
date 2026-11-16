# Climbing — Stopping well: the idle retreat

What the limbs and body do when the player lets go or changes direction, layer by layer. Builds on the [stance pins](limb-ik.md). Part of the [Climbing docs](README.md).

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

All of it is presentation on top of the existing pins: the capsule, the Mover sim and the pin placement rules are untouched (one exception - the skip holds the input for a moment), so the drift-free rules of the [stance pins](limb-ik.md) still hold.

### 1. Bad-segment skip — don't stop in a stretch that can't look good
**What you see:** releasing in the few frames where a stop always looks wrong (mid-reach) plays on to the end of that short stretch, then stops as usual. Releasing anywhere else stops at once.

- **Bad-segment skip (strike markers) — a stop can't land in a stretch that looks bad.** A clip may carry short `StrikeCommit` → `StrikeEnd` marker pairs (sync markers or notifies; names `ClimbStrikeCommitMarkerName` / `ClimbStrikeEndMarkerName`) around only the stretches where stopping produces a bad pose - e.g. the reach, where the in-place torso leads the root and a stop strands the just-planted limbs far from it. A release inside a segment keeps feeding the last input (`ResolveClimbMoveInput`, called from the player's `ProduceInput`) until the clip crosses the segment's end marker, then stops as usual (blend to idle + retreat). It's a **skip, not an auto-complete**: the end pose is wherever the segment ends, not necessarily a full strike, and segments are kept as short as possible so the forwarding isn't felt. Untagged clips, and everything outside a segment, stop on release as normal.
  - **Decision:** one query on the dominant clip at the live `ClimbPhase` - is the most recent strike marker before the playhead (circular, so a segment may wrap the loop seam) a `StrikeCommit`? It both starts the skip (on release) and ends it (crossing `StrikeEnd` flips the answer). Holding new input in any direction passes straight through and cancels it.
  - **Authoring:** alternate Commit, End (two Commits in a row run on to the next End). A clip with commits but no `StrikeEnd` anywhere never skips - it would otherwise latch the input forever - and logs a warning.
  - **Wiring:** `bCommitClimbStrikes` toggles it; `IsClimbStrikeCommitting()` is true while skipping. The AnimBP's climb move→idle rule must use `GetClimbMoveSpeedFraction()` (it sees the held input), not raw keys or velocity. Debug: `[ClimbStrike]` logs each release's decision ("finishing the strike" / "before the commit marker, going idle" / "no strike markers, going idle") and the segment end ("reached the strike end").
  - *History:* built first as **strike commit — finish the pull** (a commit marker per strike, so a release past it finished the whole strike into a four-point hold near idle). Dropped as unwanted auto-completion, then repurposed as these short skip segments once a mid-reach stop proved to be the case that breaks the retreat. Identifiers keep the original "strike / commit" names so tuned values and authored markers survive.

### 2. Hold, then sequence — one support at a time
**What you see:** nothing lets go the instant the player stops (so a quick resume is seamless); then the limbs re-grip toward the idle pose one diagonal at a time, the worst-placed first, and the body never hangs on fewer than two diagonal holds. Contralateral pairs (hand + opposite foot) are the point: a diagonal support resists the "barn door" swing that a same-side pair would let the body make.

- **Idle — staged, sequenced retreat.** On move→idle nothing releases: gripping limbs **hold** their move grips (a quick resume is seamless), and a limb caught mid-swing — already unpinned — approaches its idle grip at once. After `IdleRetreatDelaySeconds` of committed idle the sequencer (`BuildIdleRetreatPlan` / `AdvanceIdleRetreat`) retreats the still-gripping limbs: those displaced more than `LimbRegripMinDistance` → a **contralateral pair** (LH+RF / RH+LF) when both of a diagonal must move, else singles → most-displaced step first (a pair counts its sum) → each step waits for its limbs to arrive (the arc lands; with the arc off, the follow within `RegripArrivalTolerance`). Never both hands or both feet at once, so a planted diagonal always remains. In a pair the hand leads (`bIdleRetreatPairHandLeads`) and the follower trails by `IdleRetreatPairStagger`.

**Schedule at rest, respond while moving.** The sequencer runs **only for the retreat to idle**. At rest the character is stable, the viewer studies the pose, and the player has handed over control, so floating is most visible and scheduling costs nothing - and it's the system's job to avoid it. Out-of-reach limbs never break it: the leash drags them along the wall while they stay pinned, and the plan waits until the dragging stops (layer 5). While moving the input is the authority: every policy change (a clip switch, a re-grip) applies **at once** to all limbs it concerns, because lag between stick and body reads as unresponsive, motion masks a brief float, and a float caused by a fast input change is the player's choice.

### 3. The step — lift, travel, place, with a peel
**What you see:** a limb peels off its hold - the edge nearest the body first, fingertips/toes last - lifts, crosses and comes straight down onto the new grip, instead of sliding along the wall. Short re-grips are quick little hops; long ones are slower, higher arcs.

- **Re-grip swing — lift, travel, place.** A re-gripping limb doesn't slide along the wall: it travels a **cubic Bézier** (`EvaluateRegripSwing`) — `P0` = where it was released, `P1 = P0 + N·h`, `P2 = target + N·h`, `P3` = the target — so it peels straight off its grip, crosses, and comes straight down onto the new grip (the procedural step of spider/creature rigs). Duration = distance ÷ `RegripSwingSpeed` (≥ `RegripSwingMinDuration`); peak lift = distance × `RegripSwingLiftRatio` (≤ `RegripSwingMaxLift`; handles scaled by 4/3 since a cubic's midpoint reaches ¾ of its handle height). Timing = smoothstep, or the optional `RegripSwingProfile` curve. During a swing the arc **replaces** the IK follow (which would lag and flatten it); the follow is parked on the arc so it takes over at rest when the swing lands, and landing is the sequencer's arrival. `bRegripArc` off = the follow slides the target across instead.
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

- **Reach leash — a limit, not a move.** `LeashLimbTargetsWithinReach`, every climbing tick (moving, idle and the corner turn), before the idle block and the resolvers. For each **held** target - pinned, or fading out on its frozen grip after a clip switch (row "IK on → IK off" in layer 6); not one approaching a plant (re-resolved from the live body every tick) or on a re-grip arc - it measures root → IK target (the pin pushed out by `LimbPinSurfaceOffset`; root = `Left/RightHandRootBoneName` = upperarm, `Left/RightFootRootBoneName` = thigh, last evaluated pose, so the weight shift counts) against its **limit**. Reach = the limb's straight reach (reference-pose chain length root → contact tip - hands: `Left/RightHandSocketName`, the palm sockets; feet: `Left/RightFootReachTipName` = ball - × mesh scale, cached per mesh asset), so hands and feet stop at their own stretch.
  - **The limit, per hold.**
    - `LimbReachLeashRatio` × reach,
    - or, if the clip placed the hold farther (an overhead reach planted at 99%), **its distance when first held** - capped at the full reach.
    - Measured at the first leash pass after each lock (`FClimbLimbPin::LeashPlantDistance`; every lock resets it).
    - So the leash only undoes distance the body adds **after** the plant - it never drags a hold off the spot the clip put it.
  - **The drag.** Past the limit the target moves back into reach by **exactly the excess** - the first of these that works:
    - **a) Along the clip's line**, whichever way is shorter. Clip line = the climb push on the wall (`GetMovementIntent` on the dominant plane) = the blendspace input, so the direction the playing clip strikes every limb along: a left clip drags a held limb sideways (**keeps its height**), a left-up clip along its 45°.
    - **b) The nearest spot back in reach** - at idle (no clip line), or when no move along the line gets it back.
    - **On the surface map** when the pin is on it (same facing within ~35°, within 15 cm of the map's surface): a curve is followed - round a cylinder, sideways toward the body closes the distance far faster than on the flat plane at the pin. The map's own error at the pin (a lip smaller than a cell) is carried through the search.
    - **Else on the face's plane at the pin** (no map: a corner turn, an inner corner's second face): the clip's line on that plane, else the reachable disc's nearest point (centred on the root's projection, radius √(L² − h²)).
    - Then re-trace onto the real surface there (`ProbeContactToward`: a bumpy face is followed; past a wall's edge or into a corner there's no same-face surface, so a limb is never dragged off its face).
    - *Why the clip's line:* the disc pulls straight toward the shoulder, so a hand held above it slid **down** while climbing sideways round a cylinder. The live body travel (tried next) is near zero and noisy exactly when the clip's torso motion pulls a limb out of reach.
    - No speed, no hysteresis: the pin moves only while, and as fast as, the body pulls it.
  - **The follow moves with it.** The IK follow absorbs *jumps*; the leash is a continuous constraint, so the followed target is translated by the same delta. Otherwise the follow's lag would trail the shown limb behind the dragged target, back out of reach.
  - **Last resort.** A *pinned* limb it can't keep on its face within reach (the face has no reachable point left - the body turned away from it or pushed off - or that point is off the surface): the climb lets go and **re-grips** on the arc onto this tick's plant (the resolver re-locks it right after; out of any retreat plan, `DropLimbFromRetreatPlan`); the corner turn **holds** - a reactive re-acquire would land on the same unreachable face - and the reach cap bends it until the corner clip's stance ends.
  - **At idle** this is what fixes the classic stop: the idle clip reclaims the torso while the limbs are still on their move grips, often leaving *both hands* out of reach. The leash drags both at once - neither leaves the wall, so "never both hands off" holds - and the retreat plan is built only once no pinned target was dragged that tick, so it measures from settled grips and keeps its one-support-at-a-time order. A not-yet-released limb the weight shift pulls out of reach mid-sequence is dragged, never released out of turn.
  - `bLimbReachLeash` off = held pins stay wherever they are (the cap still bends them).
  - Debug:
    - root → dragged target while it drags: **magenta** = along the clip's line, **orange** = to the nearest spot; **cyan** arrow = the clip's line at the pin;
    - red root → target line and `[ClimbReach] XX can't stay on its face within reach (N% of its reach) - re-grips` for the last resort;
    - `game.climbing.DebugLeash 1`: `[ClimbLeash]` per drag - which way, map or plane, the limit (leash ratio or plant distance), how much was vertical, why it fell back.
- **Reach cap (visual, every IK target).** The leash covers held targets; the cap is the visual net for the rest - a target approaching a plant, a clip-switch ease sliding through unreachable points, the start of a re-grip arc, a corner pin the leash couldn't keep in reach, the follow's few frames of easing. `CapToLimbReach` (in the component-space conversion, after the follow): the target handed to the rig is pulled toward the limb's root so its distance never exceeds `LimbReachLimit` x reach - untouched below `LimbReachSoftStart` x reach, then `d' = s + (M - s)(1 - e^(-(d - s)/(M - s)))` (slope 1 at the start, asymptotic to M; M <= s = hard cap). An out-of-reach target reads as "reaching toward it, still bent" and the cap fades by itself as the target returns in reach - no weight change, no hysteresis. Purely visual: pins and the follow keep the REAL target. Keep `LimbReachLeashRatio` ≤ `LimbReachSoftStart` and the cap never touches a held pin - except a hold the clip planted past `LimbReachSoftStart` (the leash keeps its plant distance), which the cap bends a little. A re-grip swing starts from the last SHOWN point (`FClimbLimbPin::DisplayedWorld`). Elbow bend vs ratio (equal segments): 2·acos(r) - 16° at 0.99, 36° at 0.95, 51° at 0.9, 74° at 0.8. Known gap: the intentionally straight dead hang gets bent (hands short of holds) - to be special-cased later. Debug: yellow line = the unreachable part (shown point -> real target).
- Settings: `Mover|Climbing|LimbIK|Reach`.

### 6. Direction changes — blend, don't pop
**What you see:** pressing a new direction mid-strike (W then D) hands each limb over to the new clip's holds - planted limbs step across on the arc, the rest blend - instead of snapping. All at once, following the input (see *Schedule at rest, respond while moving*, layer 2).

**Per-limb policy on a clip switch.** "IK on" = the clip has the limb in stance (pinned or approaching a plant); "IK off" = it has it mid-swing (the clip drives it, IK weight 0).

| # | Old clip | New clip | What the code does | What you see |
|---|---|---|---|---|
| 1a | IK on, **locked** | IK on | **Re-grip** (`RegripLockedLimbsOnClipSwitch`): if the new clip's plant is farther than `LimbRegripMinDistance` (8 cm - shared with the idle retreat), the limb lets go and the re-grip arc carries it onto the new plant, re-locked the same tick; within the distance it keeps its hold. Once per limb per plant. | The planted limb **steps across** to where the new clip wants it (or stays put if that's nearly the same spot). |
| 1b | IK on, **approaching** | IK on | Its target switches to the new clip's landing and the IK follow eases it there at the window's rate; locks there if the new stance is high enough. | The limb **glides** from where it was heading to the new hold. |
| 2 | IK on | IK off | Release, but inside the clip-switch blend window the weight **eases out** instead of dropping to 0, the target frozen on the old hold meanwhile - dragged by the reach leash if the body would leave it out of reach. | The limb **lets go gradually** into the new clip's swing, following the body. |
| 3 | IK off | IK on | Approach the new clip's plant: the target jumps there unseen (weight 0), then the weight **eases in** at the window's rate; may lock at once. | The limb **blends from the new clip's pose onto its new hold**. |
| 4 | IK off | IK off | Nothing - weight stays 0. | The limb follows the clips (the blendspace's own crossfade). |

Timing: the IK switches at the raw clip change (the component reads the unsmoothed input), while the pose crossfades over the blendspace's own smoothing. The IK's blend window is **read off the blendspace asset** (`GetClimbClipSwitchBlendTime`), so the two can't drift apart: Sample Smoothing's Weight Speed if set (`1 / speed`, or `~0.87 / speed` with its ease-in/out "Smoothing" on - the engine's critically damped weight spring, ~95% there), else the axes' Input Interpolation time, else 0 (the pose pops, so does the IK). `bClipSwitchBlend` off = the old instant switch. Settings: `Mover|Climbing|LimbIK|ClipSwitch`. Debug: `[ClimbIK] blendspace clip switch: A -> B (limbs blend over N s, from the blendspace)` and `[ClimbIK] XX re-grips onto <clip>'s plant (N cm away)`.

- **Blendspace clip switch — blend, don't pop.** A direction change mid-strike (W then D: Up -> Up-Right) flips the dominant clip, so every limb's destination landing changes at once. Two things used to pop it: (1) the target spring's teleport guard - cross-clip landings are easily > `LimbIKSpringSnapDistance` apart, so the target snapped; (2) release zeroed the weight in one tick - when the new clip says the limb is mid-swing, a limb held at full weight dropped straight to FK. Within ONE clip both are correct and invisible (plant-to-plant jumps happen in the swing at weight 0; an authored stance ramp is ~0 before release), so the fix keys off the switch itself: `RefreshLimbPins` compares the dominant clip with the last MOVING tick's (`LastDominantClimbClip`, cleared at idle / off the sustained climb, so idle<->move edges don't count). On a change: LOCKED limbs the new clip also plants elsewhere re-grip on the arc (case 1a in the table above), and a window as long as the pose's crossfade opens (read off the blendspace) in which the IK follow runs at a rate covering ~95% in that time for the target AND the weight - in and OUT (release fades instead of zeroing; the frozen Location keeps it aimed where it was). (The teleport guard and its `bClipSwitchEase` bypass went away with the spring - see the [IK follow](limb-ik.md#ik-follow).) `bClipSwitchBlend` off = the old instant switch. Debug: `[ClimbIK] blendspace clip switch: A -> B`.

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
  - white sphere (size = IK weight) + white line to the pin - the target the rig is ACTUALLY given (after the follow, a swing and the reach cap); a green pin the limb hasn't reached yet is the weight fading in, the follow easing, a swing in flight, or the cap;
  - orange - the retreat plan (move grip → idle grip, step numbers, 3 s), the swing's arc point and peeled normal;
  - amber / white points - the load-bearing / all-four centroids; amber line + sphere - the body shift;
  - magenta / orange line - the reach leash, taut (limb root → dragged target) while it drags (along the clip's line / to the nearest spot); red line - a held limb it couldn't keep in reach (2 s on a climb re-grip; continuous while a corner pin holds); yellow line - the part of a target the reach cap is holding back;
  - logs: `[ClimbRetreat]` (plan), `[ClimbReach]` (a leash re-grip), `[ClimbStrike]` (every release's skip decision), `[ClimbIK]` (clip switches, re-grips).
- **Settings:** `Mover|Climbing|LimbIK|IdleRetreat`, `…|RegripSwing`, `…|Reach`, `…|ClipSwitch`, `…|Placement` (`LimbRegripMinDistance`), `…|Follow` (`LimbIKFollowSpeed`), and `Mover|Climbing|WeightShift`. Every layer has an off switch for A/B comparison, and the newer settings are `EditAnywhere` for live tuning in PIE.

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
