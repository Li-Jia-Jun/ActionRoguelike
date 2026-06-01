// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ActionSystem/Interface/AbilityAttackInfoProvider.h"
#include "ActionSystem/GameplayAbility/FRogueGameplayAbilitySpec.h"
#include "Projectile/RogueProjectileBase.h"
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
	
	UPROPERTY(EditDefaultsOnly, Category = "Attack")
	TSubclassOf<URogueGameplayAbility> PrimaryAttackAbilityCls;
	
	UPROPERTY(EditDefaultsOnly, Category = "Attack")
	TSubclassOf<URogueGameplayAbility> SecondaryAttackAbilityCls;
	
	UPROPERTY(EditDefaultsOnly, Category = "Attack")
	TSubclassOf<URogueGameplayAbility> SpecialAttackAbilityCls;
	
	// Cache from ASC
	FRogueGameplayAbilitySpec PrimaryAttackAbilitySpec; 
	FRogueGameplayAbilitySpec SecondaryAttackAbilitySpec;
	FRogueGameplayAbilitySpec SpecialAttackAbilitySpec;
	
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
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Component")
	TObjectPtr<URogueActionSystemComponent> ActionSystemComp;
	
	UPROPERTY(VisibleAnywhere, Category="Component")
	TObjectPtr<UCameraComponent> CameraComp;

	UPROPERTY(VisibleAnywhere, Category="Component")
	TObjectPtr<USpringArmComponent> SpringArmComp;

	void Move(const FInputActionValue& InValue);

	void Look(const FInputActionValue& InValue);

	void PrimaryAttack(const FInputActionValue& InValue);
	
	void SecondaryAttack(const FInputActionValue& InValue);
	
	void SpecialAttack(const FInputActionValue& InValue);
};
