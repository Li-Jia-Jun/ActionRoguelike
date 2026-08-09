// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueAICharacter.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "ActionSystem/RogueActionSystemComponent.h"
#include "UI/RogueAttributeBarWidgetComponent.h"
#include "RogueSharedGameplayTags.h"
#include "Core/RogueGameInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Significance/RogueSignificanceManager.h"
#include "Significance/RogueSignificanceDeveloperSettings.h"


ARogueAICharacter::ARogueAICharacter()
{
	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	
	ActionSystemComp = CreateDefaultSubobject<URogueActionSystemComponent>("ActionSystemComp");
	
	HealthAttributeBarWidgetComp = CreateDefaultSubobject<URogueAttributeBarWidgetComponent>("HealthBarWidgetComp");
	HealthAttributeBarWidgetComp->SetupAttachment(RootComponent);
	HealthAttributeBarWidgetComp->SetWidgetSpace(EWidgetSpace::Screen);
	HealthAttributeBarWidgetComp->SetDrawSize(FVector2D(100.0f, 15.0f));

	// Drive animation Update Rate Optimization from significance instead of the default screen-size
	// heuristic (which never throttles in a top-down crowd where every agent is large on screen).
	// The rate is chosen from the mesh's LOD, which SignificanceLODChanged forces per tier.
	USkeletalMeshComponent* MeshComp = GetMesh();
	MeshComp->bEnableUpdateRateOptimizations = true;
	MeshComp->bSkipKinematicUpdateWhenInterpolating = true;
	MeshComp->bSkipBoundsUpdateWhenInterpolating = true;
	MeshComp->OnAnimUpdateRateParamsCreated.BindUObject(this, &ThisClass::HandleAnimUpdateRateParamsCreated);
}

void ARogueAICharacter::HandleAnimUpdateRateParamsCreated(FAnimUpdateRateParameters* Params)
{
	// Pick the URO rate from the (significance-forced) mesh LOD rather than screen size.
	// Values are frame-skips: 0 = evaluate every frame, 2 = every 3rd, 5 = every 6th.
	Params->bShouldUseLodMap = true;
	Params->LODToFrameSkipMap.Add(0, 0);
	Params->LODToFrameSkipMap.Add(1, 2);
	Params->LODToFrameSkipMap.Add(2, 5);
}

void ARogueAICharacter::BeginPlay()
{
	Super::BeginPlay();
	
	ActionSystemComp->GameplayEventReceivedDelegate.AddDynamic(this, &ThisClass::OnHit);
	
	// Setup overlay effect
	HitOverlayMID = UMaterialInstanceDynamic::Create(GetMesh()->GetOverlayMaterial(), this);
	GetMesh()->SetOverlayMaterial(HitOverlayMID);
	GetMesh()->SetOverlayMaterialMaxDrawDistance(1);
	
	GetGameInstance<URogueGameInstance>()->AliveAIEnemies.Add(this);

	RegisterWithSignificanceManager();
}

void ARogueAICharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterFromSignificanceManager();

	Super::EndPlay(EndPlayReason);

	GetGameInstance<URogueGameInstance>()->AliveAIEnemies.RemoveSingleSwap(this);
}

void ARogueAICharacter::RegisterWithSignificanceManager()
{
	// A None tag means this class never participates.
	if (SignificanceTag.IsNone())
	{
		return;
	}

	// Opt-in is driven by the developer settings: only register if this Tag is configured there.
	const URogueSignificanceDeveloperSettings* Settings = GetDefault<URogueSignificanceDeveloperSettings>();
	if (Settings->FindBucketInfo(SignificanceTag) == nullptr)
	{
		return;
	}

	URogueSignificanceManager* SignificanceManager = USignificanceManager::Get<URogueSignificanceManager>(GetWorld());
	if (SignificanceManager == nullptr)
	{
		return;
	}
	
	SignificanceManager->RegisterObject(this, SignificanceTag,
		[](USignificanceManager::FManagedObjectInfo* ObjectInfo, const FTransform& Viewpoint) -> float
		{
			// Negative value for significance
			const AActor* Actor = CastChecked<AActor>(ObjectInfo->GetObject());
			float NegativeDistSquared = -1.0f * FVector::DistSquared(Actor->GetActorLocation(), Viewpoint.GetLocation());
			
			if (Actor->WasRecentlyRendered())
			{
				NegativeDistSquared *= 0.5;
			}
			
			return NegativeDistSquared;
		});

	bRegisteredWithSignificance = true;
}

void ARogueAICharacter::UnregisterFromSignificanceManager()
{
	if (!bRegisteredWithSignificance)
	{
		return;
	}

	if (URogueSignificanceManager* SignificanceManager = USignificanceManager::Get<URogueSignificanceManager>(GetWorld()))
	{
		SignificanceManager->UnregisterObject(this);
	}
	bRegisteredWithSignificance = false;
}

void ARogueAICharacter::SignificanceLODChanged(int32 NewLOD)
{
	SignificanceLOD = NewLOD;
	
	EKinematicBonesUpdateToPhysics::Type KBonesUpdateType = EKinematicBonesUpdateToPhysics::SkipSimulatingBones;
	bool MovementAviodanceEnabled = true;
	EMovementMode MovementMode = MOVE_Walking;
	float MovementTickInterval = 0.0f;
	float BehaviorTreeTickInterval = 0.0f;
	float ActorTickInterval = 0.0f;
	switch (NewLOD)
	{
	case 0:
		KBonesUpdateType = EKinematicBonesUpdateToPhysics::SkipSimulatingBones;
		MovementAviodanceEnabled = true;
		MovementMode = MOVE_Walking;
		MovementTickInterval = 0.0f;
		BehaviorTreeTickInterval = 0.0f;
		ActorTickInterval = 0.0f;
		break;
	case 1:
		KBonesUpdateType = EKinematicBonesUpdateToPhysics::SkipAllBones;
		MovementAviodanceEnabled = true;
		MovementMode = MOVE_NavWalking;
		MovementTickInterval = 0.08f;
		BehaviorTreeTickInterval = 0.25f;
		ActorTickInterval = 0.15f;
		break;
	default:
		KBonesUpdateType = EKinematicBonesUpdateToPhysics::SkipAllBones;
		MovementAviodanceEnabled = false; // Turn off avoidance (AIs will still push each other away by collision)
		MovementMode = MOVE_NavWalking;	  // Skip regular walk's physics checking
		MovementTickInterval = 0.15f;	  
		BehaviorTreeTickInterval = 0.5f;
		ActorTickInterval = 0.3f;
		break;
	}
	
	GetMesh()->KinematicBonesUpdateType = KBonesUpdateType;

	// Force the render LOD to the significance tier. This also feeds URO's LOD-map rate selection
	// (SetForcedLOD is 1-based: 0 = auto, 1 = LOD 0, ...), so animation throttles by significance.
	GetMesh()->SetForcedLOD(NewLOD + 1);

	GetCharacterMovement()->SetAvoidanceEnabled(MovementAviodanceEnabled);
	GetCharacterMovement()->SetGroundMovementMode(MovementMode);
	GetCharacterMovement()->SetComponentTickInterval(MovementTickInterval);
	
	if (AAIController* AIController = Cast<AAIController>(GetController()))
	{
		if (UBehaviorTreeComponent* BehaviorTreeComp = Cast<UBehaviorTreeComponent>(AIController->BrainComponent))
		{
			BehaviorTreeComp->SetComponentTickInterval(BehaviorTreeTickInterval);
		}
		
		AIController->SetActorTickInterval(ActorTickInterval);
	}

}

void ARogueAICharacter::OnHit(FGameplayTag EventTag, FRogueGameplayEventData Payload)
{
	if (!EventTag.MatchesTag(RogueSharedGameplayTags::Event_OnHit))
	{
		return;
	}
	
	GetMesh()->SetOverlayMaterialMaxDrawDistance(0);
	HitOverlayMID->SetScalarParameterValue(FName("HitTime"), GetWorld()->TimeSeconds);

	GetWorldTimerManager().SetTimer(OverlayTimerHandle, [this]()
	{
		GetMesh()->SetOverlayMaterialMaxDrawDistance(1);
	}, 1.0f, false);
}


