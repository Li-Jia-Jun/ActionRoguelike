// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "RogueGameViewportClient.generated.h"

/**
 * Custom viewport client whose only job is to drive the significance manager's Update() once per frame
 * (throttled). Registered via GameViewportClientClassName in DefaultEngine.ini.
 */
UCLASS()
class ACTIONROGUELIKE_API URogueGameViewportClient : public UGameViewportClient
{
	GENERATED_BODY()

public:
	
	virtual void Tick(float DeltaTime) override;
};
