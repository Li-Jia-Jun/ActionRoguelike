// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueGameInstance.h"


void URogueGameInstance::OnWorldChanged(UWorld* OldWorld, UWorld* NewWorld)
{
	Super::OnWorldChanged(OldWorld, NewWorld);

	ensure(AliveAIEnemies.Num() == 0);
}