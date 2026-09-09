// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueClimbTransition.h"

#include "RogueCharacterMoverComponent.h"
#include "RogueClimbMode.h"
#include "MoverComponent.h"
#include "MoverSimulationTypes.h"
#include "MoverDataModelTypes.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RogueClimbTransition)

FTransitionEvalResult URogueClimbTransition::Evaluate_Implementation(const FSimulationTickParams& Params) const
{
	const URogueCharacterMoverComponent* MoverComp = Cast<URogueCharacterMoverComponent>(GetMoverComponent());
	if (!MoverComp)
	{
		return FTransitionEvalResult::NoTransition;
	}

	const FCharacterDefaultInputs* CharacterInputs = Params.StartState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
	const bool bIsClimbing = (Params.StartState.SyncState.MovementMode == URogueClimbMode::ModeName);

	if (bIsClimbing)
	{
		// Leave climbing on a jump-off, or when there's no longer a climbable surface (climbed over the top / off a side).
		const bool bJumpedOff = CharacterInputs && CharacterInputs->bIsJumpJustPressed;
		if (bJumpedOff || !MoverComp->CanClimbNow())
		{
			const UCommonLegacyMovementSettings* Settings = MoverComp->FindSharedSettings<UCommonLegacyMovementSettings>();
			return FTransitionEvalResult(Settings ? Settings->AirMovementModeName : DefaultModeNames::Falling);
		}
	}
	else if (CharacterInputs)
	{
		// Contextual entry: a climbable wall ahead, the player pushing into it, the airborne gate satisfied,
		// and not within the brief re-entry cooldown after a previous climb.
		const bool bAirborneOk = !MoverComp->RequiresAirborneToGrab() || MoverComp->IsAirborne();

		// Also guard against re-grabbing the wall during a mantle (airborne + still near a climbable surface).
		if (MoverComp->CanClimbNow() && bAirborneOk && !MoverComp->IsMantling() && !MoverComp->IsClimbReentryOnCooldown(Params.TimeStep.BaseSimTimeMs))
		{
			const FVector MoveIntentDir = CharacterInputs->GetMoveInput_WorldSpace().GetSafeNormal();
			const FVector WallNormal = MoverComp->GetClimbDominantSurfaceNormal();
			const float IntoWallDot = FVector::DotProduct(MoveIntentDir, -WallNormal);

			if (IntoWallDot >= MoverComp->GetClimbEnterIntoWallDot())
			{
				return FTransitionEvalResult(URogueClimbMode::ModeName);
			}
		}
	}

	return FTransitionEvalResult::NoTransition;
}

void URogueClimbTransition::Trigger_Implementation(const FSimulationTickParams& Params)
{
	// Start the re-entry cooldown whenever this transition is taken. The stamp on the EXIT is the one that matters
	// (it blocks a contextual re-grab for a moment after a jump-off); the stamp on entry is harmless.
	if (URogueCharacterMoverComponent* MoverComp = Cast<URogueCharacterMoverComponent>(GetMoverComponent()))
	{
		MoverComp->BeginClimbReentryCooldown(Params.TimeStep.BaseSimTimeMs);
	}
}
