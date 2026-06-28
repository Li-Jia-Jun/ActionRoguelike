// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueEnvQueryContext_AlivePlayers.h"
#include "Player/SPlayerCharacter.h"
#include "EngineUtils.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"
#include "EnvironmentQuery/EnvQueryTypes.h"

void URogueEnvQueryContext_AlivePlayers::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	Super::ProvideContext(QueryInstance, ContextData);
	
	TArray<AActor*> AlivePlayers;
	for (ASPlayerCharacter* Player : TActorRange<ASPlayerCharacter>(QueryInstance.World))
	{
		if (Player->IsAlive())
		{
			AlivePlayers.Add(Player);
		}
	}
	
	UEnvQueryItemType_Actor::SetContextHelper(ContextData, AlivePlayers);
}
