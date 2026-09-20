// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueMantleDownMode.h"

#include "RogueCharacterMoverComponent.h"
#include "MoverComponent.h"
#include "MoverDataModelTypes.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "MoveLibrary/MovementUtils.h"
#include "MoveLibrary/MovementRecord.h"
#include "Components/PrimitiveComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RogueMantleDownMode)

const FName URogueMantleDownMode::ModeName = FName(TEXT("MantlingDown"));

URogueMantleDownMode::URogueMantleDownMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Reuse the common legacy settings for verticality / mode-name lookups (the motion itself is root-motion).
	SharedSettingsClasses.Add(UCommonLegacyMovementSettings::StaticClass());
}

void URogueMantleDownMode::GenerateMove_Implementation(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, FProposedMove& OutProposedMove) const
{
	// Motion is supplied by the OverrideAll anim-root-motion layered move, so this mode proposes nothing. While that
	// layered move is active this is skipped entirely; on the single post-montage frame the completion transition
	// (URogueMantleDownTransition) intercepts before SimulationTick, so the zero is never executed.
	OutProposedMove.LinearVelocity = FVector::ZeroVector;
	OutProposedMove.AngularVelocityDegrees = FVector::ZeroVector;
}

void URogueMantleDownMode::SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState)
{
	// Executes the (root-motion) proposed move against the world, sliding on contact so the capsule tracks the
	// edge/face instead of tunnelling. Mirrors URogueMantleMode::SimulationTick, with the orientation blend inverted.
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
	// The clip/warp turn (the ~180deg swing to face the wall) rides in the proposed angular velocity: capture its facing.
	const FVector ClipForward = TargetOrientQuat.GetForwardVector();

	// Orientation: HOLD upright (the standing entry pose) until the montage signals the tilt blend, then ease into the
	// wall lean so we land on climb's exact wall-tilted orientation. Inverse of URogueMantleMode's hold-tilt-then-upright.
	// The face normal comes from the probe's cached detected plane (NOT the live grid, which the forward sweep loses
	// mid-traversal while the character is out over the edge).
	const URogueCharacterMoverComponent* RogueMoverComp = Cast<URogueCharacterMoverComponent>(MoverComp);
	const FVector WallNormal = RogueMoverComp ? RogueMoverComp->GetMantleDownWallNormal() : FVector::ZeroVector;

	if (RogueMoverComp && RogueMoverComp->ShouldMantleDownBlendTilt() && !WallNormal.IsNearlyZero())
	{
		// After the event: ease toward the wall lean - up-axis follows the wall's up-slope, forward faces into the wall
		// (-WallNormal), matching climb's MakeFromZX(WallUp, -WallNormal) so the hand-off to Climbing doesn't pop.
		const FVector UpDir = MoverComp->GetUpDirection();
		FVector WallUp = (UpDir - UpDir.ProjectOnToNormal(WallNormal)).GetSafeNormal();
		if (WallUp.IsNearlyZero()) { WallUp = UpDir; }
		const FQuat WallTilt = FRotationMatrix::MakeFromZX(WallUp, -WallNormal).ToQuat();
		const float BlendSpeed = RogueMoverComp->GetMantleDownTiltBlendSpeed();
		TargetOrientQuat = (BlendSpeed > 0.0f)
			? FMath::QInterpTo(StartingOrient.Quaternion(), WallTilt, DeltaSeconds, BlendSpeed)
			: WallTilt;
	}
	else if (CommonLegacySettings->bShouldRemainVertical)
	{
		// Before the event: stay upright, yaw following the clip/warp turn (keep ClipForward but force vertical up).
		TargetOrientQuat = FRotationMatrix::MakeFromZX(MoverComp->GetUpDirection(), ClipForward).ToQuat();
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
		FMoverOnImpactParams ImpactParams(URogueMantleDownMode::ModeName, Hit, MoveDelta);
		MoverComp->HandleImpact(ImpactParams);
		UMovementUtils::TryMoveToSlideAlongSurface(FMovingComponentSet(MoverComp), MoveDelta, 1.0f - Hit.Time, TargetOrientQuat, Hit.Normal, Hit, true, MoveRecord);
	}

	CaptureFinalState(UpdatedComponent, MoveRecord, *StartingSyncState, ProposedMove.AngularVelocityDegrees, OutputSyncState);
}

void URogueMantleDownMode::CaptureFinalState(USceneComponent* UpdatedComponent, const FMovementRecord& Record, const FMoverDefaultSyncState& StartSyncState, const FVector& AngularVelocityDegrees, FMoverDefaultSyncState& OutputSyncState) const
{
	const FVector FinalLocation = UpdatedComponent->GetComponentLocation();
	const FVector FinalVelocity = Record.GetRelevantVelocity();

	OutputSyncState.SetTransforms_WorldSpace(FinalLocation, UpdatedComponent->GetComponentRotation(), FinalVelocity, AngularVelocityDegrees, nullptr /*no movement base*/);

	UpdatedComponent->ComponentVelocity = FinalVelocity;
}

void URogueMantleDownMode::Activate()
{
	Super::Activate();

	// Relax capsule collision so the warped root motion isn't pinned against the lip/face during the over-and-down.
	if (const UMoverComponent* MoverComp = GetMoverComponent())
	{
		if (UPrimitiveComponent* Capsule = Cast<UPrimitiveComponent>(MoverComp->GetUpdatedComponent()))
		{
			PrevCollisionEnabled = Capsule->GetCollisionEnabled();
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
}

void URogueMantleDownMode::Deactivate()
{
	// Restore the capsule collision we relaxed on Activate() (the hang spot was fit-tested, so we end in a clear spot).
	if (const UMoverComponent* MoverComp = GetMoverComponent())
	{
		if (UPrimitiveComponent* Capsule = Cast<UPrimitiveComponent>(MoverComp->GetUpdatedComponent()))
		{
			Capsule->SetCollisionEnabled(PrevCollisionEnabled);
		}
	}

	Super::Deactivate();
}

void URogueMantleDownMode::OnRegistered(const FName InModeName)
{
	Super::OnRegistered(InModeName);

	CommonLegacySettings = GetMoverComponent()->FindSharedSettings<UCommonLegacyMovementSettings>();
	ensureMsgf(CommonLegacySettings, TEXT("Failed to find CommonLegacyMovementSettings on %s. Mantle-down mode may not function."), *GetPathNameSafe(this));
}

void URogueMantleDownMode::OnUnregistered()
{
	CommonLegacySettings = nullptr;

	Super::OnUnregistered();
}
