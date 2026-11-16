// Fill out your copyright notice in the Description page of Project Settings.

#include "RigUnit_RogueClimbLimbIK.h"

#include "Units/RigUnitContext.h"
#include "Math/ControlRigMathLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_RogueClimbLimbIK)

namespace RogueClimbLimbIK
{
	// A gap longer than this between evaluations (a hitch, the rig paused) drops last frame's swivel instead of easing from it.
	constexpr float MaxContinuousDeltaTime = 0.25f;
	constexpr int32 DebugCircleSegments = 24;
}

float RogueClimbRig::InitialBoneLength(const URigHierarchy* Hierarchy, const FCachedRigElement& From, const FCachedRigElement& To,
	const FTransform& LiveFrom)
{
	const FTransform InitialFrom = Hierarchy->GetInitialGlobalTransform(From);
	FVector Scale = FVector::OneVector;
	if (InitialFrom.GetScale3D().SizeSquared() > SMALL_NUMBER)
	{
		Scale = LiveFrom.GetScale3D() / InitialFrom.GetScale3D();
	}
	const FVector Delta = (InitialFrom.GetLocation() - Hierarchy->GetInitialGlobalTransform(To).GetLocation()) * Scale;
	return static_cast<float>(Delta.Size());
}

FRigUnit_RogueClimbLimbIK_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	URigHierarchy* Hierarchy = ExecuteContext.Hierarchy;
	if (Hierarchy == nullptr)
	{
		return;
	}

	Swivel = 0.0f;
	bAllowed = true;
	Strain = 0.0f;

	if (!CachedItemA.UpdateCache(ItemA, Hierarchy) || !CachedItemB.UpdateCache(ItemB, Hierarchy))
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("ItemA / ItemB not found."));
		return;
	}
	CachedEffectorItem.UpdateCache(EffectorItem, Hierarchy);
	const bool bHasEffectorItem = CachedEffectorItem.IsValid();

	// The FK chain as the clip (plus anything earlier in the graph, e.g. the weight shift) left it.
	FTransform TransformA = Hierarchy->GetGlobalTransform(CachedItemA);
	const FTransform FKTransformA = TransformA;
	const FTransform FKTransformB = Hierarchy->GetGlobalTransform(CachedItemB);
	const FTransform FKTransformC = bHasEffectorItem ? Hierarchy->GetGlobalTransform(CachedEffectorItem) : Effector;
	const FVector Root = FKTransformA.GetLocation();
	const FVector FKMid = FKTransformB.GetLocation();
	const FVector FKEnd = FKTransformC.GetLocation();

	// The skeleton's own bone lengths, from the reference pose (scaled like the live chain).
	if (!bHasEffectorItem)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("EffectorItem not found - it's needed for the second bone's length."));
		return;
	}
	const float LengthA = RogueClimbRig::InitialBoneLength(Hierarchy, CachedItemA, CachedItemB, FKTransformA);
	const float LengthB = RogueClimbRig::InitialBoneLength(Hierarchy, CachedItemB, CachedEffectorItem, FKTransformB);
	if (LengthA < SMALL_NUMBER || LengthB < SMALL_NUMBER)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Bone lengths can't be read from the hierarchy's reference pose."));
		return;
	}

	// --- The hinge (world): the FK limb's own (upper x lower) when it's bent, remembered in ItemA's space for when it's
	// straight - so phi = 0 is always the clip's bend, even through a fully extended FK limb.
	const FVector Hinge = FRogueLimbSwivelSolver::ResolveHinge(FKTransformA.GetRotation(), Root, FKMid, FKEnd, HingeAxis,
		bLearnHingeAxis, WorkData.bHasLearnedHinge, WorkData.LearnedHingeLocal);

	const float DeltaTime = ExecuteContext.GetDeltaTime<float>();
	const float Alpha = FMath::Clamp(Weight, 0.0f, 1.0f);
	if (Alpha <= SMALL_NUMBER)
	{
		// FK this frame. Remember the FK bend so the swivel eases in from it (MaxSwivelSpeed) when the weight returns.
		const FVector FKAxis = (FKEnd - Root).GetSafeNormal();
		const FVector FKBend = FRogueLimbSwivelSolver::ComputeReferenceBend(Root, FKEnd, Hinge, FKAxis);
		WorkData.bHasPreviousBend = !FKBend.IsNearlyZero();
		WorkData.PreviousBend = FKBend;
		return;
	}

	// --- The goal. Target blend: aim at a point between the FK end and the effector, at full solve strength.
	FTransform Goal = Effector;
	if (BlendMode == ERogueClimbLimbIKBlend::Target && Alpha < 1.0f - SMALL_NUMBER)
	{
		Goal.SetLocation(FMath::Lerp(FKEnd, Effector.GetLocation(), static_cast<double>(Alpha)));
		Goal.SetRotation(FQuat::Slerp(FKTransformC.GetRotation(), Effector.GetRotation(), Alpha).GetNormalized());
	}

	// --- Colliders: bone-attached body capsules resolved to rig space, plus the rig-space inputs.
	TArray<FRogueLimbIKCapsule, TInlineAllocator<16>> AllCapsules;
	AllCapsules.Append(Capsules);
	for (const FRogueLimbIKBodyCollider& Collider : BodyColliders)
	{
		const int32 BoneIndex = Hierarchy->GetIndex(Collider.Bone);
		if (BoneIndex == INDEX_NONE)
		{
			continue;
		}
		const FTransform BoneTransform = Hierarchy->GetGlobalTransform(BoneIndex);
		FRogueLimbIKCapsule& Capsule = AllCapsules.AddDefaulted_GetRef();
		Capsule.Start = BoneTransform.TransformPosition(Collider.Start);
		Capsule.End = BoneTransform.TransformPosition(Collider.End);
		Capsule.Radius = Collider.Radius;
	}
	FRogueLimbSwivelConstraints Constraints;
	Constraints.Planes = Planes;
	Constraints.Spheres = Spheres;
	Constraints.Capsules = AllCapsules;

	// --- The swivel search.
	const FVector IKAxis = (Goal.GetLocation() - Root).GetSafeNormal();
	const FVector ReferenceBend = FRogueLimbSwivelSolver::ComputeReferenceBend(Root, FKEnd, Hinge, IKAxis);
	FRogueLimbSwivelSolver Solver;
	if (!Solver.Prepare(Root, Goal.GetLocation(), LengthA, LengthB, ReferenceBend, Limits, Constraints))
	{
		return;
	}

	double Previous = 0.0;
	const bool bHasPrevious = WorkData.bHasPreviousBend && DeltaTime < RogueClimbLimbIK::MaxContinuousDeltaTime
		&& Solver.SwivelOf(WorkData.PreviousBend, Previous);
	const double MaxStep = (MaxSwivelSpeed > 0.0f && DeltaTime > 0.0f) ? FMath::DegreesToRadians(static_cast<double>(MaxSwivelSpeed)) * DeltaTime : 0.0;
	const double Preferred = FMath::DegreesToRadians(static_cast<double>(PreferredSwivelOffset));
	bool bSwivelAllowed = true;
	const double Chosen = Solver.ChooseSwivel(Preferred, bHasPrevious, Previous, MaxStep, bSwivelAllowed);

	// --- The bones: the engine's two-bone solve (bone orientation, secondary-axis roll, stretch) with a pole at the chosen
	// swivel - far out along the bend, so the solve's bend direction is exactly it.
	FTransform TransformB = TransformA;  // the engine convention: B starts from A's rotation
	TransformB.SetLocation(FKMid);
	FTransform TransformC = Goal;
	TransformC.SetLocation(Solver.GetClampedTarget());
	FControlRigMathLibrary::SolveBasicTwoBoneIK(TransformA, TransformB, TransformC, Solver.PoleAt(Chosen), PrimaryAxis, SecondaryAxis,
		SecondaryAxisWeight, LengthA, LengthB, bEnableStretch, StretchStartRatio, StretchMaximumRatio);

	if (BlendMode == ERogueClimbLimbIKBlend::Rotation && Alpha < 1.0f - SMALL_NUMBER)
	{
		const FVector PositionB = TransformA.InverseTransformPosition(TransformB.GetLocation());
		const FVector PositionC = TransformB.InverseTransformPosition(TransformC.GetLocation());
		TransformA.SetRotation(FQuat::Slerp(FKTransformA.GetRotation(), TransformA.GetRotation(), Alpha));
		TransformB.SetRotation(FQuat::Slerp(FKTransformB.GetRotation(), TransformB.GetRotation(), Alpha));
		TransformC.SetRotation(FQuat::Slerp(FKTransformC.GetRotation(), TransformC.GetRotation(), Alpha));
		TransformB.SetLocation(TransformA.TransformPosition(PositionB));
		TransformC.SetLocation(TransformB.TransformPosition(PositionC));
	}

	Hierarchy->SetGlobalTransform(CachedItemA, TransformA, bPropagateToChildren);
	Hierarchy->SetGlobalTransform(CachedItemB, TransformB, bPropagateToChildren);
	if (bHasEffectorItem)
	{
		Hierarchy->SetGlobalTransform(CachedEffectorItem, TransformC, bPropagateToChildren);
	}

	WorkData.PreviousBend = Solver.BendAt(Chosen);
	WorkData.bHasPreviousBend = true;
	Swivel = static_cast<float>(FMath::RadiansToDegrees(Chosen));
	bAllowed = bSwivelAllowed;
	Strain = static_cast<float>(Solver.GetShortfall() + Solver.Violation(Chosen));

	// --- Debug.
	FRigVMDrawInterface* Draw = ExecuteContext.GetDrawInterface();
	if (Draw == nullptr || !DebugSettings.bEnabled)
	{
		return;
	}
	const FTransform& Offset = DebugSettings.WorldOffset;
	const FLinearColor AllowedColor(0.1f, 0.85f, 0.4f);
	const FLinearColor BlockedColor(0.95f, 0.25f, 0.15f);
	const FLinearColor ColliderColor(0.6f, 0.6f, 0.7f);

	// The mid-joint circle, coloured per arc.
	const double SegmentAngle = UE_TWO_PI / RogueClimbLimbIK::DebugCircleSegments;
	for (int32 Segment = 0; Segment < RogueClimbLimbIK::DebugCircleSegments; ++Segment)
	{
		const double From = Segment * SegmentAngle;
		const double To = From + SegmentAngle;
		const bool bSegmentAllowed = Solver.Violation(0.5 * (From + To)) <= 1.0e-3;
		Draw->DrawLine(Offset, Solver.MidAt(From), Solver.MidAt(To), bSegmentAllowed ? AllowedColor : BlockedColor, 0.5f);
	}
	// The chosen mid joint (white spoke) and the preferred one (yellow spoke) when the colliders moved it.
	Draw->DrawLine(Offset, Solver.GetCenter(), Solver.MidAt(Chosen), FLinearColor::White, 0.5f);
	if (FMath::Abs(FMath::UnwindRadians(Chosen - Preferred)) > FMath::DegreesToRadians(0.5))
	{
		Draw->DrawLine(Offset, Solver.GetCenter(), Solver.MidAt(Preferred), FLinearColor::Yellow, 0.25f);
	}
	// The part of the target the distance clamp held back.
	if (Solver.GetShortfall() > 0.1)
	{
		Draw->DrawLine(Offset, Solver.GetClampedTarget(), Goal.GetLocation(), FLinearColor::Yellow, 0.5f);
	}

	if (DebugSettings.bDrawColliders)
	{
		for (const FRogueLimbIKCapsule& Capsule : AllCapsules)
		{
			Draw->DrawLine(Offset, Capsule.Start, Capsule.End, ColliderColor, 0.5f);
			Draw->DrawCircle(Offset, FTransform(Capsule.Start), Capsule.Radius, ColliderColor, 0.25f, 16);
			Draw->DrawCircle(Offset, FTransform(Capsule.End), Capsule.Radius, ColliderColor, 0.25f, 16);
		}
		for (const FRogueLimbIKSphere& Sphere : Spheres)
		{
			Draw->DrawCircle(Offset, FTransform(Sphere.Center), Sphere.Radius, ColliderColor, 0.25f, 16);
		}
		for (const FRogueLimbIKPlane& Plane : Planes)
		{
			Draw->DrawLine(Offset, Plane.Point, Plane.Point + Plane.Normal.GetSafeNormal() * 20.0, ColliderColor, 0.5f);
		}
	}
}
