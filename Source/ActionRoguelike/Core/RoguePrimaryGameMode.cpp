// Fill out your copyright notice in the Description page of Project Settings.


#include "RoguePrimaryGameMode.h"

#include "RogueGameInstance.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "AI/RogueAICharacter.h"
#include "AI/RogueAIEnemyDataTable.h"
#include "RogueLog.h"


ARoguePrimaryGameMode::ARoguePrimaryGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.2f;
}

void ARoguePrimaryGameMode::StartPlay()
{
	Super::StartPlay();
	
	FRandomStream GlobalStream = FRandomStream(EnemySpawnSeed);
	
	// Assign random seed to each director
	for (FRogueAIEnemySpawnDirectorData& Director : EnemySpawnDirectorData)
	{
		int32 NewSeed = GlobalStream.RandRange(0, MAX_int32-1);
		Director.RandomStream_EnemySelection = FRandomStream(NewSeed);
		
		UE_LOG(LogGameMode, Log, TEXT("AI Enemy Spawn Director Seed: %d"), 
			Director.RandomStream_EnemySelection.GetInitialSeed());
	}
}

void ARoguePrimaryGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	
	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	if (PlayerController == nullptr)
	{
		return;
	}
	ACharacter* PlayerCharacter = PlayerController->GetPawn<ACharacter>();
	if (PlayerCharacter == nullptr)
	{
		return;
	}
	
	float TotalTimeElapsed = GetWorld()->GetTimeSeconds();
	
	if (bEnableEnemySpawn)
	{
		// Enemy Spawn Directors credits update
		URogueGameInstance* GI = GetGameInstance<URogueGameInstance>();
		for (int i = 0; i < EnemySpawnDirectorData.Num(); i++)
		{
			FRogueAIEnemySpawnDirectorData& Director = EnemySpawnDirectorData[i];
			if (Director.EnemySpawnData == nullptr)
			{
				continue;
			}
		
			float CreditsPerSecond = Director.CreditGainCurve.GetRichCurve()->Eval(TotalTimeElapsed);
			Director.CurrentCredits += CreditsPerSecond * DeltaSeconds;
		
			FString DebugMessage = FString::Printf(TEXT("Director %s: Total Credits {%.2f}, Next Tick Time {%.2f}"), 
				*Director.Name.ToString(), Director.CurrentCredits, Director.NextTickTime);
			GEngine->AddOnScreenDebugMessage(i, PrimaryActorTick.TickInterval, FColor::Blue, DebugMessage);
		}
	
		// Spawn enemy maximum count check
		if (GI->AliveAIEnemies.Num() >= MaxEnemyCount)
		{
			return;
		}
	
		// Enemy Spawn Directors spawn time check and spawn by waves
		for (int i = 0; i < EnemySpawnDirectorData.Num(); i++)
		{
			FRogueAIEnemySpawnDirectorData& Director = EnemySpawnDirectorData[i];
			if (Director.EnemySpawnData == nullptr)
			{
				return;
			}
		
			// Spawn time check
			if (TotalTimeElapsed < Director.NextTickTime)
			{
				continue;
			}
		
			// Spawn by waves
			if (TrySpawnEnemyByDirector(Director))
			{
				// Spawn succeeds, so next spawn will happen within the wave
				Director.NextTickTime = TotalTimeElapsed + Director.TickInterval;
			}
			else
			{
				// Spawn fails, so wait for next wave
				Director.NextTickTime = TotalTimeElapsed + Director.TimeBetweenWaves;
			}
		}
	}
}

bool ARoguePrimaryGameMode::TrySpawnEnemyByDirector(FRogueAIEnemySpawnDirectorData& Director)
{
	TArray<FAIEnemySpawnData*> AllRows;
	Director.EnemySpawnData->GetAllRows("SelectMonster", AllRows);
	
	// Select random row by seed and weights
	float TotalWeight = 0;
	for (FAIEnemySpawnData* Row : AllRows)
	{
		TotalWeight += Row->SpawnWeight;
	}
	float RandomWeight = Director.RandomStream_EnemySelection.FRandRange(0, TotalWeight);
	FAIEnemySpawnData* SelectedRow = nullptr;
	for (FAIEnemySpawnData* Row : AllRows)
	{
		RandomWeight -= Row->SpawnWeight;
		if (RandomWeight <= 0)
		{
			SelectedRow = Row;
			break;
		}
	}
	
	// Check cost
	if (Director.CurrentCredits < SelectedRow->SpawnCost)
	{
		UE_LOG(LogRogueEnemySpawn, Verbose, TEXT("Not enough credits to spawn enemy"));
		return false;
	}
	
	// EQS
	FQueryFinishedSignature CompleteDelegate = FQueryFinishedSignature::CreateUObject(this, 
		&ThisClass::OnEnemySpawnEnvQueryCompleted, &Director, SelectedRow);
	FEnvQueryRequest Request(Director.SpawnEnvQuery, this);
	int32 StartResult = Request.Execute(EEnvQueryRunMode::SingleResult, CompleteDelegate);
	if (StartResult == INDEX_NONE)
	{
		return false;
	}
	
	// Update credits
	// Update here cause EQS and Asset Load can take multiple frames
	Director.CurrentCredits -= SelectedRow->SpawnCost;
	
	return true;
}

void ARoguePrimaryGameMode::OnEnemySpawnEnvQueryCompleted(TSharedPtr<FEnvQueryResult> QueryResult, 
	FRogueAIEnemySpawnDirectorData* Director, FAIEnemySpawnData* SelectEnemy)
{
	FVector SpawnLocation = QueryResult->GetItemAsLocation(0);
	
	SelectEnemy->EnemyClass.LoadAsync(FLoadSoftObjectPathAsyncDelegate::CreateUObject(this, 
		&ThisClass::OnEnemyClassLoaded, SpawnLocation, Director, SelectEnemy));
}

void ARoguePrimaryGameMode::OnEnemyClassLoaded(const FSoftObjectPath& LoadedObjectPath, UObject* LoadedObject, 
	FVector SpawnLocation, FRogueAIEnemySpawnDirectorData* Director, FAIEnemySpawnData* SelectEnemy)
{
	// Spawn to world
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ARogueAICharacter* NewMonster = GetWorld()->SpawnActor<ARogueAICharacter>(SelectEnemy->EnemyClass.Get(), 
		SpawnLocation, FRotator::ZeroRotator, SpawnParams);
	
	UE_VLOG_SPHERE(this, LogGameMode, Log, SpawnLocation, 32.0f, FColor::Blue, TEXT("Spawned: %s\nCost: %02f"),
		*SelectEnemy->EnemyClass->GetName(), SelectEnemy->SpawnCost);
}