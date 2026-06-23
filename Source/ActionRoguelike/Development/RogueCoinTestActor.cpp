// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueCoinTestActor.h"
#include "NavigationSystem.h"
#include "World/Coins/RogueCoinsPickupSubsystem.h"

ARogueCoinTestActor::ARogueCoinTestActor()
{
	SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComponent"));
	RootComponent = SceneComponent;
}


void ARogueCoinTestActor::SpawnCoins(int32 SpawnCount)
{
	TArray<FVector> CoinLocations;
	TArray<int32> CoinPoints;

	UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetNavigationSystem(this);
	FVector ActorLocation = GetActorLocation();

	for (int i = 0; i < SpawnCount; ++i)
	{
		FNavLocation NavLocation;
		NavSystem->GetRandomPointInNavigableRadius(ActorLocation, 1024, NavLocation);

		CoinLocations.Add(NavLocation.Location);
		CoinPoints.Add(10);
	}

	URogueCoinsPickupSubsystem* CoinSystem = GetWorld()->GetSubsystem<URogueCoinsPickupSubsystem>();

	CoinSystem->AddCoins(CoinLocations, CoinPoints);
}