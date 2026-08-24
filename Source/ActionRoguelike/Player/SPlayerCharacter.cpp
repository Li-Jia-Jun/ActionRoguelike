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
#include "ActionSystem/AttributeSet/RogueAttributeSet.h"
#include "Player/Movement/RogueCharacterMoverComponent.h"
#include "Player/Movement/RogueClimbMode.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "Backends/MoverStandaloneLiaison.h"
#include "MoverDataModelTypes.h"

ASPlayerCharacter::ASPlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	SpringArmComp = CreateDefaultSubobject<USpringArmComponent>("SpringArmComp");
	SpringArmComp->SetupAttachment(RootComponent);
	SpringArmComp->bUsePawnControlRotation = true; // Character rotates with camera controller

	CameraComp = CreateDefaultSubobject<UCameraComponent>("CameraComp");
	CameraComp->SetupAttachment(SpringArmComp);

	ActionSystemComp = CreateDefaultSubobject<URogueActionSystemComponent>("ActionSystemComp");
	
	MoverComp = CreateDefaultSubobject<URogueCharacterMoverComponent>("MoverComp");
	MoverComp->BackendClass = UMoverStandaloneLiaisonComponent::StaticClass();

	// The inherited CharacterMovementComponent is neutralized in BeginPlay so it never fights Mover for the capsule.
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
	InputComp->BindAction(Input_Move, ETriggerEvent::Completed, this, &ASPlayerCharacter::MoveCompleted);
	InputComp->BindAction(Input_Look, ETriggerEvent::Triggered, this, &ASPlayerCharacter::Look);

	InputComp->BindAction(Input_Jump, ETriggerEvent::Started, this, &ASPlayerCharacter::JumpStart);
	InputComp->BindAction(Input_Jump, ETriggerEvent::Completed, this, &ASPlayerCharacter::JumpStop);

	InputComp->BindAction(Input_Sprint, ETriggerEvent::Started, this, &ASPlayerCharacter::SprintStart);
	InputComp->BindAction(Input_Sprint, ETriggerEvent::Completed, this, &ASPlayerCharacter::SprintStop);

	InputComp->BindAction(Input_Climb, ETriggerEvent::Started, this, &ASPlayerCharacter::ClimbToggle);

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

	// Neutralize the inherited CharacterMovementComponent so it never moves the capsule; Mover owns movement now.
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->StopMovementImmediately();
		CMC->SetMovementMode(MOVE_None);
		CMC->Deactivate();
		CMC->SetComponentTickEnabled(false);
	}

	// Let Mover smooth the visual mesh (offset relative to the capsule) rather than the capsule itself.
	MoverComp->SetPrimaryVisualComponent(GetMesh());

	// Grant inborn abilities
	for (TSubclassOf<URogueGameplayAbility> AbilityCls : InbornAbilities)
	{
		FRogueGameplayAbilitySpec TempSpec;
		ActionSystemComp->GrantGameplayAbility(AbilityCls, TempSpec);
	}
	ActionSystemComp->GrantGameplayAbility(PrimaryAttackAbilityCls, PrimaryAttackAbilitySpec);
	ActionSystemComp->GrantGameplayAbility(SecondaryAttackAbilityCls, SecondaryAttackAbilitySpec);
	ActionSystemComp->GrantGameplayAbility(SpecialAttackAbilityCls, SpecialAttackAbilitySpec);
	ActionSystemComp->GrantGameplayAbility(SprintAbilityCls, SprintAbilitySpec);

	// Player character must have speed scale attribute
	FRogueAttributeSetSnapshot Snapshot = ActionSystemComp->TakeAttributeSnapshot();
	FAttributeNumericData SpeedAttribute;
	ensure(UAttributeSetFunctionLibrary::FindAttributeDataByTag(Snapshot.Attributes, MovementSpeedScaleTag, SpeedAttribute));
	ActionSystemComp->AttributeSetChangedDelegateCPP.AddUObject(this, &ThisClass::OnAttributeSetChanged);

	// Capture the base max speed, then apply the initial speed scale through the single-writer helper.
	if (UCommonLegacyMovementSettings* MoverSettings = GetMoverSettings())
	{
		OriginalMovementMaxSpeed = MoverSettings->MaxSpeed;
	}
	CurrentSpeedScale = SpeedAttribute.CurrentValue;
	RefreshMaxSpeed();

	// Player character binds OnHit event
	ActionSystemComp->GameplayEventReceivedDelegate.AddDynamic(this, &ThisClass::OnHit);
	
	// Overlap setup
	HitOverlayMID = UMaterialInstanceDynamic::Create(GetMesh()->GetOverlayMaterial(), this);
	GetMesh()->SetOverlayMaterial(HitOverlayMID);
	GetMesh()->SetOverlayMaterialMaxDrawDistance(1);
}

bool ASPlayerCharacter::IsAlive() const
{
	FRogueAttributeSetSnapshot AttributeSetSnapshot = ActionSystemComp->TakeAttributeSnapshot();
	FAttributeNumericData CurrentHealthData;
	if (UAttributeSetFunctionLibrary::FindAttributeDataByTag(AttributeSetSnapshot.Attributes, RogueSharedGameplayTags::Attribute_CurrentHealth, CurrentHealthData))
	{
		return CurrentHealthData.CurrentValue > 0;
	}
	
	return false;
}

void ASPlayerCharacter::Move(const FInputActionValue& InValue)
{
	// Cache local-space intent (X=forward, Y=right). ProduceInput() rotates it into world space and hands it to Mover.
	// Unlike CMC's AddMovementInput, Mover consumes input only at simulation time, so we must not apply it immediately.
	FVector2D Input2D = InValue.Get<FVector2D>();
	CachedMoveInputIntent.X = Input2D.X;
	CachedMoveInputIntent.Y = Input2D.Y;
	CachedMoveInputIntent.Z = 0.0f;
}

void ASPlayerCharacter::MoveCompleted(const FInputActionValue& InValue)
{
	// Clear cached intent so the character stops when input is released (Triggered stops firing but the cache would persist).
	CachedMoveInputIntent = FVector::ZeroVector;
}

void ASPlayerCharacter::Look(const FInputActionValue& InValue)
{
	FVector2D Input2D = InValue.Get<FVector2D>();

	AddControllerYawInput(Input2D.X);
	AddControllerPitchInput(Input2D.Y);
}

void ASPlayerCharacter::JumpStart(const FInputActionValue& InValue)
{
	// Feed jump state into the Mover input cmd; UCharacterMoverComponent (bHandleJump) performs the jump.
	bIsJumpJustPressed = !bIsJumpPressed;
	bIsJumpPressed = true;
}

void ASPlayerCharacter::JumpStop(const FInputActionValue& InValue)
{
	bIsJumpPressed = false;
	bIsJumpJustPressed = false;
}

void ASPlayerCharacter::SprintStart(const FInputActionValue& InValue)
{
	// Don't sprint while airborne, or while movement is suppressed (e.g. stunned drops MaxSpeed near zero).
	const UCommonLegacyMovementSettings* MoverSettings = GetMoverSettings();
	if (!MoverComp->IsOnGround() || (MoverSettings && MoverSettings->MaxSpeed < 10.0f))
	{
		return;
	}

	// Sprint is GAS-driven: the ability applies the Sprint GE, which grants State.Sprinting and boosts the
	// MovementSpeedScale attribute. That change flows through OnAttributeSetChanged -> RefreshMaxSpeed to Mover.
	URogueGameplayAbility* OutAbility = nullptr;
	if (ActionSystemComp->TryActivateAbilityByTag(SprintAbilitySpec.AbilityTag, OutAbility) && OutAbility)
	{
		OutAbility->CommitAbility_Implementation();
	}
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
		CurrentSpeedScale = NewSpeedScale;
		RefreshMaxSpeed();
	}
	
	// Death
	FAttributeNumericData CurrentHealthAttribute;
	UAttributeSetFunctionLibrary::FindAttributeDataByTag(NewSnapshot.Attributes, RogueSharedGameplayTags::Attribute_CurrentHealth, CurrentHealthAttribute);
	if (FMath::IsNearlyZero(CurrentHealthAttribute.CurrentValue))
	{
		Die();
	}
}

void ASPlayerCharacter::OnHit(FGameplayTag EventTag, FRogueGameplayEventData Payload)
{
	if (!EventTag.MatchesTag(RogueSharedGameplayTags::Event_OnHit))
	{
		return;
	}
	
	// Turn on overlay effect (0 means always draw)
	GetMesh()->SetOverlayMaterialMaxDrawDistance(0);
	HitOverlayMID->SetScalarParameterValue(FName("HitTime"), GetWorld()->TimeSeconds);

	GetWorldTimerManager().SetTimer(OverlayTimerHandle, [this]()
	{
		GetMesh()->SetOverlayMaterialMaxDrawDistance(1);
	}, 1.0f, false);
}

void ASPlayerCharacter::RefreshMaxSpeed()
{
	if (UCommonLegacyMovementSettings* MoverSettings = GetMoverSettings())
	{
		MoverSettings->MaxSpeed = OriginalMovementMaxSpeed * CurrentSpeedScale;
	}
}

// IAbilityAttackInfoProvider

FTransform ASPlayerCharacter::GetAimingTransform_Implementation() const
{
	return CameraComp->GetComponentTransform();
}

// IMoverInputProducerInterface

void ASPlayerCharacter::ProduceInput_Implementation(int32 SimTimeMs, FMoverInputCmdContext& InputCmdResult)
{
	// Authored on the game thread just before each Mover simulation frame. This is NOT re-run during rollback,
	// so only translate accumulated player input here; keep gameplay decisions inside movement modes.
	FCharacterDefaultInputs& CharacterInputs = InputCmdResult.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();

	if (GetController() == nullptr)
	{
		// Not possessed yet: contribute do-nothing input so the simulation has something valid to consume.
		CharacterInputs.SetMoveInput(EMoveInputType::DirectionalIntent, FVector::ZeroVector);
		CharacterInputs.OrientationIntent = FVector::ZeroVector;
		return;
	}

	CharacterInputs.ControlRotation = GetControlRotation();

	if (MoverComp->IsClimbing())
	{
		// While climbing, hand the mode RAW stick intent (X = up/down the wall, Y = left/right along it). The climb
		// mode maps these onto the wall plane; orientation is driven by the mode (facing the wall), not by input.
		CharacterInputs.SetMoveInput(EMoveInputType::DirectionalIntent, CachedMoveInputIntent);
		CharacterInputs.OrientationIntent = FVector::ZeroVector;
	}
	else
	{
		// On the ground/air: rotate cached local-space intent by camera yaw into world space, and orient to movement.
		FRotator ControlRotYaw = CharacterInputs.ControlRotation;
		ControlRotYaw.Pitch = 0.0f;
		ControlRotYaw.Roll = 0.0f;
		const FVector WorldMoveIntent = ControlRotYaw.RotateVector(CachedMoveInputIntent);
		CharacterInputs.SetMoveInput(EMoveInputType::DirectionalIntent, WorldMoveIntent);

		static constexpr float MoveIntentTolerance = 1e-3f;
		if (CharacterInputs.GetMoveInput().SizeSquared() > MoveIntentTolerance)
		{
			CharacterInputs.OrientationIntent = CharacterInputs.GetMoveInput().GetSafeNormal();
		}
		else
		{
			CharacterInputs.OrientationIntent = FVector::ZeroVector;
		}
	}

	CharacterInputs.bIsJumpPressed = bIsJumpPressed;
	CharacterInputs.bIsJumpJustPressed = bIsJumpJustPressed;

	// Hand off any pending climb-toggle request as a suggested movement mode, then consume it.
	CharacterInputs.SuggestedMovementMode = PendingSuggestedMovementMode;
	PendingSuggestedMovementMode = NAME_None;

	CharacterInputs.bUsingMovementBase = false;

	// Consume the one-frame jump edge so it isn't re-applied on subsequent simulation frames.
	bIsJumpJustPressed = false;
}

void ASPlayerCharacter::ClimbToggle(const FInputActionValue& InValue)
{
	if (MoverComp->IsClimbing())
	{
		// Toggle off: drop back into the air movement mode next input frame.
		PendingSuggestedMovementMode = DefaultModeNames::Falling;
	}
	else if (MoverComp->CanStartClimbing())
	{
		// Toggle on: only when the coverage grid says the wall ahead is climbable.
		PendingSuggestedMovementMode = URogueClimbMode::ModeName;
	}
}

UCommonLegacyMovementSettings* ASPlayerCharacter::GetMoverSettings() const
{
	return MoverComp ? MoverComp->FindSharedSettings_Mutable<UCommonLegacyMovementSettings>() : nullptr;
}
