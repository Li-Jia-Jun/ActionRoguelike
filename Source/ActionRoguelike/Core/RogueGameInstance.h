// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "RogueGameInstance.generated.h"

/**
 * 
 */
UCLASS()
class ACTIONROGUELIKE_API URogueGameInstance : public UGameInstance
{
	GENERATED_BODY()
	
	
public:
	
	virtual void OnWorldChanged(UWorld* OldWorld, UWorld* NewWorld);
	
	UPROPERTY()
	TArray<APawn*> AliveAIEnemies;
};
