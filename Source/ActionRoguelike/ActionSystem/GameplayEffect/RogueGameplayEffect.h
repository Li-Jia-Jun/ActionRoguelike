// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "RogueGameplayEffect.generated.h"

class URogueGameplayEffectInstance;
class URogueActionSystemComponent;


UENUM(BlueprintType)
enum class ERogueGameplayEffectStackPolicy : uint8
{
	eIndependent UMETA(DisplayName = "Independent Stack"),
	eRefresh UMETA(DisplayName = "Refresh Stack"),
	eAccumulate UMETA(DisplayName = "Accumulate Stack"),
};


UCLASS(BlueprintType)
class URogueGameplayEffect: public UDataAsset
{
	GENERATED_BODY()
	
public:
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Effect")
	FGameplayTag EffectTag;

	UPROPERTY(EditDefaultsOnly, meta=(Category = "Rogue Gameplay Effect",
		BaseStruct="/Script/ActionRoguelike.RougeGameplayEffectModifyPolicy"))
	FInstancedStruct ModifyPolicy;
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Effect",
		meta = (BaseStruct = "/Script/ActionRoguelike.RogueGameplayEffectDurationPolicy"))
	FInstancedStruct DurationPolicy;
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Effect")
	ERogueGameplayEffectStackPolicy StackPolicy;
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Effect")
	uint8 StackLimit;
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Effect", 
		meta = (ToolTips = "Tags that block this effect from apply"))
	FGameplayTagContainer TagsThatBlock;
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Effect",
		meta = (ToolTips = "Tags that are present before this effect can apply"))
	FGameplayTagContainer TagsThatRequire;// PreconditionTags
	
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
