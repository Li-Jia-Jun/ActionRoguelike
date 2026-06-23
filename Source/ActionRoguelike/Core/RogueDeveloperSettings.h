// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RogueDeveloperSettings.generated.h"

/**
 * 
 */
UCLASS(Config=Game, DefaultConfig)
class ACTIONROGUELIKE_API URogueDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()
	
public:
	
	UPROPERTY(Config, EditDefaultsOnly, Category=CoinsPickup)
	TSoftObjectPtr<UStaticMesh> CoinMesh;
	
	UPROPERTY(Config, EditDefaultsOnly, Category=CoinsPickup)
	TSoftObjectPtr<USoundBase> CoinPickupSound;
	
	UPROPERTY(Config, EditDefaultsOnly, Category=CoinsPickup)
	FName CoinPickupTriggerParameter;
	
	virtual FName GetCategoryName() const override
	{
		return FApp::GetProjectName();
	}
};
