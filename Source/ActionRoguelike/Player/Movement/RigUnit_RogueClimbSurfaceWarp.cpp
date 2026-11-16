// Fill out your copyright notice in the Description page of Project Settings.

#include "RigUnit_RogueClimbSurfaceWarp.h"

#include "RigUnit_RogueClimbLimbIK.h"
#include "RogueLimbSwivelSolver.h"
#include "Units/RigUnitContext.h"
#include "Math/ControlRigMathLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigUnit_RogueClimbSurfaceWarp)

FRigUnit_RogueClimbSurfaceWarp_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	URigHierarchy* Hierarchy = ExecuteContext.Hierarchy;
	if (Hierarchy == nullptr)
	{
		return;
	}

	Shift = 0.0f;
	bReachLimited = false;

	if (!CachedItemA.UpdateCache(ItemA, Hierarchy) || !CachedItemB.UpdateCache(ItemB, Hierarchy)
		|| !CachedEffectorItem.UpdateCache(EffectorItem, Hierarchy))
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("ItemA / ItemB / EffectorItem not found."));
		return;
	}

	// The pose as it arrives (the clip, plus the weight shift).
	FTransform TransformA = Hierarchy->GetGlobalTransform(CachedItemA);
	const FTransform FKTransformB = Hierarchy->GetGlobalTransform(CachedItemB);
	const FTransform FKTransformC = Hierarchy->GetGlobalTransform(CachedEffectorItem);
	const FVector Root = TransformA.GetLocation();
	const FVector FKMid = FKTransformB.GetLocation();
	const FVector FKEnd = FKTransformC.GetLocation();

	// Keep learning the hinge every frame, warped or not, so it's ready the moment the warp comes on.
	const FVector Hinge = FRogueLimbSwivelSolver::ResolveHinge(TransformA.GetRotation(), Root, FKMid, FKEnd, HingeAxis,
		bLearnHingeAxis, WorkData.bHasLearnedHinge, WorkData.LearnedHingeLocal);

	const float Alpha = FMath::Clamp(Weight, 0.0f, 1.0f);
	if (Alpha <= SMALL_NUMBER || !SurfaceMap.bValid)
	{
		return;
	}

	// The skeleton's own bone lengths, from the reference pose (scaled like the live chain).
	const float LengthA = RogueClimbRig::InitialBoneLength(Hierarchy, CachedItemA, CachedItemB, TransformA);
	const float LengthB = RogueClimbRig::InitialBoneLength(Hierarchy, CachedItemB, CachedEffectorItem, FKTransformB);
	if (LengthA < SMALL_NUMBER || LengthB < SMALL_NUMBER)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Bone lengths can't be read from the hierarchy's reference pose."));
		return;
	}

	// The map into rig space with the transform the rig renders with THIS frame (it was built in world space before the
	// movement update moved the body).
	const FClimbSurfaceMap Map = SurfaceMap.Transformed(ExecuteContext.GetToWorldSpaceTransform().Inverse());

	// --- The effector onto the surface, within reach.
	FVector Warped;
	FQuat SurfaceRotation;
	bool bLimited = false;
	if (Reach.Limit > SMALL_NUMBER)
	{
		// The reach is measured to the CONTACT point (palm / ball), exactly as the component's leash measures it. The search
		// moves the effector; shifting the root by the contact's offset from it makes "effector to shifted root" the contact's
		// distance (the warp's small turn of that offset ignored). The clip's own distance, if farther, is kept.
		FVector FKContact = FKEnd;
		const int32 ContactIndex = Reach.ContactBone.IsNone() ? INDEX_NONE
			: Hierarchy->GetIndex(FRigElementKey(Reach.ContactBone, ERigElementType::Bone));
		if (ContactIndex != INDEX_NONE)
		{
			FKContact = Hierarchy->GetGlobalTransform(ContactIndex).TransformPosition(Reach.ContactOffset);
		}
		const FVector ContactShift = FKContact - FKEnd;
		const double MaxReach = FMath::Max(static_cast<double>(Reach.Limit), FVector::Dist(Root, FKContact));
		if (!Map.WarpPointWithinReach(FKEnd, Root - ContactShift, MaxReach, Warped, SurfaceRotation, &bLimited))
		{
			return; // no surface anywhere near this limb - leave the clip's pose
		}
	}
	else if (!Map.WarpPoint(FKEnd, Warped, SurfaceRotation))
	{
		return;
	}
	const FVector Target = FMath::Lerp(FKEnd, Warped, static_cast<double>(Alpha));
	const FQuat TargetRotation = (FQuat::Slerp(FQuat::Identity, SurfaceRotation, Alpha) * FKTransformC.GetRotation()).GetNormalized();

	// --- Rebuild the limb: root fixed, the skeleton's bone lengths, the clip's own bend plane (swivel 0, no colliders), the
	// fold limit.
	FRogueLimbIKLimits Limits;
	Limits.MinMidAngle = MinMidAngle;
	Limits.MaxReachRatio = 1.0f;
	FRogueLimbSwivelSolver Solver;
	const FVector IKAxis = (Target - Root).GetSafeNormal();
	if (!Solver.Prepare(Root, Target, LengthA, LengthB, FRogueLimbSwivelSolver::ComputeReferenceBend(Root, FKEnd, Hinge, IKAxis),
		Limits, FRogueLimbSwivelConstraints()))
	{
		return;
	}

	FTransform TransformB = TransformA;  // the engine convention: B starts from A's rotation
	TransformB.SetLocation(FKMid);
	FTransform TransformC = FKTransformC;
	TransformC.SetRotation(TargetRotation);
	TransformC.SetLocation(Solver.GetClampedTarget());
	FControlRigMathLibrary::SolveBasicTwoBoneIK(TransformA, TransformB, TransformC, Solver.PoleAt(0.0), PrimaryAxis, SecondaryAxis,
		SecondaryAxisWeight, LengthA, LengthB, /*bEnableStretch*/ false, 1.0f, 1.0f);

	Hierarchy->SetGlobalTransform(CachedItemA, TransformA, bPropagateToChildren);
	Hierarchy->SetGlobalTransform(CachedItemB, TransformB, bPropagateToChildren);
	Hierarchy->SetGlobalTransform(CachedEffectorItem, TransformC, bPropagateToChildren);

	Shift = static_cast<float>(FVector::Dist(FKEnd, TransformC.GetLocation()));
	bReachLimited = bLimited || Solver.GetShortfall() > 0.5;

	// --- Debug.
	FRigVMDrawInterface* Draw = ExecuteContext.GetDrawInterface();
	if (Draw == nullptr || !DebugSettings.bEnabled)
	{
		return;
	}
	const FTransform& Offset = DebugSettings.WorldOffset;
	const FLinearColor WarpColor = bReachLimited ? FLinearColor(1.0f, 0.55f, 0.0f) : FLinearColor(0.0f, 0.85f, 0.9f);
	Draw->DrawLine(Offset, FKEnd, TransformC.GetLocation(), WarpColor, 0.5f);
	Draw->DrawPoint(Offset, FKEnd, 4.0f, FLinearColor(0.6f, 0.6f, 0.7f));
	Draw->DrawPoint(Offset, TransformC.GetLocation(), 6.0f, WarpColor);
	if (DebugSettings.bDrawSurfaceMap)
	{
		for (int32 Row = 0; Row < Map.Rows; ++Row)
		{
			for (int32 Column = 0; Column < Map.Columns; ++Column)
			{
				if (Map.Cell(Column, Row).bValid)
				{
					Draw->DrawPoint(Offset, Map.CellSurfacePoint(Column, Row), 3.0f, FLinearColor(0.0f, 0.75f, 0.6f));
				}
			}
		}
	}
}
