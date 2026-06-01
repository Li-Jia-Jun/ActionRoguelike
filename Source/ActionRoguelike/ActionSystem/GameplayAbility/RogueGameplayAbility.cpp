// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueGameplayAbility.h"
#include "GameFramework/Character.h"
#include "ActionSystem/RogueActionSystemComponent.h"
#include "ActionSystem/GameplayEffect/RogueGameplayEffectDurationPolicy.h"
#include "ActionSystem/GameplayEffect/URogueGameplayEffectInstance.h"


bool URogueGameplayAbility::CheckCanBeGranted(URogueActionSystemComponent* ActionSystemComponent) const
{
	return true;
}

bool URogueGameplayAbility::CanActivateAbility() const
{
	// Cost check 
	if (!OwnerActionSystemComponent->CanApplyGameplayEffect(CostEffect, this))
	{
		UE_LOG(LogTemp, Warning, TEXT("%s cannot be activated due to cost effect apply failure."), *GetName());
		return false;
	}
	
	// Cooldown check
	if (CooldownEffectInstance.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s cannot be activated due to existing cooldown effect instance. Time remaining = %f"), 
			*GetName(), CooldownEffectInstance->GetTimeRemaining());
		return false;
	}
	if (!OwnerActionSystemComponent->CanApplyGameplayEffect(CooldownEffect, this))
	{
		UE_LOG(LogTemp, Warning, TEXT("%s cannot be activated due to cooldown effect apply failure."), *GetName());
			return false;
	}
	
	return true;
}

void URogueGameplayAbility::ActivateAbility()
{
	// Status
	bIsActivated = true;
}

bool URogueGameplayAbility::CommitAbility()
{
	if (!CanActivateAbility())
	{
		return false;
	}
	
	// Apply cost effect (instant) so there will be no instance
	URogueGameplayEffectInstance* OutCostEffectInstance = nullptr;
	OwnerActionSystemComponent->ApplyGameplayEffectToSelf(CostEffect, this, false, OutCostEffectInstance);
	
	// Apply cooldown effect and save its pointer
	URogueGameplayEffectInstance* OutCooldownEffectInstance = nullptr;
	OwnerActionSystemComponent->ApplyGameplayEffectToSelf(CooldownEffect, this, false, OutCooldownEffectInstance);
	CooldownEffectInstance = OutCooldownEffectInstance;
	
	return true;
}

void URogueGameplayAbility::EndAbility()
{
	// Status
	bIsActivated = false;
	
	// Clean up delegate
	OwnerActionSystemComponent->GameplayEventReceivedDelegate.RemoveAll(this);
	
	// Clean up anim
	if (AnimInstanceToTrack.IsValid())
	{
		AnimInstanceToTrack->Montage_Stop(0.0f);
		AnimInstanceToTrack->OnMontageEnded.RemoveAll(this);
	}
	
	// Broadcast ending
	AbilityEndedDelegate.Broadcast(this, ComposeEndedData());
}

FRogueGameplayAbilityEndedData URogueGameplayAbility::ComposeEndedData() const
{
	FRogueGameplayAbilityEndedData EndedData;
	EndedData.bIsSuccessful = true;
	EndedData.Object = nullptr;
	EndedData.Value = 0.0f;
	return EndedData;
}

float URogueGameplayAbility::CooldownTimeRemaining() const
{
	if (CooldownEffectInstance.IsValid())
	{
		return CooldownEffectInstance->GetTimeRemaining();
	}

	return 0.0f;
}

void URogueGameplayAbility::PlayAnimMontageAndTrackEvent(UAnimMontage* Montage, FGameplayTag EventTag)
{
	// Play animation, track its gameplay event and anim ended event
	
	if (ACharacter* Character = Cast<ACharacter>(OwnerActionSystemComponent->GetOwner()))
	{
		if (Character->PlayAnimMontage(Montage))
		{
			EventTagToTrack = EventTag;
			AnimInstanceToTrack = Character->GetMesh()->GetAnimInstance();
			AnimInstanceToTrack->OnMontageEnded.AddDynamic(this, &URogueGameplayAbility::OnAnimMontageFinished);
			OwnerActionSystemComponent->GameplayEventReceivedDelegate.AddDynamic(this, &URogueGameplayAbility::OnAnimMontageEventReceived);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Unable to play anim montage. PlayAnimMontageAndTrackEvent() fails."));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("%s is not an ACharacter. PlayAnimMontageAndTrackEvent() fails."), *OwnerActionSystemComponent->GetOwner()->GetName());
	}
}

EDataValidationResult URogueGameplayAbility::IsDataValid(FDataValidationContext& Context) const
{
	// Cost effect rules
	if (CostEffect->DurationPolicy.GetScriptStruct() != FRogueGameplayEffectInstantApply::StaticStruct())
	{
		return EDataValidationResult::Invalid;
	}
	
	// Cooldown effect rules
	if (CooldownEffect->DurationPolicy.GetScriptStruct() != FRogueGameplayEffectDurationApply::StaticStruct())
	{
		return EDataValidationResult::Invalid;
	}
	
	return EDataValidationResult::Valid;
}
