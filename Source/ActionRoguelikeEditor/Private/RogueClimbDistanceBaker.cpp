// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueClimbDistanceBaker.h"

#include "Animation/AnimSequence.h"
#include "AnimPose.h"
#include "AnimationBlueprintLibrary.h"
#include "Animation/AnimCurveTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogClimbDistanceBaker, Log, All);

void URogueClimbDistanceBaker::OnApply_Implementation(UAnimSequence* Animation)
{
	if (!Animation)
	{
		return;
	}

	// Start clean so re-applying never leaves a stale curve behind.
	OnRevert_Implementation(Animation);

	if (DistanceCurveName.IsNone())
	{
		UE_LOG(LogClimbDistanceBaker, Warning, TEXT("[%s] DistanceCurveName is None - nothing to stamp."), *Animation->GetName());
		return;
	}

	// The clip whose root track carries the travel. Default to the modified clip only as a fallback (its root is usually static).
	UAnimSequence* Source = SourceRootMotionClip ? SourceRootMotionClip.Get() : Animation;

	const double TargetLength = Animation->GetPlayLength();
	const double SourceLength = Source->GetPlayLength();
	if (TargetLength <= 0.0 || SourceLength <= 0.0)
	{
		UE_LOG(LogClimbDistanceBaker, Warning, TEXT("[%s] zero-length clip - skipping."), *Animation->GetName());
		return;
	}

	// Read the root track as authored (no root-motion extraction - we WANT the root to move so we can measure it).
	FAnimPoseEvaluationOptions Options;
	Options.bExtractRootMotion = false;
	Options.bEvaluateCurves = false;

	// Map a TARGET-clip time to its matching SOURCE time by phase, then return the root bone's component-space location there.
	// Phase mapping keeps the stride aligned even if the IP and RM clips differ slightly in length.
	auto SampleRootLoc = [&](double TargetTime, FVector& OutLoc) -> bool
	{
		const double SourceTime = FMath::Clamp((TargetTime / TargetLength) * SourceLength, 0.0, SourceLength);

		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtTime(Source, SourceTime, Options, Pose);
		if (!Pose.IsValid())
		{
			return false;
		}

		TArray<FName> PoseBoneNames;
		UAnimPoseExtensions::GetBoneNames(Pose, PoseBoneNames);
		if (!PoseBoneNames.Contains(RootBoneName))
		{
			return false;
		}

		// EAnimPoseSpaces::World == component space for FAnimPose; for the root bone that is its authored track transform.
		OutLoc = UAnimPoseExtensions::GetBonePose(Pose, RootBoneName, EAnimPoseSpaces::World).GetLocation();
		return true;
	};

	FVector PrevLoc;
	if (!SampleRootLoc(0.0, PrevLoc))
	{
		UE_LOG(LogClimbDistanceBaker, Warning,
			TEXT("[%s] root bone '%s' not found in source '%s' - skipping."),
			*Animation->GetName(), *RootBoneName.ToString(), *Source->GetName());
		return;
	}

	// Build the curve on the IN-PLACE clip (OnRevert cleared any prior one). Key 0 = 0 distance.
	UAnimationBlueprintLibrary::AddCurve(Animation, DistanceCurveName, ERawCurveTrackTypes::RCT_Float);
	UAnimationBlueprintLibrary::AddFloatCurveKey(Animation, DistanceCurveName, 0.0f, 0.0f);

	double Accum = 0.0;
	double LastKeyedTime = 0.0;

	auto AddSampleAt = [&](double TargetTime)
	{
		FVector Loc;
		if (!SampleRootLoc(TargetTime, Loc))
		{
			return;
		}
		// Path length: componentwise weight the delta (so a zeroed axis is ignored) then take its magnitude. Monotonic by build.
		FVector Delta = Loc - PrevLoc;
		Delta *= AxisWeights;
		Accum += Delta.Size();
		PrevLoc = Loc;

		UAnimationBlueprintLibrary::AddFloatCurveKey(Animation, DistanceCurveName,
			static_cast<float>(TargetTime), static_cast<float>(Accum * DistanceScale));
		LastKeyedTime = TargetTime;
	};

	const double Step = FMath::Max(1.0 / 240.0, static_cast<double>(SampleStep));
	for (double T = Step; T <= TargetLength; T += Step)
	{
		AddSampleAt(T);
	}
	// Guarantee an endpoint key exactly at the clip length (the loop may stop just short of it).
	if (LastKeyedTime < TargetLength - KINDA_SMALL_NUMBER)
	{
		AddSampleAt(TargetLength);
	}

	if (Accum <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogClimbDistanceBaker, Warning,
			TEXT("[%s] measured ~0 travel from source '%s' - is SourceRootMotionClip pointed at a root-motion clip (not the in-place one)?"),
			*Animation->GetName(), *Source->GetName());
	}
	else
	{
		UE_LOG(LogClimbDistanceBaker, Log,
			TEXT("[%s] baked '%s' from '%s': stride=%.2f cm over %.3fs (scale %.3f)."),
			*Animation->GetName(), *DistanceCurveName.ToString(), *Source->GetName(),
			Accum * DistanceScale, TargetLength, DistanceScale);
	}

	Animation->MarkPackageDirty();
}

void URogueClimbDistanceBaker::OnRevert_Implementation(UAnimSequence* Animation)
{
	if (!Animation || DistanceCurveName.IsNone())
	{
		return;
	}

	if (UAnimationBlueprintLibrary::DoesCurveExist(Animation, DistanceCurveName, ERawCurveTrackTypes::RCT_Float))
	{
		UAnimationBlueprintLibrary::RemoveCurve(Animation, DistanceCurveName);
		Animation->MarkPackageDirty();
	}
}
