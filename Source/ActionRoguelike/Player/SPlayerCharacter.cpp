// Fill out your copyright notice in the Description page of Project Settings.


#include "SPlayerCharacter.h"
#include "RoguePlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "RogueSharedGameplayTags.h"
#include "ActionSystem/RogueActionSystemComponent.h"
#include "ActionSystem/GameplayAbility/RogueGameplayAbility.h"


ASPlayerCharacter::ASPlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
	
	SpringArmComp = CreateDefaultSubobject<USpringArmComponent>("SpringArmComp");
	SpringArmComp->SetupAttachment(RootComponent);
	SpringArmComp->bUsePawnControlRotation = true; // Character rotates with camera controller

	CameraComp = CreateDefaultSubobject<UCameraComponent>("CameraComp");
	CameraComp->SetupAttachment(SpringArmComp);
	
	ActionSystemComp = CreateDefaultSubobject<URogueActionSystemComponent>("ActionSystemComp");
	
	GetCharacterMovement()->bOrientRotationToMovement = true;

	bUseControllerRotationYaw = false;
}

void ASPlayerCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
}

void ASPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	UEnhancedInputComponent* InputComp = Cast<UEnhancedInputComponent>(PlayerInputComponent);

	InputComp->BindAction(Input_Move, ETriggerEvent::Triggered, this, &ASPlayerCharacter::Move);
	InputComp->BindAction(Input_Look, ETriggerEvent::Triggered, this, &ASPlayerCharacter::Look);
	InputComp->BindAction(Input_Jump, ETriggerEvent::Triggered, this, &ASPlayerCharacter::Jump);
	
	InputComp->BindAction(Input_Sprint, ETriggerEvent::Started, this, &ASPlayerCharacter::SprintStart);
	InputComp->BindAction(Input_Sprint, ETriggerEvent::Completed, this, &ASPlayerCharacter::SprintStop);
	
	InputComp->BindAction(Input_PrimaryAttack, ETriggerEvent::Triggered, this, 
		&ASPlayerCharacter::PrimaryAttack);
	
	InputComp->BindAction(Input_SecondaryAttack, ETriggerEvent::Triggered, this, 
		&ASPlayerCharacter::SecondaryAttack);
	
	InputComp->BindAction(Input_SpecialAttack, ETriggerEvent::Triggered, this, 
		&ASPlayerCharacter::SpecialAttack);
}

void ASPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	// Grant default abilities
	ActionSystemComp->GrantGameplayAbility(PrimaryAttackAbilityCls, PrimaryAttackAbilitySpec);
	ActionSystemComp->GrantGameplayAbility(SecondaryAttackAbilityCls, SecondaryAttackAbilitySpec);
	ActionSystemComp->GrantGameplayAbility(SpecialAttackAbilityCls, SpecialAttackAbilitySpec);
	ActionSystemComp->GrantGameplayAbility(SprintAbilityCls, SprintAbilitySpec);
	
	// Player character must have speed scale attribute
	FRogueAttributeSetSnapshot Snapshot = ActionSystemComp->TakeAttributeSnapshot();
	FAttributeNumericData SpeedAttribute;
	ensure(UAttributeSetFunctionLibrary::FindAttributeDataByTag(Snapshot.Attributes, MovementSpeedScaleTag, SpeedAttribute));
	ActionSystemComp->AttributeSetChangedDelegateCPP.AddUObject(this, &ThisClass::OnAttributeSetChanged);
	
	// Apply initial speed scale
	OriginalMovementMaxSpeed = GetCharacterMovement()->MaxWalkSpeed;
	GetCharacterMovement()->MaxWalkSpeed = OriginalMovementMaxSpeed * SpeedAttribute.CurrentValue;
}

void ASPlayerCharacter::Move(const FInputActionValue& InValue)
{
	FVector2D Input2D = InValue.Get<FVector2D>();
	
	// Get camera controller's yaw (left, right)
	FRotator ControlRot = GetControlRotation();
	ControlRot.Pitch = 0.0f;
	ControlRot.Roll = 0.0f;

	// Forward & Backward with controller pointing direction
	AddMovementInput(ControlRot.Vector(), Input2D.X);

	// Sidewalk with controller left right direction
	AddMovementInput(ControlRot.RotateVector(FVector::RightVector), Input2D.Y);
}

void ASPlayerCharacter::Look(const FInputActionValue& InValue)
{
	FVector2D Input2D = InValue.Get<FVector2D>();

	AddControllerYawInput(Input2D.X);
	AddControllerPitchInput(Input2D.Y);
}

void ASPlayerCharacter::SprintStart(const FInputActionValue& InValue)
{
	if (!GetCharacterMovement()->IsWalking() or GetCharacterMovement()->MaxWalkSpeed < 10.0f)
	{
		return;
	}
	
	URogueGameplayAbility* OutAbility = nullptr;
	ActionSystemComp->TryActivateAbilityByTag(SprintAbilitySpec.AbilityTag, OutAbility);
	OutAbility->CommitAbility_Implementation();
}

void ASPlayerCharacter::SprintStop(const FInputActionValue& InValue)
{
	ActionSystemComp->StopAbilityByTag(SprintAbilitySpec.AbilityTag);
}

void ASPlayerCharacter::PrimaryAttack(const FInputActionValue& InValue)
{
	URogueGameplayAbility* OutAbility = nullptr;
	ActionSystemComp->TryActivateAbilityByTag(PrimaryAttackAbilitySpec.AbilityTag, OutAbility);
}

void ASPlayerCharacter::SecondaryAttack(const FInputActionValue& InValue)
{
	URogueGameplayAbility* OutAbility = nullptr;
	ActionSystemComp->TryActivateAbilityByTag(SecondaryAttackAbilitySpec.AbilityTag, OutAbility);
}

void ASPlayerCharacter::SpecialAttack(const FInputActionValue& InValue)
{
	URogueGameplayAbility* OutAbility = nullptr;
	ActionSystemComp->TryActivateAbilityByTag(SpecialAttackAbilitySpec.AbilityTag, OutAbility);
}

void ASPlayerCharacter::Die()
{
	bIsDead = true;
	
	PlayAnimMontage(DeathMontage);
	GetMesh()->SetSimulatePhysics(false);
	
	ActionSystemComp->RemoveAttributeSetChangedCallback(this);
	
	if (ARoguePlayerController* PC = Cast<ARoguePlayerController>(GetController()))
	{
		PC->HandlePlayerDeath();
	}
}

void ASPlayerCharacter::OnAttributeSetChanged(FRogueAttributeSetSnapshot OldSnapshot, FRogueAttributeSetSnapshot NewSnapshot)
{
	// Update movement speed
	FAttributeNumericData SpeedScaleAttribute;
	UAttributeSetFunctionLibrary::FindAttributeDataByTag(OldSnapshot.Attributes, MovementSpeedScaleTag, SpeedScaleAttribute);
	float OldSpeedScale = SpeedScaleAttribute.CurrentValue;
	UAttributeSetFunctionLibrary::FindAttributeDataByTag(NewSnapshot.Attributes, MovementSpeedScaleTag, SpeedScaleAttribute);
	float NewSpeedScale = SpeedScaleAttribute.CurrentValue;
	if (!FMath::IsNearlyEqual(OldSpeedScale, NewSpeedScale))
	{
		GetCharacterMovement()->MaxWalkSpeed = OriginalMovementMaxSpeed * NewSpeedScale;
	}
	
	// Death
	FAttributeNumericData CurrentHealthAttribute;
	UAttributeSetFunctionLibrary::FindAttributeDataByTag(NewSnapshot.Attributes, RogueSharedGameplayTags::Attribute_CurrentHealth, CurrentHealthAttribute);
	if (FMath::IsNearlyZero(CurrentHealthAttribute.CurrentValue))
	{
		Die();
	}
}

// IAbilityAttackInfoProvider
	
FTransform ASPlayerCharacter::GetAimingTransform_Implementation() const
{
	return CameraComp->GetComponentTransform();
}