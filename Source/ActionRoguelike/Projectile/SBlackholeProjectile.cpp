// Fill out your copyright notice in the Description page of Project Settings.


#include "SBlackholeProjectile.h"

#include "NiagaraComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "PhysicsEngine/RadialForceComponent.h"

ASBlackholeProjectile::ASBlackholeProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	
	RadialForceComp = CreateDefaultSubobject<URadialForceComponent>(FName("RadialForceComp"));
	RadialForceComp->ForceStrength = -20000.0f;
	RadialForceComp->bIgnoreOwningActor = true;
	RadialForceComp->Radius = 800.0f;
	RadialForceComp->SetActive(true);
	RadialForceComp->SetupAttachment(RootComponent);
	
	CollisionSphereComp->SetSphereRadius(30.0f);
	
	MovementComp->InitialSpeed = 300.0f;
	
	// Auto destroy when black hole effect finishes
	FlyingEffectComp->OnSystemFinished.AddDynamic(this, &ASBlackholeProjectile::OnBlackholeEffectComplete);
}

void ASBlackholeProjectile::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	CollisionSphereComp->OnComponentBeginOverlap.AddDynamic(this, &ASBlackholeProjectile::OnActorBeginOverlap);
}

void ASBlackholeProjectile::OnActorBeginOverlap(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, 
	bool bFromSweep, const FHitResult & SweepResult)
{
	if (OtherActor == this->GetInstigator())
		return;
	
	if (OtherComp->GetCollisionObjectType() == ECollisionChannel::ECC_WorldStatic)
		return;
	
	if (OtherComp->IsSimulatingPhysics() == false)
		return;
	
	// Destroy objects touching the black hole
	OtherActor->Destroy();
}

void ASBlackholeProjectile::OnBlackholeEffectComplete(UNiagaraComponent* comp)
{
	Destroy();
}
