// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueProjectileBase.h"

#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "NiagaraComponent.h"
#include "Components/AudioComponent.h"

// Sets default values
ARogueProjectileBase::ARogueProjectileBase()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	
	CollisionSphereComp = CreateDefaultSubobject<USphereComponent>(TEXT("SphereComp"));
	CollisionSphereComp->SetSphereRadius(16.0f);
	RootComponent = CollisionSphereComp;
	
	MovementComp = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("MovementComp"));
	MovementComp->InitialSpeed = 1000.0f;
	MovementComp->bRotationFollowsVelocity = true;
	MovementComp->bInitialVelocityInLocalSpace = true;
	
	FlyingEffectComp = CreateDefaultSubobject<UNiagaraComponent>(TEXT("FlyingEffectComp"));
	FlyingEffectComp->SetupAttachment(CollisionSphereComp);

	FlyingSoundComp = CreateDefaultSubobject<UAudioComponent>(TEXT("FlyingSoundComp"));
	FlyingSoundComp->SetupAttachment(CollisionSphereComp);
	FlyingSoundComp->VolumeMultiplier = SoundMultiplier;
}


