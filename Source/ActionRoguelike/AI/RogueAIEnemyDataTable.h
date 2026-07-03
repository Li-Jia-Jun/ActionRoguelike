#pragma once

#include "RogueAIEnemyDataTable.generated.h"


class UEnvQuery;
class ARogueAICharacter;


USTRUCT(BlueprintType)
struct FAIEnemySpawnData : public FTableRowBase
{
	GENERATED_BODY()
	
public:
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftClassPtr<ARogueAICharacter> EnemyClass;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float SpawnCost = 0.0f;
	
	UPROPERTY(EditDefaultsOnly)
	float SpawnWeight = 0.0f;
};


USTRUCT(BlueprintType)
struct FRogueAIEnemySpawnDirectorData
{
	GENERATED_BODY()
	
public:
	
	UPROPERTY(EditDefaultsOnly, Category="Spawn System")
	FName Name;
	
	UPROPERTY(EditDefaultsOnly, Category="Spawn System")
	TObjectPtr<UEnvQuery> SpawnEnvQuery;
	
	UPROPERTY(EditDefaultsOnly, Category="Spawn System")
	TObjectPtr<UDataTable> EnemySpawnData;
	
	UPROPERTY(EditDefaultsOnly, Category="Spawn System")
	FRuntimeFloatCurve CreditGainCurve;
	
	UPROPERTY(EditDefaultsOnly, Category="Spawn System")
	float TickInterval = 0.0f;
	
	UPROPERTY(EditDefaultsOnly, Category="Spawn System")
	float TimeBetweenWaves = 6.0f;
	
	float CurrentCredits = 0.0f;
	
	float NextTickTime = 0.0f;
	
	FRandomStream RandomStream_EnemySelection;
	
};