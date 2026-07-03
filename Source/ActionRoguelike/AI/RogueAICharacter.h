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
	ARogueAICharacter();
	
	virtual void BeginPlay() override;
	
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	
	UFUNCTION()
	void OnHit(FGameplayTag EventTag, FRogueGameplayEventData Payload);
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Action System")
	URogueActionSystemComponent* ActionSystemComp;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	URogueAttributeBarWidgetComponent* HealthAttributeBarWidgetComp;
	
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> HitOverlayMID;
	FTimerHandle OverlayTimerHandle;
};
