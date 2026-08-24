// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MoverSimulationTypes.h"
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
class URogueCharacterMoverComponent;
class UCommonLegacyMovementSettings;
struct FInputActionValue;
struct FInputActionInstance;
struct FRogueHealthAttribute;
struct FMoverInputCmdContext;


UCLASS()
class ACTIONROGUELIKE_API ASPlayerCharacter : public ACharacter, public IAbilityAttackInfoProvider, public IMoverInputProducerInterface
{
	GENERATED_BODY()

public:
	ASPlayerCharacter();

	virtual void PostInitializeComponents() override;

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable)
	bool IsAlive() const;

	// IAbilityAttackInfoProvider

	virtual FTransform GetAimingTransform_Implementation() const override;
	
	// IMoverInputProducerInterface
	
	virtual void ProduceInput_Implementation(int32 SimTimeMs, FMoverInputCmdContext& InputCmdResult) override;

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

	// Base (unscaled) max speed captured from the Mover shared settings at BeginPlay.
	float OriginalMovementMaxSpeed = 0.0f;

	// Latest MovementSpeedScale attribute value, applied to Mover's MaxSpeed in RefreshMaxSpeed().
	float CurrentSpeedScale = 1.0f;

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

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> Input_Climb;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Component")
	TObjectPtr<URogueActionSystemComponent> ActionSystemComp;
	
	UPROPERTY(VisibleAnywhere, Category="Component")
	TObjectPtr<UCameraComponent> CameraComp;

	UPROPERTY(VisibleAnywhere, Category="Component")
	TObjectPtr<USpringArmComponent> SpringArmComp;

	// Drives movement via the Mover plugin (replaces the neutralized CharacterMovementComponent)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Component")
	TObjectPtr<URogueCharacterMoverComponent> MoverComp;

	// --- Cached input, consumed by ProduceInput() each simulation frame ---
	// Raw local-space move intent (X=forward, Y=right), rotated into world space at input-production time
	FVector CachedMoveInputIntent = FVector::ZeroVector;
	bool bIsJumpPressed = false;
	bool bIsJumpJustPressed = false;

	// Movement mode to hand Mover on the next input frame, used to toggle into/out of climbing. NAME_None = no request.
	FName PendingSuggestedMovementMode = NAME_None;

	void Move(const FInputActionValue& InValue);

	void MoveCompleted(const FInputActionValue& InValue);

	void Look(const FInputActionValue& InValue);

	void JumpStart(const FInputActionValue& InValue);

	void JumpStop(const FInputActionValue& InValue);

	void SprintStart(const FInputActionValue& InValue);

	void SprintStop(const FInputActionValue& InValue);

	// Toggles into climbing (when facing a climbable wall) or out of it, via Mover's SuggestedMovementMode.
	void ClimbToggle(const FInputActionValue& InValue);

	void PrimaryAttack(const FInputActionValue& InValue);
	
	void SecondaryAttack(const FInputActionValue& InValue);
	
	void SpecialAttack(const FInputActionValue& InValue);
	
	void Die();
	
	void OnAttributeSetChanged(FRogueAttributeSetSnapshot OldSnapshot, FRogueAttributeSetSnapshot NewSnapshot);
	
	UFUNCTION()
	void OnHit(FGameplayTag EventTag, FRogueGameplayEventData Payload);
	
	UCommonLegacyMovementSettings* GetMoverSettings() const;
	
	void RefreshMaxSpeed();
	
	
	bool bIsDead = false;
	
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> HitOverlayMID;
	FTimerHandle OverlayTimerHandle;
};
