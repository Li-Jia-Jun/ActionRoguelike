// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueGameViewportClient.h"
#include "SignificanceManager.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"



void URogueGameViewportClient::Tick(float DeltaTime)
{
	// Must run first: the base tick drives input/rendering plumbing.
	Super::Tick(DeltaTime);
	
	if (World == nullptr)
	{
		return;
	}
	
	if (GameInstance == nullptr)
	{
		return;
	}

	USignificanceManager* SignificanceManager = USignificanceManager::Get(World);
	if (SignificanceManager == nullptr)
	{
		return;
	}

	// Gather one viewpoint per local player from their current camera POV.
	TArray<FTransform> Viewpoints;
	const TArray<ULocalPlayer*>& LocalPlayers = GameInstance->GetLocalPlayers();
	Viewpoints.Reserve(LocalPlayers.Num());

	for (const ULocalPlayer* LocalPlayer : LocalPlayers)
	{
		if (LocalPlayer == nullptr)
		{
			continue;
		}

		if (APlayerController* PlayerController = LocalPlayer->GetPlayerController(World))
		{
			FVector ViewLocation;
			FRotator ViewRotation;
			PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
			Viewpoints.Emplace(ViewRotation, ViewLocation);
		}
	}

	// Nothing to measure significance against this frame.
	if (Viewpoints.Num() == 0)
	{
		return;
	}

	SignificanceManager->Update(Viewpoints);
}
