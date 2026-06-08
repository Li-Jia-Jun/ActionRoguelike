// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "ActionSystem/GameplayEffect/RogueGameplayEffectModifier.h"
#include "RogueAttributeSet.generated.h"


// 3 types of attributes:
// - 1. Numeric valuee - NumericData classes. E.g. Health, Stamina
// - 2. Debuff (isCursed) - GameplayTag + Modifiers (optional), a temporary condition. E.g. {Status.Burn, Health -= 1}

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
struct FAttributeDebuffHandle
{
	GENERATED_BODY()
	
	FAttributeDebuffHandle() : Handle(INDEX_NONE) {}
	
	static FAttributeDebuffHandle GenerateNewHandle()
	{
		static int32 GHandleID = 0;
		return FAttributeDebuffHandle(GHandleID++);
	}
	
	bool IsValid() const
	{
		return Handle != INDEX_NONE;
	}
	
	void Invalidate()
	{
		Handle = INDEX_NONE;
	}
	
	bool operator==(const FAttributeDebuffHandle& Other) const
	{
		return Handle == Other.Handle;
	}
	
	bool operator!=(const FAttributeDebuffHandle& Other) const
	{
		return Handle != Other.Handle;
	}
	
	friend uint32 GetTypeHash(const FAttributeDebuffHandle& InHandle)
	{
		return GetTypeHash(InHandle.Handle);
	}
	
private:
	
	explicit FAttributeDebuffHandle(int32 InHandle) : Handle(InHandle) {}
	
	int32 Handle;
};

USTRUCT(BlueprintType)
struct FAttributeDebuffData
{
	GENERATED_BODY()
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FGameplayTag Tag;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TArray<FRogueGameplayEffectModifier> Modifiers;
	
	UPROPERTY(BlueprintReadOnly)
	FAttributeDebuffHandle Handle;
	
	bool operator==(const FAttributeDebuffData& other) const 
	{
		return Handle == other.Handle;
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