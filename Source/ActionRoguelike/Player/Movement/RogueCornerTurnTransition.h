// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "MovementModeTransition.h"
#include "RogueCornerTurnTransition.generated.h"

/**
 * URogueCornerTurnTransition: global Mover transition owning ENTRY into the corner turn.
 *
 *  - Enter (contextual): climbing, the stick pushed sideways (|Y| >= CornerTurnIntentThreshold and dominant over X) toward
 *    a side whose corner probe is available and has a clip assigned -> BeginCornerTurn on Trigger.
 *  - Completion is decided by URogueCornerTurnMode itself (the playhead reaching a terminal rest), not here.
 *
 * Registered after the mantle transition (a diagonal push at a top corner prefers the mantle) and before the climb
 * transition.
 */
UCLASS(MinimalAPI)
class URogueCornerTurnTransition : public UBaseMovementModeTransition
{
	GENERATED_BODY()

public:
	virtual FTransitionEvalResult Evaluate_Implementation(const FSimulationTickParams& Params) const override;

	virtual void Trigger_Implementation(const FSimulationTickParams& Params) override;
};
