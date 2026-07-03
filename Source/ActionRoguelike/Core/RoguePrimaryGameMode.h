// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "RogueGameMode.h"
#include "AI/RogueAIEnemyDataTable.h"
#include "RoguePrimaryGameMode.generated.h"


struct FEnvQueryResult;


UCLASS()
class ACTIONROGUELIKE_API ARoguePrimaryGameMode : public ARogueGameMode
{
	GENERATED_BODY()
	
public:
	
	ARoguePrimaryGameMode();
	
	virtual void StartPlay() override;
	
	virtual void Tick(float DeltaSeconds) override;
	
protected:
	
	UPROPERTY(EditDefaultsOnly, Category="Enemy AI Spawn")
	TArray<FRogueAIEnemySpawnDirectorData> EnemySpawnDirectorData;
	
	UPROPERTY(EditDefaultsOnly, Category="Enemy AI Spawn")
	int MaxEnemyCount = 32;
	
	UPROPERTY(EditDefaultsOnly, Category="Enemy AI Spawn")
	int32 EnemySpawnSeed = 0;
	
	bool TrySpawnEnemyByDirector(FRogueAIEnemySpawnDirectorData& Director);
	
	void OnEnemySpawnEnvQueryCompleted(TSharedPtr<FEnvQueryResult> QueryResult, 
		FRogueAIEnemySpawnDirectorData* Director, FAIEnemySpawnData* SelectEnemy);
	
	void OnEnemyClassLoaded(const FSoftObjectPath& LoadedObjectPath, UObject* LoadedObject, 
		FVector SpawnLocation, FRogueAIEnemySpawnDirectorData* Director, FAIEnemySpawnData* SelectEnemy);
};
