// Fill out your copyright notice in the Description page of Project Settings.


#include "URogueGameplayEffectInstance.h"
#include "RogueGameplayEffect.h"
#include "RogueGameplayEffectDurationPolicy.h"
#include "RogueGameplayEffectModifyPolicy.h"
#include "ActionSystem/RogueActionSystemComponent.h"


void URogueGameplayEffectInstance::Init(const URogueGameplayEffect* InTemplate, URogueActionSystemComponent* InOwnerActionSystemComponent, 
	const UObject* InSender)
{
	Template = InTemplate;
	OwnerActionSystemComponent = InOwnerActionSystemComponent;
	Sender = InSender;
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
			Debuff.Handle = FAttributeDebuffHandle::GenerateNewHandle(); // Generate handle for tracking
			DebuffHandles.Add(Debuff.Handle);
		}
		OwnerActionSystemComponent->ApplyGameplayEffectDebuffs(Debuffs);
	}
}

void URogueGameplayEffectInstance::Finish()
{
	if (bFinished) return;
	bFinished = true;

	// Handle debuff removal
	if (const FRogueGameplayEffectDebuffModify* DebuffApplyData = Template->ModifyPolicy.GetPtr<FRogueGameplayEffectDebuffModify>())
	{
		TArray<FAttributeDebuffData> Debuffs = DebuffApplyData->Debuffs;
		ensure(Debuffs.Num() == DebuffHandles.Num());
		for (int i = 0; i < Debuffs.Num(); ++i)
		{
			Debuffs[i].Handle = DebuffHandles[i];
		}
		OwnerActionSystemComponent->RemoveGameplayEffectDebuffs(Debuffs);
	}
	OnFinishedDelegate.Broadcast(this);
};

