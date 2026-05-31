#include "RogueInteractionComponent.h"
#include "RogueGameTypes.h"
#include "RogueInteractionInterface.h"
#include "Engine/OverlapResult.h"


TAutoConsoleVariable<bool> CVarInteractionDebugDrawing(TEXT("game.interaction.DebugDraw"), false,
	TEXT("Enable interaction component debug rendering. (0 = off, 1 = enabled)"),
	ECVF_Cheat);


URogueInteractionComponent::URogueInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void URogueInteractionComponent::Interact()
{
	if (SelectedActor != nullptr)
	{
		IRogueInteractionInterface::Execute_Interact(SelectedActor);
	}
}

void URogueInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	bool bDebugDrawEnable = CVarInteractionDebugDrawing.GetValueOnGameThread(); 
	
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	APlayerController* PC = CastChecked<APlayerController>(GetOwner());

	FVector SelfLocation = PC->GetPawn()->GetActorLocation();
	
	FVector CameraLocation = PC->PlayerCameraManager->GetCameraLocation();

	ECollisionChannel CollisionChannel = INTERACTION_CHANNEL;

	FCollisionShape Shape;
	Shape.SetSphere(InteractionRadius);

	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByChannel(Overlaps, SelfLocation, FQuat::Identity, CollisionChannel, Shape);

	AActor* BestActor = nullptr;
	float BestTotalWeights = -1.0;

	for (FOverlapResult& Overlap : Overlaps)
	{
		AActor* OverlapActor = Overlap.GetActor();
		
		FVector OverlapBoundOrigin;
		FVector OverlapBoundBoxExtent;
		OverlapActor->GetActorBounds(true, OverlapBoundOrigin, OverlapBoundBoxExtent);

		FVector OverlapLocation = OverlapActor->GetActorLocation();

		FVector OverlapDirection = (OverlapLocation - CameraLocation).GetSafeNormal();

		FVector CameraDirection = PC->GetControlRotation().Vector().GetSafeNormal();

		// Look weight
		float DotResult = FVector::DotProduct(OverlapDirection, CameraDirection);
		float DotResultWeight = (DotResult * 0.5f + 0.5f) * InteractionPickActorLookWeight;
		
		// Distance weight
		float Distance = (OverlapLocation - SelfLocation).Size();
		float DistanceWeight = (1.0f - (Distance / InteractionRadius)) * InteractionPickActorDistanceWeight; 

		float TotalWeights = DotResultWeight + DistanceWeight;
		if (TotalWeights > BestTotalWeights)
		{
			BestActor = OverlapActor;
			BestTotalWeights = TotalWeights;
		}
		
		if (bDebugDrawEnable)
		{
			DrawDebugBox(GetWorld(), OverlapBoundOrigin, OverlapBoundBoxExtent, FColor::Red);
			FString DebugString = FString::Printf(
				TEXT("LookWeight: %03f, DistanceWeight: %03f, TotalWeights: %03f"), 
				DotResultWeight, DistanceWeight, TotalWeights);
			DrawDebugString(GetWorld(), OverlapLocation, DebugString, nullptr, FColor::Red, 0.0f, false);
		}
	}
	
	SelectedActor = BestActor;

	if (bDebugDrawEnable)
	{
		if (BestActor)
		{
			DrawDebugBox(GetWorld(), BestActor->GetActorLocation(), FVector(60.0f), FColor::Green, false, 0.0f, 0, 2.0f);
		}
		DrawDebugSphere(GetWorld(), SelfLocation, InteractionRadius, 32, FColor::White);
	}
}


