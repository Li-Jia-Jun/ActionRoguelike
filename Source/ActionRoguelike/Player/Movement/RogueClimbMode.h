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

	// Constant speed pressed toward the wall so the capsule stays in contact (collision absorbs the inward part).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climb", meta = (ForceUnits = "cm/s"))
	float ClimbIntoWallSpeed = 40.0f;

	TObjectPtr<const UCommonLegacyMovementSettings> CommonLegacySettings;
};
