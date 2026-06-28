// Fill out your copyright notice in the Description page of Project Settings.


#include "RoguePrimaryGameMode.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "AI/RogueAICharacter.h"
#include "AI/RogueAIEnemyDataTable.h"


ARoguePrimaryGameMode::ARoguePrimaryGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 1.0f;
}

void ARoguePrimaryGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	
	// Enemy AI Spawn data reads 
	TArray<FAIEnemySpawnData*> AllRows;
	EnemySpawnData->GetAllRows("SelectMonster", AllRows);
	
	// Randomly select an enemy AI spawn data row
	int32 SelectedIndex = FMath::RandRange(0, AllRows.Num()-1);
	FAIEnemySpawnData* SelectedRow = AllRows[SelectedIndex];
	
	// Enemy AI Spawn EQS
	FQueryFinishedSignature CompleteDelegate = FQueryFinishedSignature::CreateUObject(this, 
		&ThisClass::OnEnemySpawnEnvQueryCompleted, SelectedRow);
	
	FEnvQueryRequest Request(EnemySpawnEnvQuery, this);
	Request.Execute(EEnvQueryRunMode::SingleResult, CompleteDelegate);
}

void ARoguePrimaryGameMode::OnEnemySpawnEnvQueryCompleted(TSharedPtr<FEnvQueryResult> QueryResult, FAIEnemySpawnData* SelectEnemy)
{
	FVector SpawnLocation = QueryResult->GetItemAsLocation(0);
	
	SelectEnemy->EnemyClass.LoadAsync(FLoadSoftObjectPathAsyncDelegate::CreateUObject(this, 
		&ThisClass::OnEnemyClassLoaded, SpawnLocation, SelectEnemy));
}

void ARoguePrimaryGameMode::OnEnemyClassLoaded(const FSoftObjectPath& LoadedObjectPath, UObject* LoadedObject, 
	FVector SpawnLocation, FAIEnemySpawnData* SelectEnemy)
{
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ARogueAICharacter* NewMonster = GetWorld()->SpawnActor<ARogueAICharacter>(SelectEnemy->EnemyClass.Get(), SpawnLocation, FRotator::ZeroRotator, SpawnParams);
}