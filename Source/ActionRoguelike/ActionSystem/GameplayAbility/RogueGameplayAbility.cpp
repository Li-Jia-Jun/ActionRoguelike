// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueGameplayAbility.h"
#include "GameFramework/Character.h"
#include "ActionSystem/RogueActionSystemComponent.h"
#include "ActionSystem/GameplayEffect/RogueGameplayEffectDurationPolicy.h"
#include "ActionSystem/GameplayEffect/URogueGameplayEffectInstance.h"


bool URogueGameplayAbility::CheckCanBeGranted_Implementation(URogueActionSystemComponent* ActionSystemComponent) const
{
	return true;
}

bool URogueGameplayAbility::CanActivateAbility_Implementation() const
{
	// Tags check
	for (const FGameplayTag& Tag : TagsThatBlock)
	{
		if (OwnerActionSystemComponent->IsTagActive(Tag))
		{
			UE_LOG(LogTemp, Warning, TEXT("%s cannot be activated, blocked by tag %s."), 
				*GetName(), *Tag.ToString());
			return false;
		}
	}
	if (OwnerActionSystemComponent->IsTagBlocked(AbilityTag))
	{
		UE_LOG(LogTemp, Warning, TEXT("%s cannot be activated, it is in the block list"), 
			*GetName());
		return false;
	}
	
	// Cost check 
	if (CostEffect && !OwnerActionSystemComponent->CanApplyGameplayEffect(CostEffect, this))
	{
		UE_LOG(LogTemp, Warning, TEXT("%s cannot be activated due to unable to apply cost effect %s."), 
			*GetName(), *CostEffect->GetName());
		return false;
	}
	
	// Cooldown check
	if (CooldownEffectInstance.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s cannot be activated due to existing cooldown effect instance. Time remaining = %f"), 
			*GetName(), CooldownEffectInstance->GetTimeRemaining());
		return false;
	}
	if (CooldownEffect && !OwnerActionSystemComponent->CanApplyGameplayEffect(CooldownEffect, this))
	{
		UE_LOG(LogTemp, Warning, TEXT("%s cannot be activated due to unable to apply cooldown effect %s."), 
			*GetName(), *CooldownEffect->GetName());
			return false;
	}
	
	// Active effects check
	for (const TObjectPtr<URogueGameplayEffect>& ActiveEffect : ActiveEffects)
	{
		if (!OwnerActionSystemComponent->CanApplyGameplayEffect(ActiveEffect, this))
		{
			UE_LOG(LogTemp, Warning, TEXT("%s cannot be activated due to unable to apply active effect %s."), 
				*GetName(), *ActiveEffect->GetName());
			return false;
		}
	}
	
	return true;
}

void URogueGameplayAbility::ActivateAbility_Implementation()
{
	// Status
	bIsActivated = true;
}

bool URogueGameplayAbility::CommitAbility_Implementation()
{
	if (!CanActivateAbility())
	{
		return false;
	}
	
	// Apply cost effect (instant) so there will be no instance
	if (CostEffect)
	{
		URogueGameplayEffectInstance* OutCostEffectInstance = nullptr;
		OwnerActionSystemComponent->ApplyGameplayEffectToSelf(CostEffect, this, false, OutCostEffectInstance);
	}
	
	// Apply cooldown effect and save its pointer
	if (CooldownEffect)
	{
		URogueGameplayEffectInstance* OutCooldownEffectInstance = nullptr;
		OwnerActionSystemComponent->ApplyGameplayEffectToSelf(CooldownEffect, this, false, OutCooldownEffectInstance);
		CooldownEffectInstance = OutCooldownEffectInstance;
	}
	
	// Apply active effects
	for (const TObjectPtr<URogueGameplayEffect>& ActiveEffect : ActiveEffects)
	{
		URogueGameplayEffectInstance *EffectInstance = nullptr;
		OwnerActionSystemComponent->ApplyGameplayEffectToSelf(ActiveEffect, this, false, EffectInstance);
		ActiveEffectInstances.Add(TWeakObjectPtr<URogueGameplayEffectInstance>(EffectInstance));
	}
	// Also add self tag
	OwnerActionSystemComponent->GrantActiveTag(AbilityTag);
	
	// Grant tags
	for (const FGameplayTag& Tag : TagsToGrant)
	{
		OwnerActionSystemComponent->GrantActiveTag(Tag);
	}
	
	return true;
}

void URogueGameplayAbility::EndAbility_Implementation()
{
	// Status
	bIsActivated = false;
	
	// Clean up active effects
	for (auto EffectInstance : ActiveEffectInstances)
	{
		if (EffectInstance.IsValid())
		{
			EffectInstance->Terminate();
		}
	}
	ActiveEffectInstances.Empty();
	
	// Clean up anim
	if (AnimInstanceToTrack.IsValid())
	{
		AnimInstanceToTrack->Montage_Stop(0.0f);
		AnimInstanceToTrack->OnMontageEnded.RemoveAll(this);
	}
	
	// ASC clean up
	if (OwnerActionSystemComponent)
	{
		OwnerActionSystemComponent->GameplayEventReceivedDelegate.RemoveAll(this);
		
		// Clean up granted tags
		for (const FGameplayTag& Tag : TagsToGrant)
		{
			OwnerActionSystemComponent->RemoveActiveTag(Tag);
		}
		OwnerActionSystemComponent->RemoveActiveTag(AbilityTag);
		
		// Broadcast ending (ASC OnAbilityEnded will remove it from ActiveAbilities)
		AbilityEndedDelegate.Broadcast(this, ComposeEndedData());
	}
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

#if WITH_EDITOR
EDataValidationResult URogueGameplayAbility::IsDataValid(FDataValidationContext& Context) const
{
	// Cost effect rules
	if (CostEffect && CostEffect->DurationPolicy.GetScriptStruct() != FRogueGameplayEffectInstantApply::StaticStruct())
	{
		UE_LOG(LogTemp, Error, TEXT("Cost effect requires instant apply."));
		return EDataValidationResult::Invalid;
	}
	
	// Cooldown effect rules
	if (CooldownEffect && CooldownEffect->DurationPolicy.GetScriptStruct() != FRogueGameplayEffectDurationApply::StaticStruct())
	{
		UE_LOG(LogTemp, Error, TEXT("Cooldown effect requires duration apply."));
		return EDataValidationResult::Invalid;
	}
	
	// Retrigger rule
	if (bRetriggerInstance && InstancePolicy == ERogueGameplayAbilityInstancePolicy::eNotInstanced)
	{
		UE_LOG(LogTemp, Error, TEXT("Retrigger cannot be not instanced. Please adjust InstancePolicy."));
		return EDataValidationResult::Invalid;
	}
	
	return EDataValidationResult::Valid;
}
#endif

