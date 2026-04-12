// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "ERogueGameplayAbilityInstancePolicy.h"
#include "RogueGameplayAbility.generated.h"

class URogueGameplayEffect;
class URogueActionSystemComponent;


UCLASS()
class ACTIONROGUELIKE_API URogueGameplayAbility : public UObject
{
	GENERATED_BODY()
	
public:
	FGameplayTag AbilityTag;
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Ability")
	TObjectPtr<URogueGameplayEffect> CostEffect;
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Ability")
	TObjectPtr<URogueGameplayEffect> CooldownEffect;
	
	UPROPERTY(EditDefaultsOnly, Category = "Rogue Gameplay Ability")
	ERogueGameplayAbilityInstancePolicy InstancePolicy;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rogue Gameplay Ability")
	TObjectPtr<URogueActionSystemComponent> OwnerActionSystemComponent;
	
	UFUNCTION(BlueprintCallable, Category = "Rogue Gameplay Ability")
	virtual bool CanActivateAbility() const;
	
	UFUNCTION(BlueprintCallable, Category = "Rogue Gameplay Ability")
	virtual void ActivateAbility();
	
	UFUNCTION(BlueprintCallable, Category = "Rogue Gameplay Ability")
	virtual bool CommitAbility();
	
	UFUNCTION(BlueprintCallable, Category = "Rogue Gameplay Ability")
	virtual void EndAbility();
	
	// TODO editor check: 
	//	- cost effect must be attribute effect, must be instant
	//  - cooldown effect must be a duration debuff effect, must be constant 
	
};
