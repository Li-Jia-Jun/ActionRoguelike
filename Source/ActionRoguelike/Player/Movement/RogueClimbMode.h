// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MovementMode.h"
#include "RogueClimbMode.generated.h"

class UCommonLegacyMovementSettings;

/**
 * URogueClimbMode: a Mover movement mode for climbing a wall surface.
 *
 * Modeled on UFlyingMode (free movement, no floor, stays upright), but constrained to the wall plane:
 * GenerateMove maps the player's raw stick input onto the wall (up/right along the surface), orients the
 * character to face the wall, and biases slightly into it to keep contact. The wall plane comes from the
 * URogueCharacterMoverComponent's cached dominant surface normal. The mode auto-exits to Falling when the
 * surface is lost (CanClimbNow() becomes false, e.g. climbing over the top).
 */
UCLASS(MinimalAPI, Blueprintable, BlueprintType)
class URogueClimbMode : public UBaseMovementMode
{
	GENERATED_UCLASS_BODY()

public:
	// Registered movement-mode name for climbing.
	ACTIONROGUELIKE_API static const FName ModeName;

	virtual void GenerateMove_Implementation(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, FProposedMove& OutProposedMove) const override;

	virtual void SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState) override;

protected:
	virtual void OnRegistered(const FName ModeName) override;
	virtual void OnUnregistered() override;

	// Writes the post-move location/orientation/velocity back into the output sync state.
	void CaptureFinalState(USceneComponent* UpdatedComponent, const FMovementRecord& Record, const FMoverDefaultSyncState& StartSyncState, const FVector& AngularVelocityDegrees, FMoverDefaultSyncState& OutputSyncState) const;

	// --- Climb tuning ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb", meta = (ForceUnits = "cm/s"))
	float ClimbMaxSpeed = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb", meta = (ForceUnits = "cm/s^2"))
	float ClimbAcceleration = 512.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb", meta = (ForceUnits = "cm/s^2"))
	float ClimbDeceleration = 1024.0f;

	// Max speed of the wall-contact correction (the clamp on the standoff controller). The capsule is pulled toward the
	// wall when farther than ClimbWallStandoff and eased to a stop as it arrives (and nudged back out if it has sunk
	// past it) - NOT a constant press, so it can't bulldoze the body into the wall on a hang.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb", meta = (ForceUnits = "cm/s"))
	float ClimbIntoWallSpeed = 40.0f;

	// Target perpendicular distance from the capsule centre to the dominant wall plane - the standoff the contact
	// controller holds. 0 = auto (use the capsule radius, so the body just touches). If set manually, keep it >= the
	// capsule radius, or on a hang (wall only up at the hands, nothing to brace the pull) the body is drawn INTO the wall.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb", meta = (ForceUnits = "cm", ClampMin = "0.0"))
	float ClimbWallStandoff = 0.0f;

	TObjectPtr<const UCommonLegacyMovementSettings> CommonLegacySettings;
};
