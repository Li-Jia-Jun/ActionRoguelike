// Fill out your copyright notice in the Description page of Project Settings.


#include "SMagicProjectile.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "RogueSharedGameplayTags.h"
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
	if (OtherActor == GetInstigator())
	{
		return;
	}
	
	// Deal damage
	if (DamageTypeClass)
	{
		FVector DamageDirection = GetActorRotation().Vector();
		UGameplayStatics::ApplyPointDamage(OtherActor, 10.0, DamageDirection, Hit, GetInstigatorController(), this, DamageTypeClass);
	}
	
	if (auto ActionSystemComponent = OtherActor->FindComponentByClass<URogueActionSystemComponent>())
	{
		// Apply gameplay effects to ASC
		for(auto gameplayEffect : GameplayEffects)
		{
			URogueGameplayEffectInstance* EffectInstance = nullptr;
			ActionSystemComponent->ApplyGameplayEffectToSelf(gameplayEffect, this, true, EffectInstance);
		}
		
		// OnHit event
		FRogueGameplayEventData OnHitEventData;
		OnHitEventData.EventTag = RogueSharedGameplayTags::Event_OnHit;
		OnHitEventData.SourceObject = this;
		OnHitEventData.TargetObject = OtherActor;
		OnHitEventData.VectorValue = NormalImpluse;
		ActionSystemComponent->HandleGameplayEvent(RogueSharedGameplayTags::Event_OnHit, OnHitEventData);
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
