// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/DataAsset.h"
#include "RogueGameplayEffectModifier.h"
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
		meta = (ToolTips = "Do not apply this effect when target objects have any effect from this list"))
	TArray<FGameplayTag> ImmunityEffects;
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Effect",
		meta = (ToolTips = "Can only apply this effect when target objects have all the effects from this list"))
	TArray<FGameplayTag> PreconditionEffects;
};
