// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "RogueGameMode.h"
#include "RoguePrimaryGameMode.generated.h"

struct FAIEnemySpawnData;
struct FEnvQueryResult;
class UEnvQuery;
/**
 * 
 */
UCLASS()
class ACTIONROGUELIKE_API ARoguePrimaryGameMode : public ARogueGameMode
{
	GENERATED_BODY()
	
public:
	
	ARoguePrimaryGameMode();
	
	virtual void Tick(float DeltaSeconds) override;
	
protected:
	
	UPROPERTY(EditDefaultsOnly, Category="Enemy AI Spawn")
	TObjectPtr<UEnvQuery> EnemySpawnEnvQuery;
	
	UPROPERTY(EditDefaultsOnly, Category="Enemy AI Spawn")
	TObjectPtr<UDataTable> EnemySpawnData;
	
	void OnEnemySpawnEnvQueryCompleted(TSharedPtr<FEnvQueryResult> QueryResult, FAIEnemySpawnData* SelectEnemy);
	
	void OnEnemyClassLoaded(const FSoftObjectPath& LoadedObjectPath, UObject* LoadedObject, 
		FVector SpawnLocation, FAIEnemySpawnData* SelectEnemy);
};
