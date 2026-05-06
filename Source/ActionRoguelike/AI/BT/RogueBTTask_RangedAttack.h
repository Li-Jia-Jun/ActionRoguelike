// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "RogueBTTask_RangedAttack.generated.h"


class ARogueProjectileBase;

/**
 * 
 */
UCLASS()
class ACTIONROGUELIKE_API URogueBTTask_RangedAttack : public UBTTaskNode
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category="AI")
	FBlackboardKeySelector TargetActorKey;
	
	UPROPERTY(EditAnywhere, Category="AI")
	TSubclassOf<ARogueProjectileBase> ProjectileClass;
	
	UPROPERTY(EditAnywhere, Category="AI")
	float BulletSpread = 15.0f;
	
	UPROPERTY(EditAnywhere, Category="AI")
	FName MuzzleSocketName;
	
public:
	
	URogueBTTask_RangedAttack();
	
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
