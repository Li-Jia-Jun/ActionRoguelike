// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "SignificanceManager.h"
#include "RogueSignificanceManager.generated.h"

/**
 * Extend ManagedObjectInfo to also store LOD index and timestamp (for LOD change freezing window)
 */
struct FExtendedManagedObjectInfo : USignificanceManager::FManagedObjectInfo
{
	int32 LOD;					// Current LOD
	int32 NewLOD;				// Target LOD, applied later by the manager's apply phase
	float LastLODChangeTimeInSeconds;
	bool bPendingLODChange;		// True while queued in the manager's pending list (dedup + safe removal)

	FExtendedManagedObjectInfo(UObject* InObject, FName InTag,
		USignificanceManager::FManagedObjectSignificanceFunction InSignificanceFunction,
		USignificanceManager::EPostSignificanceType InPostSignificanceType = USignificanceManager::EPostSignificanceType::None,
		USignificanceManager::FManagedObjectPostSignificanceFunction InPostSignificanceFunction = nullptr)
			: USignificanceManager::FManagedObjectInfo(InObject, InTag, MoveTemp(InSignificanceFunction), InPostSignificanceType, MoveTemp(InPostSignificanceFunction))
			, LOD(-1), NewLOD(-1), LastLODChangeTimeInSeconds(-1.0f), bPendingLODChange(false)
	{
	}
};

/**
 * Extend Significance Manager for Actor LOD management
 */
UCLASS()
class ACTIONROGUELIKE_API URogueSignificanceManager : public USignificanceManager
{
	GENERATED_BODY()
	
public:

	URogueSignificanceManager();

	virtual void RegisterObject(UObject* Object, FName Tag, FManagedObjectSignificanceFunction SignificanceFunction, EPostSignificanceType InPostSignificanceType = EPostSignificanceType::None,
		FManagedObjectPostSignificanceFunction InPostSignificanceFunction = nullptr) override;

	virtual void UnregisterObject(UObject* Object) override;

	virtual void Update(TArrayView<const FTransform> InViewpoints) override;

protected:

	void OnWorldBeginPlay();

	/** Heavy phase (every Nth tick): recompute significance, bucket, and queue objects whose LOD changed. */
	void UpdateCalculation(TArrayView<const FTransform> InViewpoints);

	/** Light phase (other ticks): apply up to MaxLODChangesPerUpdate queued changes, resuming via the cursor. */
	void UpdateLOD(TArrayView<const FTransform> InViewpoints);

	/** Sets an object's target LOD and queues it (deduped, freeze-gated) when it needs to change. */
	void MarkTargetLOD(FExtendedManagedObjectInfo* ObjectInfo, int32 TargetLOD, float FreezeInterval, float NowInSeconds);

	int32 UpdateCounter = 0;

	/** FIFO queue of objects awaiting an LOD change, drained across frames. Holds raw infos owned by the base class. */
	TArray<FExtendedManagedObjectInfo*> PendingLODChanges;

	/** Read position into PendingLODChanges; entries before it were already applied this cycle. */
	int32 PendingCursor = 0;
};
