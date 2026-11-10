// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueClimbContactBaker.h"

#include "Animation/AnimSequence.h"
#include "AnimPose.h"
#include "AnimationBlueprintLibrary.h"
#include "Animation/AnimCurveTypes.h"
#include "Player/Movement/RogueClimbContactMetaData.h"

DEFINE_LOG_CATEGORY_STATIC(LogClimbContactBaker, Log, All);

void URogueClimbContactBaker::OnApply_Implementation(UAnimSequence* Animation)
{
	if (!Animation)
	{
		return;
	}

	// Start clean so re-applying never stacks duplicate tables.
	OnRevert_Implementation(Animation);

	const double Length = Animation->GetPlayLength();

	// In-place clip: we want the pose as authored (no root motion baked into bone transforms).
	FAnimPoseEvaluationOptions Options;
	Options.bExtractRootMotion = false;
	Options.bEvaluateCurves = false;

	URogueClimbContactMetaData* Meta = NewObject<URogueClimbContactMetaData>(Animation);

	for (const FRogueClimbContactBakeEntry& Entry : Limbs)
	{
		if (Entry.StanceCurveName.IsNone() || Entry.ContactBone.IsNone())
		{
			continue;
		}

		// Resolve this limb's plant specs (PlantTime + CaptureTime, in seconds). A non-empty manual Plants list wins;
		// otherwise auto-detect from the stance curve's holds.
		struct FPlantSpec { double PlantTime; double CaptureTime; };
		TArray<FPlantSpec> Specs;

		if (Entry.Plants.Num() > 0)
		{
			for (const FRogueClimbContactBakePlant& P : Entry.Plants)
			{
				const double PlantTime = FMath::Clamp<double>(P.PlantTime, 0.0, Length);
				// A negative authored CaptureTime means "capture at the plant frame" (exact, no fall-short); else clamp it.
				const double CaptureTime = (P.CaptureTime < 0.0f) ? PlantTime : FMath::Clamp<double>(P.CaptureTime, 0.0, Length);
				Specs.Add({ PlantTime, CaptureTime });
			}
		}
		else
		{
			// Auto-detect: each RISING crossing of PlantDetectThreshold on the stance curve is one plant - the moment the limb
			// reaches full contact, which is the right frame to sample since an in-place clip's planted foot then slides back
			// through the hold. MinPlantSeparation rejects a second crossing from a plateau that dips and re-crosses.
			//
			// The curve is treated as CIRCULAR (the clip loops): seed "above" from the clip's END value, so a hold that WRAPS
			// the loop seam (high at the last frame AND at frame 0) is NOT mis-counted as a fresh plant at T=0 - its real plant
			// is the rising edge BEFORE the seam. If the curve is low at the seam this seeds false, and T=0 behaves normally.
			const float EndValue = Animation->EvaluateCurveData(Entry.StanceCurveName, static_cast<float>(Length), /*bForceUseRawData*/ true);
			bool bAbove = EndValue >= PlantDetectThreshold;
			double LastPlant = 0.0;
			for (double T = 0.0; T <= Length; T += PlantScanStep)
			{
				// bForceUseRawData=true: read the authored curve directly, independent of compressed data.
				const float V = Animation->EvaluateCurveData(Entry.StanceCurveName, static_cast<float>(T), /*bForceUseRawData*/ true);
				const bool bNowAbove = V >= PlantDetectThreshold;
				if (bNowAbove && !bAbove && (Specs.Num() == 0 || (T - LastPlant) >= MinPlantSeparation))
				{
					const double CaptureTime = FMath::Max(0.0, T - static_cast<double>(Entry.CaptureLeadSeconds));
					Specs.Add({ T, CaptureTime });
					LastPlant = T;
				}
				bAbove = bNowAbove;
			}

			if (Specs.Num() == 0)
			{
				// No rising edge found. Either the curve never reaches the threshold (skip + warn), or the limb is held for the
				// WHOLE clip (always above -> no rising edge at all): then bake a single plant at the clip start.
				const float StartValue = Animation->EvaluateCurveData(Entry.StanceCurveName, 0.0f, /*bForceUseRawData*/ true);
				if (StartValue >= PlantDetectThreshold)
				{
					Specs.Add({ 0.0, 0.0 });
				}
				else
				{
					UE_LOG(LogClimbContactBaker, Warning,
						TEXT("[%s] stance curve '%s' never reaches %.2f (and no manual Plants) - skipping limb."),
						*Animation->GetName(), *Entry.StanceCurveName.ToString(), PlantDetectThreshold);
					continue;
				}
			}
		}

		FRogueClimbLimbContact Contact;
		Contact.LimbId = Entry.StanceCurveName;

		for (const FPlantSpec& Spec : Specs)
		{
			// Sample the contact bone in COMPONENT space (EAnimPoseSpaces::World == component for FAnimPose) at the plant time.
			FAnimPose Pose;
			UAnimPoseExtensions::GetAnimPoseAtTime(Animation, Spec.PlantTime, Options, Pose);

			TArray<FName> PoseBoneNames;
			if (Pose.IsValid())
			{
				UAnimPoseExtensions::GetBoneNames(Pose, PoseBoneNames);
			}

			if (!PoseBoneNames.Contains(Entry.ContactBone))
			{
				UE_LOG(LogClimbContactBaker, Warning,
					TEXT("[%s] contact bone '%s' not found in pose - skipping a '%s' plant at %.3fs."),
					*Animation->GetName(), *Entry.ContactBone.ToString(), *Entry.StanceCurveName.ToString(), Spec.PlantTime);
				continue;
			}

			FRogueClimbLimbPlant Plant;
			Plant.PlantTime = static_cast<float>(Spec.PlantTime);
			Plant.CaptureTime = static_cast<float>(Spec.CaptureTime);
			Plant.Landing = UAnimPoseExtensions::GetBonePose(Pose, Entry.ContactBone, EAnimPoseSpaces::World);
			Contact.Plants.Add(Plant);

			UE_LOG(LogClimbContactBaker, Log,
				TEXT("[%s] baked '%s' -> bone '%s' plant=%.3fs capture=%.3fs loc=%s"),
				*Animation->GetName(), *Entry.StanceCurveName.ToString(), *Entry.ContactBone.ToString(),
				Plant.PlantTime, Plant.CaptureTime, *Plant.Landing.GetLocation().ToCompactString());
		}

		// Keep plants ordered by time so the runtime's upcoming-plant search is a simple forward scan.
		Contact.Plants.Sort([](const FRogueClimbLimbPlant& A, const FRogueClimbLimbPlant& B) { return A.PlantTime < B.PlantTime; });

		if (Contact.Plants.Num() > 0)
		{
			Meta->Contacts.Add(Contact);
		}
	}

	if (Meta->Contacts.Num() > 0)
	{
		Animation->AddMetaData(Meta);
	}

	// Stamp a monotonic 0..1 phase curve so the runtime can read the clip's position live (OnRevert above cleared any prior
	// one). Two endpoint keys = a linear ramp; the runtime maps this phase to the upcoming plant.
	if (!PhaseCurveName.IsNone())
	{
		UAnimationBlueprintLibrary::AddCurve(Animation, PhaseCurveName, ERawCurveTrackTypes::RCT_Float);
		UAnimationBlueprintLibrary::AddFloatCurveKey(Animation, PhaseCurveName, 0.0f, 0.0f);
		UAnimationBlueprintLibrary::AddFloatCurveKey(Animation, PhaseCurveName, static_cast<float>(Length), 1.0f);
	}

	Animation->MarkPackageDirty();
}

void URogueClimbContactBaker::OnRevert_Implementation(UAnimSequence* Animation)
{
	if (!Animation)
	{
		return;
	}

	// Collect first (GetMetaData returns a const ref we must not mutate while removing).
	TArray<UAnimMetaData*> ToRemove;
	for (UAnimMetaData* MetaData : Animation->GetMetaData())
	{
		if (MetaData && MetaData->IsA<URogueClimbContactMetaData>())
		{
			ToRemove.Add(MetaData);
		}
	}

	bool bChanged = ToRemove.Num() > 0;
	for (UAnimMetaData* MetaData : ToRemove)
	{
		Animation->RemoveMetaData(MetaData);
	}

	// Remove the stamped phase curve too, so Apply -> Revert -> Apply stays clean.
	if (!PhaseCurveName.IsNone() && UAnimationBlueprintLibrary::DoesCurveExist(Animation, PhaseCurveName, ERawCurveTrackTypes::RCT_Float))
	{
		UAnimationBlueprintLibrary::RemoveCurve(Animation, PhaseCurveName);
		bChanged = true;
	}

	if (bChanged)
	{
		Animation->MarkPackageDirty();
	}
}
