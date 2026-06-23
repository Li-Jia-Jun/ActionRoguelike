// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "GameplayTagContainer.h"
#include "ERogueGameplayAbilityInstancePolicy.h"
#include "ActionSystem/GameplayEvent/FRogueGameplayEventData.h"
#include "FRogueGameplayAbilityEndedData.h"
#include "RogueGameplayAbility.generated.h"

class URogueGameplayAbility;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FAbilityEndedSignature, URogueGameplayAbility*, Ability, const FRogueGameplayAbilityEndedData&, EndedData
);

class UAnimMontage;
class URogueGameplayEffect;
class URogueActionSystemComponent;
class URogueGameplayEffectInstance;

UCLASS(Blueprintable, Abstract)
class ACTIONROGUELIKE_API URogueGameplayAbility : public UObject
{
	GENERATED_BODY()
	
public:
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rogue Gameplay Ability")
	TObjectPtr<URogueActionSystemComponent> OwnerActionSystemComponent;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rogue Gameplay Ability")
	FGameplayTag AbilityTag;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rogue Gameplay Ability")
	FGameplayTagContainer TagsToGrant;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rogue Gameplay Ability")
	FGameplayTagContainer TagsThatBlock;
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Ability")
	TObjectPtr<URogueGameplayEffect> CostEffect;
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Ability")
	TObjectPtr<URogueGameplayEffect> CooldownEffect;
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Ability",
		meta = (ToolTip = "Effects applied during active. Must be debuff modify policy and inifite duration policy"))
	TArray<TObjectPtr<URogueGameplayEffect>> ActiveEffects;
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Ability",
		meta = (ToolTip = "If true, will retrigger ability instance when activated"))
	ERogueGameplayAbilityInstancePolicy InstancePolicy;
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Ability")
	bool bRetriggerInstance = false;
	
	UPROPERTY(BlueprintCallable, Category = "Rogue Gameplay Ability")
	FAbilityEndedSignature AbilityEndedDelegate;
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Rogue Gameplay Ability")
	bool CheckCanBeGranted(URogueActionSystemComponent* ActionSystemComponent) const;
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Rogue Gameplay Ability")
	bool CanActivateAbility() const;
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Rogue Gameplay Ability")
	void ActivateAbility();
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Rogue Gameplay Ability")
	bool CommitAbility();
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Rogue Gameplay Ability")
	void EndAbility();
	
	UFUNCTION(BlueprintCallable, BlueprintCallable, Category = "Rogue Gameplay Ability")
	float CooldownTimeRemaining() const;
	
	bool operator==(const URogueGameplayAbility& Other) const
	{
		return AbilityTag.MatchesTag(Other.AbilityTag);
	}
	
	bool Matches(const FGameplayTag& OtherTag) const
	{
		return AbilityTag.MatchesTag(OtherTag);
	}

protected:
	
	virtual FRogueGameplayAbilityEndedData ComposeEndedData() const;
	
	// Status
	
	UPROPERTY(VisibleAnywhere, Category = "Rogue Gameplay Ability")
	bool bIsActivated;
	
	// Animation Montage
	
	void PlayAnimMontageAndTrackEvent(UAnimMontage* Montage, FGameplayTag EventTag);
	
	UFUNCTION()
	virtual void OnAnimMontageFinished(UAnimMontage* Montage, bool bInterrupted)
	PURE_VIRTUAL(URogueGameplayAbility::OnAnimMontageFinished, );
	
	UFUNCTION()
	virtual void OnAnimMontageEventReceived(FGameplayTag EventTag, FRogueGameplayEventData EventData) 
	PURE_VIRTUAL(URogueGameplayAbility::OnAnimMontageEventReceived, );
	
	TWeakObjectPtr<URogueGameplayEffectInstance> CooldownEffectInstance;
	
	TArray<TWeakObjectPtr<URogueGameplayEffectInstance>> ActiveEffectInstances;
	
	TWeakObjectPtr<UAnimInstance> AnimInstanceToTrack;
	FGameplayTag EventTagToTrack;
	
	friend class URogueActionSystemComponent;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
