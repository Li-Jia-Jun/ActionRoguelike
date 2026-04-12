// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "RogueProjectileBase.h"
#include "STeleportProjectile.generated.h"

UCLASS(Abstract)
class ACTIONROGUELIKE_API ASTeleportProjectile : public ARogueProjectileBase
{
	GENERATED_BODY()

public:
	
	ASTeleportProjectile();
	
	virtual void PostInitializeComponents() override;
	
	virtual void Tick(float DeltaTime) override;
	
protected:
	UPROPERTY(EditDefaultsOnly, Category="Teleport")
	float TeleportAutoTriggerTime = 1.0f;
	float TeleportAutoTriggerTimer = -1.0f; // Count down timer. Below 0 means inactive
	
	UPROPERTY(EditDefaultsOnly, Category="Teleport")
	float TeleportDelayTime = 0.6f;
	float TeleportDelayTimer = -1.0f; 
	
	UPROPERTY(EditDefaultsOnly, Category = "Teleport")
	TObjectPtr<UNiagaraSystem> OnTeleportEffect;
	
	UPROPERTY(EditDefaultsOnly, Category = "Teleport")
	TObjectPtr<USoundBase> OnTeleportSound;
	
	UFUNCTION()
	virtual void OnActorHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, 
		UPrimitiveComponent* OtherComponent, FVector NormalImpluse, const FHitResult& Hit);
	
	void TriggerTeleport();
	
	void Teleport();
	
	FVector TeleportLocation;
};
