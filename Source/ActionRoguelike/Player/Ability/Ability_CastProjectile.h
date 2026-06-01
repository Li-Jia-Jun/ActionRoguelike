// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ActionSystem/GameplayAbility/RogueGameplayAbility.h"
#include "Ability_CastProjectile.generated.h"


class ARogueProjectileBase;
class UNiagaraSystem;
class USoundBase;
class ACharacter;
class UAnimMontage;

/**
 * Ability to cast a projectile from the character's hand.
 */
UCLASS(Blueprintable, Abstract)
class ACTIONROGUELIKE_API UAbility_CastProjectile : public URogueGameplayAbility
{
	GENERATED_BODY()
	
public:
	
	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	FName ProjectileSocketName;
	
	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	TSubclassOf<ARogueProjectileBase> ProjectileClass;
	
	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	UAnimMontage* CastingAnimMontage;
	
	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	FGameplayTag CastingAnimMontageWaitForEventTag;
	
	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	UNiagaraSystem* CastingHandEffect;
	
	UPROPERTY(EditDefaultsOnly, Category = "Projectile")
	USoundBase* CastingSound;
	
	virtual bool CheckCanBeGranted(URogueActionSystemComponent* ActionSystemComponent) const override;
	
	virtual void ActivateAbility() override;
	
	virtual bool CommitAbility() override;
	
	virtual void EndAbility() override;
	
	void CastProjectile();
	
protected:
	
	virtual FRogueGameplayAbilityEndedData ComposeEndedData() const override;
	
	virtual void OnAnimMontageFinished(UAnimMontage* Montage, bool bInterrupted) override;
	
	virtual void OnAnimMontageEventReceived(FGameplayTag EventTag, FRogueGameplayEventData EventData) override;
	
	// Per-run state
	
	void ResetState()
	{
		OwnerCharacter = nullptr;
		AnimInstance = nullptr;
		bIsAnimInterrupted = false;
		bIsCommitted = false;
	}
	
	TWeakObjectPtr<ACharacter> OwnerCharacter;
	
	UAnimInstance* AnimInstance;
	
	bool bIsAnimInterrupted;
	
	bool bIsCommitted;
};
