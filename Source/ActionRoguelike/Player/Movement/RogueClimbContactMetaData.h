// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimMetaData.h"
#include "RogueClimbContactMetaData.generated.h"

/**
 * One plant (one strike) of a limb in a climb clip: where (clip/component space) and when it lands. Authored OFFLINE by the
 * climb-contact baker and read at RUNTIME for predictive limb IK - the runtime maps Landing to the real wall via the live
 * root and eases the limb's IK in over its swing so it arrives already on it.
 */
USTRUCT(BlueprintType)
struct FRogueClimbLimbPlant
{
	GENERATED_BODY()

	// Time (seconds) into the clip where this limb is fully planted for this strike - the frame Landing was sampled from.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb")
	float PlantTime = 0.0f;

	// Time (seconds) at which the runtime SNAPSHOTS the live body to freeze this strike's world landing - decouples WHEN the
	// pin locks from the blend ramp. < PlantTime leans on prediction (more runway, risk of falling short as the body climbs);
	// == PlantTime measures the body exactly at contact (no fall-short, least runway). Resolved from the baker (a negative
	// authored value means "capture at PlantTime").
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb")
	float CaptureTime = 0.0f;

	// The contact bone/socket at PlantTime, in COMPONENT space (relative to the mesh root). For an in-place clip the root is
	// at origin, so this is the limb's root-relative landing - the quantity the runtime multiplies by the live world root to
	// get the predicted world contact (then traces onto the real surface).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb")
	FTransform Landing;
};

/**
 * One limb's baked contacts for a clip. Climb-walk clips are TWO-strike (a big stride then a small ~1/3 stride - see
 * Climbing_Design.md "Animation <-> movement sync"), so a limb plants more than once per clip; hence an ARRAY of plants
 * (sorted by time). LimbId is the limb's stance-curve name, so the runtime can both find these landings and read the curve
 * of the same name as the limb's IK blend profile.
 */
USTRUCT(BlueprintType)
struct FRogueClimbLimbContact
{
	GENERATED_BODY()

	// Matches the limb's stance curve name (LeftFootStanceCurveName, etc.).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb")
	FName LimbId;

	// Every plant this limb makes in the clip, sorted ascending by PlantTime (the baker sorts them).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb")
	TArray<FRogueClimbLimbPlant> Plants;

	// The plant whose PlantTime is nearest the given clip time, measured CIRCULARLY over Period (the clip length) so a hold
	// that WRAPS the loop seam resolves to its OWN plant (the end-of-clip one), not the next interior plant. The runtime
	// resolver calls this to pick which strike's landing to aim at: approaching a plant -> that plant (it's just ahead);
	// in a hold -> that plant (it's just behind, incl. a seam-wrapped hold). During a swing W~0 so IK is off, so a tie at
	// the swing midpoint doesn't matter. Null only when there are no plants.
	const FRogueClimbLimbPlant* FindNearestPlant(float Time, float Period) const
	{
		const FRogueClimbLimbPlant* Best = nullptr;
		float BestDist = TNumericLimits<float>::Max();
		for (const FRogueClimbLimbPlant& P : Plants)
		{
			float Dist = FMath::Abs(P.PlantTime - Time);
			if (Period > 0.0f) { Dist = FMath::Min(Dist, Period - Dist); } // circular over the loop
			if (Dist < BestDist) { BestDist = Dist; Best = &P; }
		}
		return Best;
	}
};

/**
 * Per-clip baked limb-contact table for predictive climb IK. One instance is attached to a climb AnimSequence as
 * UAnimMetaData by URogueClimbContactBaker; the climb mover component reads it to aim each limb's IK at its (predicted,
 * then traced) landing during the swing instead of only snapping on arrival.
 */
UCLASS(BlueprintType)
class ACTIONROGUELIKE_API URogueClimbContactMetaData : public UAnimMetaData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb")
	TArray<FRogueClimbLimbContact> Contacts;

	// Returns the baked contacts for LimbId, or nullptr if this clip has none for that limb.
	const FRogueClimbLimbContact* FindContact(FName LimbId) const
	{
		return Contacts.FindByPredicate([LimbId](const FRogueClimbLimbContact& C) { return C.LimbId == LimbId; });
	}
};
