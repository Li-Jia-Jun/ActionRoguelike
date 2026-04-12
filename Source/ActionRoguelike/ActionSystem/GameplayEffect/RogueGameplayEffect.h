// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/DataAsset.h"
#include "ActionSystem/AttributeSet/RogueAttributeSet.h"
#include "RogueGameplayEffectModifier.h"
#include "RogueGameplayEffectCalculation.h"
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


UCLASS(BlueprintType, Abstract)
class URogueGameplayEffect: public UDataAsset
{
	GENERATED_BODY()
	
public:
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Effect")
	FGameplayTag EffectTag;
	
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
	
protected:
	
	friend class URogueGameplayEffectInstance;
	friend class URogueActionSystemComponent;
};


UCLASS(BlueprintType)
class URogueAttributeGameplayEffect: public URogueGameplayEffect
{
	GENERATED_BODY()
	
public:
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Effect")
	TArray<FRogueGameplayEffectModifier> Modifiers;
	
	UPROPERTY(EditDefaultsOnly, Instanced, Category = "Rogue Gameplay Effect")
	TArray<TObjectPtr<URogueGameplayEffectCalculation>> Calculations;
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Effect")
	TArray<FGameplayTag> EffectsToCure;
	
	
	// TODO:: editor check:
	// - only instant and periodic apply can work here
};


UCLASS(BlueprintType)
class URogueDebuffGameplayEffect: public URogueGameplayEffect
{
	GENERATED_BODY()
	
public:
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Effect")
	TArray<FAttributeDebuffData> Debuffs;
	
#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif
	
	// TODO:: editor check:
	// - only duration and infinite time apply can work here
};