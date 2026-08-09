// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "RogueBTService_CheckDistanceTo.generated.h"

/**
 *  Check if the owner is within range of the target actor. Set WithinRangeKey to true if within range, false otherwise.
 */
UCLASS()
class ACTIONROGUELIKE_API URogueBTService_CheckDistanceTo : public UBTService
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="AI")
	FBlackboardKeySelector TargetActorKey;

	UPROPERTY(EditAnywhere, Category="AI")
	FBlackboardKeySelector WithinRangeKey;

	UPROPERTY(EditAnywhere, Category="AI")
	float DistanceRange = 1000;
	
	UPROPERTY(EditAnywhere, Category="AI", meta=(ToolTip="If checked, the service will return true if the owner is outside the distance range."))
	bool ReverseCheck = false;
	
public:
	URogueBTService_CheckDistanceTo();

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
};
