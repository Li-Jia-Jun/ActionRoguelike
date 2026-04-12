// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "RogueGameplayEffectModifier.generated.h"

struct FRogueActionAttributeSet;
struct FGameplayTag;


UENUM(BlueprintType)
enum class ModifierType : uint8
{
	eFlatValue UMETA(DisplayName = "Flat Value"),								// Flat value + Operation
	eOtherAttributeMagnitude UMETA(DisplayName = "Other Attribute Magnitude"),	// Other Attr value * Magnitude
};

UENUM(BlueprintType)
enum class ERogueAttributeModifierOp : uint8
{
	eAdd UMETA(DisplayName = "Add"),
	eSubtract UMETA(DisplayName = "Subtract"),
	eMultiply UMETA(DisplayName = "Multiply"),
};

USTRUCT(BlueprintType)
struct FRogueGameplayEffectModifier
{
	GENERATED_BODY()
	
	FRogueGameplayEffectModifier()
	{
		Type = ModifierType::eFlatValue;
		FlatValueOp = ERogueAttributeModifierOp::eAdd;
		FlatValue = 0.0f;
		OtherAttributeMagnitude = 1.0f;
	}

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modifier")
	ModifierType Type;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modifier")
	FGameplayTag TargetAttribute;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modifier")
	ERogueAttributeModifierOp FlatValueOp;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modifier")
	float FlatValue;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modifier")
	FGameplayTag OtherAttributeTag;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modifier")
	float OtherAttributeMagnitude;
};
