// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MovementMode.h"
#include "Engine/EngineTypes.h"
#include "RogueCornerTurnMode.generated.h"

class UCommonLegacyMovementSettings;

/**
 * URogueCornerTurnMode: an INPUT-DRIVEN corner traversal (inner or outer) between two climb faces.
 *
 * Unlike the mantles (fire-and-forget montage + anim-root-motion layered move), the stick drives the corner clip's
 * PLAYHEAD and the capsule follows the clip's own root path at that playhead, warped onto the probe's Mid/End targets by
 * a deterministic, time-parameterized skew (URogueCharacterMoverComponent::AdvanceCornerTurn). Body and pose are a
 * function of the same playhead, so the hands stay phase-locked at any push - including stopped or reversing. Released
 * mid-strike, it settles to the nearest rest pose (clip start / the 4-limb corner rest / clip end).
 *
 * A distinct mode (not a climb sub-state) because mid-turn the climb grid straddles two faces and would fail -> the
 * climb transition would drop to Falling. Completes back into Climbing on either face, seeding the climb plane.
 * URogueCornerTurnTransition owns entry; completion is decided here (the playhead reaching a terminal rest).
 */
UCLASS(MinimalAPI, Blueprintable, BlueprintType)
class URogueCornerTurnMode : public UBaseMovementMode
{
	GENERATED_UCLASS_BODY()

public:
	// Registered movement-mode name for the corner turn.
	ACTIONROGUELIKE_API static const FName ModeName;

	virtual void GenerateMove_Implementation(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, FProposedMove& OutProposedMove) const override;

	virtual void SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState) override;

	// Relax capsule collision for the turn: the path wraps a corner (outer) or tucks into it (inner), and both the Mid
	// and End poses were fit-tested by the probe, so the clip path is trusted rather than slid against the faces.
	virtual void Activate() override;
	virtual void Deactivate() override;

protected:
	virtual void OnRegistered(const FName ModeName) override;
	virtual void OnUnregistered() override;

	TObjectPtr<const UCommonLegacyMovementSettings> CommonLegacySettings;

	// Capsule collision setting captured on Activate() and restored on Deactivate().
	TEnumAsByte<ECollisionEnabled::Type> PrevCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
};
