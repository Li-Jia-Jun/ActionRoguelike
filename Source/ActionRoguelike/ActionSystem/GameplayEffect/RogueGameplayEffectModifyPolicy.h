#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/AttributeSet/RogueAttributeSet.h"
#include "RogueGameplayEffectCalculation.h"
#include "RogueGameplayEffectModifyPolicy.generated.h"
/*
 * Class to store data that modifies attributes for Gameplay Effect.
 */
USTRUCT(BlueprintType, meta=(Hidden))
struct FRougeGameplayEffectModifyPolicy
{
	GENERATED_BODY()
};

USTRUCT(BlueprintType, DisplayName="Permanent Modify")
struct FRogueGameplayEffectPermanentModify: public FRougeGameplayEffectModifyPolicy
{
	GENERATED_BODY()
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Effect")
	TArray<FRogueGameplayEffectModifier> Modifiers;
	
	UPROPERTY(EditDefaultsOnly, Instanced, Category = "Rogue Gameplay Effect")
	TArray<TObjectPtr<URogueGameplayEffectCalculation>> Calculations;
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Effect")
	TArray<FGameplayTag> EffectsToCure;
};

USTRUCT(BlueprintType, DisplayName="Debuff Modify")
struct FRogueGameplayEffectDebuffModify : public FRougeGameplayEffectModifyPolicy
{
	GENERATED_BODY()
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Effect")
	TArray<FAttributeDebuffData> Debuffs;
};
