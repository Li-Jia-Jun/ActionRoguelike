// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "RogueBTTask_ApplyGameplayEffect.generated.h"


class URogueGameplayEffect;
class URogueGameplayEffectInstance;

/**
 *  Apply a GameplayEffect to ASC, finish when the effect is done
 */
UCLASS()
class ACTIONROGUELIKE_API URogueBTTask_ApplyGameplayEffect : public UBTTaskNode
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = "AI")
	TObjectPtr<URogueGameplayEffect> GameplayEffect;
	
public:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	
protected:
	
	UPROPERTY()
	TObjectPtr<UBehaviorTreeComponent> MyOwnerComp;
	
	UFUNCTION()
	void OnGameplayEffectFinished(URogueGameplayEffectInstance* Instance);
};
