// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "RoguePickableItemBase.generated.h"


/* 
 * Base class for items that can be picked up by characters and change its attributes
 */

class UTextRenderComponent;
class UPrimitiveComponent;
class USphereComponent;
class UStaticMeshComponent;
class USoundBase;
class URogueActionSystemComponent;
class URogueGameplayEffect;


UCLASS()
class ACTIONROGUELIKE_API ARoguePickableItemBase : public AActor
{
	GENERATED_BODY()
	
public:
	ARoguePickableItemBase();

	void Tick(float DeltaSeconds) override;
	
protected:
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComp;
	
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USphereComponent> CollisionSphereComp;
	
	UPROPERTY(EditDefaultsOnly, Category = "PickUp")
	FGameplayTag Item;
	
	UPROPERTY(EditDefaultsOnly, Category = "PickUp")
	TObjectPtr<USoundBase> PickUpSound;
	
	UPROPERTY(EditDefaultsOnly, Category = "PickUp")
	TArray<TObjectPtr<URogueGameplayEffect>> Effects;
	
	UPROPERTY(EditDefaultsOnly, Category = "Debug")
	FString DebugDisplayName;
	
	UPROPERTY(EditDefaultsOnly, Category = "Debug")
	float DebugDrawDistance = 600.0f;
	
	UPROPERTY(EditDefaultsOnly, Category = "Debug")
	float DebugOffsetHorizontal = -50.0f;
	
	UPROPERTY(EditDefaultsOnly, Category = "Debug")
	float DebugOffsetVertical = 80.0f;
	
	UFUNCTION()
	void OnActorBeginOverlap(UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, 
		bool bFromSweep, const FHitResult & SweepResult);
	
	void OnPickUp(AActor* Actor, URogueActionSystemComponent* ActionSystemComponent);
	
	bool ApplyEffects(URogueActionSystemComponent* ActionSystemComponent);
};
