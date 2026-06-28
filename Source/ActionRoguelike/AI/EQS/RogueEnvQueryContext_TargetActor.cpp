// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueEnvQueryContext_TargetActor.h"
#include "RogueGameTypes.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"
#include "RogueEQSTesingPawn.h"

void URogueEnvQueryContext_TargetActor::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	Super::ProvideContext(QueryInstance, ContextData);
	
	if (ARogueEQSTesingPawn* TestingPawn = Cast<ARogueEQSTesingPawn>(QueryInstance.Owner.Get()))
	{
		// For testing pawn, TargetActor is from property
		UEnvQueryItemType_Actor::SetContextHelper(ContextData, TestingPawn->TargetActor);
		return;
	}
	
	APawn* QuerierPawn = Cast<APawn>(QueryInstance.Owner.Get());
	if (ensure(QuerierPawn))
	{
		AAIController* QuerierController = Cast<AAIController>(QuerierPawn->GetController());
		check(QuerierController);
		AActor* TargetActor = Cast<AActor>(QuerierController->GetBlackboardComponent()->GetValueAsObject(NAME_TargetActor));
		
		UEnvQueryItemType_Actor::SetContextHelper(ContextData, TargetActor);
	}
}
