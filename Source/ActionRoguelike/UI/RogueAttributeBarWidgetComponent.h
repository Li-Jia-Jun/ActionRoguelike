// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "GameplayTagContainer.h"
#include "RogueAttributeBarWidgetComponent.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class ACTIONROGUELIKE_API URogueAttributeBarWidgetComponent : public UWidgetComponent
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	
protected:
	
	UPROPERTY(EditDefaultsOnly, Category = "Attribute UI")
	FGameplayTag CurrentAttributeTag;
	
	UPROPERTY(EditDefaultsOnly, Category = "Attribute UI")
	FGameplayTag MaxAttributeTag;
};
