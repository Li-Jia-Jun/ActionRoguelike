// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "GameplayTagContainer.h"
#include "ActionSystem/GameplayEvent/FRogueGameplayEventData.h"
#include "RogueCharacterMoverComponent.generated.h"

class UAnimMontage;
class UAnimSequence;
class UMotionWarpingComponent;
class UBlendSpace;
class UCurveFloat;
class USkeletalMeshComponent;
class USkeletalMesh;
struct FRogueClimbLimbContact;
struct FRogueClimbLimbPlant;

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
 * Transient result of one limb-contact sweep toward a climb face (ProbeContactToward): the predictive plant target sweeps
 * from the baked landing's anchor, the corner turn from each LIVE limb bone, and the arm edge-search reuses it too.
 * Not stored frame-to-frame - it feeds RefreshLimbPins, which owns the persistent world-locked pin (FClimbLimbPin).
 */
struct FClimbHandGrip
{
	// True when a climbable grip was found by the sweep.
	bool bFound = false;

	// World grip point (sweep contact on the wall) + its surface normal. When !bFound, a sensible nearby point: the anchor
	// projected onto the plane - so a consumer never reads world origin.
	FVector Location = FVector::ZeroVector;
	FVector Normal = FVector::ZeroVector;
};

/**
 * Per-limb contact pin for the Control Rig plant/grip IK. Driven by an authored per-clip STANCE curve (0..1, 1 = fully
 * in a holding section): while a limb is in its stance window and a surface is reachable, the mode pins the limb to a
 * WORLD spot on the wall and holds it there while the body climbs (so the hand/foot doesn't slide); during swing the pin
 * releases and the limb follows the clip. The pin is a ONE-WAY ratchet within a stance - acquire once, then hold until the
 * stance ends or the body outreaches it - so it can't flicker. Weight/Location/Normal are what the Control Rig consumes;
 * the trailing fields are latch bookkeeping and persist across ticks (so these structs are NOT reset each refresh).
 */
USTRUCT(BlueprintType)
struct FClimbLimbPin
{
	GENERATED_BODY()

	// Control Rig: IK blend weight 0..1 - the authored stance alpha while pinned, 0 while swinging/unpinned. This is the
	// single on/off AND strength signal: Weight > 0 == this limb is pinned. The stance curve's own edge ramps ease it
	// in/out, so no extra C++ smoothing is needed.
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|LimbIK")
	float Weight = 0.0f;

	// The WORLD pin target + its surface normal. Frozen to the LAST pin when unpinned (never zeroed), so a pin blending
	// out never drags the effector toward world origin. For debug / world-space consumers - the Control Rig uses the
	// component-space pair below.
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|LimbIK")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|LimbIK")
	FVector Normal = FVector::ZeroVector;

	// Control Rig: the same pin target + normal in the MESH COMPONENT's space (the world pair converted by the skeletal
	// mesh transform each tick). Feed THESE to the IK effectors - CR works in component space, so a world position flings
	// the rig. Recomputed every tick, so while a held pin stays world-fixed its component-space value tracks the moving mesh.
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|LimbIK")
	FVector LocalLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|LimbIK")
	FVector LocalNormal = FVector::ZeroVector;

	// --- internal latch (persists across ticks; NOT exposed - Weight > 0 is the public "is pinned") ---
	bool bPinned = false;                             // currently holding a world-locked pin (the one-way ratchet)
	bool bHasLastPin = false;                         // a pin has been established at least once (arm fallback grace-holds it)
	FVector LastPinLocation = FVector::ZeroVector;
	FVector LastPinNormal = FVector::ZeroVector;

	// --- IK-target spring latch (persists across ticks; smooths the WORLD target so an acquire/relocate eases in instead of
	// popping). A FVectorSpringState mirrored as bare fields so this header needn't pull in KismetMathLibrary. ---
	FVector SpringSmoothedWorld = FVector::ZeroVector; // last sprung Control-Rig target (world, incl. the surface offset)
	FVector SpringPrevTarget = FVector::ZeroVector;
	FVector SpringVelocity = FVector::ZeroVector;
	bool bSpringValid = false;

	// --- predictive-IK latch (persists across ticks; used only by the predictive path) ---
	int32 PredictSelectedPlant = 0;    // the baked plant index currently approached/locked (phase-picked; for debug/clarity)
	bool bClipSwitchEase = false;      // the blendspace's dominant clip changed under this limb: its target eases past the teleport guard until it arrives
	bool bApproaching = false;         // unpinned and aiming at a plant the resolver re-resolves every tick (the reach leash leaves it alone)
	FVector DisplayedWorld = FVector::ZeroVector; // the world IK target actually handed to the rig last tick (after the reach cap) - a re-grip swing starts here
	bool bClipSwitchRegripped = false; // already re-gripped on a clip switch this plant - cleared when the clip next swings it (stops a flickering switch hopping it)
	bool bRegripSwing = false;         // on a re-grip swing (an idle-retreat step, a clip-switch re-grip, the leash's last resort), travelling to its new grip (cleared on arrival)

	// --- re-grip swing (the lift-travel-place arc; see EvaluateRegripSwing) ---
	FVector RegripSwingFrom = FVector::ZeroVector;        // the world IK target at release - where the arc starts
	FVector RegripSwingFromNormal = FVector::ZeroVector;  // surface normal at the start grip - the lift-off direction
	double RegripSwingStartMs = 0.0;                      // sim time of the release
	float RegripSwingDuration = 0.0f;                     // sized on the swing's first tick, once the target is known (0 = not yet)
	float RegripSwingLift = 0.0f;                         // peak lift off the wall (cm), sized with the duration
	FVector RegripSwingPeelDir = FVector::ZeroVector;     // in-plane "away from the body" at the swing - the peel leans the normal toward it
};

/** Limb ids for the idle-retreat sequencer - the order of RefreshLimbPins' pin array (LF, RF, LH, RH). */
namespace RogueClimbLimb
{
	enum Type : int32 { LeftFoot = 0, RightFoot, LeftHand, RightHand, Count };
}

/**
 * One step of the idle-retreat sequence (BuildIdleRetreatPlan): a single limb, or a CONTRALATERAL pair (a hand + the
 * opposite foot) whose follower releases IdleRetreatPairStagger after the lead. Steps run strictly one after another.
 */
struct FClimbRetreatStep
{
	int32 Lead = INDEX_NONE;     // RogueClimbLimb id, released when the step starts
	int32 Follow = INDEX_NONE;   // the pair's second limb (INDEX_NONE for a single)
	float Displacement = 0.0f;   // ranking key: the limb's distance to its idle grip, or the pair's SUM
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

	// Round (pre-square) climb push amount (0..1), direction-independent: 1 at any full push. Feed this to the blendspace
	// PLAYRATE (x your motion-speed scale); the body follows the anim via ClimbDistance, so the playrate IS the climb speed.
	// Do NOT use |GetClimbMoveIntent()| for playrate - its square mapping reaches sqrt(2) on diagonals and would make
	// diagonal climbing play (and therefore move) too fast.
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing")
	float GetClimbMoveSpeedFraction() const;

	// The climb move input to feed the simulation this frame - called by the player's ProduceInput with the raw stick while
	// climbing or corner turning. Normally the raw input, passed through. STRIKE COMMIT: if the stick is released while the
	// strike in progress is past its StrikeCommit marker, it keeps returning the last commanded input until the clip crosses
	// that strike's StrikeEnd marker - so the climb finishes pulling over instead of stopping mid-strike - then lets go.
	FVector ResolveClimbMoveInput(const FVector& RawInput);

	// True while a released stick is being held to finish a committed strike (see ResolveClimbMoveInput).
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing")
	bool IsClimbStrikeCommitting() const { return bClimbStrikeCommitting && IsClimbing(); }

	// The along-wall SPEED (cm/s) for this tick, derived from the authored ClimbDistance curve's per-tick advance (its slope
	// IS the surge; no reference speed). Returns false when the push is idle or no ClimbDistance curve is on the pose - then
	// the climb mode keeps the solver's own velocity (decelerates to a stop with no input). Refreshed each pre-sim tick by
	// RefreshClimbDistanceSpeed().
	bool GetClimbAlongWallSpeed(float& OutSpeed) const { OutSpeed = ClimbAlongWallSpeedThisTick; return bClimbDistanceSpeedValid; }

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

	// Reads the blended ClimbDistance curve and differences it across pre-sim ticks to get the along-wall speed (Δdistance /
	// Δt), wrap-handled via a tracked loop stride. Run each pre-sim tick; resets when not climbing, or when no ClimbDistance
	// curve is present (then GetClimbAlongWallSpeed returns false and the climb mode keeps the solver's own velocity).
	void RefreshClimbDistanceSpeed();

	// Per-foot foothold probes (run each pre-sim tick while climbing; reads the animated foot sockets). Populates
	// LeftFootProbe/RightFootProbe and, from them, bLowerBodyHanging (BOTH feet without a foothold). Self-gates on
	// IsClimbing() + a valid plane.
	void RefreshFootProbes();

	// Sphere-sweeps one foot (by socket) from just off the wall toward the dominant plane; returns the foothold result
	// (bHanging when none reachable). Uses the coverage grid's plane only for the sweep direction, not the leg rows.
	// bWasHanging is the foot's previous state, for release hysteresis (a planted foot searches wider).
	FClimbFootProbeResult ProbeFoot(FName FootSocketName, bool bWasHanging) const;

	// Sphere-sweeps from FromLoc toward a given (Normal, PlanePoint) plane with explicit extents; returns the climbable
	// contact (bFound when hit), else FromLoc projected onto the plane. The general probe behind the predictive plant target
	// (ResolvePlantTarget) and the corner turn's two-face probe (RefreshLimbPins calls it per face and takes the nearest hit).
	FClimbHandGrip ProbeContactToward(const FVector& FromLoc, const FVector& Normal, const FVector& PlanePoint,
		float Radius, float Reach, float Backoff) const;

	// Per-limb stance pins (run each pre-sim tick AFTER the foot probes). Sustained climb: PREDICTIVE - each limb aims at its
	// baked landing on the destination clip (the dominant blendspace clip while moving, ClimbIdleClip while idle) and the idle
	// retreat sequencer settles the gripping limbs to the idle pose. Corner turn: REACTIVE - each limb probes both faces from
	// its live bone (ResolveLimbPin), since the corner clips carry no baked contacts. Populates the four FClimbLimbPin members.
	void RefreshLimbPins();

	// Corner-turn (reactive) pin resolution for one limb this tick. The limb's authored stance curve on the corner clip gates
	// pin/release (absent curve = dormant); in stance it acquires at the probed surface hit (bSurfaceFound + SurfaceLocation/
	// Normal) and holds it world-locked until the stance ends (the reach leash, run just before, keeps it in reach). An arm
	// over a void edge-searches back toward its last grip (VoidTarget = the clip's intended grip); a leg over a void stays
	// unpinned. Weight = the stance curve while pinned.
	void ResolveLimbPin(FClimbLimbPin& Pin, FName StanceCurveName, bool bSurfaceFound, const FVector& SurfaceLocation,
		const FVector& SurfaceNormal, const FVector& VoidTarget, const FVector& WallNormal, bool bIsArm) const;

	// PREDICTIVE limb resolution (the sustained climb). It aims the limb at its BAKED landing for the current stance
	// (URogueClimbContactMetaData on DominantClip), resolved against the live mesh root (MeshXform) and traced onto the real
	// surface - re-resolved each frame through the approach (so the body's forward travel is folded in, no fall-short) then
	// WORLD-LOCKED at full contact (W >= PredictiveLockStanceValue). The plant (big/small strike) is picked by the live
	// ClimbPhase curve; W is the destination clip's own stance curve at that phase. Weight = W, smoothed.
	void ResolveLimbPinPredictive(FClimbLimbPin& Pin, FName StanceCurveName, const UAnimSequence* DominantClip,
		const FTransform& MeshXform, const FVector& WallNormal, const FVector& PlanePoint,
		float ProbeRadius, float ProbeReach, float ProbeBackoff, bool bIdle) const;

	// The world spot a predictive limb aims at for a baked plant: its root-relative landing resolved against the live mesh
	// (MeshXform), traced onto the surface along -WallNormal. A miss (over a void) aims at the predicted anchor itself.
	FClimbHandGrip ResolvePlantTarget(const FRogueClimbLimbPlant& Plant, const FTransform& MeshXform, const FVector& WallNormal,
		const FVector& PlanePoint, float ProbeRadius, float ProbeReach, float ProbeBackoff) const;

	// Idle-retreat SEQUENCER. BuildIdleRetreatPlan runs ONCE at committed idle: measures each gripping limb's distance to its
	// idle grip (IdleClip's baked landing), groups the limbs that need to move into contralateral pairs + singles, and ranks
	// the steps most-displaced first into RetreatPlan. AdvanceIdleRetreat runs every idle tick BEFORE the resolvers: it
	// releases the current step's limbs (lead, then the follower after the stagger) - each re-locks at its idle grip that same
	// tick and the spring eases it there - and moves on once they arrive. ResetIdleRetreat drops any plan + re-arms the timer.
	void BuildIdleRetreatPlan(const UAnimSequence* IdleClip, const FTransform& MeshXform, const FVector& WallNormal, const FVector& PlanePoint);
	void AdvanceIdleRetreat();
	void ResetIdleRetreat();

	// The world IK target this tick for a limb on an re-grip swing: a cubic Bezier from where it was released, straight
	// up off the wall, across, and straight down onto To (its current target) - sized from the distance on the first tick,
	// timed by RegripSwingProfile (or a smoothstep). OutNormal = the surface normal blended along the swing, tilted by
	// the effector peel (RegripHandPeelAngle / RegripFootPeelAngle). Clears
	// Pin.bRegripSwing when the swing completes (the sequencer's arrival).
	FVector EvaluateRegripSwing(FClimbLimbPin& Pin, const FVector& To, FVector& OutNormal) const;

	// Idle-retreat WEIGHT SHIFT. A limb is UNLOADED from its sequencer step's start until it lands (IsLimbUnloaded).
	// RefreshClimbWeightShift (end of RefreshLimbPins) shifts the body toward the limbs still bearing weight, presses the
	// hips in while a hand is unloaded, smooths it, and publishes ClimbBodyShift + ClimbChestRotation in mesh space.
	bool IsLimbUnloaded(int32 Limb) const;
	void RefreshClimbWeightShift(const USkeletalMeshComponent* Mesh);

	// Limb lookups by RogueClimbLimb id (the sequencer's limb handle).
	FClimbLimbPin& GetLimbPin(int32 Limb);
	const FClimbLimbPin& GetLimbPin(int32 Limb) const;
	FName GetLimbStanceCurveName(int32 Limb) const;

	// Unpin a limb for a RE-GRIP swing: the resolver re-locks it this same tick (idle grip at idle, the current clip's plant
	// while moving) and it travels there on the re-grip arc (bRegripSwing until it lands). Every lock-to-lock move goes
	// through this: the idle sequencer, the clip-switch re-grip and the reach leash's last resort (a limb it can't keep on
	// its face within reach).
	void BeginRegripSwing(FClimbLimbPin& Pin) const;

	// REACH LEASH (bLimbReachLeash), every climbing context, before the resolvers: a pinned target - or one still fading out
	// on its frozen grip - never sits farther than LimbReachLeashRatio x its limb's reach from the limb's root; past that it's
	// pulled back along its face by exactly the excess, re-traced onto the real surface (the spring state moves with it, so
	// the shown limb doesn't lag). A pinned limb it can't keep on its face within reach: bPredictive (the climb) -> re-grips
	// on the arc onto this tick's plant; the corner turn -> holds (its stance ends it). Returns true when it dragged a PINNED target.
	bool LeashLimbTargetsWithinReach(const USkeletalMeshComponent* Mesh, bool bPredictive);

	// Clip-switch re-grip (bRegripOnClipSwitch), run on the switch tick before the resolvers: every LOCKED limb the new clip
	// also has in stance, whose new plant is farther than LimbRegripMinDistance, re-grips onto it - all at once.
	void RegripLockedLimbsOnClipSwitch(const UAnimSequence* Clip, const FTransform& MeshXform, const FVector& WallNormal,
		const FVector& PlanePoint);

	// Take a limb that left out of turn (the reach leash's last-resort re-grip) out of the rest of the idle-retreat plan.
	void DropLimbFromRetreatPlan(int32 Limb);

	// Straight reach of a limb (root to contact tip, reference pose, x mesh scale); cached per mesh asset.
	float GetLimbReachLength(const USkeletalMeshComponent* Mesh, int32 Limb);
	FName GetLimbRootBoneName(int32 Limb) const;
	FName GetLimbReachTipName(int32 Limb) const;

	// The reach cap (bCapLimbReach): Target pulled in toward the limb's root (last evaluated pose) so it never exceeds
	// LimbReachLimit x reach, softly past LimbReachSoftStart. Returns Target unchanged when within reach / off / no data.
	FVector CapToLimbReach(const USkeletalMeshComponent* Mesh, int32 Limb, const FVector& Target);

	// The highest-weight climb clip for the current blendspace input (GetClimbMoveIntent), queried off ClimbBlendSpace (the
	// component drives that input, so it can self-serve without AnimBP plumbing). Null when no BS / no samples. OutWeight = its weight.
	UAnimSequence* GetDominantClimbClip(float& OutWeight);

	// How long the climb POSE takes to crossfade on a clip switch, read off ClimbBlendSpace's own settings (0 when it doesn't
	// smooth, or bClipSwitchBlend is off): Sample Smoothing's Weight Speed if set, else the axes' Input Interpolation time.
	// The limb IK's clip-switch blend uses it, so it always matches the pose.
	float GetClimbClipSwitchBlendTime() const;

	// True when the strike in progress on the dominant climb clip is past its commit marker: the most recent strike marker at
	// or before the live phase (circular - the clip loops) is a commit, not an end. False without markers / a clip / a phase.
	// bLogDecision logs the clip, phase and outcome (or why it couldn't decide) - used on the first check after a release.
	bool IsPastClimbStrikeCommit(bool bLogDecision = false);

	// The baked limb-contact entry for LimbId on Clip (reads the clip's URogueClimbContactMetaData), or null if unbaked.
	const FRogueClimbLimbContact* GetLimbContact(const UAnimSequence* Clip, FName LimbId) const;

	// Reads a float curve from the mesh's anim instance; returns false when the curve is absent from the current pose
	// (which keeps that limb's pin dormant - the per-limb opt-in).
	bool GetStanceCurveValue(FName CurveName, float& OutValue) const;

	// Arm void fallback: sample a couple of points along [VoidTarget -> LastGrip] and sphere-trace each toward the wall;
	// outputs the nearest-to-void climbable hit, else LastGrip (grace-hold). Returns false only with nothing to hold.
	bool FindHandFallbackGrip(const FVector& VoidTarget, const FVector& LastGrip, const FVector& WallNormal,
		FVector& OutLocation, FVector& OutNormal) const;

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

	// Temporal debounce: once a foot's plant<->hang state flips, it must hold the new state for this long before it may
	// flip again. Stops the plant/hang state switching too frequently at a ledge edge (a time rule, complementing the
	// position-based idle latch and release hysteresis above). 0 = off.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|FootIK", meta = (ForceUnits = "s", ClampMin = "0.0"))
	float FootHangCooldownSeconds = 0.12f;

	// --- Per-hand grip probe (sweep params for a hand's contact trace) ---
	// A hand's grip is found by sweeping toward the wall: from the baked landing's anchor (predictive climb) or the live hand
	// bone (corner turn). These tune that sweep; it also backs the arm edge-search fallback.

	// Sphere radius of the hand-grip probe (search tolerance around the anchor).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|HandIK", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float HandProbeRadius = 12.0f;

	// How far toward the wall (from the backed-off origin) to search for the grip.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|HandIK", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float HandProbeReach = 40.0f;

	// How far to back the sweep origin OFF the wall (along the plane normal) so it starts in free space in front of the face.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|HandIK", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float HandProbeBackoff = 25.0f;

	// --- Per-limb stance pins (Control Rig plant/grip IK) ---
	// Each limb is pinned to the wall during its authored STANCE (holding) curve window and released during swing, so the
	// contact is world-locked (no slide) while the body climbs free. A per-clip float curve (0..1) per limb drives it; an
	// ABSENT curve leaves that limb's pin dormant (weight 0, no change), so you can author + enable this one limb at a time.

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Setup")
	FName LeftFootStanceCurveName = FName("Stance_FootL");

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Setup")
	FName RightFootStanceCurveName = FName("Stance_FootR");

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Setup")
	FName LeftHandStanceCurveName = FName("Stance_HandL");

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Setup")
	FName RightHandStanceCurveName = FName("Stance_HandR");

	// Live hand bones the corner-turn pins trace from, so a hand pins where the corner CLIP reached (the corner clips carry
	// no baked contacts). The feet trace from LeftFootSocketName / RightFootSocketName the same way.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mover|Climbing|LimbIK|Setup")
	FName LeftHandSocketName = FName("hand_l");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mover|Climbing|LimbIK|Setup")
	FName RightHandSocketName = FName("hand_r");

	// Stance-curve value above which a limb counts as "holding" (fires the commit that establishes the world pin).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|CornerTurn", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LimbStanceThreshold = 0.5f;

	// A limb whose NEW grip (the new clip's plant on a clip switch; its idle grip in the idle retreat) is within this of its
	// current one keeps the current one - a hop that short just looks restless.
	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|LimbIK|Placement", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float LimbRegripMinDistance = 8.0f;

	// How far the pinned contact is held OFF the wall surface, along the surface normal (the "lip" standoff) - hand/foot
	// thickness, or a grip that stands proud of the face. Applied to the Control Rig target (LocalLocation) each tick from
	// the raw on-surface pin, so it never accumulates on a held pin. 0 = flush on the surface.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Placement", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float LimbPinSurfaceOffset = 2.0f;

	// Spring-smooth the Control Rig IK target so a pin that ACQUIRES or RELOCATES (re-grab, corner face-switch) EASES to its
	// new spot over a few frames instead of popping. It smooths the WORLD target, which is constant while a pin is held, so a
	// settled plant gets no added lag - only target JUMPS are absorbed. Toggle OFF to compare against the raw, instant target.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Smoothing")
	bool bUseLimbIKSpring = true;

	// Spring stiffness: higher = snappier (reaches the new target faster), lower = looser / laggier. Pairs with the damping.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Smoothing", meta = (ClampMin = "0.0", EditCondition = "bUseLimbIKSpring"))
	float LimbIKSpringStiffness = 120.0f;

	// Spring damping: 1 = critically damped (no overshoot - the safe default), < 1 = springy / overshoots, > 1 = overdamped / sluggish.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Smoothing", meta = (ClampMin = "0.0", EditCondition = "bUseLimbIKSpring"))
	float LimbIKSpringCriticalDamping = 1.0f;

	// Teleport guard: if the target jumps farther than this in one tick (a far relocation, or the first acquire from a stale
	// last-pin) the spring SNAPS instead of easing across the whole gap. Keep it above a normal re-grab's step but below a
	// cross-wall jump. Only the ease is skipped - a held pin is unaffected (its target doesn't move). An idle-retreat
	// sequencer move is EXEMPT: it's an intended long move from a fresh target, so it always eases however far it goes.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Smoothing", meta = (ForceUnits = "cm", ClampMin = "0.0", EditCondition = "bUseLimbIKSpring"))
	float LimbIKSpringSnapDistance = 50.0f;

	// --- Predictive limb IK (the sustained climb; consumes the baked URogueClimbContactMetaData) ---

	// The climb blendspace asset (the same one dragged into the AnimBP). The component queries it with the blend input it
	// already drives (GetClimbMoveIntent) to find the dominant climb clip, so it can read that clip's baked limb-contact
	// metadata - NO AnimBP plumbing needed. Required for limb IK while moving: null = no destination clip, so the moving
	// limbs stay dormant (IK weight 0, the clip plays as authored).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Setup")
	TObjectPtr<UBlendSpace> ClimbBlendSpace;

	// Optional idle cling clip (your "climb idle" state's pose), BAKED like the move clips (flat 1.0 stance -> the baker's
	// always-planted fallback gives one landing per limb = the authored cling grips). When set, while IDLE the limbs settle
	// to these authored grips (its flat stance drives W=1) instead of holding wherever they last gripped. Null -> idle HOLDS
	// the last pins (the simpler behavior). Must be baked AND must carry the ClimbPhase curve (the baker stamps both).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Setup")
	TObjectPtr<UAnimSequence> ClimbIdleClip;

	// STAGED idle retreat (needs ClimbIdleClip). On returning to idle, a limb caught mid-swing retreats to its idle grip
	// immediately, but limbs that were GRIPPING HOLD their move grips - so a quick resume is seamless. Only after the body has
	// been idle this long (no move input, else it wouldn't still be idle) do the gripping limbs ALSO retreat to the authored
	// idle pose - SEQUENCED (the knobs below), never all at once. 0 = start the sequence as soon as idle begins.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|IdleRetreat", meta = (ForceUnits = "s", ClampMin = "0.0"))
	float IdleRetreatDelaySeconds = 0.5f;

	// --- Idle-retreat SEQUENCER (the committed-idle retreat above) ---
	// The gripping limbs retreat to their idle grips in a sequence that never moves both hands or both feet together: a
	// CONTRALATERAL pair (a hand + the opposite foot) when both of that diagonal need to move - the other diagonal stays
	// planted as a stable support - otherwise one limb at a time. Most-displaced step first (a pair counts its sum); each
	// step waits for its limbs to ARRIVE before the next starts. A limb within LimbRegripMinDistance of its idle grip stays put.

	// Within a pair, the follower releases this long after the lead - overlapping, but never simultaneous.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|IdleRetreat", meta = (ForceUnits = "s", ClampMin = "0.0"))
	float IdleRetreatPairStagger = 0.1f;

	// Within a pair: true = the HAND leads; false = the more-displaced limb leads.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|IdleRetreat")
	bool bIdleRetreatPairHandLeads = true;

	// Pause between a step's limbs arriving and the next step starting. 0 = start the next step on arrival.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|IdleRetreat", meta = (ForceUnits = "s", ClampMin = "0.0"))
	float IdleRetreatStepGap = 0.05f;

	// Safety: a step that hasn't arrived this long after it started moves on anyway, so the sequence can't stall. Keep it
	// above the longest swing (distance / RegripSwingSpeed), or the next step starts while a limb is still travelling.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|IdleRetreat", meta = (ForceUnits = "s", ClampMin = "0.0"))
	float IdleRetreatStepTimeout = 2.0f;

	// REACH LEASH (every climbing context: moving, idle, corner turn) - a limb is never left out of reach. A pinned target -
	// or one still fading out on its frozen grip after a clip switch - farther than LimbReachLeashRatio x the limb's straight
	// reference-pose length from its root (shoulder / hip, live pose) is pulled back along its face by exactly the excess,
	// the shortest way back into reach, and re-traced onto the real surface. It reads as the body dragging a hand that won't
	// let go, never pulling it straight. No state, no speed: the pin moves only while, and as fast as, the body pulls it.
	// Only a pinned limb it can't keep on its face (the face has no reachable point left, or that point is off the surface -
	// past an edge) lets go: the climb re-grips it on the arc onto the current plant; the corner turn holds it (the reach cap
	// bends it) until the corner clip's stance ends. Replaces the over-extension release, the idle reach slide and the old
	// body-travel break distance. Off = pins hold wherever they are (the reach cap still bends an out-of-reach limb).
	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|LimbIK|Reach")
	bool bLimbReachLeash = true;

	// The leash length as a fraction of each limb's OWN reach, so hands and feet each stop at their own stretch. At or below
	// LimbReachSoftStart, the reach cap never touches a pinned limb.
	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|LimbIK|Reach", meta = (ClampMin = "0.1", ClampMax = "1.0", EditCondition = "bLimbReachLeash"))
	float LimbReachLeashRatio = 0.8f;

	// REACH CAP (visual; every IK target - climb, re-grip swing, corner turn). The target handed to the Control Rig never
	// sits farther from the limb's root than LimbReachLimit x its reach: below LimbReachSoftStart x reach it's untouched,
	// beyond it the distance compresses smoothly (exponential ease - position and velocity stay continuous) toward the limit
	// and never passes it. An out-of-reach target - the start of a release swing, a clip-switch ease, a stranded pin - then
	// reads as "reaching toward it, still bent" instead of a locked-straight / stretched limb, and the cap lets go by itself
	// as the target comes back in reach. Pins and the spring keep the REAL target. With the leash on, pinned targets stay
	// below the cap, so it mostly catches targets still approaching a plant and a limb the leash couldn't keep on its face.
	// Put SoftStart at/above the extension your clips author at a plant, so normal plants stay exact. Known gap: an
	// intentionally straight pose (the dead hang) is bent too - to be special-cased.
	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|LimbIK|Reach")
	bool bCapLimbReach = true;

	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|LimbIK|Reach", meta = (ClampMin = "0.1", ClampMax = "1.0", EditCondition = "bCapLimbReach"))
	float LimbReachSoftStart = 0.85f;

	// The hard ceiling (fraction of reach). At/below SoftStart = a plain hard cap at this value.
	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|LimbIK|Reach", meta = (ClampMin = "0.1", ClampMax = "1.0", EditCondition = "bCapLimbReach"))
	float LimbReachLimit = 0.95f;

	// Limb roots for the reach measure (the chain's first joint). Hands measure to Left/RightHandSocketName (the palm
	// contact); feet to the tips below (the ball - where the foot pins aim - not the ankle).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Reach")
	FName LeftHandRootBoneName = FName("upperarm_l");

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Reach")
	FName RightHandRootBoneName = FName("upperarm_r");

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Reach")
	FName LeftFootRootBoneName = FName("thigh_l");

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Reach")
	FName RightFootRootBoneName = FName("thigh_r");

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Reach")
	FName LeftFootReachTipName = FName("ball_l");

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Reach")
	FName RightFootReachTipName = FName("ball_r");

	// --- RE-GRIP SWING (how a limb travels from one grip to another - every lock-to-lock move: an idle-retreat step, a
	// clip-switch re-grip, the reach leash's last resort) ---
	// A re-gripping limb is carried along a cubic Bezier - straight up off its grip (along the surface normal), across, and
	// straight down onto the new grip: the procedural "step" of spider/creature rigs - instead of sliding along the wall.

	// Off = the limb-IK spring slides the target along the wall to the new grip instead (no lift, no peel).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|RegripSwing")
	bool bRegripArc = true;

	// Arc OFF only: a re-gripping limb has ARRIVED once its spring-eased IK target is within this of the new grip (the move
	// is then the limb-IK spring sliding the target across; LimbIKSpringStiffness sets how long it takes).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|RegripSwing", meta = (ForceUnits = "cm", ClampMin = "0.1", EditCondition = "!bRegripArc"))
	float RegripArrivalTolerance = 2.0f;

	// Travel speed: a swing lasts its distance / this, but at least RegripSwingMinDuration.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|RegripSwing", meta = (ForceUnits = "cm/s", ClampMin = "1.0", EditCondition = "bRegripArc"))
	float RegripSwingSpeed = 100.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|RegripSwing", meta = (ForceUnits = "s", ClampMin = "0.01", EditCondition = "bRegripArc"))
	float RegripSwingMinDuration = 0.3f;

	// Peak lift off the wall, as a fraction of the swing distance (a short re-grip barely hops, a long one arcs), capped at
	// RegripSwingMaxLift.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|RegripSwing", meta = (ClampMin = "0.0", EditCondition = "bRegripArc"))
	float RegripSwingLiftRatio = 0.3f;

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|RegripSwing", meta = (ForceUnits = "cm", ClampMin = "0.0", EditCondition = "bRegripArc"))
	float RegripSwingMaxLift = 15.0f;

	// Optional timing profile along the arc (X = time 0..1, Y = progress 0..1, should start at 0 and end at 1). Empty = a
	// smoothstep ease-in/out. Shape it for a quick lift and a soft set-down: steep at the start, flattening into the end.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|RegripSwing", meta = (EditCondition = "bRegripArc"))
	TObjectPtr<UCurveFloat> RegripSwingProfile;

	// Effector PEEL during a swing: the published normal (what the Control Rig orients the hand/foot from) leans toward the
	// in-plane "away from the body" direction and back, tracking the lift (0 on both grips, peak mid-swing). With the palm/
	// sole laid across the normal, the edge nearest the body comes off first and the far edge (fingertips/toes) leaves last
	// and lands first. Negative flips it; 0 = no peel.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|RegripSwing", meta = (ForceUnits = "deg", ClampMin = "-90.0", ClampMax = "90.0", EditCondition = "bRegripArc"))
	float RegripHandPeelAngle = 25.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|RegripSwing", meta = (ForceUnits = "deg", ClampMin = "-90.0", ClampMax = "90.0", EditCondition = "bRegripArc"))
	float RegripFootPeelAngle = 15.0f;

	// --- Idle-retreat WEIGHT SHIFT (the body moves onto the remaining holds; RefreshClimbWeightShift) ---
	// While the sequencer has a limb UNLOADED (from its step's start until it lands), the pelvis shifts toward the centroid
	// of the limbs still bearing weight, the hips press in when a hand lets go, and the chest tilts/leans with it. Published
	// as ClimbBodyShift + ClimbChestRotation (mesh space) for the Control Rig: offset `pelvis`, then rotate the spine, BEFORE
	// the limb IK - the planted limbs, world-locked, bend to absorb it. Zero at rest. EditAnywhere: tweakable live in PIE.

	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|WeightShift")
	bool bIdleWeightShift = true;

	// The body shifts by (load-bearing centroid - all-four centroid) x these, split along the wall: sideways (horizontal) and
	// up/down (gravity flattened onto the wall). A single limb lifting shifts toward the other three; a diagonal pair barely
	// shifts (the support diagonal runs through the body's centre).
	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|WeightShift", meta = (ClampMin = "0.0", EditCondition = "bIdleWeightShift"))
	float WeightShiftLateralGain = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|WeightShift", meta = (ClampMin = "0.0", EditCondition = "bIdleWeightShift"))
	float WeightShiftVerticalGain = 0.3f;

	// Cap on the along-wall shift. Keep it small: two-bone IK can't stretch, so a planted limb would pull off its hold.
	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|WeightShift", meta = (ForceUnits = "cm", ClampMin = "0.0", EditCondition = "bIdleWeightShift"))
	float WeightShiftMaxDistance = 8.0f;

	// Hips pressed toward the wall while a HAND is unloaded (keeps the weight on the feet). Separate from the cap above.
	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|WeightShift", meta = (ForceUnits = "cm", ClampMin = "0.0", EditCondition = "bIdleWeightShift"))
	float WeightShiftHipInDepth = 3.0f;

	// Hip-in fades out as the wall leans back into a slab (climbers stand OFF a slab): full on vertical/overhang, zero once
	// the wall leans back this far.
	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|WeightShift", meta = (ForceUnits = "deg", ClampMin = "1.0", ClampMax = "90.0", EditCondition = "bIdleWeightShift"))
	float WeightShiftHipInSlabFadeAngle = 30.0f;

	// The step's limbs unload at its start; they LIFT this much later, so the body is already moving. 0 = lift at once (the
	// body then shifts during the swing).
	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|WeightShift", meta = (ForceUnits = "s", ClampMin = "0.0", EditCondition = "bIdleWeightShift"))
	float WeightShiftLead = 0.12f;

	// Critically damped smoothing of the shift (no overshoot): ~60% of the way after this long, ~90% after twice it.
	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|WeightShift", meta = (ForceUnits = "s", ClampMin = "0.0", EditCondition = "bIdleWeightShift"))
	float WeightShiftSmoothingTime = 0.12f;

	// Speed cap on the shift (incl. hip-in; the chest follows the shift, so its rotation rate is capped with it). The
	// smoothing alone can move fast when the target jumps far - crossed limbs put the load centroid well across, and the
	// next step can swing it to the other side. 0 = uncapped.
	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|WeightShift", meta = (ForceUnits = "cm/s", ClampMin = "0.0", EditCondition = "bIdleWeightShift"))
	float WeightShiftMaxSpeed = 25.0f;

	// Chest (a): sideways tilt toward the supporting side - the chest's top swings toward the sideways shift. 0 = off.
	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|WeightShift", meta = (EditCondition = "bIdleWeightShift"))
	float WeightShiftChestTiltPerCm = 1.0f;

	// Chest (b): lean back as the hips press in, sized to keep the shoulders where they were (atan(hip-in / shoulder
	// height)) so the planted arms are unaffected. 1 = that exact counter, 0 = off.
	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|WeightShift", meta = (ClampMin = "0.0", EditCondition = "bIdleWeightShift"))
	float WeightShiftChestLeanScale = 1.0f;

	// Pelvis-to-shoulder distance, for sizing the lean-back.
	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|WeightShift", meta = (ForceUnits = "cm", ClampMin = "1.0", EditCondition = "bIdleWeightShift"))
	float WeightShiftShoulderHeight = 50.0f;

	// Stance-curve value at/above which a predictive approach WORLD-LOCKS (the plant). Below it the destination re-resolves
	// each frame against the live root (folding in the body's travel - no fall-short); at/above it the pin freezes in world.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Placement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PredictiveLockStanceValue = 0.95f;

	// Name of the monotonic 0->1 phase curve the baker stamps on each climb clip. Read live (blended, via the anim instance)
	// to pick which baked plant (strike) a limb is approaching by CLIP POSITION - robust to where in the cycle climb was
	// entered, unlike a rising-edge counter. Must match the baker's PhaseCurveName.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Setup")
	FName ClimbPhaseCurveName = FName("ClimbPhase");

	// Smooths the predictive IK WEIGHT toward the raw stance-curve value, so a DISCONTINUITY in the weight (the clip playhead
	// jumping on an idle<->move state transition, or a held approach resuming) eases the foot in instead of snapping it.
	// Authored ramps are followed closely; only jumps are capped. Higher = snappier/less smoothing; 0 = none (raw weight).
	// Lock/release still use the RAW stance value, so pin timing is unaffected.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|Smoothing", meta = (ClampMin = "0.0"))
	float PredictiveWeightInterpSpeed = 12.0f;

	// BLENDSPACE CLIP SWITCH (e.g. W then D: Up -> Up-Right mid-strike): the dominant clip - and with it every limb's destination
	// landing - changes at once. For as long as the POSE takes to crossfade (GetClimbClipSwitchBlendTime: read off
	// ClimbBlendSpace's own smoothing, so there's nothing to keep in sync) the limbs blend instead of popping: a target jump
	// eases through the IK spring even past LimbIKSpringSnapDistance (until it arrives), and the weight eases (in AND out,
	// ~95% over that time) instead of dropping to 0 when the new clip says the limb is mid-swing. Progress within ONE clip is
	// unaffected (its plant-to-plant jumps happen at weight 0). Off = the old instant switch.
	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|LimbIK|ClipSwitch")
	bool bClipSwitchBlend = true;

	// CLIP-SWITCH RE-GRIP (case "locked + new clip in stance"): on a switch, a LOCKED limb the new clip also has in stance
	// would otherwise keep its old hold - a pose that suits the old clip, not the new one. Instead it re-grips: it lets go
	// and travels the re-grip arc (lift, travel, place + peel) onto the new clip's plant. All qualifying limbs go at once -
	// while moving the policy follows the input instantly; scheduling is for idle only. Once per limb per plant (a flickering
	// switch can't make it hop); a new plant within LimbRegripMinDistance keeps the hold. Off = keep the hold (the reach
	// leash keeps it in reach until the clip swings it).
	UPROPERTY(EditAnywhere, Category = "Mover|Climbing|LimbIK|ClipSwitch")
	bool bRegripOnClipSwitch = true;

	// Arm void fallback: when a hand is in its stance but found no grip, sample along the segment from the intended (void)
	// grip back to the last good grip and pin to the climbable hit NEAREST the void (most reach). These are the two sample
	// positions (0 = at the void target, 1 = at the last grip); "near" is checked first. None hit -> grace-hold the last grip.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|CornerTurn", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HandFallbackSampleNear = 0.25f;

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|LimbIK|CornerTurn", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HandFallbackSampleFar = 0.75f;

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

	// --- Climb speed (distance-driven, synced to the climb anim) ---

	// Monotonic 0->stride float curve authored on every climb clip (cumulative along-wall travel in cm; flat = hold/reach,
	// steep = pull - bake it from the root-motion sibling with URogueClimbDistanceBaker). The body advances by that curve's
	// change each tick, so its slope IS the surge and there is NO reference speed (the input magnitude is already in the
	// anim playrate). A clip missing it moves at plain solver speed (push x ClimbMaxSpeed).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Speed")
	FName ClimbDistanceCurveName = FName("ClimbDistance");

	// --- Strike commit (finish the pull on release) ---
	// The climb clips are two-strike: each strike is a reach, then a pull. Author two markers per strike on every climb clip:
	// a commit marker (the point of no return - e.g. once the reaching limbs have planted) and an end marker (the end of the
	// pull: four limbs planted, torso over them). Releasing the stick after a strike's commit marker keeps the climb going
	// to that strike's end marker, so it stops in an authored four-point hold close to idle; releasing before it stops as
	// before (blend to idle + retreat). A clip without the markers never commits. Either SYNC markers or plain NOTIFIES of
	// these names work; sync markers additionally keep the blendspace's samples phase-aligned on diagonal blends.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Strike")
	bool bCommitClimbStrikes = true;

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Strike", meta = (EditCondition = "bCommitClimbStrikes"))
	FName ClimbStrikeCommitMarkerName = FName("StrikeCommit");

	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Strike", meta = (EditCondition = "bCommitClimbStrikes"))
	FName ClimbStrikeEndMarkerName = FName("StrikeEnd");

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

	// Per-limb Control Rig plant/grip pins; refreshed by RefreshLimbPins after the foot/hand probes. These PERSIST across
	// ticks (they carry the pin latch), so unlike the probe structs they are not reset each refresh.
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|LimbIK", meta = (AllowPrivateAccess = "true"))
	FClimbLimbPin LeftFootPin;
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|LimbIK", meta = (AllowPrivateAccess = "true"))
	FClimbLimbPin RightFootPin;
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|LimbIK", meta = (AllowPrivateAccess = "true"))
	FClimbLimbPin LeftHandPin;
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|LimbIK", meta = (AllowPrivateAccess = "true"))
	FClimbLimbPin RightHandPin;

	// Idle-retreat weight shift for the Control Rig, both in the MESH COMPONENT's space (RefreshClimbWeightShift). Apply
	// before the limb IK: add ClimbBodyShift to `pelvis` (global), then rotate the spine chain by ClimbChestRotation
	// (global, split across the spine bones). Zero / identity at rest.
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|WeightShift", meta = (AllowPrivateAccess = "true"))
	FVector ClimbBodyShift = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|WeightShift", meta = (AllowPrivateAccess = "true"))
	FRotator ClimbChestRotation = FRotator::ZeroRotator;

	// The weight shift's smoothed WORLD offset + its rate (CriticallyDampedSmoothing state).
	FVector BodyShiftWorld = FVector::ZeroVector;
	FVector BodyShiftRate = FVector::ZeroVector;

	// Cache index for UBlendSpace::GetSamplesFromBlendInput (perf; seeded INDEX_NONE). Used by GetDominantClimbClip.
	int32 ClimbBSTriangulationCache = INDEX_NONE;

	// Blendspace clip-switch detection (RefreshLimbPins): the dominant clip on the last MOVING tick (cleared at idle / off the
	// sustained climb, so idle<->move edges don't count) and the sim time of the last switch (opens the GetClimbClipSwitchBlendTime window).
	TWeakObjectPtr<const UAnimSequence> LastDominantClimbClip;
	double ClimbClipSwitchMs = -1.0e9;

	// Sim time (ms) of the current pre-sim tick, set at the top of HandlePreSimulationTick; feeds the per-foot hang-flip
	// debounce (FootHangCooldownSeconds).
	double LastPreSimTimeMs = 0.0;

	// Duration (s) of the current pre-sim step, derived from consecutive LastPreSimTimeMs (clamped); feeds the limb-IK weight
	// smoothing + target spring and the ClimbDistance speed.
	float LastPreSimDeltaSeconds = 0.0f;

	// Idle-retreat sequencer state. IdleRetreatArmSimTimeMs = the last tick we were NOT idle-climbing (moving, corner turning,
	// off the wall), so committed idle = IdleRetreatDelaySeconds past it. The plan is built once per idle (bIdleRetreatPlanned)
	// and stepped by RetreatStepIndex; RetreatStepStartMs is when the current step begins (in the future during the inter-step
	// gap), and the two flags record which of its limbs have been released.
	double IdleRetreatArmSimTimeMs = 0.0;
	bool bIdleRetreatPlanned = false;
	TArray<FClimbRetreatStep> RetreatPlan;
	int32 RetreatStepIndex = INDEX_NONE;
	double RetreatStepStartMs = 0.0;
	bool bRetreatLeadReleased = false;
	bool bRetreatFollowReleased = false;

	// Last tick's idle state (an idle<->move edge clears every limb's bClipSwitchRegripped) and the cached straight reach per
	// limb (reference pose; rebuilt when the mesh asset changes - the reach leash and the reach cap measure against it).
	bool bLimbPinsWasIdle = false;
	float LimbReachLengthCache[RogueClimbLimb::Count] = {};
	TWeakObjectPtr<const USkeletalMesh> LimbReachLengthCacheMesh;

	// Per-foot sim time (ms) until which that foot's plant/hang state may not flip again (FootHangCooldownSeconds debounce).
	double LeftFootHangUnlockSimTimeMs = 0.0;
	double RightFootHangUnlockSimTimeMs = 0.0;

	// Distance-driven along-wall speed state (RefreshClimbDistanceSpeed): the speed this tick + whether a ClimbDistance curve
	// drove it, plus the previous curve value and the tracked loop stride (for the wrap).
	float ClimbAlongWallSpeedThisTick = 0.0f;
	bool bClimbDistanceSpeedValid = false;
	float ClimbDistancePrev = 0.0f;
	bool bHasClimbDistancePrev = false;
	float ClimbDistanceStride = 0.0f;

	// Strike commit (ResolveClimbMoveInput): the last commanded (non-idle) climb input - held after a release to finish a
	// committed strike - and whether it is being held right now.
	FVector ClimbLastMoveInput = FVector::ZeroVector;
	bool bClimbStrikeCommitting = false;

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
