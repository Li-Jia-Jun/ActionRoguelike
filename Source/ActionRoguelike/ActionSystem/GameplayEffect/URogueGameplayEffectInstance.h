// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "RogueGameplayEffect.h"
#include "RogueGameplayEffectDurationPolicy.h"
#include "UObject/Object.h"
#include "URogueGameplayEffectInstance.generated.h"

class URogueGameplayEffect;
class URogueAttributeGameplayEffect;
class URogueDebuffGameplayEffect;
class URogueActionSystemComponent;

class URogueGameplayEffectInstance;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameplayEffectInstanceFinishedSignature, URogueGameplayEffectInstance*, Target);

/**
 * Runtime instance of a gameplay effect
 *	- Use timer to handle its own lifetime
 *	- Use Effect to apply effect
 *	- Use delegate to communicate with ASC
 */
UCLASS()
class ACTIONROGUELIKE_API URogueGameplayEffectInstance : public UObject
{
	GENERATED_BODY()
		
public:
	virtual void Init(const URogueGameplayEffect* InTemplate, URogueActionSystemComponent* InOwnerActionSystemComponent, 
		const UObject* InSender, uint8 InStackIndex = 0)
	{
		Template = InTemplate;
		OwnerActionSystemComponent = InOwnerActionSystemComponent;
		Sender = InSender;
		StackIndex = InStackIndex;
	}
	
	virtual void Start() {}

	FOnGameplayEffectInstanceFinishedSignature OnFisnihedDelegate;
	
	bool operator==(const URogueGameplayEffectInstance& Other) const
	{
		return Template->EffectTag.MatchesTag(Other.Template->EffectTag);
	}

protected:
	
	virtual void Apply() {};
	
	virtual void Finish()
	{
		if (TimerHandle.IsValid())
		{
			FTimerManager& TimerManager = GetWorld()->GetTimerManager();
			TimerManager.ClearTimer(TimerHandle);
		}
		OnFisnihedDelegate.Broadcast(this);
	};
	
	void ApplyGameplayEffectModifiers(const TArray<FRogueGameplayEffectModifier>& Modifiers);
	
	void ApplyGameplayEffectDebuffs(const TArray<FAttributeDebuffData>& Debuffs);
	
	void RemoveGameplayEffectDebuffs(const TArray<FAttributeDebuffData>& Debuffs);
	
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<const URogueGameplayEffect> Template;
	
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<URogueActionSystemComponent> OwnerActionSystemComponent;
	
	UPROPERTY(VisibleAnywhere)
	const UObject* Sender;
	
	FTimerHandle TimerHandle;
	
	uint8 StackIndex;
	
	friend class URogueActionSystemComponent;
};

UCLASS()
class URogueAttributeEffectInstance : public URogueGameplayEffectInstance
{
	GENERATED_BODY()
	
public:
	virtual void Init(const URogueGameplayEffect* InTemplate, URogueActionSystemComponent* InOwnerActionSystemComponent, 
		const UObject* InSender, uint8 InStackIndex) override;

	virtual void Start() override;
	
protected:
	
	virtual void Apply() override;
	
	UPROPERTY()
	TObjectPtr<const URogueAttributeGameplayEffect> AttributeEffect;
	
	float PeriodicApplyCountdown;
	void SetupPeriodicApplyTimer(const FRogueGameplayEffectPeriodicApply& PeriodicApply);
	void OnPeriodicApplyExpired();
};


UCLASS()
class URogueDebuffEffectInstance : public URogueGameplayEffectInstance
{
	GENERATED_BODY()
	
public:
	virtual void Init(const URogueGameplayEffect* InTemplate, URogueActionSystemComponent* InOwnerActionSystemComponent, 
		const UObject* InSender, uint8 InStackIndex) override;
	
	virtual void Start() override;
	
protected:
	
	virtual void Apply() override;
	
	virtual void Finish() override;
	
	UPROPERTY()
	const URogueDebuffGameplayEffect* DebuffEffect;
	
	UPROPERTY()
	TArray<FAttributeDebuffData> InstancedDebuffs; // Cache from Effect and owns stack index
	
	void SetupDurationApplyTimer(const FRogueGameplayEffectDurationApply& DurationApply);
	void OnDurationApplyExpired();
};