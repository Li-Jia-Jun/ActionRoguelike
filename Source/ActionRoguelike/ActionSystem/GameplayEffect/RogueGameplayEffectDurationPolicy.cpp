// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueGameplayEffectDurationPolicy.h"


void URogueGameplayEffectDurationPolicyInstance::Apply()
{
	OnApplyDelegateCPP.Broadcast();
}

void URogueGameplayEffectDurationPolicyInstance::Finish()
{
	if (TimerHandle.IsValid())
	{
		FTimerManager& TimerManager = GetWorld()->GetTimerManager();
		TimerManager.ClearTimer(TimerHandle);
	}
	OnFinishDelegateCPP.Broadcast();
}


// Periodic apply

void URogueGameplayEffectPeriodApplyInstance::Start()
{
	Super::Start();
	
	PeriodicApply = static_cast<const FRogueGameplayEffectPeriodicApply*>(DurationPolicy);
	
	if (PeriodicApply->ApplyFirstImmediately)
	{
		// Total count being 1 means instant apply
		if (PeriodicApply->TotalCount == 1)
		{	
			Apply();
			Finish();
		}
		else
		{
			Apply();
			PeriodicCountdown = PeriodicApply->TotalCount - 1;
			GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &ThisClass::OnTimerCallback, PeriodicApply->Interval, true);
		}
	}
	else
	{
		PeriodicCountdown = PeriodicApply->TotalCount;
		GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &ThisClass::OnTimerCallback, PeriodicApply->Interval, true);
	}
}

void URogueGameplayEffectPeriodApplyInstance::OnTimerCallback()
{	
	Apply();
	PeriodicCountdown--;
	
	if (PeriodicCountdown == 0)
	{
		Finish();
	}
}

float URogueGameplayEffectPeriodApplyInstance::TimeRemaining() const
{
	if (!TimerHandle.IsValid())
	{
		return 0.0f;
	}
	
	float CurrentCountdownRemaining = GetWorld()->GetTimerManager().GetTimerRemaining(TimerHandle);
	
	return PeriodicApply->Interval * PeriodicCountdown + CurrentCountdownRemaining;
}

// Duration apply

void URogueGameplayEffectDurationApplyInstance::Start()
{
	Super::Start();
	
	DurationApply = static_cast<const FRogueGameplayEffectDurationApply*>(DurationPolicy);
	
	Apply();
	GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &ThisClass::OnTimerCallback, DurationApply->Duration, false);
}

void URogueGameplayEffectDurationApplyInstance::OnTimerCallback()
{
	Finish();
}

// Infinite Time apply

void URogueGameplayEffectInfiniteTimeApplyInstance::Start()
{
	Apply();
}
