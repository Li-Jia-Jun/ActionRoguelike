// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueCornerTurnMode.h"

#include "RogueCharacterMoverComponent.h"
#include "RogueSharedGameplayTags.h"
#include "RogueClimbMode.h"
#include "MoverComponent.h"
#include "MoverDataModelTypes.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "MoveLibrary/MovementUtils.h"
#include "MoveLibrary/MovementRecord.h"
#include "Components/PrimitiveComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RogueCornerTurnMode)

const FName URogueCornerTurnMode::ModeName = FName(TEXT("CornerTurning"));

URogueCornerTurnMode::URogueCornerTurnMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SharedSettingsClasses.Add(UCommonLegacyMovementSettings::StaticClass());

	// Mode tag (child of Mover.Climb - see URogueCharacterMoverComponent::IsInClimbState / IsCornerTurning).
	GameplayTags.AddTag(RogueSharedGameplayTags::Mover_Climb_CornerTurn);
}

void URogueCornerTurnMode::GenerateMove_Implementation(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, FProposedMove& OutProposedMove) const
{
	// The motion is a pure function of the playhead, advanced (and committed) once per tick in SimulationTick - proposing
	// it here too would mean advancing the playhead twice. So propose nothing; SimulationTick moves to the evaluated pose.
	OutProposedMove.LinearVelocity = FVector::ZeroVector;
	OutProposedMove.AngularVelocityDegrees = FVector::ZeroVector;
}

void URogueCornerTurnMode::SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState)
{
	URogueCharacterMoverComponent* MoverComp = Cast<URogueCharacterMoverComponent>(GetMoverComponent());
	USceneComponent* UpdatedComponent = Params.MovingComps.UpdatedComponent.Get();
	if (!MoverComp || !UpdatedComponent)
	{
		return;
	}

	const FMoverDefaultSyncState* StartingSyncState = Params.StartState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
	check(StartingSyncState);
	FMoverDefaultSyncState& OutputSyncState = OutputState.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
	OutputSyncState.MoveDirectionIntent = FVector::ZeroVector;

	const float DeltaSeconds = Params.TimeStep.StepMs * 0.001f;

	// Raw stick while turning (ProduceInput keeps sending it, as in climb): Y = right(+)/left(-) relative to the character,
	// which stays meaningful all the way round the corner - holding the same direction keeps going.
	const FCharacterDefaultInputs* CharacterInputs = Params.StartState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
	const float StickY = CharacterInputs ? CharacterInputs->GetMoveInput().Y : 0.0f;

	FTransform Target;
	const ERogueCornerTurnStep Step = MoverComp->AdvanceCornerTurn(StickY, DeltaSeconds, Target);

	// Move straight to the evaluated pose (collision is relaxed for the turn; see Activate).
	const FVector StartLocation = StartingSyncState->GetLocation_WorldSpace();
	const FVector MoveDelta = Target.GetLocation() - StartLocation;
	const FQuat TargetQuat = Target.GetRotation();

	FHitResult Hit(1.0f);
	FMovementRecord MoveRecord;
	MoveRecord.SetDeltaSeconds(DeltaSeconds);
	if (!MoveDelta.IsNearlyZero() || !StartingSyncState->GetOrientation_WorldSpace().Quaternion().Equals(TargetQuat, KINDA_SMALL_NUMBER))
	{
		UMovementUtils::TrySafeMoveUpdatedComponent(Params.MovingComps, MoveDelta, TargetQuat, true, Hit, ETeleportType::None, MoveRecord);
	}

	const FVector FinalLocation = UpdatedComponent->GetComponentLocation();
	const FVector FinalVelocity = MoveRecord.GetRelevantVelocity();
	OutputSyncState.SetTransforms_WorldSpace(FinalLocation, UpdatedComponent->GetComponentRotation(), FinalVelocity, FVector::ZeroVector, nullptr /*no movement base*/);
	UpdatedComponent->ComponentVelocity = FinalVelocity;

	// The playhead reached a terminal rest: back to Climbing on that face, with the climb plane seeded to it.
	if (Step != ERogueCornerTurnStep::InProgress)
	{
		MoverComp->FinishCornerTurn(Step == ERogueCornerTurnStep::FinishedOnNewFace);
		OutputState.MovementEndState.NextModeName = URogueClimbMode::ModeName;
		OutputState.MovementEndState.RemainingMs = 0.0f;
	}
}

void URogueCornerTurnMode::Activate()
{
	Super::Activate();

	if (const UMoverComponent* MoverComp = GetMoverComponent())
	{
		if (UPrimitiveComponent* Capsule = Cast<UPrimitiveComponent>(MoverComp->GetUpdatedComponent()))
		{
			PrevCollisionEnabled = Capsule->GetCollisionEnabled();
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
}

void URogueCornerTurnMode::Deactivate()
{
	if (const UMoverComponent* MoverComp = GetMoverComponent())
	{
		if (UPrimitiveComponent* Capsule = Cast<UPrimitiveComponent>(MoverComp->GetUpdatedComponent()))
		{
			Capsule->SetCollisionEnabled(PrevCollisionEnabled);
		}
	}

	Super::Deactivate();
}

void URogueCornerTurnMode::OnRegistered(const FName InModeName)
{
	Super::OnRegistered(InModeName);

	CommonLegacySettings = GetMoverComponent()->FindSharedSettings<UCommonLegacyMovementSettings>();
	ensureMsgf(CommonLegacySettings, TEXT("Failed to find CommonLegacyMovementSettings on %s. Corner-turn mode may not function."), *GetPathNameSafe(this));
}

void URogueCornerTurnMode::OnUnregistered()
{
	CommonLegacySettings = nullptr;

	Super::OnUnregistered();
}
