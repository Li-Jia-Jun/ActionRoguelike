// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueActionSystemComponent.h"
#include "RogueLog.h"
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
		UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: Startup GE applied: %s."), 
			*GetOwner()->GetName(), *Effect->GetName());
	}
}

void URogueActionSystemComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// Manual effect instances finish
	for (auto Instance : GameplayEffectInstances)
	{
		if (Instance)
		{
			Instance->OnFinishedDelegate.RemoveAll(this);
			Instance->Finish();
		
			UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: clean up GE instance on component destroyed: %s."), 
				*GetOwner()->GetName(), *Instance->Template->EffectTag.ToString());
		}
	}
	GameplayEffectInstances.Empty();
	
	// Clean up active abilities
	for (URogueGameplayAbility* Ability : ActiveAbilities)
	{
		if (Ability)
		{
			Ability->AbilityEndedDelegate.RemoveAll(this);
			Ability->EndAbility();
			
			UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: clean up GE instance on component destroyed: %s."), 
				*GetOwner()->GetName(), *Ability->AbilityTag.ToString());
		}
	}
	ActiveAbilities.Empty();
	
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

// Gameplay Tags

void URogueActionSystemComponent::GrantActiveTag(const FGameplayTag Tag)
{
	if (!ActiveTagCountMap.Contains(Tag))
	{
		ActiveTagCountMap.Add(Tag, 1);
	}
	else
	{
		ActiveTagCountMap[Tag] += 1;
	}
			
	UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: grant active tag: %s, new count = %d."), 
		*GetOwner()->GetName(), *Tag.ToString(), ActiveTagCountMap[Tag]);
}
	
bool URogueActionSystemComponent::RemoveActiveTag(const FGameplayTag Tag)
{
	if (ActiveTagCountMap.Contains(Tag))
	{
		int OldCount = ActiveTagCountMap[Tag];
		
		ActiveTagCountMap[Tag] -= 1;
		
		UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: remove active tag: %s, new count = %d."), 
			*GetOwner()->GetName(), *Tag.ToString(), ActiveTagCountMap[Tag]);
		
		if (ActiveTagCountMap[Tag] <= 0)
		{
			ActiveTagCountMap.Remove(Tag);
			
			UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: remove active tag from map as count <= 0: %s."), 
				*GetOwner()->GetName(), *Tag.ToString());
		}
		
		return true;
	}
	else
	{
		return false;
	}
}

bool URogueActionSystemComponent::IsTagActive(const FGameplayTag Tag) const
{
	for (const TTuple<FGameplayTag, int>& Pair : ActiveTagCountMap)
	{
		if (Tag.MatchesTag(Pair.Key))
		{
			UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: is tag %s active: true."), 
				*GetOwner()->GetName(), *Tag.ToString());
			return true;
		}
	}
	
	UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: is tag %s active: false."), 
		*GetOwner()->GetName(), *Tag.ToString());
	
	return false;
}

void URogueActionSystemComponent::GrantBlockTag(const FGameplayTag Tag)
{
	if (!BlockedTagCountMap.Contains(Tag))
	{
		BlockedTagCountMap.Add(Tag, 1);
	}
	else
	{
		BlockedTagCountMap[Tag] += 1;
	}
	
	UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: grant block tag: %s, new count = %d."), 
		*GetOwner()->GetName(), *Tag.ToString(), BlockedTagCountMap[Tag]);
}

bool URogueActionSystemComponent::RemoveBlockTag(const FGameplayTag Tag)
{
	if (BlockedTagCountMap.Contains(Tag))
	{
		BlockedTagCountMap[Tag] -= 1;
		
		UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: remove block tag: %s, new count = %d."), 
			*GetOwner()->GetName(), *Tag.ToString(), BlockedTagCountMap[Tag]);
		
		if (BlockedTagCountMap[Tag] <= 0)
		{
			BlockedTagCountMap.Remove(Tag);
			
			UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: remove block tag %s from count map as count <= 0."), 
				*GetOwner()->GetName(), *Tag.ToString());
		}
		
		return true;
	}
	else
	{
		return false;
	}
}

bool URogueActionSystemComponent::IsTagBlocked(const FGameplayTag Tag) const
{
	for (const TTuple<FGameplayTag, int>& Pair : BlockedTagCountMap)
	{
		if (Tag.MatchesTag(Pair.Key))
		{
			UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: is tag %s blocked: true."), 
				*GetOwner()->GetName(), *Tag.ToString());
			
			return true;
		}
	}
	
	UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: is tag %s blocked: false."), 
		*GetOwner()->GetName(), *Tag.ToString());
	
	return false;
}

// Ability

bool URogueActionSystemComponent::FindGrantedAbility(FGameplayTag AbilityTag, FRogueGameplayAbilitySpec& OutAbilitySpec)
{
	for (auto& Ability : GrantedAbilitySpecs)
	{
		if (Ability.AbilityTag.MatchesTag(AbilityTag))
		{
			OutAbilitySpec = Ability;
			
			UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: found granted ability %s."), 
				*GetOwner()->GetName(), *Ability.AbilityTag.ToString());
			
			return true;
		}
	}

	UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: did not find granted ability %s."), 
		*GetOwner()->GetName(), *AbilityTag.ToString());

	return false;
}

bool URogueActionSystemComponent::GrantGameplayAbility(TSubclassOf<URogueGameplayAbility> GameplayAbilityCls, FRogueGameplayAbilitySpec& OutAbilitySpec)
{
	if (!GameplayAbilityCls)
	{
		return false;
	}
	
	URogueGameplayAbility* AbilityCDO = GameplayAbilityCls.GetDefaultObject();
	
	if (not AbilityCDO->CheckCanBeGranted(this))
	{
		UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: Ability %s cannot be granted."), 
			*GetOwner()->GetName(), *AbilityCDO->AbilityTag.ToString());
		return false;
	}
	
	FRogueGameplayAbilitySpec FoundAbility;
	if (FindGrantedAbility(AbilityCDO->AbilityTag, FoundAbility))
	{
		UE_LOG(LogRogueGAS, Warning, TEXT("%s ASC: Ability %s is already granted."), 
			*GetOwner()->GetName(), *AbilityCDO->AbilityTag.ToString());
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
	
	UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: Ability %s is granted."), 
		*GetOwner()->GetName(), *AbilityCDO->AbilityTag.ToString());
	
	return true;
}

bool URogueActionSystemComponent::TryActivateAbilityByTag(FGameplayTag AbilityTag, URogueGameplayAbility*& OutAbility)
{
	FRogueGameplayAbilitySpec FoundAbilitySpec;
	if (!FindGrantedAbility(AbilityTag, FoundAbilitySpec))
	{
		UE_LOG(LogRogueGAS, Warning, TEXT("%s ASC: Ability %s is not granted. Try activate failed."), 
			*GetOwner()->GetName(), *AbilityTag.ToString());
		return false;
	}
	
	// Check running abilities
	for (URogueGameplayAbility* ActiveAbility : ActiveAbilities)
	{
		if (ActiveAbility->AbilityTag.MatchesTag(AbilityTag))
		{
			if (ActiveAbility->bRetriggerInstance)
			{
				// Handle ability retrigger
				if (ActiveAbility->CanActivateAbility())
				{
					UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: Found ability %s existing instance. End this instance for later retrigger."), 
						*GetOwner()->GetName(), *AbilityTag.ToString());
					ActiveAbility->EndAbility();
					break;
				}
				else
				{
					UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: Found ability %s existing instance, it is marked to be retrigger but can activate failed."), 
						*GetOwner()->GetName(), *AbilityTag.ToString());
					return false;
				}
			}
			else
			{
				// non-retrigger ability is already running, skip
				UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: Found ability %s existing instance and it is not marked as retrigger. Skip new activation."), 
					*GetOwner()->GetName(), *AbilityTag.ToString());
				return false;
			}
		}
	}
	
	if (ActivateAbilityBySpec(FoundAbilitySpec, OutAbility))
	{
		// Register end ability callback
		if (OutAbility->InstancePolicy != ERogueGameplayAbilityInstancePolicy::eNotInstanced)
		{
			OutAbility->AbilityEndedDelegate.AddUniqueDynamic(this, &URogueActionSystemComponent::OnAbilityEnded);
		}
		UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: Try activate ability %s by tag succeeds."), 
			*GetOwner()->GetName(), *AbilityTag.ToString());
		return true;
	}
	
	UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: Try activate ability %s by tag fails."), 
		*GetOwner()->GetName(), *AbilityTag.ToString());
	return false;
}

bool URogueActionSystemComponent::StopAbilityByTag(FGameplayTag AbilityTag)
{
	for (URogueGameplayAbility* ActiveAbility : ActiveAbilities)
	{
		if (ActiveAbility->AbilityTag.MatchesTag(AbilityTag))
		{
			ActiveAbility->EndAbility();
			UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: Stop ability %s by tag succeeds."), 
				*GetOwner()->GetName(), *AbilityTag.ToString());
			return true;
		}
	}
	
	UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: stop ability %s by tag fails, not found in active list"), 
		*GetOwner()->GetName(), *AbilityTag.ToString());
	return false;
}

FGameplayTagContainer URogueActionSystemComponent::GetActiveAbilityTags() const
{
	FGameplayTagContainer Tags;
	for (URogueGameplayAbility* ActiveAbility : ActiveAbilities)
	{
		Tags.AddTag(ActiveAbility->AbilityTag);
	}
	return Tags;
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
			UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: activate ability %s by spec succeeds, instance is CDO."), 
				*GetOwner()->GetName(), *OutAbility->AbilityTag.ToString());
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
		UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: activate ability %s by spec succeeds, ability instance added to active list."), 
				*GetOwner()->GetName(), *OutAbility->AbilityTag.ToString());
		return true;
	}
	
	UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: activate ability %s by spec fails."), 
		*GetOwner()->GetName(), *OutAbility->AbilityTag.ToString());
	return false;
}

void URogueActionSystemComponent::OnAbilityEnded(URogueGameplayAbility* Ability, const FRogueGameplayAbilityEndedData& EndedData)
{
	// Remove ability from active list
	for (URogueGameplayAbility* ActiveAbility : ActiveAbilities)
	{
		if (ActiveAbility->AbilityTag.MatchesTag(Ability->AbilityTag))
		{
			ActiveAbilities.RemoveSingle(Ability);
			Ability->AbilityEndedDelegate.RemoveAll(this);
			UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: on ability %s ended, remove it from active list."), 
				*GetOwner()->GetName(), *Ability->AbilityTag.ToString());
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

// Gameplay Effect

bool URogueActionSystemComponent::CanApplyGameplayEffect(const URogueGameplayEffect* GameplayEffect, const UObject* Sender) const
{
	if (GameplayEffect == nullptr)
	{
		UE_LOG(LogRogueGAS, Warning, TEXT("%s ASC: CanApplyGameplayEffect: empty gameplay effect from %s"), *GetOwner()->GetName(), 
			*Sender->GetName());
		return false;
	}
	
	FString FailMessage = FString::Printf(TEXT("%s cannot apply %s from %s: "), 
			*GetOwner()->GetName(), *Sender->GetName(), *GameplayEffect->GetName());
	
	// Tag check
	if (!GameplayEffectCanApplyTagCheck(GameplayEffect, FailMessage))
	{
		UE_LOG(LogRogueGAS, Verbose, TEXT("%s"), *FailMessage);
		return false;
	}
	
	// Stacking check
	if (GameplayEffect->StackPolicy == ERogueGameplayEffectStackPolicy::eAccumulate and 
		!GameplayEffectCanApplyStackCheck(GameplayEffect, FailMessage))
	{
		UE_LOG(LogRogueGAS, Verbose, TEXT("%s"), *FailMessage);
		return false;
	}
	
	// Permanent modify value check
	if (const FRogueGameplayEffectPermanentModify* PermanentModify = GameplayEffect->ModifyPolicy.GetPtr<FRogueGameplayEffectPermanentModify>())
	{
		if (PermanentModify->Modifiers.Num() > 0 and !AttributeSet->CanAffordModifiers(PermanentModify->Modifiers, FailMessage))
		{
			UE_LOG(LogRogueGAS, Verbose, TEXT("%s"), *FailMessage);
			return false;
		}
		
		// TODO calculation check
	}
	
	return true;
}

bool URogueActionSystemComponent::ApplyGameplayEffectToSelf(const URogueGameplayEffect* GameplayEffect, const UObject* Sender, 
	bool ForceApply, URogueGameplayEffectInstance*& OutInstance)
{
	if (GameplayEffect == nullptr)
	{
		UE_LOG(LogRogueGAS, Warning, TEXT("%s ASC: Empty GE from sender %s, apply GE to self fails."), 
			*GetOwner()->GetName(), *Sender->GetName());
		return false;
	}
	
	if (!ForceApply and !CanApplyGameplayEffect(GameplayEffect, this))
	{
		UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: Apply GE %s from %s to self fails (not force apply)."), 
			*GetOwner()->GetName(), *GameplayEffect->GetName(), *Sender->GetName());
		return false;
	}
	
	// Early exit for instant apply (instant apply is guaranteed to use permanent modify)
	if (GameplayEffect->DurationPolicy.GetScriptStruct() == FRogueGameplayEffectInstantApply::StaticStruct())
	{
		const FRogueGameplayEffectPermanentModify* PermanentModify = GameplayEffect->ModifyPolicy.GetPtr<FRogueGameplayEffectPermanentModify>();
		ApplyGameplayEffectModifiers(PermanentModify->Modifiers);
		UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: Apply GE %s from %s to self succeeds, instant apply."), 
			*GetOwner()->GetName(), *GameplayEffect->GetName(), *Sender->GetName());
		return true;
	}
	
	// Create instances for all non-instant effects
	URogueGameplayEffectInstance* EffectInstance = NewObject<URogueGameplayEffectInstance>(GetOwner());
	
	// Handle stack policy
	if (GameplayEffect->StackPolicy == ERogueGameplayEffectStackPolicy::eRefresh)
	{
		// Remove the last instance
		for (URogueGameplayEffectInstance* ExistingInstance : GameplayEffectInstances)
		{
			if (ExistingInstance->Template->EffectTag.MatchesTag(GameplayEffect->EffectTag))
			{
				GameplayEffectInstances.Remove(ExistingInstance);
				ExistingInstance->OnFinishedDelegate.RemoveAll(this);
				ExistingInstance->Finish();
				break;
			}
		}
	}
	
	// On finish notification so that this instance can be removed from active list
	EffectInstance->OnFinishedDelegate.AddDynamic(this, &URogueActionSystemComponent::OnActiveGameplayEffectFinished);
	
	EffectInstance->Init(GameplayEffect, this, Sender);
	
	AddActiveGameplayEffect(EffectInstance);
	
	EffectInstance->Start();
	
	OutInstance = EffectInstance;
	
	UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: Apply GE %s from %s to self succeeds."), 
		*GetOwner()->GetName(), *GameplayEffect->GetName(), *Sender->GetName());
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
	
	// Update active tags
	for (const FAttributeDebuffData& Debuff : Debuffs)
	{
		GrantActiveTag(Debuff.Tag);
		UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: Added GE debuff tag %s to active tag list."), 
			*GetOwner()->GetName(), *Debuff.Tag.ToString());
	}
	
	FRogueAttributeSetSnapshot NewSnapshot = AttributeSet->TakeSnapshot();
	BroadcastAttributeSetChanged(OldSnapshot, NewSnapshot);
}

void URogueActionSystemComponent::RemoveGameplayEffectDebuffs(const TArray<FAttributeDebuffData>& Debuffs)
{
	FRogueAttributeSetSnapshot OldSnapshot = AttributeSet->TakeSnapshot();
	
	AttributeSet->RemoveDebuffs(Debuffs);
	
	// Update active tags
	for (const FAttributeDebuffData& Debuff : Debuffs)
	{
		RemoveActiveTag(Debuff.Tag);
		UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: Removed GE debuff tag %s from active tag list."), 
			*GetOwner()->GetName(), *Debuff.Tag.ToString());
	}
	
	FRogueAttributeSetSnapshot NewSnapshot = AttributeSet->TakeSnapshot();
	BroadcastAttributeSetChanged(OldSnapshot, NewSnapshot);
}

FGameplayTagContainer URogueActionSystemComponent::GetTagsFromEffectInstances(const TArray<URogueGameplayEffectInstance*>& InGameplayEffectInstances) const
{
	FGameplayTagContainer Tags;
	for (URogueGameplayEffectInstance* GameplayEffectInstance : InGameplayEffectInstances)
	{
		Tags.AddTag(GameplayEffectInstance->Template->EffectTag);
	}
	return Tags;
}

void URogueActionSystemComponent::AddActiveGameplayEffect(URogueGameplayEffectInstance* GameplayEffectInstance)
{
	FGameplayTagContainer OldTags = GetTagsFromEffectInstances(GameplayEffectInstances);
	
	GameplayEffectInstances.Add(GameplayEffectInstance);
	
	UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: GE instance %s added to active list."), 
		*GetOwner()->GetName(), *GameplayEffectInstance->Template->GetName());
	
	FGameplayTagContainer NewTags = OldTags;
	NewTags.AddTag(GameplayEffectInstance->Template->EffectTag);
	GameplayEffectChangedDelegate.Broadcast(OldTags, NewTags);
}

bool URogueActionSystemComponent::RemoveActiveGameplayEffect(URogueGameplayEffectInstance* GameplayEffectInstance)
{
	for (URogueGameplayEffectInstance* EffectInstance : GameplayEffectInstances)
	{
		if (EffectInstance->MatchesTag(*GameplayEffectInstance))
		{
			FGameplayTagContainer OldTags = GetActiveGameplayEffectTags();
			
			// Remove effect instance from list
			GameplayEffectInstance->OnFinishedDelegate.RemoveAll(this);
			GameplayEffectInstances.Remove(GameplayEffectInstance);
			
			UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: GE instance %s removed from active list."), 
				*GetOwner()->GetName(), *GameplayEffectInstance->Template->GetName());
			
			FGameplayTagContainer NewTags = OldTags;
			NewTags.RemoveTag(GameplayEffectInstance->Template->EffectTag);
			GameplayEffectChangedDelegate.Broadcast(OldTags, NewTags);
		
			// GC
			GameplayEffectInstance->MarkAsGarbage();
		
			return true;
		}
	}
	
	UE_LOG(LogRogueGAS, Verbose, TEXT("%s ASC: GE instance %s failed to remove from active list due to not found."), 
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
	for (const FGameplayTag& RequireTag : GameplayEffect->TagsThatRequire)
	{
		if (!IsTagActive(RequireTag))
		{
			FailMessage.Append("Missing required tag " + RequireTag.ToString());
			return false;
		}
	}
	
	// Self block tags check
	if (GameplayEffect->TagsThatBlock.Num() > 0)
	{
		for (const FGameplayTag& Tag : GameplayEffect->TagsThatBlock)
		{
			if (IsTagActive(Tag))
			{
				FailMessage.Append("blocked by tag " + Tag.ToString());
				return false;
			}
		}
	}
	
	// Other block tags check
	if (IsTagBlocked(GameplayEffect->EffectTag))
	{
		FailMessage.Append("effect tag in the block tag list");
		return false;
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

FGameplayTagContainer URogueActionSystemComponent::GetActiveGameplayEffectTags() const
{
	FGameplayTagContainer Tags;
	for (URogueGameplayEffectInstance* GameplayEffectInstance : GameplayEffectInstances)
	{
		Tags.AddTag(GameplayEffectInstance->Template->EffectTag);
	}
	return Tags;
}

// Attribute Set

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

// Gameplay Event

void URogueActionSystemComponent::HandleGameplayEvent(FGameplayTag EventTag, const FRogueGameplayEventData& Payload)
{
	GameplayEventReceivedDelegate.Broadcast(EventTag, Payload);
}
