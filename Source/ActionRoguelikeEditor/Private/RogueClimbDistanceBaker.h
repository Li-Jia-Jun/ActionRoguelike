// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimationModifier.h"
#include "RogueClimbDistanceBaker.generated.h"

class UAnimSequence;

/**
 * Offline baker for the climb-walk distance-curve locomotion. It reads the ROOT bone of a ROOT-MOTION clip - whose root
 * track IS the authored body travel - accumulates the path length over the clip, and stamps a monotonic DistanceCurveName
 * (default "ClimbDistance", in centimetres) onto the IN-PLACE clip the blendspace actually plays. At runtime the mover
 * differences that curve and divides by dt to drive the body's along-wall speed, so the baked stride is what keeps the
 * planted feet from skating.
 *
 * Add it to an in-place climb AnimSequence's Animation Modifiers, point SourceRootMotionClip at its root-motion sibling,
 * and Apply. The IP clip has a static root (zero travel), which is why the stride has to come from the RM clip; leave
 * SourceRootMotionClip empty only if you applied this to a clip that already carries its own root motion.
 *
 * It bakes PATH LENGTH (sum of per-sample |delta|), which is monotonic by construction - the runtime requires a
 * non-decreasing curve. AxisWeights lets you drop an axis before measuring (e.g. zero Z so a vertical bob in the root
 * doesn't inflate the along-wall distance). DistanceScale is a post-multiply for no-skate fine-tuning without re-editing
 * keys. After baking you can still hand-edit the keys in the curve editor to reshape the hold/pull feel.
 */
UCLASS()
class URogueClimbDistanceBaker : public UAnimationModifier
{
	GENERATED_BODY()

public:
	// The root-motion sibling whose root track supplies the stride. Leave empty to read the root of the clip being modified
	// (only useful if THAT clip has root motion - an in-place clip's root is static and would bake a flat zero curve).
	UPROPERTY(EditAnywhere, Category = "Climb")
	TObjectPtr<UAnimSequence> SourceRootMotionClip;

	// The curve stamped on the in-place clip (cumulative along-wall travel in cm). Must match the mover component's
	// ClimbDistanceCurveName. The runtime differences this live, so it has to be the curve the blendspace blends.
	UPROPERTY(EditAnywhere, Category = "Climb")
	FName DistanceCurveName = FName("ClimbDistance");

	// The root bone to sample on the source clip. "root" on the Mannequin skeleton.
	UPROPERTY(EditAnywhere, Category = "Climb")
	FName RootBoneName = FName("root");

	// Sample resolution (s). Finer = a smoother distance curve (more keys), slightly slower bake. 1/60 matches the contact baker.
	UPROPERTY(EditAnywhere, Category = "Climb", meta = (ForceUnits = "s", ClampMin = "0.001"))
	float SampleStep = 1.0f / 60.0f;

	// Per-axis multiply applied to each root delta BEFORE taking its length (component space). Default (1,1,1) = full 3D path
	// length. Zero an axis to ignore it - e.g. (1,1,0) drops a vertical root bob so it doesn't count as along-wall distance.
	UPROPERTY(EditAnywhere, Category = "Climb")
	FVector AxisWeights = FVector(1.0, 1.0, 1.0);

	// Post-multiply on the whole baked curve - a quick no-skate trim (e.g. 1.03) without reshaping keys. 1.0 = exact root travel.
	UPROPERTY(EditAnywhere, Category = "Climb", meta = (ClampMin = "0.0"))
	float DistanceScale = 1.0f;

	virtual void OnApply_Implementation(UAnimSequence* Animation) override;
	virtual void OnRevert_Implementation(UAnimSequence* Animation) override;
};
