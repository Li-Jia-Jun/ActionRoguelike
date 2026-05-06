// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Blueprint/UserWidget.h"
#include "ActionSystem/AttributeSet/RogueAttributeSet.h"
#include "RogueAttributeBarWidget.generated.h"

class UProgressBar;
class URogueActionSystemComponent;

/**
 * Display a Progressbar using CurrentAttribute and MaxAttribute from ASC. 
 * To work with RogueAttributeBarWidgetComponent.
 */
UCLASS(Abstract)
class ACTIONROGUELIKE_API URogueAttributeBarWidget : public UUserWidget
{
	GENERATED_BODY()
	
public:
	void InitializeWithASC(URogueActionSystemComponent* InASC, FGameplayTag InCurrentAttributeTag, FGameplayTag InMaxAttributeTag);
	
protected:
	
	void OnAttributeSetChanged(FRogueAttributeSetSnapshot OldSnapshot, FRogueAttributeSetSnapshot NewSnapshot);
	
	UPROPERTY(meta = (BindWidget))
	UProgressBar* ProgressBar;
	
private:
	
	FGameplayTag CurrentAttributeTag;
	FGameplayTag MaxAttributeTag;
	TWeakObjectPtr<URogueActionSystemComponent> ASC;
};
