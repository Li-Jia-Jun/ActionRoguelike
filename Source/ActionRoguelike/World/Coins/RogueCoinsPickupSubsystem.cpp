// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueCoinsPickupSubsystem.h"
#include "Components/AudioComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Core/RogueDeveloperSettings.h"
#include "Player/SPlayerCharacter.h"
#include "ProfilingDebugging/CountersTrace.h"

TRACE_DECLARE_INT_COUNTER(TraceCoinsCount, TEXT("Coins Count"));

void URogueCoinsPickupSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	
	const URogueDeveloperSettings* DefaultSettings = GetDefault<URogueDeveloperSettings>();
	
	// Setup mesh
	WorldISM = NewObject<UInstancedStaticMeshComponent>(&InWorld, NAME_None, RF_Transient);
	WorldISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WorldISM->SetAffectDistanceFieldLighting(false);
	WorldISM->RegisterComponentWithWorld(&InWorld);
	DefaultSettings->CoinMesh.LoadAsync(
		FLoadSoftObjectPathAsyncDelegate::CreateWeakLambda(this, [this](const FSoftObjectPath&, UObject* LoadedObject)
		{
			WorldISM->SetStaticMesh(Cast<UStaticMesh>(LoadedObject));
		}
		));
	
	// Setup audio
	PickupAudioComp = NewObject<UAudioComponent>(&InWorld, NAME_None, RF_Transient);
	PickupAudioComp->SetAutoActivate(false);
	PickupAudioComp->bAllowSpatialization = false;
	PickupAudioComp->RegisterComponentWithWorld(&InWorld);
	DefaultSettings->CoinPickupSound.LoadAsync(
		FLoadSoftObjectPathAsyncDelegate::CreateWeakLambda(this, [this](const FSoftObjectPath&, UObject* LoadedObject)
		{
			PickupAudioComp->SetSound(Cast<USoundBase>(LoadedObject));
		}
		));
	PickupAudioTriggerParamName = DefaultSettings->CoinPickupTriggerParameter;
	
	TRACE_COUNTER_SET(TraceCoinsCount,CoinLocations.Num())
}

void URogueCoinsPickupSubsystem::AddCoins(TArray<FVector> Locations, TArray<int32> Points)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URogueCoinsPickupSubsystem::AddCoins);
	
	CoinLocations.Append(Locations);
	CoinPoints.Append(Points);
	
	TArray<FTransform> MeshTransforms;
	for (int i = 0; i < Locations.Num(); ++i)
	{
		MeshTransforms.Add(FTransform(Locations[i] + FVector(0, 0, 50.0f)));
	}
	TArray<FPrimitiveInstanceId> NewMeshIDs = WorldISM->AddInstancesById(MeshTransforms, true,false);
	MeshIDs.Append(NewMeshIDs);
	
	TRACE_COUNTER_SET(TraceCoinsCount,CoinLocations.Num())
}

void URogueCoinsPickupSubsystem::RemoveCoin(int32 CoinIndex)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URogueCoinsPickupSubsystem::RemoveCoin);
	
	CoinLocations.RemoveAtSwap(CoinIndex, EAllowShrinking::No);
	CoinPoints.RemoveAtSwap(CoinIndex, EAllowShrinking::No);
	WorldISM->RemoveInstanceById(MeshIDs[CoinIndex]);
	MeshIDs.RemoveAtSwap(CoinIndex, EAllowShrinking::No);
	
	TRACE_COUNTER_SET(TraceCoinsCount,CoinLocations.Num())
}

void URogueCoinsPickupSubsystem::PlayPickupSound() const
{
	if (!PickupAudioComp->IsPlaying())
	{
		PickupAudioComp->Play();
	}
	PickupAudioComp->SetTriggerParameter(PickupAudioTriggerParamName);
}

void URogueCoinsPickupSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	
	// Get player locations
	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (PlayerController == nullptr)
	{
		return;
	}
	
	ACharacter* Character = PlayerController->GetCharacter();
	if (Character == nullptr)
	{
		return;
	}
	FVector PlayerLocation = Character->GetActorLocation();

	const float PickupRadiusSquared = 22500.0f; // radius = 150
	
	TArray<int32> PickedIndices;
	
	// Pick coins
	{
		TRACE_CPUPROFILER_EVENT_SCOPE("URogueCoinsPickupSubsystem::Tick::DistanceCheck");
		
		for (int i = 0; i < CoinLocations.Num(); ++i)
		{
			if (FVector::DistSquared(PlayerLocation, CoinLocations[i]) < PickupRadiusSquared)
			{
				PickedIndices.Add(i);
			}
		}
	}
	
	// Accumulate points
	int32 TotalPoints = 0;
	for (int i = PickedIndices.Num()-1; i >= 0; --i)
	{
		int32 CoinIndex = PickedIndices[i];
		
		TotalPoints += CoinPoints[CoinIndex];
		
		RemoveCoin(CoinIndex);
	}
	if (TotalPoints > 0)
	{
		PlayPickupSound();
		UE_LOG(LogTemp, Warning, TEXT("Picked coins total points: %d"), TotalPoints);
	}
	
	// Debug draw
	// for (int i = 0; i < CoinLocations.Num(); ++i)
	// {
	// 	DrawDebugPoint(World, CoinLocations[i], 10.0f, FColor::Yellow);
	// }
}