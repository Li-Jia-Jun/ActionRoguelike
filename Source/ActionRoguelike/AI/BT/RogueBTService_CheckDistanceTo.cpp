// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueBTService_CheckDistanceTo.h"
#include "RogueGameTypes.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"


URogueBTService_CheckDistanceTo::URogueBTService_CheckDistanceTo()
{
	TargetActorKey.SelectedKeyName = NAME_TargetActor;
}

void URogueBTService_CheckDistanceTo::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);
	
	UBlackboardComponent* BBComp = OwnerComp.GetBlackboardComponent();
	check(BBComp);
	
	if (AActor* TargetActor = Cast<AActor>(BBComp->GetValueAsObject(TargetActorKey.SelectedKeyName)))
	{
		AAIController* ThisController = OwnerComp.GetAIOwner();
		APawn* ThisActor = ThisController->GetPawn();
		check(ThisActor);
		
		float Distance = FVector::Dist(ThisActor->GetActorLocation(), TargetActor->GetActorLocation());
		
		bool bHasLOS = ThisController->LineOfSightTo(TargetActor);
		
		BBComp->SetValueAsBool(WithinRangeKey.SelectedKeyName, (Distance < DistanceRange) && bHasLOS);
	}
}