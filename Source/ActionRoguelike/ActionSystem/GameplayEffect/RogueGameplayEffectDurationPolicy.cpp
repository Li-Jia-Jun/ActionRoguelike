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

// Period apply

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
	
	// Forever apply
	if (PeriodicCountdown == -1)
	{
		return;
	}
	
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
	
	// Forever apply
	if (PeriodicCountdown == -1)
	{
		return -1.0f;
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
	
	// Forever apply
	if (FMath::IsNearlyEqual(DurationApply->Duration, -1.0f))
	{
		return;
	}
	
	GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &ThisClass::OnTimerCallback, DurationApply->Duration, false);
}

float URogueGameplayEffectDurationApplyInstance::TimeRemaining() const
{
	// Forever apply
	if (FMath::IsNearlyEqual(DurationApply->Duration, -1.0f))
	{
		return -1.0f;
	}
	
	if (TimerHandle.IsValid())
	{
		return GetWorld()->GetTimerManager().GetTimerRemaining(TimerHandle);
	}
	
	return -1.0f;
}

void URogueGameplayEffectDurationApplyInstance::OnTimerCallback()
{
	Finish();
}
