// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "RogueGameplayEffectDurationPolicy.generated.h"

// Data

USTRUCT(BlueprintType, meta=(HiddenByDefault))
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
	
	UPROPERTY(EditDefaultsOnly, Category = "Apply Policy", meta = (ToopTip = "How many times to apply in total. -1 denotes forever."))
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
	
	UPROPERTY(EditDefaultsOnly, Category = "Apply Policy", meta = (ToopTip = "How many seconds to last. -1 denotes forever."))
	float Duration;
};


// Instance

DECLARE_MULTICAST_DELEGATE(FOnGameplayEffectDurationPolicyOnApplySignatureCPP);
DECLARE_MULTICAST_DELEGATE(FOnGameplayEffectDurationPolicyOnFinishSignatureCPP);

UCLASS(BlueprintType)
class URogueGameplayEffectDurationPolicyInstance : public UObject
{
	GENERATED_BODY()
	
public:
	
	template<typename UserClass>
	void Setup(const FRogueGameplayEffectDurationPolicy* InDurationPolicy, UserClass* Owner,
		typename TMemFunPtrType<false, UserClass, void()>::Type OnApply,
		typename TMemFunPtrType<false, UserClass, void()>::Type OnFinish)
	{
		DurationPolicy = InDurationPolicy;	
		OnApplyDelegateCPP.AddUObject(Owner, OnApply);
		OnFinishDelegateCPP.AddUObject(Owner, OnFinish);
	}
	
	virtual void Start() {};

	void Apply();
	
	void Finish();
	
	UFUNCTION()
	virtual float TimeRemaining() const {return 0.0f;}
	
protected:
	
	const FRogueGameplayEffectDurationPolicy* DurationPolicy;
	
	FOnGameplayEffectDurationPolicyOnApplySignatureCPP OnApplyDelegateCPP;
	FOnGameplayEffectDurationPolicyOnFinishSignatureCPP OnFinishDelegateCPP;
	
	FTimerHandle TimerHandle;
};

UCLASS(BlueprintType)
class URogueGameplayEffectPeriodApplyInstance : public URogueGameplayEffectDurationPolicyInstance
{
	GENERATED_BODY()
	
public:
	
	virtual void Start() override;
	
	virtual float TimeRemaining() const override;

protected:
	
	virtual void OnTimerCallback();
	
	const FRogueGameplayEffectPeriodicApply* PeriodicApply;
	int PeriodicCountdown;
};

UCLASS(BlueprintType)
class URogueGameplayEffectDurationApplyInstance : public URogueGameplayEffectDurationPolicyInstance
{
	GENERATED_BODY()
	
public:
	
	virtual void Start() override;
	
	virtual float TimeRemaining() const override;

protected:
	
	virtual void OnTimerCallback();
	
	const FRogueGameplayEffectDurationApply* DurationApply;
};
