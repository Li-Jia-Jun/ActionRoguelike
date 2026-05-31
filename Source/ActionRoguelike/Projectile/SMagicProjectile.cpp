// Fill out your copyright notice in the Description page of Project Settings.


#include "SMagicProjectile.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "ActionSystem/RogueActionSystemComponent.h"
#include "Components/SphereComponent.h"

void ASMagicProjectile::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	CollisionSphereComp->OnComponentHit.AddDynamic(this, &ASMagicProjectile::OnActorHit);
	CollisionSphereComp->IgnoreActorWhenMoving(GetInstigator(), true);
	
	SetLifeSpan(10.f);
}


void ASMagicProjectile::OnActorHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, 
		UPrimitiveComponent* OtherComponent, FVector NormalImpluse, const FHitResult& Hit)
{
	// Deal damage
	if (DamageTypeClass)
	{
		FVector DamageDirection = GetActorRotation().Vector();
		UGameplayStatics::ApplyPointDamage(OtherActor, 10.0, DamageDirection, Hit, GetInstigatorController(), this, DamageTypeClass);
	}
	
	// Apply gameplay effects if it has an ASC
	if (auto ActionSystemComponent = OtherActor->FindComponentByClass<URogueActionSystemComponent>())
	{
		for(auto gameplayEffect : GameplayEffects)
		{
			URogueGameplayEffectInstance* EffectInstance = nullptr;
			ActionSystemComponent->ApplyGameplayEffectToSelf(gameplayEffect, this, EffectInstance);
		}
	}
	
	// On Hit effect
	if (OnHitSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, OnHitSound, GetActorLocation(), SoundMultiplier);
	}
	if (OnHitEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), OnHitEffect, GetActorLocation());
	}
	
	Destroy();
}
