// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "RogueGameplayEffectDurationPolicy.generated.h"


USTRUCT(BlueprintType)
struct FRogueGameplayEffectDurationPolicy
{
	GENERATED_BODY()
};


USTRUCT(DisplayName = "Instant Apply")
struct FRogueGameplayEffectInstantApply : public FRogueGameplayEffectDurationPolicy
{
	GENERATED_BODY()
};

USTRUCT(DisplayName = "Periodic Apply")
struct FRogueGameplayEffectPeriodicApply : public FRogueGameplayEffectDurationPolicy
{
	GENERATED_BODY()
	
	FRogueGameplayEffectPeriodicApply()
	{
		TotalCount = 0;
		Interval = 0.0f;
		ApplyFirstImmediately = false;
	}
	
	UPROPERTY(EditDefaultsOnly, Category = "Apply Policy", meta = (ToopTip = "How many times to apply in total"))
	int TotalCount;
	
	UPROPERTY(EditDefaultsOnly, Category = "Apply Policy", meta = (ToopTip = "Interval (in seconds) between each apply"))
	float Interval;
	
	UPROPERTY(EditDefaultsOnly, Category = "Apply Policy", meta = (ToopTip = "Whether to have the first apply immediately"))
	bool ApplyFirstImmediately;
};

USTRUCT(DisplayName = "Duration Apply")
struct FRogueGameplayEffectDurationApply : public FRogueGameplayEffectDurationPolicy
{
	GENERATED_BODY()
	
	FRogueGameplayEffectDurationApply()
	{
		Duration = 0.0f;
	}
	
	UPROPERTY(EditDefaultsOnly, Category = "Apply Policy")
	float Duration;
};

USTRUCT(DisplayName = "Infinite Time Apply")
struct FRogueGameplayEffectInfiniteTimeApply : public FRogueGameplayEffectDurationPolicy
{
	GENERATED_BODY()
};