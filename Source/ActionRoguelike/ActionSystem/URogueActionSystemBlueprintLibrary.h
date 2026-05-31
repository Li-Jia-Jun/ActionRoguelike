// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "ActionSystem/GameplayEvent/FRogueGameplayEventData.h"
#include "URogueActionSystemBlueprintLibrary.generated.h"


UCLASS()
class ACTIONROGUELIKE_API UURogueActionSystemBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	
	UFUNCTION(BlueprintCallable)
	static void SendGameplayEventToActor(AActor* TargetActor, FGameplayTag EventTag, const FRogueGameplayEventData& Payload);
};
