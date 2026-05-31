// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RogueProjectileBase.h"
#include "SMagicProjectile.generated.h"


class URogueGameplayEffect;

UCLASS(Abstract)
class ACTIONROGUELIKE_API ASMagicProjectile : public ARogueProjectileBase
{
	GENERATED_BODY()

public:	
	virtual void PostInitializeComponents() override;
	
protected:
	
	UPROPERTY(EditDefaultsOnly, Category = "OnHit")
	TSubclassOf<UDamageType> DamageTypeClass;
	
	UPROPERTY(EditDefaultsOnly, Category = "OnHit")
	TArray<TObjectPtr<URogueGameplayEffect>> GameplayEffects;
	
	UPROPERTY(EditDefaultsOnly, Category = "OnHit")
	TObjectPtr<UNiagaraSystem> OnHitEffect;

	UPROPERTY(EditDefaultsOnly, Category = "OnHit")
	TObjectPtr<USoundBase> OnHitSound;
	
	UFUNCTION()
	virtual void OnActorHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, 
		UPrimitiveComponent* OtherComponent, FVector NormalImpluse, const FHitResult& Hit);
};
