// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "RogueCharacterMoverComponent.generated.h"

/** One sample of the climb coverage grid: a short forward probe at one grid cell. */
struct FClimbSurfaceSample
{
	// True only if this probe landed on a valid (near-vertical, character-facing) climb surface.
	bool bHit = false;
	FVector Point = FVector::ZeroVector;
	FVector Normal = FVector::ZeroVector;
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

	virtual void BeginPlay() override;
	
	const TArray<FHitResult>& GetCurrentWallHits() const { return CurrentWallHits; }
	
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing")
	bool IsWallDetected() const { return CurrentWallHits.Num() > 0; }

	// True when the most recent coverage grid deemed the surface ahead climbable (cached; refreshed each tick).
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing")
	bool CanClimbNow() const { return bFacingClimbableSurface; }

	// True while the climb movement mode is the active movement mode.
	UFUNCTION(BlueprintPure, Category = "Mover|Climbing")
	bool IsClimbing() const;

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

protected:

	UFUNCTION()
	void HandlePreSimulationTick(const FMoverTimeStep& TimeStep, const FMoverInputCmdContext& InputCmd);
	
	void SweepAndStoreWallHits();
	
	void RefreshClimbSurfaceInfo();

	bool IsSurfaceClimbable(float SteepnessDotProduct) const;
	
	bool EyeHeightTrace(const float TraceDistance) const;

	// --- Detection tuning ---
	
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Detection", meta = (ForceUnits = "cm"))
	float ClimbDetectionCapsuleRadius = 50.0f;
	
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Detection", meta = (ForceUnits = "cm"))
	float ClimbDetectionCapsuleHalfHeight = 72.0f;
	
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Detection", meta = (ForceUnits = "cm"))
	float ClimbDetectionForwardOffset = 20.0f;
	
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|Detection", meta = (ForceUnits = "degrees"))
	float MinHorizontalDegreesToStartClimbing = 25.0f;

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

	// Fraction of the GRIP (upper) rows that must be backed by surface to allow starting a climb.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CoverageGrid", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinClimbCoverageRatio = 0.6f;

	// Max angle a valid sample normal may deviate from the averaged normal (rejects corners / fragmented walls).
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CoverageGrid", meta = (ForceUnits = "degrees"))
	float MaxClimbNormalDeviationDegrees = 35.0f;

	// Max |dot(normal, up)| for a surface to count as a near-vertical wall. 0 = perfectly vertical.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CoverageGrid", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxClimbableSteepnessDot = 0.3f;

	// Number of bottom rows treated as "lower body": drives LowerBodySupport / hang, and excluded from the entry gate.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CoverageGrid", meta = (ClampMin = "1"))
	int32 ClimbLowerBodyRowCount = 2;

	// VInterpTo speed the cached dominant normal/location follow the raw grid average. Damps jitter on curved
	// surfaces (cylinders). 0 = snap (no smoothing); higher = snappier but jitterier.
	UPROPERTY(EditDefaultsOnly, Category = "Mover|Climbing|CoverageGrid", meta = (ClampMin = "0.0"))
	float ClimbNormalSmoothingSpeed = 10.0f;

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
	
	TArray<FHitResult> CurrentWallHits;
	
	FCollisionQueryParams ClimbQueryParams;
	
	ACharacter* CacheOwnerCharacter;

	// --- Cached climb-surface state (refreshed each pre-sim-tick by RefreshClimbSurfaceInfo) ---

	bool bFacingClimbableSurface = false;
	FVector ClimbDominantSurfaceNormalCache = FVector::ZeroVector;
	FVector ClimbDominantSurfaceLocationCache = FVector::ZeroVector;
	TArray<FClimbSurfaceSample> ClimbSurfaceSamplesCache;

	// Sim time (ms) until which contextual climb re-entry is blocked. Set by BeginClimbReentryCooldown on exit.
	double ClimbReentryUnblockSimTimeMs = 0.0;

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
