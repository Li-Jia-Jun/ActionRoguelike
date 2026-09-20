// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MovementMode.h"
#include "Engine/EngineTypes.h"
#include "RogueMantleDownMode.generated.h"

class UCommonLegacyMovementSettings;

/**
 * URogueMantleDownMode: a one-shot Mover mode for climbing DOWN over a ledge into a hang - the inverse of the mantle top-out.
 *
 * Entered from a grounded state (button-triggered) with an authored, motion-warped root-motion montage that carries
 * the character over the edge and down onto the wall face, ending by handing off to Climbing (not Walking). Like
 * URogueMantleMode the motion is an FLayeredMove_AnimRootMotion (OverrideAll), collision is relaxed for the traversal,
 * and orientation is managed here - but INVERTED: the capsule HOLDS upright (matching the standing entry) until the
 * montage fires the tilt-blend event, then eases into the wall lean so the hand-off to the climb mode lands on climb's
 * exact wall-tilted orientation. The ~180deg facing turn rides the clip/warp; the event gates only the tilt.
 * URogueMantleDownTransition owns entry (from the ground) and the completion hand-off to Climbing.
 */
UCLASS(MinimalAPI, Blueprintable, BlueprintType)
class URogueMantleDownMode : public UBaseMovementMode
{
	GENERATED_UCLASS_BODY()

public:
	// Registered movement-mode name for the mantle-down.
	ACTIONROGUELIKE_API static const FName ModeName;

	virtual void GenerateMove_Implementation(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, FProposedMove& OutProposedMove) const override;

	virtual void SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState) override;

	// Relax the capsule's collision for the duration of the traversal so the warped root motion drives freely over the
	// edge and down onto the face without snagging the lip (the hang spot was capsule-fit-tested by the probe on entry).
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
