// Fill out your copyright notice in the Description page of Project Settings.


#include "STeleportProjectile.h"
#include "Components/SphereComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"


ASTeleportProjectile::ASTeleportProjectile()
{
	TeleportAutoTriggerTimer = TeleportDelayTime;
}

void ASTeleportProjectile::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	CollisionSphereComp->OnComponentHit.AddDynamic(this, &ASTeleportProjectile::OnActorHit);
	CollisionSphereComp->IgnoreActorWhenMoving(GetInstigator(), true);
}

void ASTeleportProjectile::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	if (TeleportDelayTimer > 0.0f)
	{
		TeleportDelayTimer -= DeltaTime;
		if (TeleportDelayTimer <= 0.0f)
		{
			Teleport();
		}
	}

	if (TeleportAutoTriggerTimer > 0.0f)
	{
		TeleportAutoTriggerTimer -= DeltaTime;
		if (TeleportAutoTriggerTimer <= 0.0f)
		{
			TeleportLocation = GetActorLocation();
			TriggerTeleport();
		}
	}
}

void ASTeleportProjectile::OnActorHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, 
                                      UPrimitiveComponent* OtherComponent, FVector NormalImpluse, const FHitResult& Hit)
{
	TeleportLocation = Hit.Location;
	TriggerTeleport();
}

void ASTeleportProjectile::TriggerTeleport()
{
	// Do not trigger twice
	if (TeleportDelayTimer > 0.0f)
		return;
	
	if (OnTeleportSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, OnTeleportSound, TeleportLocation, SoundMultiplier);
	}
	
	if (OnTeleportEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), OnTeleportEffect, TeleportLocation);
	}
	
	// Start teleport count down
	TeleportDelayTimer = TeleportDelayTime;
}

void ASTeleportProjectile::Teleport()
{
	GetInstigator()->TeleportTo(TeleportLocation, GetInstigator()->GetActorRotation());
	Destroy();
}
