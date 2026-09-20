// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MovementModeTransition.h"
#include "RogueMantleDownTransition.generated.h"

/**
 * URogueMantleDownTransition: global Mover transition owning entry into the mantle-down mode and its hand-off back out.
 *
 *  - Enter (button): grounded, a valid ledge+face found by the mover component's mantle-down probe (Zelda-style, so
 *    the press always succeeds), a mantle-down montage assigned, and the player tapping the mantle-down button
 *    (FRogueTraversalInputs::bWantsToMantleDown).
 *  - Complete: mantling-down and the anim-root-motion layered move has ended (montage finished) -> hand off to Climbing.
 *
 * Registered globally in URogueCharacterMoverComponent::InitializeComponent ahead of the other traversal transitions.
 */
UCLASS(MinimalAPI)
class URogueMantleDownTransition : public UBaseMovementModeTransition
{
	GENERATED_BODY()

public:
	virtual FTransitionEvalResult Evaluate_Implementation(const FSimulationTickParams& Params) const override;

	// Fires when this transition is taken; kicks off the montage + layered move + warp targets on entry.
	virtual void Trigger_Implementation(const FSimulationTickParams& Params) override;
};
