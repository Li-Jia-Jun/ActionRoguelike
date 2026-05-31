// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueBTTask_ApplyGameplayEffect.h"
#include "AIController.h"
#include "ActionSystem/RogueActionSystemComponent.h"
#include "ActionSystem/GameplayEffect/RogueGameplayEffectDurationPolicy.h"
#include "ActionSystem/GameplayEffect/RogueGameplayEffect.h"
#include "ActionSystem/GameplayEffect/URogueGameplayEffectInstance.h"
#include "GameFramework/Character.h"


EBTNodeResult::Type URogueBTTask_ApplyGameplayEffect::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	MyOwnerComp = &OwnerComp;
	
	ACharacter* ThisCharacter = Cast<ACharacter>(OwnerComp.GetAIOwner()->GetPawn());
	check(ThisCharacter);
	
	URogueActionSystemComponent* ThisASC = ThisCharacter->GetComponentByClass<URogueActionSystemComponent>();
	check(ThisASC);
	
	if (!ThisASC->CanApplyGameplayEffect(GameplayEffect, ThisCharacter))
	{
		UE_LOG(LogBehaviorTree, Error, TEXT("%s cannot receive gameplay effect %s"), 
			*ThisCharacter->GetName(), *GameplayEffect->EffectTag.ToString());
		return EBTNodeResult::Failed;
	}
	
	// Apply effect
	URogueGameplayEffectInstance* Instance = nullptr;
	ThisASC->ApplyGameplayEffectToSelf(GameplayEffect, ThisCharacter, Instance);
	
	if (GameplayEffect->DurationPolicy.GetScriptStruct() == FRogueGameplayEffectInstantApply::StaticStruct())
	{
		// Instance apply marks execution complete
		return EBTNodeResult::Succeeded;
	}
	else
	{
		// Duration apply will continue execution till finish callback
		Instance->OnFinishedDelegate.AddDynamic(this, &URogueBTTask_ApplyGameplayEffect::OnGameplayEffectFinished);
		return EBTNodeResult::InProgress;
	}
}


void URogueBTTask_ApplyGameplayEffect::OnGameplayEffectFinished(URogueGameplayEffectInstance* Instance)
{
	Instance->OnFinishedDelegate.RemoveAll(this);
	FinishLatentTask(*MyOwnerComp, EBTNodeResult::Succeeded);
}

