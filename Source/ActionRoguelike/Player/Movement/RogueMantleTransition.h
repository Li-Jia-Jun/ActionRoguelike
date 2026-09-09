// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MovementModeTransition.h"
#include "RogueMantleTransition.generated.h"

/**
 * URogueMantleTransition: global Mover transition owning entry into the mantle mode and its handoff back out.
 *
 *  - Enter (contextual): climbing, a valid ledge+landing found by the mover component's mantle probe, a mantle
 *    montage assigned, and the player pushing UP the wall past MantleUpIntentThreshold.
 *  - Complete: mantling and the anim-root-motion layered move has ended (montage finished) -> hand off to Walking.
 *
 * Registered globally in URogueCharacterMoverComponent's constructor, BEFORE the climb transition, so at the lip
 * the mantle wins over the climb-exit-to-Falling.
 */
UCLASS(MinimalAPI)
class URogueMantleTransition : public UBaseMovementModeTransition
{
	GENERATED_BODY()

public:
	virtual FTransitionEvalResult Evaluate_Implementation(const FSimulationTickParams& Params) const override;

	// Fires when this transition is taken; kicks off the montage + layered move + warp target on entry.
	virtual void Trigger_Implementation(const FSimulationTickParams& Params) override;
};
