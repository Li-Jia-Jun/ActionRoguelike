// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueMantleTransition.h"

#include "RogueCharacterMoverComponent.h"
#include "RogueClimbMode.h"
#include "RogueMantleMode.h"
#include "MoverComponent.h"
#include "MoverSimulationTypes.h"
#include "MoverDataModelTypes.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "DefaultMovementSet/LayeredMoves/AnimRootMotionLayeredMove.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RogueMantleTransition)

FTransitionEvalResult URogueMantleTransition::Evaluate_Implementation(const FSimulationTickParams& Params) const
{
	const URogueCharacterMoverComponent* MoverComp = Cast<URogueCharacterMoverComponent>(GetMoverComponent());
	if (!MoverComp)
	{
		return FTransitionEvalResult::NoTransition;
	}

	const FName StartMode = Params.StartState.SyncState.MovementMode;

	if (StartMode == URogueMantleMode::ModeName)
	{
		// Completion: the anim-root-motion layered move has finished (montage ended / never queued) -> stand on top.
		if (!Params.StartState.SyncState.LayeredMoves.HasMove<FLayeredMove_AnimRootMotion>())
		{
			const UCommonLegacyMovementSettings* Settings = MoverComp->FindSharedSettings<UCommonLegacyMovementSettings>();
			return FTransitionEvalResult(Settings ? Settings->GroundMovementModeName : DefaultModeNames::Walking);
		}
	}
	else if (StartMode == URogueClimbMode::ModeName)
	{
		// Contextual entry: a valid ledge is in reach, a montage is assigned, and the player is pushing up the wall.
		// While climbing, ProduceInput sends RAW stick where X = up/down the wall, so GetMoveInput().X is the up-intent.
		const FCharacterDefaultInputs* CharacterInputs = Params.StartState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
		const float MoveX = CharacterInputs ? CharacterInputs->GetMoveInput().X : 0.0f;

		if (CharacterInputs
			&& MoverComp->IsMantleLedgeAvailable()
			&& MoverComp->GetMantleMontage()
			&& MoveX >= MoverComp->GetMantleUpIntentThreshold())
		{
			return FTransitionEvalResult(URogueMantleMode::ModeName);
		}
	}

	return FTransitionEvalResult::NoTransition;
}

void URogueMantleTransition::Trigger_Implementation(const FSimulationTickParams& Params)
{
	URogueCharacterMoverComponent* MoverComp = Cast<URogueCharacterMoverComponent>(GetMoverComponent());
	if (!MoverComp)
	{
		return;
	}

	// The active mode has not advanced yet at Trigger time, so the start mode distinguishes entry from completion.
	// Only kick off the montage + layered move + warp targets when ENTERING the mantle (from climbing).
	if (Params.StartState.SyncState.MovementMode == URogueClimbMode::ModeName)
	{
		MoverComp->BeginMantle();
	}
}
