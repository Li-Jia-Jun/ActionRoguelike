// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueCornerTurnTransition.h"

#include "RogueCharacterMoverComponent.h"
#include "RogueClimbMode.h"
#include "RogueCornerTurnMode.h"
#include "MoverComponent.h"
#include "MoverSimulationTypes.h"
#include "MoverDataModelTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RogueCornerTurnTransition)

namespace
{
	// Which side (if any) the stick commits a corner turn toward. While climbing ProduceInput sends RAW stick: X = up/down
	// the wall, Y = right/left along it. Sideways must be past the threshold AND dominant, so an up-diagonal at a top
	// corner reads as a mantle, not a turn.
	bool GetCornerTurnSide(const URogueCharacterMoverComponent& MoverComp, const FSimulationTickParams& Params, bool& bOutRight)
	{
		const FCharacterDefaultInputs* CharacterInputs = Params.StartState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
		if (!CharacterInputs)
		{
			return false;
		}
		const FVector Stick = CharacterInputs->GetMoveInput();
		if (FMath::Abs(Stick.Y) < MoverComp.GetCornerTurnIntentThreshold() || FMath::Abs(Stick.Y) <= FMath::Abs(Stick.X))
		{
			return false;
		}
		bOutRight = Stick.Y > 0.0f;
		return true;
	}
}

FTransitionEvalResult URogueCornerTurnTransition::Evaluate_Implementation(const FSimulationTickParams& Params) const
{
	const URogueCharacterMoverComponent* MoverComp = Cast<URogueCharacterMoverComponent>(GetMoverComponent());
	if (!MoverComp || Params.StartState.SyncState.MovementMode != URogueClimbMode::ModeName)
	{
		return FTransitionEvalResult::NoTransition;
	}

	bool bRight = false;
	if (GetCornerTurnSide(*MoverComp, Params, bRight) && MoverComp->CanBeginCornerTurn(bRight))
	{
		return FTransitionEvalResult(URogueCornerTurnMode::ModeName);
	}

	return FTransitionEvalResult::NoTransition;
}

void URogueCornerTurnTransition::Trigger_Implementation(const FSimulationTickParams& Params)
{
	URogueCharacterMoverComponent* MoverComp = Cast<URogueCharacterMoverComponent>(GetMoverComponent());
	if (!MoverComp || Params.StartState.SyncState.MovementMode != URogueClimbMode::ModeName)
	{
		return;
	}

	// Same input as Evaluate this tick, so the side resolves identically.
	bool bRight = false;
	if (GetCornerTurnSide(*MoverComp, Params, bRight))
	{
		MoverComp->BeginCornerTurn(bRight);
	}
}
