// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ActionSystem/Interface/AbilityAttackInfoProvider.h"
#include "ActionSystem/GameplayAbility/FRogueGameplayAbilitySpec.h"
#include "ActionSystem/AttributeSet//RogueAttributeSet.h"
#include "SPlayerCharacter.generated.h"


class URogueActionSystemComponent;
class URogueGameplayAbility;
class UNiagaraSystem;
class UCameraComponent;
class USpringArmComponent;
class UInputAction;
class UAnimMontage;
struct FInputActionValue;
struct FInputActionInstance;
struct FRogueHealthAttribute;


UCLASS()
class ACTIONROGUELIKE_API ASPlayerCharacter : public ACharacter, public IAbilityAttackInfoProvider
{
	GENERATED_BODY()
	
public:
	ASPlayerCharacter();

	virtual void PostInitializeComponents() override;
	
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	
	virtual void BeginPlay() override;
	
	// IAbilityAttackInfoProvider
	
	virtual FTransform GetAimingTransform_Implementation() const override;

protected:
	
	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	TArray<TSubclassOf<URogueGameplayAbility>> InbornAbilities;
	
	UPROPERTY(EditDefaultsOnly, Category = "Attack")
	TSubclassOf<URogueGameplayAbility> PrimaryAttackAbilityCls;
	FRogueGameplayAbilitySpec PrimaryAttackAbilitySpec; 
	
	UPROPERTY(EditDefaultsOnly, Category = "Attack")
	TSubclassOf<URogueGameplayAbility> SecondaryAttackAbilityCls;
	FRogueGameplayAbilitySpec SecondaryAttackAbilitySpec;
	
	UPROPERTY(EditDefaultsOnly, Category = "Attack")
	TSubclassOf<URogueGameplayAbility> SpecialAttackAbilityCls;
	FRogueGameplayAbilitySpec SpecialAttackAbilitySpec;
	
	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	FGameplayTag MovementSpeedScaleTag;
	
	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	TSubclassOf<URogueGameplayAbility> SprintAbilityCls;
	FRogueGameplayAbilitySpec SprintAbilitySpec;
	
	float OriginalMovementMaxSpeed = 0.0f;
	
	UPROPERTY(EditDefaultsOnly, Category="Death")
	TObjectPtr<UAnimMontage> DeathMontage;
	
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> Input_Move;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> Input_Look;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> Input_PrimaryAttack;
	
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> Input_SecondaryAttack;
	
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> Input_SpecialAttack;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> Input_Jump;
	
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> Input_Sprint;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Component")
	TObjectPtr<URogueActionSystemComponent> ActionSystemComp;
	
	UPROPERTY(VisibleAnywhere, Category="Component")
	TObjectPtr<UCameraComponent> CameraComp;

	UPROPERTY(VisibleAnywhere, Category="Component")
	TObjectPtr<USpringArmComponent> SpringArmComp;

	void Move(const FInputActionValue& InValue);

	void Look(const FInputActionValue& InValue);
	
	void SprintStart(const FInputActionValue& InValue);
	
	void SprintStop(const FInputActionValue& InValue);

	void PrimaryAttack(const FInputActionValue& InValue);
	
	void SecondaryAttack(const FInputActionValue& InValue);
	
	void SpecialAttack(const FInputActionValue& InValue);
	
	void Die();
	
	void OnAttributeSetChanged(FRogueAttributeSetSnapshot OldSnapshot, FRogueAttributeSetSnapshot NewSnapshot);
	
	UFUNCTION()
	void OnHit(FGameplayTag EventTag, FRogueGameplayEventData Payload);
	
	bool bIsDead = false;
	
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> HitOverlayMID;
	FTimerHandle OverlayTimerHandle;
};
