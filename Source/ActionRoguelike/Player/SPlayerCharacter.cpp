// Fill out your copyright notice in the Description page of Project Settings.


#include "SPlayerCharacter.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "ActionSystem/RogueActionSystemComponent.h"


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

void ASPlayerCharacter::PrimaryAttack(const FInputActionValue& InValue)
{
	ActionSystemComp->TryActivateAbilityByTag(PrimaryAttackAbilitySpec.AbilityTag);
}

void ASPlayerCharacter::SecondaryAttack(const FInputActionValue& InValue)
{
	ActionSystemComp->TryActivateAbilityByTag(SecondaryAttackAbilitySpec.AbilityTag);
}

void ASPlayerCharacter::SpecialAttack(const FInputActionValue& InValue)
{
	ActionSystemComp->TryActivateAbilityByTag(SpecialAttackAbilitySpec.AbilityTag);
}

// IAbilityAttackInfoProvider
	
FTransform ASPlayerCharacter::GetAimingTransform() const
{
	return CameraComp->GetComponentTransform();
}