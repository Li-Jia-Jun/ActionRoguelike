// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MovementModeTransition.h"
#include "RogueClimbTransition.generated.h"

/**
 * URogueClimbTransition: global Mover transition owning entry into and exit from the climb mode.
 *
 *  - Enter (contextual): not climbing, a climbable wall ahead (CanClimbNow), the player pushing into it
 *    (move intent vs wall normal >= ClimbEnterIntoWallDot), the airborne gate satisfied, and not on re-entry cooldown.
 *  - Exit: climbing and either the player jumps off or the surface is lost.
 *
 * Registered globally in URogueCharacterMoverComponent's constructor.
 */
UCLASS(MinimalAPI)
class URogueClimbTransition : public UBaseMovementModeTransition
{
	GENERATED_BODY()

public:
	virtual FTransitionEvalResult Evaluate_Implementation(const FSimulationTickParams& Params) const override;

	// Fires when this transition is taken; used to start the re-entry cooldown when leaving the climb.
	virtual void Trigger_Implementation(const FSimulationTickParams& Params) override;
};
