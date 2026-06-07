// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability_CastProjectile.h"
#include "Projectile/RogueProjectileBase.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "ActionSystem/Interface/AbilityAttackInfoProvider.h"
#include "ActionSystem/RogueActionSystemComponent.h"



TAutoConsoleVariable<float> CVarProjectileDebugDrawing(TEXT("game.ability.CastProjectile.DebugDraw"), 0.0f,
	TEXT("Enable casting projectile debug rendering. (0 = off, > 0 is duration)"),
	ECVF_Cheat);

bool UAbility_CastProjectile::CheckCanBeGranted_Implementation(URogueActionSystemComponent* ActionSystemComponent) const
{
	AActor* OwnerActor = ActionSystemComponent->GetOwner();
	
	// Check anim montage exist
	if (not CastingAnimMontage)
	{
		UE_LOG(LogTemp, Error, TEXT("CastingAnimMontage cannot be empty."));
		return false;
	}
	
	// Check character for playing the anim montage
	ACharacter* Character = Cast<ACharacter>(OwnerActor);
	if (not Character)
	{
		UE_LOG(LogTemp, Error, TEXT("Ability_CastProjectile requires owner actor to be an ACharacter, %s is not."), 
			*OwnerActor->GetName());
		return false;
	}
	
	// Check can get attack info
	if (not Character->GetClass()->ImplementsInterface(UAbilityAttackInfoProvider::StaticClass()))
	{
		UE_LOG(LogTemp, Error, TEXT("Owner actor needs to implement IAbilityAttackInfoProvider, %s does not."), 
			*OwnerActor->GetName());
		return false;
	}
	
	return Super::CheckCanBeGranted_Implementation(ActionSystemComponent);
}

void UAbility_CastProjectile::ActivateAbility_Implementation()
{
	Super::ActivateAbility_Implementation();
	
	// Reset last run
	ResetState();
	
	// Cache info
	OwnerCharacter = Cast<ACharacter>(OwnerActionSystemComponent->GetOwner());

	// Play anim, register callback, cache anim instance
	PlayAnimMontageAndTrackEvent(CastingAnimMontage, CastingAnimMontageWaitForEventTag);
	AnimInstance = OwnerCharacter->GetMesh()->GetAnimInstance();
	
	// Play hand particle effect and attach to hand
	UNiagaraFunctionLibrary::SpawnSystemAttached(CastingHandEffect, OwnerCharacter->GetMesh(), 
		ProjectileSocketName, FVector::ZeroVector, FRotator::ZeroRotator,
		EAttachLocation::Type::SnapToTarget, true);
	
	// Play sound
	UGameplayStatics::PlaySound2D(this, CastingSound, 0.6f);
}

bool UAbility_CastProjectile::CommitAbility_Implementation()
{
	if (not Super::CommitAbility_Implementation())
	{
		return false;
	}
	
	CastProjectile();
	bIsCommitted = true;
	return true;
}

void UAbility_CastProjectile::EndAbility_Implementation()
{
	Super::EndAbility_Implementation();
	
	// Stop anim if it is running
	if (OwnerCharacter.IsValid())
	{
		if (AnimInstance && AnimInstance->Montage_IsPlaying(CastingAnimMontage))
		{
			AnimInstance->Montage_Stop(0.0f, CastingAnimMontage);
		}
	}
	
	// Reset state
	ResetState();
}

FRogueGameplayAbilityEndedData UAbility_CastProjectile::ComposeEndedData() const
{
	FRogueGameplayAbilityEndedData EndedData = Super::ComposeEndedData();
	EndedData.bIsSuccessful = not bIsAnimInterrupted;
	return EndedData;
}

void UAbility_CastProjectile::CastProjectile()
{
	auto World = GetWorld();
	
	FVector SpawnLocation = OwnerCharacter->GetMesh()->GetSocketLocation(ProjectileSocketName);
	FRotator EyeRotation = OwnerCharacter->GetControlRotation();
	
	// Adjust projectile direction with line trace
	
	FTransform AimingTransform = IAbilityAttackInfoProvider::Execute_GetAimingTransform(OwnerCharacter.Get());
	
	FVector EyeLocation = AimingTransform.GetLocation();
	FVector TraceEnd = SpawnLocation + EyeRotation.Vector() * 20000.0f;
	
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwnerCharacter.Get());
	
	FVector AdjustEndLocation;
	FHitResult HitResult;
	if (World->LineTraceSingleByChannel(HitResult, EyeLocation, TraceEnd, ECC_Visibility, QueryParams))
	{
		AdjustEndLocation = HitResult.Location;
	}
	else
	{
		AdjustEndLocation = TraceEnd;
	}
	
	FRotator AdjustedSpawnRotation = (AdjustEndLocation - SpawnLocation).Rotation();
	
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Instigator = OwnerCharacter.Get();

	World->SpawnActor<AActor>(ProjectileClass, SpawnLocation, AdjustedSpawnRotation, SpawnParams);
	
	// Debug draw
	float DebugDrawDuration = CVarProjectileDebugDrawing.GetValueOnGameThread();
	if (DebugDrawDuration > 0.0f)
	{
		DrawDebugLine(World, SpawnLocation, AdjustEndLocation, FColor::Green, false,
			DebugDrawDuration, 0, 2.0f);
		DrawDebugBox(World, AdjustEndLocation, FVector(20.0f), FColor::Green, false,
			DebugDrawDuration, 0, 1.0f);
		
		// Direction before adjust
		DrawDebugLine(World, SpawnLocation, (SpawnLocation + EyeRotation.Vector() * 20000.f), 
			FColor::Purple, false, DebugDrawDuration, 0, 2.0f);
	}
}

void UAbility_CastProjectile::OnAnimMontageFinished(UAnimMontage* Montage, bool bInterrupted)
{	
	// If interrupted, end ability
	if (bInterrupted)
	{
		bIsAnimInterrupted = true;
		EndAbility();
		return;
	}
	
	// If not getting expected gameplay event, commit and end ability
	if (!bIsCommitted)
	{
		CommitAbility();
	}
	EndAbility();
}

void UAbility_CastProjectile::OnAnimMontageEventReceived(FGameplayTag EventTag, FRogueGameplayEventData EventData)
{
	if (EventTag.MatchesTag(CastingAnimMontageWaitForEventTag))
	{
		CommitAbility();
	}
}
