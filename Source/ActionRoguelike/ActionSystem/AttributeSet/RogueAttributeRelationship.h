// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "RogueAttributeRelationship.generated.h"

UENUM(BlueprintType)
enum class EAttributeSetRelation : uint8
{
	eNotGreaterThan UMETA(DisplayName = "Not Greater Than"),
	eGreaterThan UMETA(DisplayName = "Greater Than"),
	eNotLessThan UMETA(DisplayName = "Not Less Than"),
	eLessThan UMETA(DisplayName = "Less Than"),
};

/**
 * Defines the relationship between two attributes, such as current health must be less or equal to max health.
 */
USTRUCT()
struct ACTIONROGUELIKE_API FRogueAttributeRelationship
{
	GENERATED_BODY()
	
	UPROPERTY(EditDefaultsOnly)
	FGameplayTag Attribute01;
	
	UPROPERTY(EditDefaultsOnly)
	EAttributeSetRelation eRelation;
	
	UPROPERTY(EditDefaultsOnly)
	FGameplayTag Attribute02;
	
	FRogueAttributeRelationship()
	{
		eRelation = EAttributeSetRelation::eNotGreaterThan;
	}
	
	FString ToString() const
	{
		FText RelationText = UEnum::GetDisplayValueAsText(eRelation);
		return FString::Printf(TEXT("%s %s %s"), *Attribute01.ToString(), *RelationText.ToString(), *Attribute02.ToString());
	}
};
