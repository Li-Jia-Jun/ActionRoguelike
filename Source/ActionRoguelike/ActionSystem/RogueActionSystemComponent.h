// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AttributeSet/RogueAttributeSet.h"
#include "GameplayAbility/FRogueGameplayAbilitySpec.h"
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



UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class ACTIONROGUELIKE_API URogueActionSystemComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URogueActionSystemComponent();
	
	virtual void BeginPlay() override;
	
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	
	void GrantGameplayAbility(TSubclassOf<URogueGameplayAbility> GameplayAbilityCls);
	
	bool TryActivateAbilityByTag(FGameplayTag AbilityTag);
	
	void EndAbility(URogueGameplayAbility* Ability);
	
	bool CanApplyGameplayEffect(const URogueGameplayEffect* GameplayEffect, const UObject* Sender) const;
	
	bool ApplyGameplayEffectToSelf(const URogueGameplayEffect* GameplayEffect, 
		const UObject* Sender, URogueGameplayEffectInstance*& OutInstance);
	
	template <typename UserClass>
	void RegisterAttributeSetChangedCallback(UserClass *Object, 
		typename TMemFunPtrType<false, UserClass, void(FRogueAttributeSetSnapshot, FRogueAttributeSetSnapshot)>::Type InFunc)
	{
		AttributeSetChangedDelegateCPP.AddUObject(Object, InFunc);
	}
	
	void RemoveAttributeSetChangedCallback(UObject *Object);
	
	FRogueAttributeSetSnapshot TakeAttributeSnapshot() const;
	
	UFUNCTION(BlueprintCallable)
	TArray<FGameplayTag> GetActiveGameplayEffectTags() const;

protected:
	
	bool GameplayEffectCanApplyTagCheck(const URogueGameplayEffect* GameplayEffect, FString& FailMessage) const;
	
	bool GameplayEffectCanApplyStackCheck(const URogueGameplayEffect* GameplayEffect, FString& FailMessage) const;
	
	int GetGameplayEffectStackCount(const URogueGameplayEffect* GameplayEffect) const;
	
	void ApplyGameplayEffectModifiers(const TArray<FRogueGameplayEffectModifier>& Modifiers);
	
	void ApplyGameplayEffectDebuffs(const TArray<FAttributeDebuffData>& Debuffs);
	
	void RemoveGameplayEffectDebuffs(const TArray<FAttributeDebuffData>& Debuffs);
	
	void AddActiveGameplayEffect(URogueGameplayEffectInstance* GameplayEffectInstance);
	
	void RemoveActiveGameplayEffect(URogueGameplayEffectInstance* GameplayEffectInstance);
	
	UFUNCTION()
	void OnActiveGameplayEffectFinished(URogueGameplayEffectInstance* InGameplayEffectInstance);
	
	TArray<FGameplayTag> GetTagsFromEffectInstances(const TArray<URogueGameplayEffectInstance*>& GameplayEffectInstances);
	
	// Gameplay Ability
	
	bool FindGrantedAbility(FGameplayTag AbilityTag, FRogueGameplayAbilitySpec& OutAbility);
	bool ActivateAbilityBySpec(const FRogueGameplayAbilitySpec& AbilitySpec);
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="GameplayAbility")
	TArray<FRogueGameplayAbilitySpec> GrantedAbilities;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="GameplayAbility")
	TArray<TObjectPtr<URogueGameplayAbility>> ActiveAbilities;
	
	// Gameplay Effect
	
	UPROPERTY(VisibleAnywhere, Category="GameplayEffect")
	TArray<TObjectPtr<URogueGameplayEffectInstance>> GameplayEffectInstances;
	
	UPROPERTY(BlueprintAssignable, Category="GameplayEffect")
	FGameplayEffectChangedSignature GameplayEffectChangedDelegate;
	
	friend class URogueGameplayEffectInstance;
	
	// Attribute Set
	
	void BroadcastAttributeSetChanged(const FRogueAttributeSetSnapshot& OldSnapshot, const FRogueAttributeSetSnapshot& NewSnapshot) const;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AttributeSet")
	TObjectPtr<URogueAttributeSet> AttributeSet;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AttributeSet")
	TObjectPtr<URogueAttributeSetTemplate> AttributeSetTemplate;
	
	UPROPERTY(BlueprintAssignable, Category="AttributeSet")
	FAttributeSetChangedSignature AttributeSetChangedDelegate;
	
	FAttributeSetChangedSignatureCPP AttributeSetChangedDelegateCPP;
};
