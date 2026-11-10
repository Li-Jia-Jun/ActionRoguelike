// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimationModifier.h"
#include "RogueClimbContactBaker.generated.h"

/** One plant (one strike) to bake for a limb: when it is fully planted, and when the runtime should freeze it. */
USTRUCT()
struct FRogueClimbContactBakePlant
{
	GENERATED_BODY()

	// Seconds into the clip where the limb is fully planted for this strike - authored by eye. The baker samples the limb's
	// ContactBone in component space at exactly this time. Keep it roughly where the stance curve plateaus at full contact.
	UPROPERTY(EditAnywhere, Category = "Climb", meta = (ClampMin = "0.0"))
	float PlantTime = 0.0f;

	// When the runtime snapshots the live body to freeze this strike's landing. Leave at -1 to capture AT PlantTime (exact,
	// no fall-short, least blend runway); set earlier (toward swing start) to freeze sooner for more blend runway, accepting
	// that the body travels further before the real plant (the pin may fall short unless that travel is predicted).
	UPROPERTY(EditAnywhere, Category = "Climb")
	float CaptureTime = -1.0f;
};

/** One limb to bake: its identity, its contact bone, and every strike it plants in this clip. */
USTRUCT()
struct FRogueClimbContactBakeEntry
{
	GENERATED_BODY()

	// The limb's stance curve name (e.g. Stance_FootL). The baker does NOT read the curve - this is stored as the contact's
	// LimbId so the runtime can (a) find these landings and (b) read the curve of the same name as the limb's authored IK
	// blend profile. Must match the curve on the clip AND the component's *StanceCurveName knob.
	UPROPERTY(EditAnywhere, Category = "Climb")
	FName StanceCurveName;

	// The contact bone to sample as the landing (e.g. ball_l for a foot, hand_l for a hand). Must be a skeleton BONE -
	// sockets are not sampleable here; for a palm, sample the wrist and let the runtime's hand->palm offset finish it.
	UPROPERTY(EditAnywhere, Category = "Climb")
	FName ContactBone;

	// MANUAL plant list (override). Leave this EMPTY to AUTO-DETECT the plants from the stance curve instead: the baker finds
	// each place the curve rises into a hold (reaches PlantDetectThreshold) and bakes one plant there - so a two-strike limb
	// yields two plants with NO hand-authored times, and retiming is just reshaping the curve. Fill this only to override.
	UPROPERTY(EditAnywhere, Category = "Climb")
	TArray<FRogueClimbContactBakePlant> Plants;

	// AUTO-DETECT only: capture this many seconds BEFORE each detected plant (CaptureTime = PlantTime - this, clamped >= 0).
	// 0 = capture at the plant (exact, no fall-short). Raise it to freeze earlier for more blend runway, across all strikes.
	UPROPERTY(EditAnywhere, Category = "Climb", meta = (ForceUnits = "s", ClampMin = "0.0"))
	float CaptureLeadSeconds = 0.0f;
};

/**
 * Offline baker for predictive climb limb IK. For each configured limb it finds the plant frames - AUTO-DETECTED from the
 * stance curve's holds (each rising crossing of PlantDetectThreshold) unless the entry's manual Plants list overrides them -
 * samples the contact bone in COMPONENT space at each, and writes the results into a URogueClimbContactMetaData on the clip.
 * Add it to a climb AnimSequence's Animation Modifiers, configure the limbs, and Apply. Runs on the IN-PLACE clip (the one
 * the game plays): component space there is root-relative, which is exactly what the runtime needs to re-anchor each landing
 * onto the real wall. Sampling at the rising crossing is deliberate - that's the moment of full contact, before the in-place
 * clip's planted foot slides back through the hold.
 */
UCLASS()
class URogueClimbContactBaker : public UAnimationModifier
{
	GENERATED_BODY()

public:
	// The limbs to bake for this clip. Leave a limb out if it never plants in this clip.
	UPROPERTY(EditAnywhere, Category = "Climb")
	TArray<FRogueClimbContactBakeEntry> Limbs;

	// --- Auto-detect (used for any entry whose manual Plants list is empty) ---

	// Stance-curve value at/above which the limb counts as "in a hold" - each rising crossing becomes one plant. Set it a
	// touch below your plateau level (if the curve tops out at 1.0, 0.95 is safe) so a hold that doesn't quite reach 1 still registers.
	UPROPERTY(EditAnywhere, Category = "Climb", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PlantDetectThreshold = 0.95f;

	// Reject a second detected plant within this of the previous one - stops a wobbly plateau that dips and re-crosses the
	// threshold from registering as two plants. Keep it well below the gap between your real strikes.
	UPROPERTY(EditAnywhere, Category = "Climb", meta = (ForceUnits = "s", ClampMin = "0.0"))
	float MinPlantSeparation = 0.1f;

	// Curve scan resolution (s) for auto-detect. Finer = more precise plant times, slightly slower bake.
	UPROPERTY(EditAnywhere, Category = "Climb", meta = (ForceUnits = "s", ClampMin = "0.001"))
	float PlantScanStep = 1.0f / 60.0f;

	// The baker also STAMPS a monotonic 0->1 curve of this name on the clip (two keys: 0 at the start, 1 at the end), so the
	// runtime can read the clip's PHASE live (via the anim instance, same path as the stance curves) and pick the upcoming
	// plant robustly - a non-monotonic stance curve can't locate you in the clip, a monotonic one can. Must match the mover
	// component's ClimbPhaseCurveName. Set to None to skip stamping it.
	UPROPERTY(EditAnywhere, Category = "Climb")
	FName PhaseCurveName = FName("ClimbPhase");

	virtual void OnApply_Implementation(UAnimSequence* Animation) override;
	virtual void OnRevert_Implementation(UAnimSequence* Animation) override;
};
