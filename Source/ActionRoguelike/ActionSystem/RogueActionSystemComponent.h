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
	FGameplayEffectChangedSignature, const FGameplayTagContainer&, OldTags, const FGameplayTagContainer&, NewTags
	);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FReceiveGameplayEventSignature, FGameplayTag, EventTag, FRogueGameplayEventData, Payload
	);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FAbilityActivationChagnedSignature, FGameplayTag, AbilityTag, bool, IsActivated	
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FActiveTagChagnedSignature, FGameplayTag, Tag, int, NewCount	
);


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), HideCategories=(Navigtaion, Tags, Cooking))
class ACTIONROGUELIKE_API URogueActionSystemComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URogueActionSystemComponent();
	
	virtual void BeginPlay() override;
	
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
	UFUNCTION(BlueprintCallable)
	void GrantActiveTag(const FGameplayTag Tag);
	
	UFUNCTION(BlueprintCallable)
	bool RemoveActiveTag(const FGameplayTag Tag);
	
	UFUNCTION(BlueprintCallable)
	bool IsTagActive(const FGameplayTag Tag) const;
	
	UFUNCTION(BlueprintCallable)
	void GrantBlockTag(const FGameplayTag Tag);
	
	UFUNCTION(BlueprintCallable)
	bool RemoveBlockTag(const FGameplayTag Tag);
	
	UFUNCTION()
	bool IsTagBlocked(const FGameplayTag Tag) const;
	
	UFUNCTION(BlueprintCallable)
	bool GrantGameplayAbility(TSubclassOf<URogueGameplayAbility> GameplayAbilityCls, FRogueGameplayAbilitySpec& OutAbilitySpec);
	
	UFUNCTION(BlueprintCallable)
	bool TryActivateAbilityByTag(FGameplayTag AbilityTag, URogueGameplayAbility*& OutAbility);
	
	UFUNCTION(BlueprintCallable)
	bool StopAbilityByTag(FGameplayTag AbilityTag);
	
	UFUNCTION(BlueprintCallable)
	FGameplayTagContainer GetActiveAbilityTags() const;
	
	UFUNCTION(BlueprintCallable)
	bool CanApplyGameplayEffect(const URogueGameplayEffect* GameplayEffect, const UObject* Sender) const;
	
	UFUNCTION(BlueprintCallable)
	bool ApplyGameplayEffectToSelf(const URogueGameplayEffect* GameplayEffect, 
		const UObject* Sender, bool ForceApply, URogueGameplayEffectInstance*& OutInstance);
	
	void RemoveAttributeSetChangedCallback(UObject *Object);
	
	FRogueAttributeSetSnapshot TakeAttributeSnapshot() const;
	
	UFUNCTION(BlueprintCallable)
	FGameplayTagContainer GetActiveGameplayEffectTags() const;

	UFUNCTION(BlueprintCallable)
	void HandleGameplayEvent(FGameplayTag EventTag, const FRogueGameplayEventData& Payload);
	
	// Delegates
	
	UPROPERTY(BlueprintAssignable, Category="Rogue Gameplay Ability")
	FAbilityActivationChagnedSignature AbilityActivationChangedDelegate;
	
	UPROPERTY(BlueprintAssignable, Category="Rogue Gameplay Tags")
	FActiveTagChagnedSignature ActiveTagChangedDelegate;
	
	UPROPERTY(BlueprintAssignable, Category="Rogue Gameplay Event")
	FReceiveGameplayEventSignature GameplayEventReceivedDelegate;
	
	UPROPERTY(BlueprintAssignable, Category="Rogue Gameplay Effect")
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
	
	FGameplayTagContainer GetTagsFromEffectInstances(const TArray<URogueGameplayEffectInstance*>& GameplayEffectInstances) const;
	
	// Gameplay Tags
	
	// Active tags and their counts. Multiple data sources:
	// - GA TagsToGrant
	// - GA AbilityTag
	// - GE TagsToGrant
	// - GE Debuff Tag
	// To find tags for active GA or GE, call GetActiveAbilityTags() or GetActiveGameplayEffectTags()
	UPROPERTY(VisibleAnywhere, Category="Rogue Gameplay Tags",
		meta=(ToolTip = "Tags that are currently active and their counts."))
	TMap<FGameplayTag, int32> ActiveTagCountMap;
	
	// Tags that are being blocked and their counts. Multiple data sources:
	// - GE TagsThisBlock
	UPROPERTY(VisibleAnywhere, Category="Rogue Gameplay Tags",
		meta=(ToolTip = "Tags that are being blocked and their counts."))
	TMap<FGameplayTag, int32> BlockedTagCountMap;
	
	void UpdateActiveAbilityOnNewBlockedTag(const FGameplayTag& NewBlockedTag);
	
	// Gameplay Ability
	
	bool FindGrantedAbility(FGameplayTag AbilityTag, FRogueGameplayAbilitySpec& OutAbilitySpec);
	
	bool ActivateAbilityBySpec(const FRogueGameplayAbilitySpec& AbilitySpec, URogueGameplayAbility*& OutAbility);
	
	UFUNCTION()
	void OnAbilityEnded(URogueGameplayAbility* Ability, const FRogueGameplayAbilityEndedData& EndedData);
	
	UFUNCTION(BlueprintCallable, Category="Rogue Gameplay Ability")
	bool IsAbilityActiveByTag(FGameplayTag AbilityTag) const;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rogue Gameplay Ability")
	TArray<FRogueGameplayAbilitySpec> GrantedAbilitySpecs;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rogue Gameplay Ability")
	TArray<TObjectPtr<URogueGameplayAbility>> ActiveAbilities;
	
	// Gameplay Effect
	
	UPROPERTY(EditDefaultsOnly)
	TArray<TObjectPtr<URogueGameplayEffect>> StartupGameplayEffects;
	
	UPROPERTY(VisibleAnywhere, Category="Rogue Gameplay Effect")
	TArray<TObjectPtr<URogueGameplayEffectInstance>> GameplayEffectInstances;
	
	friend class URogueGameplayEffectInstance;
	
	// Attribute Set
	
	void BroadcastAttributeSetChanged(const FRogueAttributeSetSnapshot& OldSnapshot, const FRogueAttributeSetSnapshot& NewSnapshot) const;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rogue AttributeSet")
	TObjectPtr<URogueAttributeSet> AttributeSet;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Rogue AttributeSet")
	TObjectPtr<URogueAttributeSetTemplate> AttributeSetTemplate;
};
