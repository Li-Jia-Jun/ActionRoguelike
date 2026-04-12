// Fill out your copyright notice in the Description page of Project Settings.


#include "URogueGameplayEffectInstance.h"
#include "RogueGameplayEffect.h"
#include "RogueGameplayEffectDurationPolicy.h"
#include "ActionSystem/RogueActionSystemComponent.h"

// Effect Instance

void URogueGameplayEffectInstance::ApplyGameplayEffectModifiers(const TArray<FRogueGameplayEffectModifier>& Modifiers)
{
	OwnerActionSystemComponent->ApplyGameplayEffectModifiers(Modifiers);
}
	
void URogueGameplayEffectInstance::ApplyGameplayEffectDebuffs(const TArray<FAttributeDebuffData>& Debuffs)
{
	OwnerActionSystemComponent->ApplyGameplayEffectDebuffs(Debuffs);
}
	
void URogueGameplayEffectInstance::RemoveGameplayEffectDebuffs(const TArray<FAttributeDebuffData>& Debuffs)
{
	OwnerActionSystemComponent->RemoveGameplayEffectDebuffs(Debuffs);
}

// Attribute Effect Instance

void URogueAttributeEffectInstance::Init(URogueGameplayEffect* InTemplate, URogueActionSystemComponent* InOwnerActionSystemComponent, 
	UObject* InSender, uint8 InStackIndex)
{
	Super::Init(InTemplate, InOwnerActionSystemComponent, InSender, InStackIndex);
	
	AttributeEffect = Cast<URogueAttributeGameplayEffect>(InTemplate);
}

void URogueAttributeEffectInstance::Apply()
{
	ApplyGameplayEffectModifiers(AttributeEffect->Modifiers);
}

void URogueAttributeEffectInstance::Start()
{
	if (AttributeEffect->DurationPolicy.GetScriptStruct() == FRogueGameplayEffectPeriodicApply::StaticStruct())
	{
		const FRogueGameplayEffectPeriodicApply& PeriodicApply = AttributeEffect->DurationPolicy.Get<FRogueGameplayEffectPeriodicApply>();
		SetupPeriodicApplyTimer(PeriodicApply);
	}
}

void URogueAttributeEffectInstance::SetupPeriodicApplyTimer(const FRogueGameplayEffectPeriodicApply& PeriodicApply)
{
	if (PeriodicApply.ApplyFirstImmediately)
	{
		// Total count being 1 means instant apply
		if (PeriodicApply.TotalCount == 1)
		{	
			Apply();
			OnFisnihedDelegate.Broadcast(this);
		}
		else
		{
			Apply();
			PeriodicApplyCountdown = PeriodicApply.TotalCount - 1;
			GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &URogueAttributeEffectInstance::OnPeriodicApplyExpired, PeriodicApply.Interval, true);
		}
	}
	else
	{
		PeriodicApplyCountdown = PeriodicApply.TotalCount;
		GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &URogueAttributeEffectInstance::OnPeriodicApplyExpired, PeriodicApply.Interval, true);
	}
}

void URogueAttributeEffectInstance::OnPeriodicApplyExpired()
{
	Apply();
	PeriodicApplyCountdown--;
	
	if (PeriodicApplyCountdown == 0)
	{
		Finish();
	}
}


// Debuff Effect Instance

void URogueDebuffEffectInstance::Init(URogueGameplayEffect* InTemplate, URogueActionSystemComponent* InOwnerActionSystemComponent, 
	UObject* InSender, uint8 InStackIndex)
{
	Super::Init(InTemplate, InOwnerActionSystemComponent, InSender, InStackIndex);
	
	DebuffEffect = Cast<URogueDebuffGameplayEffect>(InTemplate);
	
	// Create instanced debuff data
	InstancedDebuffs = DebuffEffect->Debuffs;
	for (FAttributeDebuffData& Debuff : InstancedDebuffs)
	{
		Debuff.Tag = DebuffEffect->EffectTag;
		Debuff.StackIndex = StackIndex;
	}
}

void URogueDebuffEffectInstance::Start()
{
	// Debuff condition will always apply immediately (and remove when finish)
	Apply();
	
	// Handle duration policy
	if (DebuffEffect->DurationPolicy.GetScriptStruct() == FRogueGameplayEffectDurationApply::StaticStruct())
	{
		const FRogueGameplayEffectDurationApply& DurationApply = DebuffEffect->DurationPolicy.Get<FRogueGameplayEffectDurationApply>();
		SetupDurationApplyTimer(DurationApply);
	}
}

void URogueDebuffEffectInstance::Apply()
{
	ApplyGameplayEffectDebuffs(InstancedDebuffs);
}

void URogueDebuffEffectInstance::Finish()
{
	RemoveGameplayEffectDebuffs(InstancedDebuffs);
	Super::Finish();
}

void URogueDebuffEffectInstance::SetupDurationApplyTimer(const FRogueGameplayEffectDurationApply& DurationApply)
{
	GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &URogueDebuffEffectInstance::OnDurationApplyExpired, DurationApply.Duration, false);
}

void URogueDebuffEffectInstance::OnDurationApplyExpired()
{
	Finish();
}



