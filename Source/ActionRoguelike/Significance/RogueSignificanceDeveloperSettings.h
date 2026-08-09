// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RogueSignificanceDeveloperSettings.generated.h"



USTRUCT()
struct FSignificanceBucketInfo
{
	GENERATED_BODY()
	
	UPROPERTY(Config, EditAnywhere)
	FName Tag;
	
	UPROPERTY(Config, EditAnywhere, meta = (ClampMin = 0))
	TArray<int32> BucketSizes;
};

/**
 * 
 */
UCLASS(Config=Game, DefaultConfig)
class ACTIONROGUELIKE_API URogueSignificanceDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()
	
public:
	
	UPROPERTY(Config, EditDefaultsOnly, Category=Significance)
	bool bEnableSignificanceManager = true;
	
	UPROPERTY(Config, EditDefaultsOnly, Category=Significance)
	TArray<FSignificanceBucketInfo> SignificanceBuckets;

	UPROPERTY(Config, EditDefaultsOnly, Category=Significance)
	float LODChangeFreezeInterval = 0.5f;

	/** Hard cap on how many objects may change LOD in a single Update, to spread cost across frames. */
	UPROPERTY(Config, EditDefaultsOnly, Category=Significance, meta=(ClampMin=1))
	int32 MaxLODChangesPerUpdate = 100;

	/**
	 * The manager runs its heavy significance+bucketing pass once every N ticks and spends the
	 * intervening ticks applying queued LOD changes. Must be >= 2 so each cycle has an apply frame.
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category=Significance, meta=(ClampMin=2))
	int32 SignificanceUpdateTickInterval = 3;

	/** Returns the bucket configuration for the given Tag, or nullptr if the Tag is not configured (i.e. not opted in). */
	const FSignificanceBucketInfo* FindBucketInfo(FName Tag) const
	{
		return SignificanceBuckets.FindByPredicate(
			[Tag](const FSignificanceBucketInfo& Info) { return Info.Tag == Tag; });
	}

	virtual FName GetCategoryName() const override
	{
		return FApp::GetProjectName();
	}
};
