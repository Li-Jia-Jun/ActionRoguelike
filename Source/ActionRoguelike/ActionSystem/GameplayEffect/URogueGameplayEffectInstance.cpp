// Fill out your copyright notice in the Description page of Project Settings.


#include "URogueGameplayEffectInstance.h"
#include "RogueGameplayEffect.h"
#include "RogueGameplayEffectDurationPolicy.h"
#include "RogueGameplayEffectModifyPolicy.h"
#include "ActionSystem/RogueActionSystemComponent.h"


void URogueGameplayEffectInstance::Init(const URogueGameplayEffect* InTemplate, URogueActionSystemComponent* InOwnerActionSystemComponent, 
	const UObject* InSender, uint8 InStackIndex)
{
	Template = InTemplate;
	OwnerActionSystemComponent = InOwnerActionSystemComponent;
	Sender = InSender;
	StackIndex = InStackIndex;
}

float URogueGameplayEffectInstance::GetTimeRemaining() const
{
	return DurationPolicyInstance->TimeRemaining();
}

void URogueGameplayEffectInstance::Start()
{
	// Create duration policy instance
	if (const FRogueGameplayEffectPeriodicApply* PeriodicApply = Template->DurationPolicy.GetPtr<FRogueGameplayEffectPeriodicApply>())
	{
		DurationPolicyInstance = NewObject<URogueGameplayEffectPeriodApplyInstance>(this);
	}
	else if (const FRogueGameplayEffectDurationApply* DurationApply = Template->DurationPolicy.GetPtr<FRogueGameplayEffectDurationApply>())
	{
		DurationPolicyInstance = NewObject<URogueGameplayEffectDurationApplyInstance>(this);
	}
	else
	{
		// Fallback: no timer needed, apply and finish immediately
		Apply();
		Finish();
		return;
	}

	const FRogueGameplayEffectDurationPolicy* DurationPolicy = Template->DurationPolicy.GetPtr<FRogueGameplayEffectDurationPolicy>();
	DurationPolicyInstance->Setup<URogueGameplayEffectInstance>(DurationPolicy, this, &ThisClass::Apply, &ThisClass::Finish);
	DurationPolicyInstance->Start();
}

void URogueGameplayEffectInstance::Apply()
{
	if (const FRogueGameplayEffectPermanentModify* PermanentModify = Template->ModifyPolicy.GetPtr<FRogueGameplayEffectPermanentModify>())
	{
		OwnerActionSystemComponent->ApplyGameplayEffectModifiers(PermanentModify->Modifiers);
	}
	else if (const FRogueGameplayEffectDebuffModify* DebuffApplyData = Template->ModifyPolicy.GetPtr<FRogueGameplayEffectDebuffModify>())
	{
		// Assign effect tag and stack index to debuffs so that they can be identified for removal
		TArray<FAttributeDebuffData> Debuffs = DebuffApplyData->Debuffs;
		for (FAttributeDebuffData& Debuff : Debuffs)
		{
			Debuff.Tag = Template->EffectTag;
			Debuff.StackIndex = StackIndex;
		}
		OwnerActionSystemComponent->ApplyGameplayEffectDebuffs(DebuffApplyData->Debuffs);
	}
}

void URogueGameplayEffectInstance::Finish()
{
	if (const FRogueGameplayEffectDebuffModify* DebuffApplyData = Template->ModifyPolicy.GetPtr<FRogueGameplayEffectDebuffModify>())
	{
		TArray<FAttributeDebuffData> Debuffs = DebuffApplyData->Debuffs;
		for (FAttributeDebuffData& Debuff : Debuffs)
		{
			Debuff.Tag = Template->EffectTag;
			Debuff.StackIndex = StackIndex;
		}
		OwnerActionSystemComponent->RemoveGameplayEffectDebuffs(Debuffs);
	}
	OnFinishedDelegate.Broadcast(this);
};

