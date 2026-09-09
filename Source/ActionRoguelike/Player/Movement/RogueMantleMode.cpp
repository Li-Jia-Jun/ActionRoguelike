// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueMantleMode.h"

#include "MoverComponent.h"
#include "MoverDataModelTypes.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "MoveLibrary/MovementUtils.h"
#include "MoveLibrary/MovementRecord.h"
#include "Components/PrimitiveComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RogueMantleMode)

const FName URogueMantleMode::ModeName = FName(TEXT("Mantling"));

URogueMantleMode::URogueMantleMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Reuse the common legacy settings for verticality / mode-name lookups (the mantle motion itself is root-motion).
	SharedSettingsClasses.Add(UCommonLegacyMovementSettings::StaticClass());
}

void URogueMantleMode::GenerateMove_Implementation(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, FProposedMove& OutProposedMove) const
{
	// The mantle's motion is supplied by the OverrideAll anim-root-motion layered move, so this mode proposes
	// nothing. While that layered move is active this is skipped entirely; on the single post-montage frame the
	// completion transition (URogueMantleTransition) intercepts before SimulationTick, so the zero is never executed.
	OutProposedMove.LinearVelocity = FVector::ZeroVector;
	OutProposedMove.AngularVelocityDegrees = FVector::ZeroVector;
}

void URogueMantleMode::SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState)
{
	// Executes the (root-motion) proposed move against the world, sliding on contact so the capsule tracks the
	// lip/landing instead of tunnelling. Mirrors URogueClimbMode::SimulationTick.
	UMoverComponent* MoverComp = GetMoverComponent();
	const FMoverTickStartData& StartState = Params.StartState;
	USceneComponent* UpdatedComponent = Params.MovingComps.UpdatedComponent.Get();
	const FProposedMove& ProposedMove = Params.ProposedMove;

	if (!UpdatedComponent)
	{
		return;
	}

	const FMoverDefaultSyncState* StartingSyncState = StartState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
	check(StartingSyncState);

	FMoverDefaultSyncState& OutputSyncState = OutputState.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
	OutputSyncState.MoveDirectionIntent = (ProposedMove.bHasDirIntent ? ProposedMove.DirectionIntent : FVector::ZeroVector);

	const float DeltaSeconds = Params.TimeStep.StepMs * 0.001f;
	const FRotator StartingOrient = StartingSyncState->GetOrientation_WorldSpace();
	const FRotator TargetOrient = UMovementUtils::ApplyAngularVelocityToRotator(StartingOrient, ProposedMove.AngularVelocityDegrees, DeltaSeconds);
	const bool bIsOrientationChanging = !StartingOrient.Equals(TargetOrient);

	FQuat TargetOrientQuat = TargetOrient.Quaternion();
	if (CommonLegacySettings->bShouldRemainVertical)
	{
		TargetOrientQuat = FRotationMatrix::MakeFromZX(MoverComp->GetUpDirection(), TargetOrientQuat.GetForwardVector()).ToQuat();
	}

	FHitResult Hit(1.0f);
	FMovementRecord MoveRecord;
	MoveRecord.SetDeltaSeconds(DeltaSeconds);
	FVector MoveDelta = ProposedMove.LinearVelocity * DeltaSeconds;
	if (!MoveDelta.IsNearlyZero() || bIsOrientationChanging)
	{
		UMovementUtils::TrySafeMoveUpdatedComponent(Params.MovingComps, MoveDelta, TargetOrientQuat, true, Hit, ETeleportType::None, MoveRecord);
	}

	if (Hit.IsValidBlockingHit())
	{
		FMoverOnImpactParams ImpactParams(URogueMantleMode::ModeName, Hit, MoveDelta);
		MoverComp->HandleImpact(ImpactParams);
		UMovementUtils::TryMoveToSlideAlongSurface(FMovingComponentSet(MoverComp), MoveDelta, 1.0f - Hit.Time, TargetOrientQuat, Hit.Normal, Hit, true, MoveRecord);
	}

	CaptureFinalState(UpdatedComponent, MoveRecord, *StartingSyncState, ProposedMove.AngularVelocityDegrees, OutputSyncState);
}

void URogueMantleMode::CaptureFinalState(USceneComponent* UpdatedComponent, const FMovementRecord& Record, const FMoverDefaultSyncState& StartSyncState, const FVector& AngularVelocityDegrees, FMoverDefaultSyncState& OutputSyncState) const
{
	const FVector FinalLocation = UpdatedComponent->GetComponentLocation();
	const FVector FinalVelocity = Record.GetRelevantVelocity();

	OutputSyncState.SetTransforms_WorldSpace(FinalLocation, UpdatedComponent->GetComponentRotation(), FinalVelocity, AngularVelocityDegrees, nullptr /*no movement base*/);

	UpdatedComponent->ComponentVelocity = FinalVelocity;
}

void URogueMantleMode::Activate()
{
	Super::Activate();

	// Relax capsule collision so the warped root motion isn't pinned against the wall/lip during the up-and-over.
	if (const UMoverComponent* MoverComp = GetMoverComponent())
	{
		if (UPrimitiveComponent* Capsule = Cast<UPrimitiveComponent>(MoverComp->GetUpdatedComponent()))
		{
			PrevCollisionEnabled = Capsule->GetCollisionEnabled();
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
}

void URogueMantleMode::Deactivate()
{
	// Restore the capsule collision we relaxed on Activate() (the landing was fit-tested, so we end in a clear spot).
	if (const UMoverComponent* MoverComp = GetMoverComponent())
	{
		if (UPrimitiveComponent* Capsule = Cast<UPrimitiveComponent>(MoverComp->GetUpdatedComponent()))
		{
			Capsule->SetCollisionEnabled(PrevCollisionEnabled);
		}
	}

	Super::Deactivate();
}

void URogueMantleMode::OnRegistered(const FName InModeName)
{
	Super::OnRegistered(InModeName);

	CommonLegacySettings = GetMoverComponent()->FindSharedSettings<UCommonLegacyMovementSettings>();
	ensureMsgf(CommonLegacySettings, TEXT("Failed to find CommonLegacyMovementSettings on %s. Mantle mode may not function."), *GetPathNameSafe(this));
}

void URogueMantleMode::OnUnregistered()
{
	CommonLegacySettings = nullptr;

	Super::OnUnregistered();
}
