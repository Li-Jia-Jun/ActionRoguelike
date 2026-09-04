// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueClimbMode.h"

#include "RogueCharacterMoverComponent.h"
#include "MoverComponent.h"
#include "MoverDataModelTypes.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "MoveLibrary/AirMovementUtils.h"
#include "MoveLibrary/MovementUtils.h"
#include "MoveLibrary/MovementRecord.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RogueClimbMode)

const FName URogueClimbMode::ModeName = FName(TEXT("Climbing"));

URogueClimbMode::URogueClimbMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Reuse the common legacy settings for turning rate, verticality, etc. (climb speed/accel are our own props).
	SharedSettingsClasses.Add(UCommonLegacyMovementSettings::StaticClass());
}

void URogueClimbMode::GenerateMove_Implementation(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, FProposedMove& OutProposedMove) const
{
	// Maps input to wall-space movement. Keep vertical up. Keep surface contact.
	
	const URogueCharacterMoverComponent* MoverComp = Cast<URogueCharacterMoverComponent>(GetMoverComponent());
	const FCharacterDefaultInputs* CharacterInputs = StartState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
	const FMoverDefaultSyncState* StartingSyncState = StartState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
	check(StartingSyncState);

	const float DeltaSeconds = TimeStep.StepMs * 0.001f;
	const FVector Up = MoverComp->GetUpDirection();
	const FVector WallNormal = MoverComp->GetClimbDominantSurfaceNormal();

	// Directional move
	FVector WallMoveDir = FVector::ZeroVector;
	{
		// No surface to climb: propose a stop. SimulationTick will queue the exit to Falling.
		if (WallNormal.IsNearlyZero())
		{
			OutProposedMove.LinearVelocity = FVector::ZeroVector;
			OutProposedMove.AngularVelocityDegrees = FVector::ZeroVector;
			return;
		}

		// Wall-plane basis: WallUp = up projected onto the wall; WallRight completes it.
		FVector WallUp = (Up - Up.ProjectOnToNormal(WallNormal)).GetSafeNormal();
		if (WallUp.IsNearlyZero()) // Nearly zero means wall up is basically aligned with world up
		{
			WallUp = Up;
		}
		const FVector WallRight = FVector::CrossProduct(WallNormal, WallUp).GetSafeNormal();

		// Input intent: X = up/down the wall, Y = right/left along it.
		const FVector StickIntent = CharacterInputs ? CharacterInputs->GetMoveInput() : FVector::ZeroVector;
		WallMoveDir = (WallUp * StickIntent.X) + (WallRight * StickIntent.Y);
		if (WallMoveDir.SizeSquared() > 1.0f)
		{
			WallMoveDir = WallMoveDir.GetSafeNormal();
		}
	}
	
	// Rotational move
	FRotator FaceWallOrientation = FRotator::ZeroRotator;
	{
		// Face into the wall.
		// ApplyGravityToOrientationIntent() ensures orientation is vertical up.
		FaceWallOrientation = (-WallNormal).ToOrientationRotator();
		FaceWallOrientation = UMovementUtils::ApplyGravityToOrientationIntent(FaceWallOrientation, MoverComp->GetWorldToGravityTransform(), CommonLegacySettings->bShouldRemainVertical);
	}
	
	// Compose final move (via free move solver)
	FFreeMoveParams MoveParams;
	MoveParams.MoveInputType = EMoveInputType::DirectionalIntent;
	MoveParams.MoveInput = WallMoveDir;
	MoveParams.OrientationIntent = FaceWallOrientation;
	MoveParams.PriorVelocity = StartingSyncState->GetVelocity_WorldSpace();
	MoveParams.PriorOrientation = StartingSyncState->GetOrientation_WorldSpace();
	MoveParams.TurningRate = CommonLegacySettings->TurningRate;
	MoveParams.TurningBoost = CommonLegacySettings->TurningBoost;
	MoveParams.MaxSpeed = ClimbMaxSpeed;
	MoveParams.Acceleration = ClimbAcceleration;
	MoveParams.Deceleration = ClimbDeceleration;
	MoveParams.DeltaSeconds = DeltaSeconds;
	MoveParams.WorldToGravityQuat = MoverComp->GetWorldToGravityTransform();
	MoveParams.bUseAccelerationForVelocityMove = CommonLegacySettings->bUseAccelerationForVelocityMove;

	OutProposedMove = UAirMovementUtils::ComputeControlledFreeMove(MoveParams);

	// Step B - surge: scale the along-wall velocity by the animation's cadence curve so the body moves with the
	// anim (slow during a reach, fast during a pull). Handles the clip's two-pulse profile automatically. No-op
	// (scale == 1) until the ClimbCadence curve is authored.
	OutProposedMove.LinearVelocity *= MoverComp->GetClimbCadenceScale();

	// Additional velocity to keep wall contact (constant, NOT surged, so contact is always maintained).
	OutProposedMove.LinearVelocity += -WallNormal * ClimbIntoWallSpeed;
}

void URogueClimbMode::SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState)
{
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

	// Record direction move intent before move
	FMoverDefaultSyncState& OutputSyncState = OutputState.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
	OutputSyncState.MoveDirectionIntent = (ProposedMove.bHasDirIntent ? ProposedMove.DirectionIntent : FVector::ZeroVector);
	
	// Perform actual move
	{
		const float DeltaSeconds = Params.TimeStep.StepMs * 0.001f;
		const FRotator StartingOrient = StartingSyncState->GetOrientation_WorldSpace();
		const FRotator TargetOrient = UMovementUtils::ApplyAngularVelocityToRotator(StartingOrient, ProposedMove.AngularVelocityDegrees, DeltaSeconds);
		const bool bIsOrientationChanging = !StartingOrient.Equals(TargetOrient);
	
		FQuat TargetOrientQuat = TargetOrient.Quaternion();
		if (CommonLegacySettings->bShouldRemainVertical)
		{
			// Correct intent orient so it stays vertical up (z-up)
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
		
		// On contact with the wall, slide the remaining move along it (this is what keeps us tracking the surface).
		if (Hit.IsValidBlockingHit())
		{
			FMoverOnImpactParams ImpactParams(URogueClimbMode::ModeName, Hit, MoveDelta);
			MoverComp->HandleImpact(ImpactParams);
			UMovementUtils::TryMoveToSlideAlongSurface(FMovingComponentSet(MoverComp), MoveDelta, 1.0f - Hit.Time, TargetOrientQuat, Hit.Normal, Hit, true, MoveRecord);
		}

		CaptureFinalState(UpdatedComponent, MoveRecord, *StartingSyncState, ProposedMove.AngularVelocityDegrees, OutputSyncState);
	}
}

void URogueClimbMode::CaptureFinalState(USceneComponent* UpdatedComponent, const FMovementRecord& Record, const FMoverDefaultSyncState& StartSyncState, const FVector& AngularVelocityDegrees, FMoverDefaultSyncState& OutputSyncState) const
{
	const FVector FinalLocation = UpdatedComponent->GetComponentLocation();
	const FVector FinalVelocity = Record.GetRelevantVelocity();

	OutputSyncState.SetTransforms_WorldSpace(FinalLocation, UpdatedComponent->GetComponentRotation(), FinalVelocity, AngularVelocityDegrees, nullptr /*no movement base*/);

	UpdatedComponent->ComponentVelocity = FinalVelocity;
}

void URogueClimbMode::OnRegistered(const FName InModeName)
{
	Super::OnRegistered(InModeName);

	CommonLegacySettings = GetMoverComponent()->FindSharedSettings<UCommonLegacyMovementSettings>();
	ensureMsgf(CommonLegacySettings, TEXT("Failed to find CommonLegacyMovementSettings on %s. Climb mode may not function."), *GetPathNameSafe(this));
}

void URogueClimbMode::OnUnregistered()
{
	CommonLegacySettings = nullptr;

	Super::OnUnregistered();
}
