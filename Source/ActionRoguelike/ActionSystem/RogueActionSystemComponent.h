// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ActionSystem/AttributeSet/RogueAttributeSet.h"
#include "ActionSystem/GameplayAbility/FRogueGameplayAbilitySpec.h"
#include "ActionSystem/GameplayAbility/FRogueGameplayAbilityEndedData.h"
#include "ActionSystem/GameplayEvent/FRogueGameplayEventData.h"
#include "RogueActionSystemComponent.generated.h"


class URogueAttributeModifierBase;
class URogueGameplayAbility;
class URogueGameplayEffect;
class URogueGameplayEffectInstance;
class UAbilitySystemComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FAttributeSetChangedSignature, FRogueAttributeSetSnapshot, OldSnapshot, FRogueAttributeSetSnapshot, NewSnapshot
	);

DECLARE_MULTICAST_DELEGATE_TwoParams(
	FAttributeSetChangedSignatureCPP, FRogueAttributeSetSnapshot, FRogueAttributeSetSnapshot
	);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FGameplayEffectChangedSignature, const TArray<FGameplayTag>&, OldTags, const TArray<FGameplayTag>&, NewTags
	);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FReceiveGameplayEventSignature, FGameplayTag, EventTag, FRogueGameplayEventData, Payload
	);


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), HideCategories=(Navigtaion, Tags, Cooking))
class ACTIONROGUELIKE_API URogueActionSystemComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URogueActionSystemComponent();
	
	virtual void BeginPlay() override;
	
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	
	void GrantActiveTag(const FGameplayTag Tag);
	
	bool RemoveActiveTag(const FGameplayTag Tag);
	
	bool HasActiveTag(const FGameplayTag Tag) const;
	
	bool GrantGameplayAbility(TSubclassOf<URogueGameplayAbility> GameplayAbilityCls, FRogueGameplayAbilitySpec& OutAbilitySpec);
	
	bool TryActivateAbilityByTag(FGameplayTag AbilityTag, URogueGameplayAbility*& OutAbility);
	
	bool StopAbilityByTag(FGameplayTag AbilityTag);
	
	bool CanApplyGameplayEffect(const URogueGameplayEffect* GameplayEffect, const UObject* Sender) const;
	
	bool ApplyGameplayEffectToSelf(const URogueGameplayEffect* GameplayEffect, 
		const UObject* Sender, bool ForceApply, URogueGameplayEffectInstance*& OutInstance);
	
	void RemoveAttributeSetChangedCallback(UObject *Object);
	
	FRogueAttributeSetSnapshot TakeAttributeSnapshot() const;
	
	UFUNCTION(BlueprintCallable)
	TArray<FGameplayTag> GetActiveGameplayEffectTags() const;

	UFUNCTION(BlueprintCallable)
	void HandleGameplayEvent(FGameplayTag EventTag, const FRogueGameplayEventData& Payload);
	
	// Delegates
	
	UPROPERTY(BlueprintAssignable, Category="GameplayEvent")
	FReceiveGameplayEventSignature GameplayEventReceivedDelegate;
	
	UPROPERTY(BlueprintAssignable, Category="GameplayEffect")
	FGameplayEffectChangedSignature GameplayEffectChangedDelegate;
	
	UPROPERTY(BlueprintAssignable, Category="AttributeSet")
	FAttributeSetChangedSignature AttributeSetChangedDelegate;
	
	FAttributeSetChangedSignatureCPP AttributeSetChangedDelegateCPP;
	
protected:
	
	bool GameplayEffectCanApplyTagCheck(const URogueGameplayEffect* GameplayEffect, FString& FailMessage) const;
	
	bool GameplayEffectCanApplyStackCheck(const URogueGameplayEffect* GameplayEffect, FString& FailMessage) const;
	
	int GetGameplayEffectStackCount(const URogueGameplayEffect* GameplayEffect) const;
	
	void ApplyGameplayEffectModifiers(const TArray<FRogueGameplayEffectModifier>& Modifiers);
	
	void ApplyGameplayEffectDebuffs(const TArray<FAttributeDebuffData>& Debuffs);
	
	void RemoveGameplayEffectDebuffs(const TArray<FAttributeDebuffData>& Debuffs);
	
	void AddActiveGameplayEffect(URogueGameplayEffectInstance* GameplayEffectInstance);
	
	bool RemoveActiveGameplayEffect(URogueGameplayEffectInstance* GameplayEffectInstance);
	
	UFUNCTION()
	void OnActiveGameplayEffectFinished(URogueGameplayEffectInstance* InGameplayEffectInstance);
	
	TArray<FGameplayTag> GetTagsFromEffectInstances(const TArray<URogueGameplayEffectInstance*>& GameplayEffectInstances);
	
	// Gameplay Tags
	
	// Active tags and their counts. Multiple data sources:
	// - GA granted tags 
	// - GE debuff tags
	TMap<FGameplayTag, int> ActiveTagCountMap;
	
	// Gameplay Ability
	
	bool FindGrantedAbility(FGameplayTag AbilityTag, FRogueGameplayAbilitySpec& OutAbilitySpec);
	
	bool ActivateAbilityBySpec(const FRogueGameplayAbilitySpec& AbilitySpec, URogueGameplayAbility*& OutAbility);
	
	UFUNCTION()
	void OnAbilityEnded(URogueGameplayAbility* Ability, const FRogueGameplayAbilityEndedData& EndedData);
	
	UFUNCTION(BlueprintCallable, Category="GameplayAbility")
	bool IsAbilityActiveByTag(FGameplayTag AbilityTag) const;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="GameplayAbility")
	TArray<FRogueGameplayAbilitySpec> GrantedAbilitySpecs;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="GameplayAbility")
	TArray<TObjectPtr<URogueGameplayAbility>> ActiveAbilities;
	
	// Gameplay Effect
	
	UPROPERTY(EditDefaultsOnly)
	TArray<TObjectPtr<URogueGameplayEffect>> StartupGameplayEffects;
	
	UPROPERTY(VisibleAnywhere, Category="GameplayEffect")
	TArray<TObjectPtr<URogueGameplayEffectInstance>> GameplayEffectInstances;
	
	friend class URogueGameplayEffectInstance;
	
	// Attribute Set
	
	void BroadcastAttributeSetChanged(const FRogueAttributeSetSnapshot& OldSnapshot, const FRogueAttributeSetSnapshot& NewSnapshot) const;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AttributeSet")
	TObjectPtr<URogueAttributeSet> AttributeSet;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AttributeSet")
	TObjectPtr<URogueAttributeSetTemplate> AttributeSetTemplate;
};
