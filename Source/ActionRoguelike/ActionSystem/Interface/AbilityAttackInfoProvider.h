// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "AbilityAttackInfoProvider.generated.h"

// This class does not need to be modified.
UINTERFACE()
class UAbilityAttackInfoProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 *  Interface for the ability owner to provide attack information
 */
class ACTIONROGUELIKE_API IAbilityAttackInfoProvider
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	FTransform GetAimingTransform() const;
};
