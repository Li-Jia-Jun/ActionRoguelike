// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MovementMode.h"
#include "Engine/EngineTypes.h"
#include "RogueMantleMode.generated.h"

class UCommonLegacyMovementSettings;

/**
 * URogueMantleMode: a one-shot Mover movement mode for the climb top-out (mantle).
 *
 * The actual motion comes from an FLayeredMove_AnimRootMotion (an authored, motion-warped root-motion montage,
 * queued by URogueCharacterMoverComponent::BeginMantle) that runs with MixMode=OverrideAll, so this mode's
 * GenerateMove is skipped while the montage plays and only needs to execute the layered move's proposed motion.
 * Being a distinct mode (not the climb mode) means IsClimbing() is false during the mantle, so the climb
 * transition's exit-to-Falling can't fire and fling the player off the top. URogueMantleTransition owns entry
 * (from climbing) and the handoff to Walking when the layered move finishes.
 */
UCLASS(MinimalAPI, Blueprintable, BlueprintType)
class URogueMantleMode : public UBaseMovementMode
{
	GENERATED_UCLASS_BODY()

public:
	// Registered movement-mode name for the mantle.
	ACTIONROGUELIKE_API static const FName ModeName;

	virtual void GenerateMove_Implementation(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, FProposedMove& OutProposedMove) const override;

	virtual void SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState) override;

	// Relax the capsule's collision for the duration of the mantle so the authored, motion-warped root motion drives it
	// freely up-and-over without snagging the wall/lip (the landing was capsule-fit-tested by the probe on entry).
	virtual void Activate() override;
	virtual void Deactivate() override;

protected:
	virtual void OnRegistered(const FName ModeName) override;
	virtual void OnUnregistered() override;

	// Writes the post-move location/orientation/velocity back into the output sync state.
	void CaptureFinalState(USceneComponent* UpdatedComponent, const FMovementRecord& Record, const FMoverDefaultSyncState& StartSyncState, const FVector& AngularVelocityDegrees, FMoverDefaultSyncState& OutputSyncState) const;

	TObjectPtr<const UCommonLegacyMovementSettings> CommonLegacySettings;

	// Capsule collision setting captured on Activate() and restored on Deactivate().
	TEnumAsByte<ECollisionEnabled::Type> PrevCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
};
