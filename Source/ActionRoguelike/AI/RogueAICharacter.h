// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Character.h"
#include "RogueAICharacter.generated.h"


class UWidgetComponent;
class URogueActionSystemComponent;
class URogueAttributeBarWidgetComponent;

UCLASS(Abstract)
class ACTIONROGUELIKE_API ARogueAICharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	ARogueAICharacter();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Action System")
	URogueActionSystemComponent* ActionSystemComp;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	URogueAttributeBarWidgetComponent* HealthAttributeBarWidgetComp;
};
