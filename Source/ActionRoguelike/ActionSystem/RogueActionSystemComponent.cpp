// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueActionSystemComponent.h"
#include "ActionSystem/AttributeSet/RogueAttributeSet.h"
#include "ActionSystem/GameplayAbility/RogueGameplayAbility.h"
#include "ActionSystem/GameplayAbility/FRogueGameplayAbilityEndedData.h"
#include "ActionSystem/GameplayEffect/RogueGameplayEffect.h"
#include "ActionSystem/GameplayEffect/RogueGameplayEffectDurationPolicy.h"
#include "ActionSystem/GameplayEffect/RogueGameplayEffectModifyPolicy.h"
#include "ActionSystem/GameplayEffect/URogueGameplayEffectInstance.h"


URogueActionSystemComponent::URogueActionSystemComponent()
{

}

void URogueActionSystemComponent::BeginPlay()
{
	Super::BeginPlay();
	
	// Init AttributeSet by template
	if (!AttributeSetTemplate)
	{
		AttributeSet = nullptr;
		UE_LOG(LogTemp, Error, TEXT("AttributeSetTemplate is not set for %s. ActionSystemComponent will not function."), *GetOwner()->GetName());
		return;
	}
	AttributeSet = NewObject<URogueAttributeSet>(this);
	AttributeSet->InitByTemplate(AttributeSetTemplate);
	
	// Init Startup Gameplay Effects (force apply)
	for (auto Effect : StartupGameplayEffects)
	{
		URogueGameplayEffectInstance* TempInstance = nullptr;
		ApplyGameplayEffectToSelf(Effect, this, true, TempInstance);
	}
}

void URogueActionSystemComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// Manual effect instances finish
	for (auto Instance : GameplayEffectInstances)
	{
		Instance->OnFinishedDelegate.RemoveAll(this);
		Instance->Finish();
	}
	GameplayEffectInstances.Empty();
	
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

bool URogueActionSystemComponent::FindGrantedAbility(FGameplayTag AbilityTag, FRogueGameplayAbilitySpec& OutAbilitySpec)
{
	for (auto& Ability : GrantedAbilitySpecs)
	{
		if (Ability.AbilityTag.MatchesTag(AbilityTag))
		{
			OutAbilitySpec = Ability;
			return true;
		}
	}
	return false;
}

bool URogueActionSystemComponent::GrantGameplayAbility(TSubclassOf<URogueGameplayAbility> GameplayAbilityCls, FRogueGameplayAbilitySpec& OutAbilitySpec)
{
	if (!GameplayAbilityCls)
	{
		UE_LOG(LogTemp, Warning, TEXT("Empty ability class provided for %s."), *GetOwner()->GetName());
		return false;
	}
	
	URogueGameplayAbility* AbilityCDO = GameplayAbilityCls.GetDefaultObject();
	
	if (not AbilityCDO->CheckCanBeGranted(this))
	{
		UE_LOG(LogTemp, Warning, TEXT("Ability %s cannot be granted for %s."), 
			*AbilityCDO->AbilityTag.ToString(), *GetOwner()->GetName());
		return false;
	}
	
	FRogueGameplayAbilitySpec FoundAbility;
	if (FindGrantedAbility(AbilityCDO->AbilityTag, FoundAbility))
	{
		UE_LOG(LogTemp, Warning, TEXT("Ability %s is already granted for %s."), 
			*AbilityCDO->AbilityTag.ToString(), *GetOwner()->GetName());
		return false;
	}
	
	FRogueGameplayAbilitySpec AbilitySpec;
	AbilitySpec.AbilityClass = GameplayAbilityCls;
	AbilitySpec.AbilityTag = AbilityCDO->AbilityTag;
	AbilitySpec.InstancePolicy = AbilityCDO->InstancePolicy;
	AbilitySpec.OwnerActionSystemComponent = this;
	
	if (AbilitySpec.InstancePolicy == ERogueGameplayAbilityInstancePolicy::eInstancePerActor)
	{
		URogueGameplayAbility* Ability = NewObject<URogueGameplayAbility>(GetOwner(), GameplayAbilityCls);
		Ability->OwnerActionSystemComponent = this;
		AbilitySpec.SingleSpawnedInstance = Ability;
	}
	
	GrantedAbilitySpecs.Add(AbilitySpec);
	
	OutAbilitySpec = AbilitySpec;
	return true;
}

bool URogueActionSystemComponent::TryActivateAbilityByTag(FGameplayTag AbilityTag, URogueGameplayAbility*& OutAbility)
{
	FRogueGameplayAbilitySpec FoundAbility;
	if (!FindGrantedAbility(AbilityTag, FoundAbility))
	{
		UE_LOG(LogTemp, Warning, TEXT("Ability %s is not granted for %s"), 
			*AbilityTag.ToString(), *GetOwner()->GetName())
		return false;
	}
	
	// If ability is already running, skip
	for (URogueGameplayAbility* ActiveAbility : ActiveAbilities)
	{
		if (ActiveAbility->AbilityTag.MatchesTag(AbilityTag))
		{
			UE_LOG(LogTemp, Warning, TEXT("Ability %s is already running for %s"),
				*AbilityTag.ToString(), *GetOwner()->GetName());
			return false;
		}
	}
	
	if (ActivateAbilityBySpec(FoundAbility, OutAbility))
	{
		// Register end ability callback
		if (OutAbility->InstancePolicy != ERogueGameplayAbilityInstancePolicy::eNotInstanced)
		{
			OutAbility->AbilityEndedDelegate.AddUniqueDynamic(this, &URogueActionSystemComponent::OnAbilityEnded);
		}
		return true;
	}
	
	return false;
}

bool URogueActionSystemComponent::StopAbilityByTag(FGameplayTag AbilityTag)
{
	for (URogueGameplayAbility* ActiveAbility : ActiveAbilities)
	{
		if (ActiveAbility->AbilityTag.MatchesTag(AbilityTag))
		{
			ActiveAbility->EndAbility();
			return true;
		}
	}
	
	return false;
}

bool URogueActionSystemComponent::ActivateAbilityBySpec(const FRogueGameplayAbilitySpec& AbilitySpec, URogueGameplayAbility*& OutAbility)
{
	// Not instanced
	if (AbilitySpec.InstancePolicy == ERogueGameplayAbilityInstancePolicy::eNotInstanced)
	{
		URogueGameplayAbility* AbilityCDO = AbilitySpec.AbilityClass->GetDefaultObject<URogueGameplayAbility>();
		if (AbilityCDO->CanActivateAbility())
		{
			AbilityCDO->ActivateAbility();
			OutAbility = AbilityCDO;
			// Not adding to the active ability list
			return true;
		}
	}
	
	// Instanced
	if (AbilitySpec.InstancePolicy == ERogueGameplayAbilityInstancePolicy::eInstancePerActor)
	{
		OutAbility = AbilitySpec.SingleSpawnedInstance;
	}
	else if (AbilitySpec.InstancePolicy == ERogueGameplayAbilityInstancePolicy::eInstancePerExecution)
	{
		OutAbility = DuplicateObject(AbilitySpec.SingleSpawnedInstance, GetOwner());
	}
	
	if (OutAbility and OutAbility->CanActivateAbility())
	{
		OutAbility->ActivateAbility();
		ActiveAbilities.Add(OutAbility);
		return true;
	}
	
	return false;
}

void URogueActionSystemComponent::OnAbilityEnded(URogueGameplayAbility* Ability, const FRogueGameplayAbilityEndedData& EndedData)
{
	for (URogueGameplayAbility* ActiveAbility : ActiveAbilities)
	{
		if (ActiveAbility->AbilityTag.MatchesTag(Ability->AbilityTag))
		{
			ActiveAbilities.RemoveSingle(Ability);
			Ability->AbilityEndedDelegate.RemoveAll(this);
			break;
		}
	}
}

bool URogueActionSystemComponent::IsAbilityActiveByTag(FGameplayTag AbilityTag) const
{
	for (URogueGameplayAbility* ActiveAbility : ActiveAbilities)
	{
		if (ActiveAbility->AbilityTag.MatchesTag(AbilityTag))
		{
			return true;
		}
	}
	
	return false;
}

bool URogueActionSystemComponent::CanApplyGameplayEffect(const URogueGameplayEffect* GameplayEffect, const UObject* Sender) const
{
	FString FailMessage = FString::Printf(TEXT("%s Cannot cannot apply %s from %s: "), 
			*GetOwner()->GetName(), *Sender->GetName(), *GameplayEffect->GetName());
	
	// Tag check
	if (!GameplayEffectCanApplyTagCheck(GameplayEffect, FailMessage))
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *FailMessage);
		return false;
	}
	
	// Stacking check
	if (GameplayEffect->StackPolicy == ERogueGameplayEffectStackPolicy::eAccumulate and 
		!GameplayEffectCanApplyStackCheck(GameplayEffect, FailMessage))
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *FailMessage);
		return false;
	}
	
	// Permanent modify value check
	if (const FRogueGameplayEffectPermanentModify* PermanentModify = GameplayEffect->ModifyPolicy.GetPtr<FRogueGameplayEffectPermanentModify>())
	{
		if (PermanentModify->Modifiers.Num() > 0 and !AttributeSet->CanAffordModifiers(PermanentModify->Modifiers, FailMessage))
		{
			UE_LOG(LogTemp, Warning, TEXT("%s"), *FailMessage);
			return false;
		}
		
		// TODO calculation check
	}
	
	return true;
}

bool URogueActionSystemComponent::ApplyGameplayEffectToSelf(const URogueGameplayEffect* GameplayEffect, const UObject* Sender, 
	bool ForceApply, URogueGameplayEffectInstance*& OutInstance)
{
	if (!ForceApply and !CanApplyGameplayEffect(GameplayEffect, this))
	{
		return false;
	}
	
	// Early exit for instant apply
	if (GameplayEffect->DurationPolicy.GetScriptStruct() == FRogueGameplayEffectInstantApply::StaticStruct())
	{
		if (const FRogueGameplayEffectPermanentModify* PermanentModify = GameplayEffect->ModifyPolicy.GetPtr<FRogueGameplayEffectPermanentModify>())
		{
			ApplyGameplayEffectModifiers(PermanentModify->Modifiers);
			return true;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Unsupported gameplay effect type for instant apply: %s"), *GameplayEffect->GetName());
		}
	}
	
	// Create instances for all non-instant effects
	URogueGameplayEffectInstance* EffectInstance = NewObject<URogueGameplayEffectInstance>(GetOwner());
	
	// Handle stack policy
	int StackCount = 0;
	if (GameplayEffect->StackPolicy == ERogueGameplayEffectStackPolicy::eAccumulate or 
		GameplayEffect->StackPolicy == ERogueGameplayEffectStackPolicy::eIndependent)
	{
		StackCount = GetGameplayEffectStackCount(GameplayEffect);
	}
	else if (GameplayEffect->StackPolicy == ERogueGameplayEffectStackPolicy::eRefresh)
	{
		// Remove the last instance
		for (URogueGameplayEffectInstance* ExistingInstance : GameplayEffectInstances)
		{
			if (ExistingInstance->Template->EffectTag.MatchesTag(EffectInstance->Template->EffectTag))
			{
				GameplayEffectInstances.Remove(ExistingInstance);
				ExistingInstance->OnFinishedDelegate.RemoveAll(this);
				ExistingInstance->Finish();
			}
		}
	}
	
	// On finish notification so that this instance can be removed from active list
	EffectInstance->OnFinishedDelegate.AddDynamic(this, &URogueActionSystemComponent::OnActiveGameplayEffectFinished);
	
	EffectInstance->Init(GameplayEffect, this, Sender, StackCount);
	
	AddActiveGameplayEffect(EffectInstance);
	
	EffectInstance->Start();
	
	OutInstance = EffectInstance;
	
	return true;
}

void URogueActionSystemComponent::ApplyGameplayEffectModifiers(const TArray<FRogueGameplayEffectModifier>& Modifiers)
{
	FRogueAttributeSetSnapshot OldSnapshot = AttributeSet->TakeSnapshot();
	
	AttributeSet->ApplyModifiers(Modifiers);
	
	FRogueAttributeSetSnapshot NewSnapshot = AttributeSet->TakeSnapshot();
	BroadcastAttributeSetChanged(OldSnapshot, NewSnapshot);
}
	
void URogueActionSystemComponent::ApplyGameplayEffectDebuffs(const TArray<FAttributeDebuffData>& Debuffs)
{
	FRogueAttributeSetSnapshot OldSnapshot = AttributeSet->TakeSnapshot();
	
	AttributeSet->ApplyDebuffs(Debuffs);
	
	FRogueAttributeSetSnapshot NewSnapshot = AttributeSet->TakeSnapshot();
	BroadcastAttributeSetChanged(OldSnapshot, NewSnapshot);
}

void URogueActionSystemComponent::RemoveGameplayEffectDebuffs(const TArray<FAttributeDebuffData>& Debuffs)
{
	FRogueAttributeSetSnapshot OldSnapshot = AttributeSet->TakeSnapshot();
	
	AttributeSet->RemoveDebuffs(Debuffs);
	
	FRogueAttributeSetSnapshot NewSnapshot = AttributeSet->TakeSnapshot();
	BroadcastAttributeSetChanged(OldSnapshot, NewSnapshot);
}

TArray<FGameplayTag> URogueActionSystemComponent::GetTagsFromEffectInstances(const TArray<URogueGameplayEffectInstance*>& InGameplayEffectInstances)
{
	TArray<FGameplayTag> Tags;
	for (URogueGameplayEffectInstance* GameplayEffectInstance : InGameplayEffectInstances)
	{
		Tags.Add(GameplayEffectInstance->Template->EffectTag);
	}
	return Tags;
}

void URogueActionSystemComponent::AddActiveGameplayEffect(URogueGameplayEffectInstance* GameplayEffectInstance)
{
	TArray<FGameplayTag> OldTags = GetTagsFromEffectInstances(GameplayEffectInstances);
	
	GameplayEffectInstances.Add(GameplayEffectInstance);
	
	TArray<FGameplayTag> NewTags = OldTags;
	NewTags.Add(GameplayEffectInstance->Template->EffectTag);
	GameplayEffectChangedDelegate.Broadcast(OldTags, NewTags);
}

bool URogueActionSystemComponent::RemoveActiveGameplayEffect(URogueGameplayEffectInstance* GameplayEffectInstance)
{
	for (URogueGameplayEffectInstance* EffectInstance : GameplayEffectInstances)
	{
		if (EffectInstance->Matches(*GameplayEffectInstance))
		{
			TArray<FGameplayTag> OldTags = GetActiveGameplayEffectTags();
			
			// Remove effect instance from list
			GameplayEffectInstance->OnFinishedDelegate.RemoveAll(this);
			GameplayEffectInstances.Remove(GameplayEffectInstance);
			
			TArray<FGameplayTag> NewTags = OldTags;
			NewTags.Remove(GameplayEffectInstance->Template->EffectTag);
			GameplayEffectChangedDelegate.Broadcast(OldTags, NewTags);
		
			// GC
			GameplayEffectInstance->MarkAsGarbage();
		
			return true;
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("%s Failed to remove active gameplay effect instance %s because it is not in list."), 
	*GetOwner()->GetName(), *GameplayEffectInstance->Template->GetName());
	return false;
}

void URogueActionSystemComponent::OnActiveGameplayEffectFinished(URogueGameplayEffectInstance* GameplayEffectInstance)
{
	RemoveActiveGameplayEffect(GameplayEffectInstance);
}

bool URogueActionSystemComponent::GameplayEffectCanApplyTagCheck(const URogueGameplayEffect* GameplayEffect, FString& FailMessage) const
{
	if (GameplayEffect == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("GameplayEffect is nullptr."));
		return false;
	}
	
	// Pre-condition checks
	if (GameplayEffect->PreconditionEffects.Num() > 0)
	{
		for (const FGameplayTag& PreconditionTag : GameplayEffect->PreconditionEffects)
		{
			bool bHasPrecondition = false;
			for (FAttributeDebuffData& Debuff : AttributeSet->Debuffs)
			{
				if (Debuff.Tag.MatchesTag(PreconditionTag))
				{
					bHasPrecondition = true;
					break;
				}
			}
			
			if (!bHasPrecondition)
			{
				FailMessage.Append("Missing precondition " + PreconditionTag.ToString());
				return false;
			}
		}
	}
	
	// Immunity checks
	if (GameplayEffect->ImmunityEffects.Num() > 0)
	{
		for (const FGameplayTag& ImmunityTag : GameplayEffect->ImmunityEffects)
		{
			for (FAttributeDebuffData& Debuff : AttributeSet->Debuffs)
			{
				if (Debuff.Tag.MatchesTag(ImmunityTag))
				{
					FailMessage.Append("immune due to tag " + ImmunityTag.ToString());
					return false;
				}
			}
		}
	}
	
	return true;
}

bool URogueActionSystemComponent::GameplayEffectCanApplyStackCheck(const URogueGameplayEffect* GameplayEffect, FString& FailMessage) const
{
	int count = GetGameplayEffectStackCount(GameplayEffect);
	if (count >= GameplayEffect->StackLimit)
	{
		FailMessage.Append("Stack limit reached for " + GameplayEffect->EffectTag.ToString());
		UE_LOG(LogTemp, Warning, TEXT("%s"), *FailMessage);
		return false;
	}
	
	return true;
}

int URogueActionSystemComponent::GetGameplayEffectStackCount(const URogueGameplayEffect* GameplayEffect) const
{
	int count = 0;
	for (URogueGameplayEffectInstance* GameplayEffectInstance : GameplayEffectInstances)
	{
		if (GameplayEffectInstance->Template->EffectTag.MatchesTag(GameplayEffect->EffectTag))
		{
			count++;
		}
	}
	return count;
}

TArray<FGameplayTag> URogueActionSystemComponent::GetActiveGameplayEffectTags() const
{
	TArray<FGameplayTag> Tags;
	for (URogueGameplayEffectInstance* GameplayEffectInstance : GameplayEffectInstances)
	{
		Tags.Add(GameplayEffectInstance->Template->EffectTag);
	}
	return Tags;
}

void URogueActionSystemComponent::BroadcastAttributeSetChanged(const FRogueAttributeSetSnapshot& OldSnapshot, const FRogueAttributeSetSnapshot& NewSnapshot) const
{
	AttributeSetChangedDelegateCPP.Broadcast(OldSnapshot, NewSnapshot);
	AttributeSetChangedDelegate.Broadcast(OldSnapshot, NewSnapshot);
}

void URogueActionSystemComponent::RemoveAttributeSetChangedCallback(UObject *Object)
{
	AttributeSetChangedDelegateCPP.RemoveAll(Object);
}

FRogueAttributeSetSnapshot URogueActionSystemComponent::TakeAttributeSnapshot() const
{
	return AttributeSet->TakeSnapshot();
}

void URogueActionSystemComponent::HandleGameplayEvent(FGameplayTag EventTag, const FRogueGameplayEventData& Payload)
{
	GameplayEventReceivedDelegate.Broadcast(EventTag, Payload);
}
