// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueMantleDownTransition.h"

#include "RogueCharacterMoverComponent.h"
#include "RogueClimbMode.h"
#include "RogueMantleDownMode.h"
#include "RogueTraversalInputs.h"
#include "MoverComponent.h"
#include "MoverSimulationTypes.h"
#include "MoverDataModelTypes.h"
#include "DefaultMovementSet/LayeredMoves/AnimRootMotionLayeredMove.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RogueMantleDownTransition)

FTransitionEvalResult URogueMantleDownTransition::Evaluate_Implementation(const FSimulationTickParams& Params) const
{
	const URogueCharacterMoverComponent* MoverComp = Cast<URogueCharacterMoverComponent>(GetMoverComponent());
	if (!MoverComp)
	{
		return FTransitionEvalResult::NoTransition;
	}

	const FName StartMode = Params.StartState.SyncState.MovementMode;

	if (StartMode == URogueMantleDownMode::ModeName)
	{
		// Completion: the anim-root-motion layered move has finished (montage ended / never queued) -> hand to Climbing.
		// Unconditional by design: the entry probe already guaranteed a climbable face (the always-succeed contract), so
		// we commit to climbing. If the grid momentarily can't see the face on this exact frame, the climb mode's own
		// exit-to-Falling self-heals a frame later rather than us pre-emptively dropping the hang.
		if (!Params.StartState.SyncState.LayeredMoves.HasMove<FLayeredMove_AnimRootMotion>())
		{
			return FTransitionEvalResult(URogueClimbMode::ModeName);
		}
	}
	else if (MoverComp->IsOnGround())
	{
		// Contextual entry from a grounded state: a valid ledge+face in reach, a montage assigned, and the button tapped.
		// IsOnGround() rules out climbing / falling (only Walking-like modes qualify); bMantleDownAvailable is only ever
		// set while grounded, so it double-guards. The facing tolerance is baked into the probe's availability check.
		if (MoverComp->IsMantleDownAvailable() && MoverComp->GetMantleDownMontage())
		{
			const FRogueTraversalInputs* TraversalInputs = Params.StartState.InputCmd.InputCollection.FindDataByType<FRogueTraversalInputs>();
			if (TraversalInputs && TraversalInputs->bWantsToMantleDown)
			{
				return FTransitionEvalResult(URogueMantleDownMode::ModeName);
			}
		}
	}

	return FTransitionEvalResult::NoTransition;
}

void URogueMantleDownTransition::Trigger_Implementation(const FSimulationTickParams& Params)
{
	URogueCharacterMoverComponent* MoverComp = Cast<URogueCharacterMoverComponent>(GetMoverComponent());
	if (!MoverComp)
	{
		return;
	}

	// The active mode has not advanced yet at Trigger time, so the start mode distinguishes entry from completion.
	// Only kick off the montage + layered move + warp targets when ENTERING the mantle-down (from a grounded state).
	if (Params.StartState.SyncState.MovementMode != URogueMantleDownMode::ModeName)
	{
		MoverComp->BeginMantleDown();
	}
}
