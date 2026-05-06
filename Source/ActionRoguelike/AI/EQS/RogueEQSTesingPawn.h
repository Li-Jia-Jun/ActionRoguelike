// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EQSTestingPawn.h"
#include "RogueEQSTesingPawn.generated.h"

UCLASS()
class ACTIONROGUELIKE_API ARogueEQSTesingPawn : public AEQSTestingPawn
{
	GENERATED_BODY()

public:
	
	UPROPERTY(EditAnywhere, Category = "EQS Custom Testing")
	AActor* TargetActor;
};
