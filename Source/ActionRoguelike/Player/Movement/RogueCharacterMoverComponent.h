// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "GameplayTagContainer.h"
#include "ActionSystem/GameplayEvent/FRogueGameplayEventData.h"
#include "RogueCharacterMoverComponent.generated.h"

class UAnimMontage;
class UAnimSequence;
class UMotionWarpingComponent;

/** One sample of the climb coverage grid: a short forward probe at one grid cell. */
struct FClimbSurfaceSample
{
	// True only if this probe landed on a valid (near-vertical, character-facing) climb surface.
	bool bHit = false;
	FVector Point = FVector::ZeroVector;
	FVector Normal = FVector::ZeroVector;
};

/**
 * Per-foot climb IK probe result. A sphere sweep follows the animated foot bone toward the dominant climb plane; this
 * is what decides whether a single foot plants or hangs (independent of the coverage grid's leg rows). Drives the
 * per-foot procedural tuck now and the Control Rig two-bone plant IK later.
 */
USTRUCT(BlueprintType)
struct FClimbFootProbeResult
{
	GENERATED_BODY()

	// True when this foot found NO reachable climbable foothold -> it should tuck/hang (per-foot tuck, or IK weight -> 0).
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing")
	bool bHanging = false;

	// True when a foothold was found. TargetLocation/Normal are meaningful only then.
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing")
	bool bHasTarget = false;

	// World foothold to plant the foot on (sweep contact point) and its surface normal. Valid when bHasTarget.
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing")
	FVector TargetLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing")
	FVector TargetNormal = FVector::ZeroVector;
};

/**
 * Per-hand grip point on the climb wall. The grip is found from a BODY-anchored sweep (capsule + a fixed hand offset,
 * projected to the plane), NOT the animated hand bone - so it's a stable world target the Control Rig can pin a hand
 * effector to (e.g. the FBIK dead-hang, where the hands stay anchored while the body drops). Refreshed while climbing.
 */
USTRUCT(BlueprintType)
struct FClimbHandGrip
{
	GENERATED_BODY()

	// True when a climbable grip was found for this hand this tick. Drive the pin weight off it (smoothed): it is false
	// whenever not Climbing (incl. the corner turn, where the clip moves the hands), so the pins must blend OUT on it.
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing")
	bool bFound = false;

	// World grip point (sweep contact on the wall) and its surface normal - the hand effector target + facing. Always a
	// sensible nearby point, even when !bFound: the anchor projected onto the wall while climbing, else the last grip
	// (frozen when leaving Climbing) - so a pin blending out never targets world origin.
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing")
	FVector Normal = FVector::ZeroVector;
};

/** Which kind of corner a side probe found. Inner = concave (the new wall faces back at you), outer = convex (the face wraps away). */
UENUM(BlueprintType)
enum class ERogueClimbCornerType : uint8
{
	None,
	Inner,
	Outer
};

/**
 * Per-side corner-turn probe result (refreshed each pre-sim tick while climbing). Everything is expressed in the CORNER
 * FRAME: the corner line is the exact intersection of the current climb plane and the detected side face, and the two
 * transforms are built off it - Mid = rounding the corner (facing the bisector; the clip's first strike), End = on the
 * new face in the climb mode's pose (the second strike / the climb hand-off). Capsule-centre transforms.
 */
USTRUCT(BlueprintType)
struct FClimbCornerProbeResult
{
	GENERATED_BODY()

	// True when a corner of a supported angle was found on this side AND both transforms fit AND the new face has enough
	// hand purchase to keep climbing (the always-succeed contract - a commit while this is true can't strand you).
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|Corner")
	bool bAvailable = false;

	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|Corner")
	ERogueClimbCornerType Type = ERogueClimbCornerType::None;

	// Angle between the current wall normal and the new face normal (90 = a square corner).
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|Corner")
	float TurnDegrees = 0.0f;

	// A point on the corner line at the character's height, and the line direction (oriented upward).
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|Corner")
	FVector CornerPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|Corner")
	FVector CornerDirection = FVector::ZeroVector;

	// The new face's plane (from the hand-row probe hit).
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|Corner")
	FVector NewWallNormal = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|Corner")
	FVector NewWallLocation = FVector::ZeroVector;

	// Strike 1: at the corner, facing into the bisector of the two faces.
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|Corner")
	FTransform MidTransform = FTransform::Identity;

	// Strike 2: on the new face, oriented exactly like the climb mode (MakeFromZX(WallUp2, -N2)).
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|Corner")
	FTransform EndTransform = FTransform::Identity;
};

/** Outcome of one corner-turn tick (URogueCharacterMoverComponent::AdvanceCornerTurn). */
enum class ERogueCornerTurnStep : uint8
{
	InProgress,
	FinishedOnNewFace,   // playhead reached the clip end -> climb the new face
	FinishedOnOldFace    // settled/reversed back to the clip start -> climb the original face
};

/**
 * Live state of an in-progress corner turn (set by BeginCornerTurn, advanced by AdvanceCornerTurn). The playhead is the
 * single driver of BOTH the capsule (the clip's root path, warped) and the pose (the AnimBP evaluates the clip at it).
 * Two strikes: [Rest0 -> Rest1] warps onto the probe's Mid, [Rest1 -> Rest2] onto its End.
 */
struct FRogueCornerTurnState
{
	TObjectPtr<UAnimSequence> Animation = nullptr;
	ERogueClimbCornerType Type = ERogueClimbCornerType::None;
	float Side = 1.0f;                           // +1 right / -1 left (stick Y sign that drives forward)

	double Playhead = 0.0;
	double RestTimes[3] = { 0.0, 0.0, 0.0 };     // clip start / the 4-limb corner rest / clip end
	double CommitTimes[2] = { 0.0, 0.0 };        // per strike: released before -> settle back, after -> settle forward
	bool bHolding = false;                       // stopped at the corner rest (Rest1) with no input - drives the breathing additive

	// Per strike: capsule pose at the strike start, the warp target, and where the UNWARPED clip would end (cached at commit).
	FTransform StrikeStart[2];
	FTransform StrikeTarget[2];
	FTransform StrikeClipEnd[2];
	float StrikeClipAngle[2] = { 0.0f, 0.0f };   // the clip's own rotation over the strike (radians) - rotation-warp progress
	FTransform MeshRelative;                     // mesh-in-capsule transform (root motion lives in mesh space)

	// Both faces' planes, to seed the climb plane on whichever face we finish on.
	FVector OldWallNormal = FVector::ZeroVector;
	FVector OldWallLocation = FVector::ZeroVector;
	FVector NewWallNormal = FVector::ZeroVector;
	FVector NewWallLocation = FVector::ZeroVector;
};

/**
 * URogueCharacterMoverComponent: project-specific Mover component for the player.
 *
 * Hosts the climbing system's surface detection (and, later, climb-mode registration). This is the Mover
 * analog of the tutorial's UCustomCharacterMovementComponent. Under CMC the tutorial sweeps for walls in
 * TickComponent; here we run the same sweep on OnPreSimulationTick, which fires (on the game thread) right
 * before each Mover simulation tick, so a climb transition/mode can read fresh wall data.
 */
UCLASS(BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class ACTIONROGUELIKE_API URogueCharacterMoverComponent : public UCharacterMoverComponent
{
	GENERATED_BODY()

public:
	URogueCharacterMoverComponent();

	// Registers our custom modes/transitions before the base builds its state machine (see the .cpp for why this is
	// done here rather than in the constructor).
	virtual void InitializeComponent() override;

	virtual void BeginPlay() override;
	
	const TArray<FHitResult>& GetCurrentWallHits() const { return CurrentWallHits; }
	
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing")
	bool IsWallDetected() const { return CurrentWallHits.Num() > 0; }

	// True when the most recent grid deemed the surface ahead climbable - i.e. the hand (top) row has >= ClimbTopRowMinHits
	// cells on wall AND the valid normals are consistent (cached; refreshed each tick). Gates both entry and sustain.
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing")
	bool CanClimbNow() const { return bFacingClimbableSurface; }

	// True when there is enough hand purchase (top row >= ClimbTopRowMinHitsToMove) to traverse the wall, not just cling.
	// Below it the climb mode holds position (zeros along-wall intent) so a single handhold reads as an idle cling.
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing")
	bool CanMoveWhileClimbing() const { return bCanMoveOnWall; }

	// True when the WHOLE lower body should hang (drives the dead-hang upper body): BOTH feet lack a reachable foothold
	// (per the per-foot probes). A single foot over a void does NOT trigger this - it stays braced and is resolved
	// per-foot (see the foot probes). The AnimBP time-smooths this into the hang blend alpha. Meaningful while climbing,
	// and FROZEN through a corner turn (value from when the turn began), so one bool drives the hang in both states.
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing")
	bool IsLowerBodyHanging() const { return bLowerBodyHanging; }

	// Per-foot foothold probe results are exposed as the BlueprintReadOnly members LeftFootProbe / RightFootProbe (read
	// `.bHanging` to drive each foot's tuck, and later `.TargetLocation`/`.TargetNormal` for the Control Rig plant IK).
	// These convenience accessors return each foot's hang bool directly for use as a tuck alpha.
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing")
	bool IsLeftFootHanging() const { return LeftFootProbe.bHanging; }

	UFUNCTION(BlueprintPure, Category = "Mover|Climbing")
	bool IsRightFootHanging() const { return RightFootProbe.bHanging; }

	// True while the climb movement mode is the active movement mode (the sustained climb ONLY - see IsInClimbState).
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing")
	bool IsClimbing() const;

	// True in ANY climb-family mode: climb, mantle up, mantle down, corner turn. Each of those modes carries one child of
	// the Mover.Climb gameplay tag (Mover.Climb.Sustained/MantleUp/MantleDown/CornerTurn) - the same mechanism as Mover's own
	// IsOnGround()/IsAirborne() - and this matches the parent, so a future climb mode joins by adding its own child tag in
	// its constructor, and callers (AnimBP, gameplay) never need updating. The per-mode Is* queries match their child exact.
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing")
	bool IsInClimbState() const;
	
	// Climb move intent in wall-relative axes for the climb blendspace: X = up(+)/down(-) the wall, Y = right(+)/left(-).
	// Derived from GetMovementIntent() projected onto the wall basis, then circle->square remapped so full diagonals
	// reach the blendspace corners. Feed this to the blendspace AXES. Meaningful while climbing.
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing")
	FVector2D GetClimbMoveIntent() const;

	// Round (pre-square) climb push amount (0..1), direction-independent: 1 at any full push, matching the body's
	// constant climb speed. Feed this to the blendspace PLAYRATE (Step A). Do NOT use |GetClimbMoveIntent()| for
	// playrate - its square mapping reaches sqrt(2) on diagonals and would make diagonal climbing play too fast.
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing")
	float GetClimbMoveSpeedFraction() const;

	// Instantaneous velocity-surge multiplier, read from the ClimbCadenceCurveName float curve on the climb clips.
	// The climb mode scales its along-wall velocity by this so the body moves with the animation (reach = slow,
	// pull = fast), handling the clip's two-pulse profile automatically. Returns 1 when the curve is absent, so
	// it's a no-op until you author the curve.
	float GetClimbCadenceScale() const;

	// Dominant climb plane in world space. Meaningful when CanClimbNow() is true.
	FVector GetClimbDominantSurfaceNormal() const { return ClimbDominantSurfaceNormalCache; }

	// True = the capsule tilts to lie against the climb surface (up follows the wall's up-slope) instead of staying world-vertical.
	bool ShouldAlignToClimbSurface() const { return bAlignToClimbSurface; }
	FVector GetClimbDominantSurfaceLocation() const { return ClimbDominantSurfaceLocationCache; }

	// Per-sample grid results, row-major (row 0 = bottom). For future per-limb IK / debug.
	const TArray<FClimbSurfaceSample>& GetClimbSurfaceSamples() const { return ClimbSurfaceSamplesCache; }

	// --- Contextual-entry queries (used by URogueClimbTransition) ---

	// Minimum dot(moveIntentDir, -wallNormal) required to auto-grab. 1 = dead-on, 0 = any push toward the wall.
	float GetClimbEnterIntoWallDot() const { return ClimbEnterIntoWallDot; }

	// If true, contextual grab is only allowed while airborne.
	bool RequiresAirborneToGrab() const { return bRequireAirborneToGrab; }

	// True while contextual re-entry is blocked (just after leaving a climb) at the given sim time.
	bool IsClimbReentryOnCooldown(double SimTimeMs) const { return SimTimeMs < ClimbReentryUnblockSimTimeMs; }

	// Starts the contextual re-entry cooldown from the given sim time (called when leaving climb).
	void BeginClimbReentryCooldown(double SimTimeMs) { ClimbReentryUnblockSimTimeMs = SimTimeMs + ClimbReentryCooldownSeconds * 1000.0; }

	// --- Mantle (climb top-out) ---

	// True while the mantle movement mode is the active movement mode.
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing|Mantle")
	bool IsMantling() const;

	// True when the mantle probe found a valid ledge + landing this tick (refreshed while climbing).
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing|Mantle")
	bool IsMantleLedgeAvailable() const { return bMantleLedgeAvailable; }

	// World transform the mantle warps to: capsule center on top of the ledge + facing away from the wall.
	// Meaningful when IsMantleLedgeAvailable() is true.
	FTransform GetMantleLandingTransform() const { return MantleLandingTransformCache; }

	// Minimum wall-up stick intent (X) required to start the mantle.
	float GetMantleUpIntentThreshold() const { return MantleUpIntentThreshold; }

	// Interp speed the mantle uses to ease its capsule from the climb's wall-tilted orientation back to world-vertical.
	float GetMantleUprightBlendSpeed() const { return MantleUprightBlendSpeed; }

	// True once the mantle should ease to vertical: immediately when no gate event tag is set, otherwise only after the
	// montage has fired MantleUprightBlendEventTag (an anim "Send Gameplay Event" notify routed through the action system).
	bool ShouldMantleBlendUpright() const { return !MantleUprightBlendEventTag.IsValid() || bMantleUprightBlendRequested; }

	UAnimMontage* GetMantleMontage() const { return MantleMontage; }

	UMotionWarpingComponent* GetMotionWarping() const { return MotionWarpingComp; }

	// Plays the mantle montage on the mesh, queues the anim-root-motion layered move, and sets the two warp targets
	// (edge then landing) from the cached probe result. Called by URogueMantleTransition::Trigger on entry. Returns
	// false (mantle skipped) if the montage/warp component is missing.
	bool BeginMantle();

	// --- Mantle down (climb down from an edge) ---

	// True while the mantle-down movement mode is the active movement mode.
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing|MantleDown")
	bool IsMantlingDown() const;

	// True when the mantle-down probe found a valid edge + descendable face this tick (refreshed while grounded).
	// Published for a Zelda-style "press to climb down" affordance: the probe fully validates, so a press while this is
	// true always succeeds. Drive a HUD prompt / anim state off it.
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing|MantleDown")
	bool IsMantleDownAvailable() const { return bMantleDownAvailable; }

	// Detected wall-face normal cached at mantle-down commit. The mode reads THIS (not the live grid, which the forward
	// sweep loses mid-traversal while the character is out over the edge) to ease into the wall lean. Meaningful during
	// the mantle-down.
	FVector GetMantleDownWallNormal() const { return MantleDownWallNormalCache; }

	// True once the mantle-down should ease into the wall tilt: immediately when no gate event tag is set, otherwise only
	// after the montage has fired MantleDownTiltBlendEventTag (an anim "Send Gameplay Event" notify routed through the
	// action system). Mirror of ShouldMantleBlendUpright, inverted (upright -> wall lean).
	bool ShouldMantleDownBlendTilt() const { return !MantleDownTiltBlendEventTag.IsValid() || bMantleDownTiltBlendRequested; }

	// Interp speed the mantle-down uses to ease its capsule from upright into the climb's wall-tilted orientation.
	float GetMantleDownTiltBlendSpeed() const { return MantleDownTiltBlendSpeed; }

	UAnimMontage* GetMantleDownMontage() const { return MantleDownMontage; }

	// Plays the mantle-down montage on the mesh, queues the anim-root-motion layered move, and sets the two warp targets
	// (edge/lip then hang) from the cached probe result. Called by URogueMantleDownTransition::Trigger on entry. Returns
	// false (skipped) if the montage/warp component is missing.
	bool BeginMantleDown();

	// --- Corner turns ---

	// Corner probe result for one side (bRight = WallRight side, else left). Refreshed while climbing; see FClimbCornerProbeResult.
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing|Corner")
	FClimbCornerProbeResult GetClimbCorner(bool bRight) const { return bRight ? RightCorner : LeftCorner; }

	UFUNCTION(BlueprintPure, Category = "Mover|Climbing|Corner")
	bool IsCornerTurnAvailable(bool bRight) const { return bRight ? RightCorner.bAvailable : LeftCorner.bAvailable; }

	// True while the corner-turn movement mode is active.
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing|Corner")
	bool IsCornerTurning() const;

	// The corner clip being played and its playhead (seconds). Feed BOTH to a Sequence Evaluator (explicit time) in the
	// AnimBP: the capsule is already at the clip's (warped) root pose for this exact time, so pose and body stay locked.
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing|Corner")
	UAnimSequence* GetCornerTurnAnimation() const { return CornerTurn.Animation; }

	UFUNCTION(BlueprintPure, Category = "Mover|Climbing|Corner")
	float GetCornerTurnPlayhead() const { return static_cast<float>(CornerTurn.Playhead); }

	// True while stopped at the corner rest (the 4-limb pose) with no input: ramp the breathing additive in on this.
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing|Corner")
	bool IsCornerTurnHolding() const { return IsCornerTurning() && CornerTurn.bHolding; }

	// Kept for existing AnimBP use: identical to IsLowerBodyHanging() during a turn (which now stays frozen through it),
	// false otherwise. Prefer IsLowerBodyHanging() everywhere so one smoothed alpha spans climb <-> corner without a reset.
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing|Corner")
	bool IsCornerTurnLowerBodyHanging() const { return IsCornerTurning() && bLowerBodyHanging; }

	float GetCornerTurnIntentThreshold() const { return CornerTurnIntentThreshold; }

	// True when a corner turn toward this side can start now: the probe found an available corner and a clip is assigned.
	bool CanBeginCornerTurn(bool bRight) const;

	// Commits a corner turn toward this side from the current probe result: picks the clip, finds its rest/commit times,
	// caches the per-strike warp (start -> Mid -> End), and latches the hang state. Called by URogueCornerTurnTransition.
	bool BeginCornerTurn(bool bRight);

	// One tick of the turn: advances the playhead from the stick (StickY, raw) or settles it toward the nearest rest, and
	// returns the capsule pose for the new playhead. Called once per tick by URogueCornerTurnMode.
	ERogueCornerTurnStep AdvanceCornerTurn(float StickY, float DeltaSeconds, FTransform& OutCapsuleTarget);

	// Ends the turn and seeds the climb plane to the face we finished on, so Climbing resumes on it without the normal
	// smoothing swinging over from the old face (and the grid stacks on the right plane from its first frame).
	void FinishCornerTurn(bool bOntoNewFace);

protected:

	UFUNCTION()
	void HandlePreSimulationTick(const FMoverTimeStep& TimeStep, const FMoverInputCmdContext& InputCmd);

	// Action-system listener (bound in BeginPlay): starts the mantle upright blend when the montage fires
	// MantleUprightBlendEventTag. Sets bMantleUprightBlendRequested; consumed by ShouldMantleBlendUpright().
	UFUNCTION()
	void OnMantleUprightBlendEvent(FGameplayTag EventTag, FRogueGameplayEventData Payload);

	// Action-system listener (bound in BeginPlay): starts the mantle-down tilt blend when the montage fires
	// MantleDownTiltBlendEventTag. Sets bMantleDownTiltBlendRequested; consumed by ShouldMantleDownBlendTilt().
	UFUNCTION()
	void OnMantleDownTiltBlendEvent(FGameplayTag EventTag, FRogueGameplayEventData Payload);
	
	void SweepAndStoreWallHits();

	void RefreshClimbSurfaceInfo();

	// Per-foot foothold probes (run each pre-sim tick while climbing; reads the animated foot sockets). Populates
	// LeftFootProbe/RightFootProbe and, from them, bLowerBodyHanging (BOTH feet without a foothold). Self-gates on
	// IsClimbing() + a valid plane.
	void RefreshFootProbes();

	// Sphere-sweeps one foot (by socket) from just off the wall toward the dominant plane; returns the foothold result
	// (bHanging when none reachable). Uses the coverage grid's plane only for the sweep direction, not the leg rows.
	// bWasHanging is the foot's previous state, for release hysteresis (a planted foot searches wider).
	FClimbFootProbeResult ProbeFoot(FName FootSocketName, bool bWasHanging) const;

	// Per-hand grip probes (run each pre-sim tick while climbing): sweeps from a body-anchored point to the wall for each
	// hand and caches LeftHandGrip/RightHandGrip for the Control Rig hand pins. Self-gates on IsClimbing() + a valid plane.
	void RefreshHandGrips();

	// Sphere-sweeps from a body-anchored origin toward the plane; returns the climbable grip contact (bFound when hit).
	FClimbHandGrip ProbeHandGrip(const FVector& Anchor, const FVector& WallNormal) const;

	// Up-and-over probe (run while climbing): confirms the wall face has ended, finds the walkable top, checks
	// capsule fit, and caches the landing transform. Populates bMantleLedgeAvailable / MantleLandingTransformCache.
	void RefreshMantleProbe();

	// Precisely localizes the lip (top of the near face) at mantle COMMIT, decoupled from the landing: a binary-searched,
	// laterally-sampled forward-ray scan up the wall face. Writes a feet warp transform for the "up" phase. Returns false
	// if no lip is found (caller falls back to the cheap probe's cached edge). Cost = Iterations x LateralSamples traces, once.
	bool ComputePreciseMantleEdge(FTransform& OutEdge) const;

	// Mantle-DOWN probe (run while grounded): a cheap "is there an edge?" gate (a lower-front capsule that must be
	// collision-free), then, only when armed, BACKWARD rays from out over the drop to find + validate a descendable
	// wall face below the lip, plus min-face-height / hang-fit / swing-clearance / facing checks. On full success caches
	// the two warp transforms (lip, hang) + the face normal and sets bMantleDownAvailable. Full validation each tick
	// (behind the cheap gate) so a button press only fires when it's guaranteed to succeed (Zelda-style affordance).
	void RefreshMantleDownProbe();

	// Corner probes (run each pre-sim tick while climbing): probes BOTH sides every tick (stick-independent, so availability
	// can drive a prompt/anim and the debug draw shows both). Populates LeftCorner/RightCorner.
	void RefreshCornerProbes();

	// One side (Side = +1 right / -1 left along WallRight), at the grid's hand-row height: an INNER side ray (a
	// near-perpendicular wall beside you), else an OUTER edge test (the face ends within reach) + wrap ray (from past the
	// edge and behind the face, back toward the body - finds the side face). Then the corner line = plane intersection,
	// Mid/End transforms in the corner frame, and fit + new-face hand-purchase validation.
	FClimbCornerProbeResult ProbeCorner(float Side) const;

	// The corner clip assigned for this corner type + side (null if none).
	UAnimSequence* GetCornerTurnAnimationFor(ERogueClimbCornerType Type, bool bRight) const;

	// Capsule pose of strike [0|1] at clip time Time: the clip's own root path from the strike start, skewed so the
	// strike ends exactly on its target. A pure function of Time (no accumulated state), so scrubbing, pausing,
	// reversing and settling all land on the same path.
	FTransform EvaluateCornerTurnStrike(int32 Strike, double Time) const;

	// Where the UNWARPED clip puts the capsule at Time, starting strike [0|1] from its start pose.
	FTransform ComputeCornerClipCapsule(int32 Strike, double Time) const;

	bool IsSurfaceClimbable(float SteepnessDotProduct) const;

	// Upward sampling bias applied while climbing (hands reach high, legs curl), scaled by OnClimbAdditionalOffset
	// and projected onto the given wall plane so it follows the wall's up-slope instead of world up. On a back-tilted
	// wall the surface recedes as it rises, so a pure world-up shift would walk the sweep/grid off the surface;
	// projecting keeps them on it at any tilt (== world up on a vertical wall). Pass the last-known plane normal.
	FVector GetClimbSampleUpBias(const FVector& PlaneNormal) const;

	// --- Detection tuning ---
	
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Detection", meta = (ForceUnits = "cm"))
	float ClimbDetectionCapsuleRadius = 50.0f;
	
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Detection", meta = (ForceUnits = "cm"))
	float ClimbDetectionCapsuleHalfHeight = 72.0f;
	
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Detection", meta = (ForceUnits = "cm"))
	float ClimbDetectionForwardOffset = 20.0f;
	
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Detection", meta = (ForceUnits = "degrees"))
	float MinHorizontalDegreesToStartClimbing = 25.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Detection", meta = (ForceUnits = "cm"))
	float OnClimbAdditionalOffset = 20.0f;

	// While climbing, tilt the capsule (and thus the mesh + detection) to lie against the surface: its up-axis
	// follows the wall's up-slope, its forward faces into the wall. On a vertical wall this is a no-op; on a
	// slanted wall it reads far better than standing bolt-upright. Toggle off to keep the old world-vertical pose.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Detection")
	bool bAlignToClimbSurface = true;

	// --- Coverage grid (climbability validation) ---

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CoverageGrid", meta = (ClampMin = "1"))
	int32 ClimbGridColumns = 3;

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CoverageGrid", meta = (ClampMin = "1"))
	int32 ClimbGridRows = 5;

	// Horizontal half-extent of the sample grid (left/right from center).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CoverageGrid", meta = (ForceUnits = "cm"))
	float ClimbGridHalfWidth = 30.0f;

	// Vertical offsets (along up, relative to capsule center) of the bottom and top grid rows.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CoverageGrid", meta = (ForceUnits = "cm"))
	float ClimbGridBottomOffset = -40.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CoverageGrid", meta = (ForceUnits = "cm"))
	float ClimbGridTopOffset = 60.0f;

	// Forward length of each probe trace.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CoverageGrid", meta = (ForceUnits = "cm"))
	float ClimbSampleReach = 70.0f;

	// Hand purchase, and the SOLE climbability gate (no torso/coverage-ratio requirement): the TOP row is where the
	// hands grab, so the surface is climbable only if at least this many of its cells are backed by wall. 1 = a single
	// (centre) handhold is enough to grab on and cling idle. Clamped to the column count. Gates both contextual entry
	// and the climb's exit-to-Falling (via CanClimbNow), so 0 top-row hits drops the climb (or the mantle catches it at a lip).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CoverageGrid", meta = (ClampMin = "1"))
	int32 ClimbTopRowMinHits = 1;

	// Traversal gate: this many top-row (hand) cells must be backed by wall to MOVE along the surface (a single handhold
	// lets you cling idle but not shuffle the hands around). Below it the climb mode zeros the along-wall intent. Keep
	// >= ClimbTopRowMinHits; clamped to the column count.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CoverageGrid", meta = (ClampMin = "1"))
	int32 ClimbTopRowMinHitsToMove = 2;

	// Max angle a valid sample normal may deviate from the averaged normal (rejects corners / fragmented walls).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CoverageGrid", meta = (ForceUnits = "degrees"))
	float MaxClimbNormalDeviationDegrees = 35.0f;

	// Max lean FROM VERTICAL a surface may have to still count as a climbable WALL (vs a walkable top): 0 = perfectly
	// vertical only, 40 = walls leaning up to +/-40deg from vertical. Converted internally to a normal test,
	// |dot(normal, up)| <= sin(this) (see IsSurfaceClimbable). Shared by every climbing action (coverage grid, mantle-down
	// face probe + lip scan). EFFECTIVE ceiling ~44deg: it must stay BELOW the walkable slope (MantleMinWalkableDot 0.7 =
	// ~44.4deg from vertical) or walkable tops would classify as walls - that ordering IS the wall/walk gate.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CoverageGrid", meta = (ForceUnits = "degrees", ClampMin = "0.0", ClampMax = "89.0"))
	float MaxWallTiltFromVerticalDegrees = 40.0f;

	// Number of bottom rows treated as "lower body" (legs), excluded from the climbability gate. The per-leg hang is now
	// decided by the per-foot foothold probes (see FootIK below), not these rows - they only feed the legacy
	// LowerBodySupport scalar now.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CoverageGrid", meta = (ClampMin = "1"))
	int32 ClimbLowerBodyRowCount = 2;

	// VInterpTo speed the cached dominant normal/location follow the raw grid average. Damps jitter on curved
	// surfaces (cylinders). 0 = snap (no smoothing); higher = snappier but jitterier.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CoverageGrid", meta = (ClampMin = "0.0"))
	float ClimbNormalSmoothingSpeed = 10.0f;

	// --- Per-foot IK probe (foothold detection) ---
	// Each foot sphere-sweeps from its animated bone toward the dominant plane to find a reachable foothold; the hit
	// decides plant vs hang PER FOOT (independent of the coverage grid's leg rows).

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|FootIK")
	FName LeftFootSocketName = FName("foot_l");

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|FootIK")
	FName RightFootSocketName = FName("foot_r");

	// Sphere radius of the foothold probe - the search tolerance around the foot.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|FootIK", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float FootProbeRadius = 12.0f;

	// How far toward the wall (from the backed-off origin) a foot may reach and still count as planted - the IK
	// reachability range. Beyond this the foot hangs/tucks.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|FootIK", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float FootProbeReach = 45.0f;

	// How far to back the sweep origin OFF the wall (along the plane normal) so it starts in free space in front of the face.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|FootIK", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float FootProbeBackoff = 25.0f;

	// Hysteresis (Schmitt trigger): while a foot is already PLANTED its foothold search widens by this - on both the
	// sphere radius and the reach - so it takes a clearly-gone foothold to flip to hanging. Harder to release than to
	// acquire, which stops the plant/hang flicker at ledge edges. 0 = no hysteresis.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|FootIK", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float FootProbeReleaseHysteresis = 15.0f;

	// Idle latch: when the climb move push (GetClimbMoveSpeedFraction) is at or below this, movement counts as idle and a
	// hanging foot is NOT allowed to re-plant - so a foot at an edge settles into a stationary dangle instead of
	// flickering plant<->hang (that oscillation is only visible at idle). Above it (moving) feet re-plant normally.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|FootIK", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FootPlantIdleInputDeadzone = 0.05f;

	// --- Per-hand grip probe (for the Control Rig hand pins / FBIK dead-hang) ---
	// Each hand's grip is found by sweeping from a BODY-anchored point (capsule + offsets below, projected to the plane)
	// toward the wall - stable and pose-independent, unlike the foot probe which follows the live bone. Tune the offsets
	// so the found grips sit where the braced idle hands already grab, so pinning to them is seamless.

	// Lateral half-distance between the two hand grips, from center along the wall-right axis.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|HandIK", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float HandGripStanceHalfWidth = 18.0f;

	// Height of the hand grips above the capsule centre (along wall-up) - roughly where the braced hands grab (chest/shoulder).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|HandIK", meta = (ForceUnits = "cm"))
	float HandGripAnchorHeight = 35.0f;

	// Sphere radius of the hand-grip probe (search tolerance around the anchor).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|HandIK", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float HandProbeRadius = 12.0f;

	// How far toward the wall (from the backed-off origin) to search for the grip.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|HandIK", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float HandProbeReach = 40.0f;

	// How far to back the sweep origin OFF the wall (along the plane normal) so it starts in free space in front of the face.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|HandIK", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float HandProbeBackoff = 25.0f;

	// Gap held between the grip point and the wall surface (pushed OUT along the hit normal), so the hand sits a little
	// off the face instead of flush/embedded - accounts for hand thickness / a natural grip standoff. 0 = on the surface.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|HandIK", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float HandGripSurfaceGap = 5.0f;

	// --- Contextual entry ---

	// How directly the player must push into the wall to auto-grab: dot(moveIntentDir, -wallNormal) must reach this.
	// 1 = must push dead-on, 0.5 ~= within 60 deg, 0 = any push toward the wall.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Entry", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float ClimbEnterIntoWallDot = 0.5f;

	// If true, only auto-grab while airborne (i.e. you jumped/fell at the wall); prevents grabbing while walking on the ground.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Entry")
	bool bRequireAirborneToGrab = true;

	// After leaving a climb, block contextual re-entry for this long so a jump-off isn't instantly cancelled by a re-grab.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Entry", meta = (ForceUnits = "s", ClampMin = "0.0"))
	float ClimbReentryCooldownSeconds = 0.3f;

	// --- Cadence (velocity surge synced to the climb anim) ---

	// Name of the float curve authored on the climb clips that drives the velocity surge. Read by GetClimbCadenceScale().
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Cadence")
	FName ClimbCadenceCurveName = FName("ClimbCadence");

	// If > 0, the cadence curve value is divided by this to turn it into a mean-~1 multiplier. Use when the curve is
	// in absolute cm/s (e.g. a clip's "MovementSpeed"): set it to that curve's AVERAGE speed. Using one shared value
	// across all directions preserves their relative authored speeds. 0 = use the curve value as-is (already mean ~1).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Cadence", meta = (ForceUnits = "cm/s", ClampMin = "0.0"))
	float ClimbCadenceReferenceSpeed = 0.0f;

	// Blends the cadence toward a flat 1.0 to tame an over-aggressive surge (a curve with a big peak/average ratio):
	// 0 = no surge (constant speed), 1 = full curve. Does NOT change the average speed (set the reference first).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Cadence", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ClimbSurgeStrength = 1.0f;

	// --- Mantle (climb top-out) ---
	// The ledge-height BAND below is the single authority for BOTH the gameplay gate and the probe's vertical reach:
	// RefreshMantleProbe derives the overhead/down traces from it, so detection reach and the height policy can't
	// drift apart (retuning the band moves the trigger window; there is no separate probe-height knob to keep in sync).

	// Ledge height (top of the ledge above the character's FEET) must be within [min, max] to allow a mantle.
	// Min rejects trivial steps / surfaces at or below the feet; max rejects ledges too high to top out onto.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Mantle", meta = (ForceUnits = "cm"))
	float MantleMinLedgeHeightFromFeet = 170.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Mantle", meta = (ForceUnits = "cm"))
	float MantleMaxLedgeHeightFromFeet = 220.0f;

	// How far beyond the wall face (plus capsule radius) to search for the top surface.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Mantle", meta = (ForceUnits = "cm"))
	float MantleForwardReach = 55.0f;

	// How far below the ledge to search for the landing surface.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Mantle", meta = (ForceUnits = "cm"))
	float MantleDownReach = 100.0f;
	
	// Backward offset of the MantleUp warp target (cyan sphere)
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Mantle", meta = (ForceUnits = "cm"))
	float MantleEdgeBackward = 10.0f;
	
	// Up offset of the Mantle warp target (cyan sphere)
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Mantle", meta = (ForceUnits = "cm"))
	float MantleEdgeUp = 10.0f;

	// --- Precise lip scan (runs ONCE when the mantle commits, not per-frame) ---
	// Perf<->precision: binary-search iterations that localize the lip height along the wall face. Each iteration
	// halves the remaining error (band / 2^n), at one extra trace per lateral sample. Lower = cheaper/coarser.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Mantle", meta = (ClampMin = "1", ClampMax = "12"))
	int32 MantleLipScanIterations = 6;

	// Perf<->precision: lateral rays across the grab width; the lip is averaged over them (smooths a ragged/slanted
	// lip and rejects a single stray hit). 1 = one centre ray (cheapest). Total scan = Iterations x LateralSamples traces.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Mantle", meta = (ClampMin = "1", ClampMax = "9"))
	int32 MantleLipLateralSamples = 3;

	// Half-width of the lateral spread for the lip samples (ignored when MantleLipLateralSamples == 1).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Mantle", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float MantleLipScanHalfWidth = 20.0f;

	// Min dot(landingNormal, up) for the top to count as walkable (cosine of the max landing slope).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Mantle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MantleMinWalkableDot = 0.7f;

	// Fit test: how high above the landing the capsule starts before sweeping straight down to find its rest. Must clear
	// the walk surface's uphill slope rise within the capsule footprint (~CapR*tan(maxSlope)) so the sweep starts in free
	// space; the sweep also travels this far below, so a too-small value can miss a low rest. Bigger = safer, slightly costlier.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Mantle", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float MantleFitSweepClearance = 40.0f;

	// Min wall-up stick intent (X) required to trigger the mantle.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Mantle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MantleUpIntentThreshold = 0.6f;

	// How fast the mantle eases its capsule from the climb's wall-tilted orientation back to world-vertical (interp
	// speed; higher = snappier). Removes the orientation pop at the climb->mantle handoff on tilted walls. 0 = snap.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Mantle", meta = (ClampMin = "0.0"))
	float MantleUprightBlendSpeed = 8.0f;

	// Gameplay event tag the mantle montage fires to START the upright blend (routed through the action system's
	// GameplayEventReceivedDelegate). Leave UNSET to blend immediately at mantle entry (the pre-notify behavior); set it
	// to the montage's "Send Gameplay Event" notify tag to hold the wall tilt until that authored point in the animation.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Mantle")
	FGameplayTag MantleUprightBlendEventTag;

	// Authored ROOT-MOTION mantle montage. Author TWO Motion Warping (SkewWarp) windows: the "pull up" section
	// targeting MantleUpWarpTargetName, then the "step over" section targeting MantleForwardWarpTargetName.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Mantle")
	TObjectPtr<UAnimMontage> MantleMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Mantle")
	float MantleMontagePlayRate = 1.0f;

	// Warp-target names on the montage's two Motion Warping windows. "Up" targets the lip/edge (drives straight up the
	// wall face), "Forward" targets the landing on top (steps over) - the two-phase path avoids clipping the wall corner.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Mantle")
	FName MantleUpWarpTargetName = FName("MantleUp");

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Mantle")
	FName MantleForwardWarpTargetName = FName("MantleForward");

	// --- Mantle down (climb down from an edge) ---
	// Stage 1 arming gate: a capsule in the lower front of the character. When it is collision-free the ground has
	// ended ahead (an edge / drop), which arms the (heavier) face validation below. Placed forward of the feet so you
	// must actually be at the edge.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown", meta = (ForceUnits = "cm"))
	float MantleDownGateRadius = 20.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown", meta = (ForceUnits = "cm"))
	float MantleDownGateHalfHeight = 30.0f;

	// Forward offset (from the feet) of the arming-gate capsule centre.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown", meta = (ForceUnits = "cm"))
	float MantleDownGateForwardOffset = 45.0f;

	// Downward offset (below the feet) of the arming-gate capsule centre, so it probes for a floor just ahead.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown", meta = (ForceUnits = "cm"))
	float MantleDownGateDownOffset = 10.0f;

	// Stage 2: how far forward of the feet (plus capsule radius) to place the BACKWARD-ray origins - out over the drop,
	// beyond the lip, so a ray fired back toward the wall strikes the near face.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown", meta = (ForceUnits = "cm"))
	float MantleDownFaceSearchForward = 40.0f;

	// Number of vertical backward-ray samples down the face (topmost hit ~= the lip; needs >= 2 to span a real face).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown", meta = (ClampMin = "2"))
	int32 MantleDownFaceProbeCount = 6;

	// Depth below the feet at which the FIRST (topmost) backward ray sits - just under the top surface so it reads the
	// face, not the top lip.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown", meta = (ForceUnits = "cm"))
	float MantleDownFaceProbeTopInset = 15.0f;

	// How far below the feet the backward-ray band extends (the deepest sample). Also bounds the face-height search.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown", meta = (ForceUnits = "cm"))
	float MantleDownMaxDepth = 140.0f;

	// Minimum near-vertical face below the lip (measured along the wall's up-slope) for there to be something to hang on;
	// rejects a mere step-down. No maximum: any tall wall is fine to climb down onto.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown", meta = (ForceUnits = "cm"))
	float MantleDownMinFaceHeight = 60.0f;

	// How far below the lip (along the wall) the hang capsule centre rests - roughly where the hands grab the lip and
	// the body dangles. Sets the "down" warp target.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown", meta = (ForceUnits = "cm"))
	float MantleDownHangDrop = 60.0f;

	// Shrink applied to the fit/clearance test capsules so a capsule resting one radius off the face doesn't count the
	// face itself as an overlap.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float MantleDownFitSkin = 3.0f;

	// How far above the lip (ALONG THE WALL) the clearance corridor's TOP extends. The corridor is one capsule lying
	// parallel to the wall from the hang feet up past the lip to here - it tests the whole swing-over-and-hang volume at
	// once. Keep this modest: on a steep back-tilt the top leans back over the standing area with the wall.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown", meta = (ForceUnits = "cm"))
	float MantleDownSwingClearanceUp = 30.0f;

	// Max HORIZONTAL angle between the character's facing and the face outward normal to allow the drop (both flattened
	// before the compare, so the window is the same at any wall tilt). Motion warping snaps the remainder; ~25deg lets
	// you approach the edge loosely without lining up dead-on.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown", meta = (ForceUnits = "degrees"))
	float MantleDownFacingToleranceDegrees = 25.0f;

	// Backward nudge (toward the top, away from the drop) of the lip/edge warp target so the feet land just inside the
	// edge for the "forward" phase rather than out over it.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown", meta = (ForceUnits = "cm"))
	float MantleDownEdgeBackward = 10.0f;

	// Authored ROOT-MOTION mantle-down montage (stand at the edge -> grab lip -> swing down to a hang facing the wall).
	// Author TWO Motion Warping (SkewWarp) windows: the "forward" section targeting MantleDownForwardWarpTargetName
	// (over the lip), then the "down" section targeting MantleDownDownWarpTargetName (onto the face). Enable rotation
	// warp on the down window so the ~180deg turn + the approach-angle snap resolve onto the wall.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown")
	TObjectPtr<UAnimMontage> MantleDownMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown")
	float MantleDownMontagePlayRate = 1.0f;

	// Warp-target names on the montage's two Motion Warping windows. "Forward" targets the lip pivot (over the edge),
	// "Down" targets the hang on the face (the climb hand-off pose) - the two-phase path traces the corner.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown")
	FName MantleDownForwardWarpTargetName = FName("MantleDownForward");

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown")
	FName MantleDownDownWarpTargetName = FName("MantleDownDown");

	// How fast the mantle-down eases its capsule from upright into the climb's wall lean (interp speed; higher = snappier;
	// 0 = snap). The event (below) gates WHEN this starts.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown", meta = (ClampMin = "0.0"))
	float MantleDownTiltBlendSpeed = 8.0f;

	// Gameplay event tag the mantle-down montage fires to START the tilt blend into the wall lean (routed through the
	// action system). Leave UNSET to blend immediately at entry; set it to hold upright until that authored frame.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|MantleDown")
	FGameplayTag MantleDownTiltBlendEventTag;

	// --- Corner turns (probe) ---
	// The probe runs on both sides each tick while climbing. Inner = side ray hits a wall beside you; outer = the face ends
	// within reach and a wrap ray finds the side face around the edge. Both resolve to the same corner frame.

	// (No height knob: the probe runs at the coverage grid's TOP (hand) row - ClimbGridTopOffset + the climb up-bias - the
	// same single authority as the climb itself. A valid climb only needs the hand row, so the turn does too; lower probes
	// would wrongly reject a corner while hanging, where the faces only exist up at the hands.)

	// INNER arming: the side wall must be within (capsule radius + this) of the capsule centre.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Corner", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float CornerInnerArmDistance = 20.0f;

	// OUTER arming: the face must END within this lateral distance of the capsule centre (the edge-test ray sits here).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Corner", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float CornerOuterArmDistance = 30.0f;

	// OUTER wrap ray origin: this far past the edge-test point (laterally) ...
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Corner", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float CornerOuterWrapOut = 30.0f;

	// ... and this far BEHIND the current face's plane, so the ray fired back toward the body strikes the side face.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Corner", meta = (ForceUnits = "cm", ClampMin = "1.0"))
	float CornerOuterWrapDepth = 25.0f;

	// Accepted turn angle (between the two face normals). Below the min, the sustained climb's normal smoothing follows the
	// bend procedurally; the clips are authored for ~90 and the warp absorbs the rest of this window.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Corner", meta = (ForceUnits = "degrees", ClampMin = "0.0", ClampMax = "180.0"))
	float CornerMinTurnDegrees = 60.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Corner", meta = (ForceUnits = "degrees", ClampMin = "0.0", ClampMax = "180.0"))
	float CornerMaxTurnDegrees = 120.0f;

	// Gap from a face to the capsule centre in the Mid/End poses. 0 = auto (capsule radius, body just touching) - the same
	// rule as the climb mode's ClimbWallStandoff, so the End pose matches what Climbing will hold.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Corner", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float CornerWallStandoff = 0.0f;

	// How far along the NEW face, away from the corner line, the End pose sits (capsule centre). Tune per type until the
	// clip's own root travel lands close to it (the warp should correct, not create, the motion). Inner must exceed the
	// capsule radius or the End capsule overlaps the old face.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Corner", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float CornerInnerEndOffset = 50.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Corner", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float CornerOuterEndOffset = 50.0f;

	// Shrink applied to the Mid/End fit capsules so a pose resting exactly one radius off a face doesn't overlap it.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Corner", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float CornerFitSkin = 3.0f;

	// --- Corner turns (the input-driven traversal) ---
	// The stick drives the clip's playhead; the capsule follows the clip's root path warped onto the probe's Mid/End.
	// Clip requirements: a ROOT-MOTION sequence (verify the retargeter's dotted root line AND that the root yaws ~90deg),
	// "Force Root Lock" ON (the AnimBP plays it in a Sequence Evaluator, where only that flag keeps the mesh on the capsule),
	// and a named notify (CornerTurnRestNotifyName) on the 4-limb corner rest frame.

	// Clip per corner type x side. "Left/Right" = the side the corner is on, i.e. the stick direction that enters it.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CornerTurn")
	TObjectPtr<UAnimSequence> CornerInnerLeftAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CornerTurn")
	TObjectPtr<UAnimSequence> CornerInnerRightAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CornerTurn")
	TObjectPtr<UAnimSequence> CornerOuterLeftAnimation;

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CornerTurn")
	TObjectPtr<UAnimSequence> CornerOuterRightAnimation;

	// Name of the notify marking the corner rest frame (between the two strikes) on each corner clip. Its time is the
	// hold pose AND the boundary between the two warps (strike 1 ends on the probe's Mid). Missing -> mid-clip + warning.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CornerTurn")
	FName CornerTurnRestNotifyName = FName("CornerRest");

	// Sideways stick (raw Y) needed to commit a turn; it must also dominate the up/down axis.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CornerTurn", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CornerTurnIntentThreshold = 0.5f;

	// Below this sideways push the stick counts as released and the playhead settles.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CornerTurn", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CornerTurnInputDeadzone = 0.15f;

	// Playhead rate at full push (1 = the clip's authored speed). Partial push scales it, like the climb playrate.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CornerTurn", meta = (ClampMin = "0.0"))
	float CornerTurnPlayRate = 1.0f;

	// Playhead rate while settling to a rest after release.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CornerTurn", meta = (ClampMin = "0.0"))
	float CornerTurnSettlePlayRate = 1.0f;

	// Per strike, the fraction of the strike after which a release settles FORWARD (finishes the strike); before it, the
	// playhead settles back to the strike's start. Put it just after the hand grabs. 0 = always finish once started.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CornerTurn", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CornerTurnStrike1CommitFraction = 0.4f;

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CornerTurn", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CornerTurnStrike2CommitFraction = 0.4f;

	// Pushing the stick the other way scrubs the clip backward (back toward the start / the corner rest). Off = an
	// opposite push just counts as a release (settle), for clips that look wrong reversed.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CornerTurn")
	bool bCornerTurnAllowReverse = true;

	TArray<FHitResult> CurrentWallHits;
	
	FCollisionQueryParams ClimbQueryParams;
	
	ACharacter* CacheOwnerCharacter;

	// --- Cached climb-surface state (refreshed each pre-sim-tick by RefreshClimbSurfaceInfo) ---

	bool bFacingClimbableSurface = false;
	// Enough hand purchase to traverse (top row >= ClimbTopRowMinHitsToMove); gates along-wall locomotion in the climb mode.
	bool bCanMoveOnWall = false;
	// BOTH feet lack a reachable foothold -> the whole lower body hangs (dead-hang upper body). Set from the foot probes.
	bool bLowerBodyHanging = false;

	// Per-foot foothold probe results (refreshed each pre-sim tick while climbing). Drive the per-foot tuck / plant IK.
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing", meta = (AllowPrivateAccess = "true"))
	FClimbFootProbeResult LeftFootProbe;
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing", meta = (AllowPrivateAccess = "true"))
	FClimbFootProbeResult RightFootProbe;

	// Per-hand grip points on the wall (body-anchored; refreshed while climbing). Pin the Control Rig hand effectors to
	// these for the FBIK dead-hang (hands stay anchored, body drops). Location/Normal valid when bFound.
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing", meta = (AllowPrivateAccess = "true"))
	FClimbHandGrip LeftHandGrip;
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing", meta = (AllowPrivateAccess = "true"))
	FClimbHandGrip RightHandGrip;
	FVector ClimbDominantSurfaceNormalCache = FVector::ZeroVector;
	FVector ClimbDominantSurfaceLocationCache = FVector::ZeroVector;
	TArray<FClimbSurfaceSample> ClimbSurfaceSamplesCache;

	// Sim time (ms) until which contextual climb re-entry is blocked. Set by BeginClimbReentryCooldown on exit.
	double ClimbReentryUnblockSimTimeMs = 0.0;

	// --- Cached mantle-probe state (refreshed each pre-sim-tick while climbing) ---
	bool bMantleLedgeAvailable = false;
	FTransform MantleLandingTransformCache = FTransform::Identity;   // "forward" warp target: the landing on top
	FTransform MantleEdgeTransformCache = FTransform::Identity;      // "up" warp target: the lip/edge at the wall face

	// Set by OnMantleUprightBlendEvent when the montage fires the upright-blend event; reset per mantle in BeginMantle.
	// Gates the mantle mode's ease-to-vertical (see ShouldMantleBlendUpright).
	bool bMantleUprightBlendRequested = false;

	// --- Cached mantle-down-probe state (refreshed each pre-sim-tick while grounded) ---
	bool bMantleDownAvailable = false;
	FTransform MantleDownEdgeTransformCache = FTransform::Identity;   // "forward" warp target: the lip pivot, over the edge
	FTransform MantleDownHangTransformCache = FTransform::Identity;   // "down" warp target: the hang on the face (climb pose)
	FVector MantleDownWallNormalCache = FVector::ZeroVector;          // detected face normal; the mode's tilt reads this

	// Set by OnMantleDownTiltBlendEvent when the montage fires the tilt-blend event; reset per drop in BeginMantleDown.
	// Gates the mantle-down mode's ease-into-wall-lean (see ShouldMantleDownBlendTilt).
	bool bMantleDownTiltBlendRequested = false;

	// --- Cached corner-probe state (refreshed each pre-sim tick while climbing) ---
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|Corner", meta = (AllowPrivateAccess = "true"))
	FClimbCornerProbeResult LeftCorner;
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|Corner", meta = (AllowPrivateAccess = "true"))
	FClimbCornerProbeResult RightCorner;

	// The in-progress corner turn (meaningful while IsCornerTurning()).
	FRogueCornerTurnState CornerTurn;

	// Owner's warping component (found in BeginPlay); drives the mantle's motion-warp alignment to the ledge.
	UPROPERTY(Transient)
	TObjectPtr<UMotionWarpingComponent> MotionWarpingComp;

	// Anim-facing coverage scalars in [0,1]; read by the AnimBP to drive hang/lean blends.
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing", meta = (AllowPrivateAccess = "true"))
	float LowerBodySupport = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing", meta = (AllowPrivateAccess = "true"))
	float UpperBodySupport = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing", meta = (AllowPrivateAccess = "true"))
	float LeftSupport = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing", meta = (AllowPrivateAccess = "true"))
	float RightSupport = 0.0f;
};
