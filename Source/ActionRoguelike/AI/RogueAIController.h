// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "RogueAIController.generated.h"

class UBehaviorTree;

UCLASS(Abstract)
class ACTIONROGUELIKE_API ARogueAIController : public AAIController
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ARogueAIController();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	UPROPERTY(EditDefaultsOnly, Category = "AI")
	TObjectPtr<UBehaviorTree> BehaviorTree;
};
