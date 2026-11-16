# Climbing — Animation ↔ movement sync

How the capsule moves with the in-place climb clips so planted limbs don't skate: the distance curve, the round-stick → square-blendspace remap, and why the climb uses scalar rather than full root motion. Part of the [Climbing docs](README.md).

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
