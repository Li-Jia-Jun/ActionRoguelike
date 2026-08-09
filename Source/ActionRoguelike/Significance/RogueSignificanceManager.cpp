// Fill out your copyright notice in the Description page of Project Settings.


#include "RogueSignificanceManager.h"

#include "RogueSignificanceDeveloperSettings.h"
#include "RogueSignificanceInterface.h"
#include "Engine/World.h"


URogueSignificanceManager::URogueSignificanceManager()
{
	// The CDO has no world outer; only bind for the real per-world instance.
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		// Manager is created during world init, before BeginPlay. But guard in case
		// this instance is ever spawned after the world already began play.
		if (World->HasBegunPlay())
		{
			OnWorldBeginPlay();
		}
		else
		{
			World->OnWorldBeginPlay.AddUObject(this, &URogueSignificanceManager::OnWorldBeginPlay);
		}
	}
}


void URogueSignificanceManager::OnWorldBeginPlay()
{
	UpdateCounter = 0;
	PendingLODChanges.Reset();
	PendingCursor = 0;
}


void URogueSignificanceManager::RegisterObject(UObject* Object, FName Tag, FManagedObjectSignificanceFunction SignificanceFunction, EPostSignificanceType InPostSignificanceType,
	FManagedObjectPostSignificanceFunction InPostSignificanceFunction)
{
	FExtendedManagedObjectInfo* ExtendedObjInfo = new FExtendedManagedObjectInfo(Object, Tag, SignificanceFunction, InPostSignificanceType, InPostSignificanceFunction);
	RegisterManagedObject(ExtendedObjInfo);
}


void URogueSignificanceManager::UnregisterObject(UObject* Object)
{
	// Super::UnregisterObject deletes the FManagedObjectInfo. If this object is still queued for an LOD
	// change, drop it from the queue first to avoid a dangling pointer during the apply phase.
	if (FManagedObjectInfo* Info = GetManagedObject(Object))
	{
		FExtendedManagedObjectInfo* ExtInfo = static_cast<FExtendedManagedObjectInfo*>(Info);
		if (ExtInfo->bPendingLODChange)
		{
			const int32 FoundIndex = PendingLODChanges.IndexOfByKey(ExtInfo);
			if (FoundIndex != INDEX_NONE)
			{
				PendingLODChanges.RemoveAt(FoundIndex, 1, EAllowShrinking::No);
				// Keep the drain cursor pointing at the same logical next entry.
				if (FoundIndex < PendingCursor)
				{
					--PendingCursor;
				}
			}
			ExtInfo->bPendingLODChange = false;
		}
	}

	Super::UnregisterObject(Object);
}


void URogueSignificanceManager::MarkTargetLOD(FExtendedManagedObjectInfo* ObjectInfo, int32 TargetLOD, float FreezeInterval, float NowInSeconds)
{
	// Always refresh the target so a queued object applies the freshest LOD when it's finally drained.
	ObjectInfo->NewLOD = TargetLOD;

	// Nothing to queue if it's already there, or already sitting in the queue.
	if (ObjectInfo->LOD == TargetLOD || ObjectInfo->bPendingLODChange)
	{
		return;
	}

	// Freeze window: don't re-queue an object that changed too recently (prevents thrash at bucket edges).
	if (ObjectInfo->LastLODChangeTimeInSeconds >= 0.0f
		&& NowInSeconds - ObjectInfo->LastLODChangeTimeInSeconds < FreezeInterval)
	{
		return;
	}

	ObjectInfo->bPendingLODChange = true;
	PendingLODChanges.Add(ObjectInfo);
}


void URogueSignificanceManager::UpdateCalculation(TArrayView<const FTransform> InViewpoints)
{
	// Heavy pass: recompute every object's significance and re-sort each tag's array
	// (descending, so index 0 is the most significant / nearest object).
	Super::Update(InViewpoints);

	// Remove objects that already changed LODs from previous frames's UpdateLODs().
	if (PendingCursor > 0)
	{
		PendingLODChanges.RemoveAt(0, PendingCursor, EAllowShrinking::No);
		PendingCursor = 0;
	}

	const URogueSignificanceDeveloperSettings* Settings = GetDefault<URogueSignificanceDeveloperSettings>();
	const float FreezeInterval = Settings->LODChangeFreezeInterval;
	const float NowInSeconds = GetWorld()->GetTimeSeconds();

	for (const FSignificanceBucketInfo& BucketInfo : Settings->SignificanceBuckets)
	{
		const TArray<FManagedObjectInfo*>& SortedObjects = GetManagedObjects(BucketInfo.Tag);
		const int32 NumObjects = SortedObjects.Num();
		const int32 LastLOD = FMath::Max(0, BucketInfo.BucketSizes.Num() - 1);

		int32 Index = 0;
		for (int32 LOD = 0; LOD < BucketInfo.BucketSizes.Num(); ++LOD)
		{
			const int32 BucketEnd = FMath::Min(Index + BucketInfo.BucketSizes[LOD], NumObjects);
			for (; Index < BucketEnd; ++Index)
			{
				MarkTargetLOD(static_cast<FExtendedManagedObjectInfo*>(SortedObjects[Index]), LOD, FreezeInterval, NowInSeconds);
			}
		}

		// Overflow: objects beyond the configured buckets fall into the most-culled (last) LOD.
		for (; Index < NumObjects; ++Index)
		{
			MarkTargetLOD(static_cast<FExtendedManagedObjectInfo*>(SortedObjects[Index]), LastLOD, FreezeInterval, NowInSeconds);
		}
	}
}


void URogueSignificanceManager::UpdateLOD(TArrayView<const FTransform> InViewpoints)
{
	const URogueSignificanceDeveloperSettings* Settings = GetDefault<URogueSignificanceDeveloperSettings>();
	if (!Settings->bEnableSignificanceManager)
	{
		return;
	}
	
	const int32 MaxLODChanges = Settings->MaxLODChangesPerUpdate;
	const float NowInSeconds = GetWorld()->GetTimeSeconds();
	

	// Drain the queue from the cursor, applying at most MaxLODChanges actual changes this frame.
	int32 AppliedChanges = 0;
	while (PendingCursor < PendingLODChanges.Num() && AppliedChanges < MaxLODChanges)
	{
		FExtendedManagedObjectInfo* ObjectInfo = PendingLODChanges[PendingCursor];
		++PendingCursor;
		ObjectInfo->bPendingLODChange = false; // consumed from the queue

		// Target may have returned to the current LOD before we got here; skip without spending budget.
		if (ObjectInfo->LOD == ObjectInfo->NewLOD)
		{
			continue;
		}

		if (IRogueSignificanceInterface* SignificanceObject = Cast<IRogueSignificanceInterface>(ObjectInfo->GetObject()))
		{
			SignificanceObject->SignificanceLODChanged(ObjectInfo->NewLOD);
		}
		ObjectInfo->LOD = ObjectInfo->NewLOD;
		ObjectInfo->LastLODChangeTimeInSeconds = NowInSeconds;
		++AppliedChanges;
	}

	// Fully drained this cycle: clear so the array doesn't keep growing.
	if (PendingCursor >= PendingLODChanges.Num())
	{
		PendingLODChanges.Reset();
		PendingCursor = 0;
	}
}


void URogueSignificanceManager::Update(TArrayView<const FTransform> InViewpoints)
{
	++UpdateCounter;

	const URogueSignificanceDeveloperSettings* Settings = GetDefault<URogueSignificanceDeveloperSettings>();
	// Interval must be >= 2 so each cycle has at least one apply frame; otherwise the queue never drains.
	const int32 Interval = FMath::Max(2, Settings->SignificanceUpdateTickInterval);

	if (UpdateCounter % Interval == 0)
	{
		UpdateCalculation(InViewpoints);
	}
	else
	{
		UpdateLOD(InViewpoints);
	}
}
