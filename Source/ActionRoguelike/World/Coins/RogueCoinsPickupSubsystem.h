// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RogueCoinsPickupSubsystem.generated.h"

/**
 * 
 */
UCLASS()
class ACTIONROGUELIKE_API URogueCoinsPickupSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
	
public:
	
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	
	virtual void Tick(float DeltaTime) override;
	
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(RogueCoinPickupSubsystem, STATGROUP_Tickables);
	}
	
	UFUNCTION(BlueprintCallable)
	void AddCoins(TArray<FVector> Locations, TArray<int32> Points);
	
protected:
	
	void RemoveCoin(int32 CoinIndex);
	
	void PlayPickupSound() const;
	
	TArray<FVector> CoinLocations;
	TArray<int32> CoinPoints;
	TArray<FPrimitiveInstanceId> MeshIDs;
	
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> WorldISM;
	
	UPROPERTY()
	TObjectPtr<UAudioComponent> PickupAudioComp;
	
	FName PickupAudioTriggerParamName;
};
