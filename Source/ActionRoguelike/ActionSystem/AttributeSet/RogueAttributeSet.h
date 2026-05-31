// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "ActionSystem/GameplayEffect/RogueGameplayEffectModifier.h"
#include "RogueAttributeSet.generated.h"


// 3 types of attributes:
// - 1. Numeric values (Health, Stamina) - NumericData classes
// - 2. Debuff (isCursed) - GameplayTag + Modifiers (optional), a temporary condition
// - TODO:: 3. States (Mental state: depressed, calm, happy) - Custom enum class so that we can see dropdown in editor

class URogueAttributeSet;
struct FRogueAttributeRelationship;


USTRUCT(BlueprintType)
struct FAttributeNumericData
{
	GENERATED_BODY()
	
	FAttributeNumericData()
	{
		BaseValue = 0.0f;
		CurrentValue = 0.0f;
	}
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FGameplayTag Tag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float BaseValue;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float CurrentValue;
	
	bool operator==(const FAttributeNumericData& other) const 
	{
		return Tag.MatchesTag(other.Tag);
	}
};


USTRUCT(BlueprintType)
struct FAttributeDebuffData
{
	GENERATED_BODY()
	
	FGameplayTag Tag; // Cache from GameplayEffect instance
	
	uint8 StackIndex; // Cache from GameplayEffect instance
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TArray<FRogueGameplayEffectModifier> Modifiers;
	
	bool operator==(const FAttributeDebuffData& other) const 
	{
		return Tag.MatchesTag(other.Tag) and StackIndex == other.StackIndex;
	}
};


UCLASS()
class ACTIONROGUELIKE_API URogueAttributeSetTemplate : public UDataAsset
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditDefaultsOnly)
	TArray<FAttributeNumericData> Attributes;
	
	UPROPERTY(EditDefaultsOnly)
	TArray<FRogueAttributeRelationship> AttributeRelationships;
};

USTRUCT(BlueprintType)
struct FRogueAttributeSetSnapshot
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadWrite)
	TArray<FAttributeNumericData> Attributes;
	
	UPROPERTY(BlueprintReadWrite)
	TArray<FAttributeDebuffData> Debuffs;
};

UCLASS(BlueprintType)
class ACTIONROGUELIKE_API URogueAttributeSet : public UObject
{
	GENERATED_BODY()
	
public:
	
	void InitByTemplate(URogueAttributeSetTemplate* InTemplate);
	
	UPROPERTY(VisibleAnywhere, Category = "Rogue Attributes")
	TObjectPtr<URogueAttributeSetTemplate> Template;
	
	UPROPERTY(VisibleAnywhere, Category = "Rogue Attributes")
	TArray<FAttributeNumericData> Attributes;
	
	UPROPERTY(VisibleAnywhere, Category = "Rogue Attributes")
	TArray<FRogueAttributeRelationship> AttributeRelationships;
	
	UPROPERTY(VisibleAnywhere, Category = "Rogue Attributes")
	TArray<FAttributeDebuffData> Debuffs;
	
	UFUNCTION(BlueprintCallable)
	FRogueAttributeSetSnapshot TakeSnapshot() const;
	
	bool CanAffordModifiers(const TArray<FRogueGameplayEffectModifier> &InModifiers, FString& FailMessage) const;
	
	void ApplyModifiers(const TArray<FRogueGameplayEffectModifier> &InModifiers);
	
	void ApplyDebuffs(const TArray<FAttributeDebuffData> &InDebuffs);
	
	void RemoveDebuffs(const TArray<FAttributeDebuffData> &InDebuffs);
	
	void RemoveDebuffsByTag(const FGameplayTag Tag);

protected:
	
	void RecalculateAttributes();
	
	template <float FAttributeNumericData::* Value>
	void ApplyModifierToAttribute(const FRogueGameplayEffectModifier& Modifier, FAttributeNumericData& Attribute) const;
	
	void ApplyRelationshipsToAttribute(const FRogueAttributeRelationship& Relationship, 
		FAttributeNumericData& Attribute01, FAttributeNumericData& Attribute02) const;
};

UCLASS()
class ACTIONROGUELIKE_API UAttributeSetFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	
	UFUNCTION(BlueprintPure, Category = "Attribute Set")
	static bool FindAttributeDataByTag(const TArray<FAttributeNumericData>& Attributes, FGameplayTag Tag, FAttributeNumericData& OutAttribute)
	{
		for (auto& Attribute : Attributes)
		{
			if (Attribute.Tag.MatchesTag(Tag))
			{
				OutAttribute = Attribute;
				return true;
			}
		}
		return false;
	}

	UFUNCTION(BlueprintPure, Category = "Attribute Set")
	static bool FindDebuffDataByTag(const TArray<FAttributeDebuffData>& Debuffs, FGameplayTag Tag, FAttributeDebuffData& OutDebuff)
	{
		for (auto& Debuff : Debuffs)
		{
			if (Debuff.Tag.MatchesTag(Tag))
			{
				OutDebuff = Debuff;
				return true;
			}
		}
		return false;
	}
};