// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "RogueBTDecorator_CheckAttributePercentage.generated.h"

UENUM(BlueprintType)
enum class EAttributeSCheckMode : uint8
{
	Higher,
	Lower
};

/**
 * Check If CurrentAttribute value is lower or higher than the percentage of MaxAttribute value
 */
UCLASS()
class ACTIONROGUELIKE_API URogueBTDecorator_CheckAttributePercentage : public UBTDecorator
{
	GENERATED_BODY()
	
public:
	
	UPROPERTY(EditAnywhere, Category = "AI")
	FGameplayTag CurrentAttributeTag;
	
	UPROPERTY(EditAnywhere, Category = "AI")
	FGameplayTag MaxAttributeTag;
	
	UPROPERTY(EditAnywhere, Category = "AI")
	EAttributeSCheckMode CheckMode = EAttributeSCheckMode::Lower;
	
	UPROPERTY(EditAnywhere, Category = "AI")
	float Percentage = 0.5f;
	
protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
};
