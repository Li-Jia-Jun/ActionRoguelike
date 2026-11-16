// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueLimbSwivelSolver.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RogueLimbSwivelSolver)

namespace RogueLimbSwivel
{
	// A violation at or below this (cm) counts as allowed.
	constexpr double AllowedTolerance = 1.0e-3;
	// Bisection steps when refining an allowed arc's boundary (each halves the error: 10 deg samples -> ~0.3 deg).
	constexpr int32 BoundaryRefineSteps = 5;

	double WrapAngle(double Angle)
	{
		return FMath::UnwindRadians(Angle);
	}

	double DistanceToSegment(const FVector& Point, const FVector& A, const FVector& B)
	{
		return FMath::PointDistToSegment(Point, A, B);
	}
}

FVector FRogueLimbSwivelSolver::ComputeReferenceBend(const FVector& FKRoot, const FVector& FKEnd, const FVector& HingeWorld, const FVector& IKAxis)
{
	const FVector FKAxis = (FKEnd - FKRoot).GetSafeNormal();
	if (FKAxis.IsNearlyZero() || IKAxis.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	// The hinge should be perpendicular to the FK root->end line (it is for a true hinge); remove any drift.
	const FVector Hinge = FVector::VectorPlaneProject(HingeWorld, FKAxis).GetSafeNormal();
	if (Hinge.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	// Carry the hinge from the FK line onto the IK line with the minimal rotation between them, then the bend direction is
	// Axis x Hinge (for Hinge = upper x lower that points from the root->end line toward the mid joint).
	const FQuat Carry = FQuat::FindBetweenNormals(FKAxis, IKAxis.GetSafeNormal());
	return FVector::CrossProduct(IKAxis.GetSafeNormal(), Carry.RotateVector(Hinge)).GetSafeNormal();
}

FVector FRogueLimbSwivelSolver::ResolveHinge(const FQuat& RootRotation, const FVector& Root, const FVector& Mid, const FVector& End,
	const FVector& FallbackLocalAxis, bool bLearn, bool& bInOutHasLearned, FVector& InOutLearnedLocal)
{
	// The FK chain counts as bent (its hinge readable) past ~5 deg, and fully trusted past ~15 deg (sines of those angles).
	constexpr double ReadableSin = 0.0872;
	constexpr double TrustedSin = 0.2588;

	FVector Hinge = RootRotation.RotateVector(FallbackLocalAxis.GetSafeNormal());
	if (!bLearn)
	{
		return Hinge;
	}
	if (bInOutHasLearned)
	{
		Hinge = RootRotation.RotateVector(InOutLearnedLocal);
	}
	const FVector Cross = FVector::CrossProduct((Mid - Root).GetSafeNormal(), (End - Mid).GetSafeNormal());
	const double SinBend = Cross.Size();
	if (SinBend > ReadableSin)
	{
		const FVector Live = Cross / SinBend;
		const double Trust = FMath::SmoothStep(ReadableSin, TrustedSin, SinBend);
		// Blend toward the live hinge as the bend grows (no pop at the threshold); a remembered hinge pointing the other way
		// (or the unlearned fallback) gives way to the live one outright.
		const bool bBlend = bInOutHasLearned && FVector::DotProduct(Hinge, Live) > 0.0;
		Hinge = bBlend ? (Hinge * (1.0 - Trust) + Live * Trust).GetSafeNormal() : Live;
		InOutLearnedLocal = RootRotation.UnrotateVector(Hinge);
		bInOutHasLearned = true;
	}
	return Hinge;
}

bool FRogueLimbSwivelSolver::Prepare(const FVector& InRoot, const FVector& InTarget, double InUpperLength, double InLowerLength,
	const FVector& ReferenceBend, const FRogueLimbIKLimits& InLimits, const FRogueLimbSwivelConstraints& InConstraints)
{
	bValid = false;
	Limits = InLimits;
	Constraints = InConstraints;
	Root = InRoot;
	UpperLength = InUpperLength;
	LowerLength = InLowerLength;
	if (UpperLength <= UE_KINDA_SMALL_NUMBER || LowerLength <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// Axis: root -> target. A target on the root has no direction - fall back to the reference bend's perpendicular.
	const FVector ToTarget = InTarget - Root;
	double Distance = ToTarget.Size();
	if (Distance > UE_KINDA_SMALL_NUMBER)
	{
		Axis = ToTarget / Distance;
	}
	else
	{
		FVector Unused;
		(ReferenceBend.IsNearlyZero() ? FVector::UpVector : ReferenceBend.GetSafeNormal()).FindBestAxisVectors(Axis, Unused);
		Distance = 0.0;
	}

	// Distance range. The fold limit is a minimum distance: d^2 = L1^2 + L2^2 - 2 L1 L2 cos(theta) at the tightest interior
	// angle. With no limit the limb may fold until the bones overlap (|L1 - L2|).
	const double StraightLength = UpperLength + LowerLength;
	const double MinAngle = FMath::DegreesToRadians(FMath::Clamp<double>(Limits.MinMidAngle, 0.0, 179.0));
	const double MinDistance = (MinAngle > 0.0)
		? FMath::Sqrt(FMath::Max(0.0, UpperLength * UpperLength + LowerLength * LowerLength - 2.0 * UpperLength * LowerLength * FMath::Cos(MinAngle)))
		: FMath::Abs(UpperLength - LowerLength);
	const double MaxDistance = FMath::Max(MinDistance, FMath::Clamp<double>(Limits.MaxReachRatio, 0.05, 4.0) * StraightLength);
	const double Clamped = FMath::Clamp(Distance, MinDistance, MaxDistance);
	ClampedTarget = Root + Axis * Clamped;
	Shortfall = FMath::Abs(Distance - Clamped);

	// The circle, evaluated for a distance the two bones can actually span (stretch past straight -> a point).
	const double Span = FMath::Clamp(Clamped, FMath::Abs(UpperLength - LowerLength) + 1.0e-4, StraightLength * (1.0 - 1.0e-6));
	const double CosAlpha = FMath::Clamp((UpperLength * UpperLength + Span * Span - LowerLength * LowerLength) / (2.0 * UpperLength * Span), -1.0, 1.0);
	const double SinAlpha = FMath::Sqrt(FMath::Max(0.0, 1.0 - CosAlpha * CosAlpha));
	Center = Root + Axis * (UpperLength * CosAlpha);
	Radius = UpperLength * SinAlpha;

	// Basis on the circle's plane: U = the reference bend (phi = 0), V = Axis x U.
	U = FVector::VectorPlaneProject(ReferenceBend, Axis);
	if (U.SizeSquared() < 1.0e-8)
	{
		FVector Unused;
		Axis.FindBestAxisVectors(U, Unused);
	}
	U.Normalize();
	V = FVector::CrossProduct(Axis, U).GetSafeNormal();

	bValid = true;
	return true;
}

FVector FRogueLimbSwivelSolver::BendAt(double Swivel) const
{
	double Sin, Cos;
	FMath::SinCos(&Sin, &Cos, Swivel);
	return U * Cos + V * Sin;
}

double FRogueLimbSwivelSolver::Violation(double Swivel) const
{
	if (!bValid || Constraints.IsEmpty())
	{
		return 0.0;
	}

	const FVector Mid = MidAt(Swivel);
	const FVector LowerMiddle = (Mid + ClampedTarget) * 0.5;  // the shin / forearm's middle - the effector end touches the wall by design
	const double Thickness = Limits.JointRadius;
	double Total = 0.0;

	// Planes: the mid joint only (with the effector on the allowed side too, the whole lower bone is then out of a plane).
	for (const FRogueLimbIKPlane& Plane : Constraints.Planes)
	{
		const FVector Normal = Plane.Normal.GetSafeNormal();
		Total += FMath::Max(0.0, static_cast<double>(Plane.Margin) - FVector::DotProduct(Mid - Plane.Point, Normal));
	}

	for (const FRogueLimbIKSphere& Sphere : Constraints.Spheres)
	{
		const double Clearance = Sphere.Radius + Thickness;
		Total += FMath::Max(0.0, Clearance - FVector::Dist(Mid, Sphere.Center));
		Total += FMath::Max(0.0, Clearance - FVector::Dist(LowerMiddle, Sphere.Center));
	}

	for (const FRogueLimbIKCapsule& Capsule : Constraints.Capsules)
	{
		const double Clearance = Capsule.Radius + Thickness;
		Total += FMath::Max(0.0, Clearance - RogueLimbSwivel::DistanceToSegment(Mid, Capsule.Start, Capsule.End));
		Total += FMath::Max(0.0, Clearance - RogueLimbSwivel::DistanceToSegment(LowerMiddle, Capsule.Start, Capsule.End));
	}
	return Total;
}

bool FRogueLimbSwivelSolver::HasAllowedSwivel(double* OutMinViolation) const
{
	if (OutMinViolation)
	{
		*OutMinViolation = 0.0;
	}
	if (!bValid || Constraints.IsEmpty())
	{
		return bValid;
	}

	const int32 NumSamples = FMath::Clamp(Limits.NumSamples, 8, 128);
	const double Step = UE_TWO_PI / NumSamples;
	double MinViolation = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		const double Penetration = Violation(Index * Step);
		if (Penetration <= RogueLimbSwivel::AllowedTolerance)
		{
			return true;
		}
		MinViolation = FMath::Min(MinViolation, Penetration);
	}
	if (OutMinViolation)
	{
		*OutMinViolation = MinViolation;
	}
	return false;
}

bool FRogueLimbSwivelSolver::SwivelOf(const FVector& Direction, double& OutSwivel) const
{
	const FVector OnPlane = FVector::VectorPlaneProject(Direction, Axis);
	if (OnPlane.SizeSquared() < 1.0e-8)
	{
		return false;
	}
	OutSwivel = FMath::Atan2(FVector::DotProduct(OnPlane, V), FVector::DotProduct(OnPlane, U));
	return true;
}

double FRogueLimbSwivelSolver::RefineBoundary(double Allowed, double Blocked) const
{
	// Unwrapped so the bisection runs along the short way between the two samples.
	double Good = Allowed;
	double Bad = Allowed + RogueLimbSwivel::WrapAngle(Blocked - Allowed);
	for (int32 Step = 0; Step < RogueLimbSwivel::BoundaryRefineSteps; ++Step)
	{
		const double Middle = 0.5 * (Good + Bad);
		if (Violation(Middle) <= RogueLimbSwivel::AllowedTolerance)
		{
			Good = Middle;
		}
		else
		{
			Bad = Middle;
		}
	}
	return Good;
}

bool FRogueLimbSwivelSolver::IsSwivelPathAllowed(double From, double To) const
{
	// The short way round, sampled at the circle's sample spacing (endpoints included).
	const double Delta = RogueLimbSwivel::WrapAngle(To - From);
	const double Step = UE_TWO_PI / FMath::Clamp(Limits.NumSamples, 8, 128);
	const int32 NumSteps = FMath::Max(1, FMath::CeilToInt32(FMath::Abs(Delta) / Step));
	for (int32 Index = 0; Index <= NumSteps; ++Index)
	{
		if (Violation(From + Delta * Index / NumSteps) > RogueLimbSwivel::AllowedTolerance)
		{
			return false;
		}
	}
	return true;
}

double FRogueLimbSwivelSolver::ChooseSwivel(double Preferred, bool bHasPrevious, double Previous, double MaxStep, bool& bOutAllowed) const
{
	bOutAllowed = true;
	if (!bValid)
	{
		return Preferred;
	}

	double Target = Preferred;
	if (!Constraints.IsEmpty())
	{
		// Sample the circle starting AT the preferred swivel (sample 0 = Preferred exactly).
		const int32 NumSamples = FMath::Clamp(Limits.NumSamples, 8, 128);
		const double Step = UE_TWO_PI / NumSamples;
		auto SampleAngle = [Preferred, Step](int32 Index) { return Preferred + Index * Step; };

		TArray<bool, TInlineAllocator<128>> Allowed;
		Allowed.SetNumUninitialized(NumSamples);
		int32 NumAllowed = 0;
		int32 LeastViolating = 0;
		double LeastViolation = TNumericLimits<double>::Max();
		for (int32 Index = 0; Index < NumSamples; ++Index)
		{
			const double Penetration = Violation(SampleAngle(Index));
			Allowed[Index] = Penetration <= RogueLimbSwivel::AllowedTolerance;
			NumAllowed += Allowed[Index] ? 1 : 0;
			if (Penetration < LeastViolation)
			{
				LeastViolation = Penetration;
				LeastViolating = Index;
			}
		}

		if (NumAllowed == 0)
		{
			// Nothing allowed: the least-bad swivel. The caller's fallbacks (the component's leash / re-grip) own this case.
			Target = SampleAngle(LeastViolating);
		}
		else if (NumAllowed < NumSamples)
		{
			const bool bPreferredAllowed = Allowed[0];
			if (bPreferredAllowed && (!bHasPrevious || IsSwivelPathAllowed(Previous, Preferred)))
			{
				// The common case: Preferred is allowed and nothing blocks the way to it from last frame's swivel.
				Target = Preferred;
			}
			else
			{
				// Candidates: Preferred (if allowed) and every allowed/blocked EDGE, refined by bisection. An allowed region's
				// nearest point to Preferred is always one of these.
				struct FCandidate
				{
					double Swivel = 0.0;
					double Distance = 0.0;  // |Swivel - Preferred|
				};
				TArray<FCandidate, TInlineAllocator<16>> Candidates;
				if (bPreferredAllowed)
				{
					Candidates.Add({ Preferred, 0.0 });
				}
				for (int32 Index = 0; Index < NumSamples; ++Index)
				{
					const int32 Next = (Index + 1) % NumSamples;
					if (Allowed[Index] == Allowed[Next])
					{
						continue;
					}
					const double Edge = Allowed[Index]
						? RefineBoundary(SampleAngle(Index), SampleAngle(Index) + Step)
						: RefineBoundary(SampleAngle(Next), SampleAngle(Next) - Step);
					const double Distance = FMath::Abs(RogueLimbSwivel::WrapAngle(Edge - Preferred));
					Candidates.Add({ Edge, Distance });
				}

				// The best candidate overall, and the best on the limb's CURRENT SIDE: one reachable from last frame's swivel the
				// short way without crossing a blocked region (the way the speed cap will move it).
				const FCandidate* Best = nullptr;
				const FCandidate* BestHome = nullptr;
				for (const FCandidate& Candidate : Candidates)
				{
					if (!Best || Candidate.Distance < Best->Distance)
					{
						Best = &Candidate;
					}
					if (bHasPrevious && (!BestHome || Candidate.Distance < BestHome->Distance) && IsSwivelPathAllowed(Previous, Candidate.Swivel))
					{
						BestHome = &Candidate;
					}
				}

				// Hysteresis on WHICH side: keep the current side unless the other is closer to Preferred by more than
				// ArcHysteresis - so a knee held at one edge of a blocked region stays there instead of swapping sides as
				// Preferred crosses the region's middle.
				const double Hysteresis = FMath::DegreesToRadians(static_cast<double>(Limits.ArcHysteresis));
				const FCandidate* Chosen = (BestHome && BestHome->Distance <= Best->Distance + Hysteresis) ? BestHome : Best;
				Target = Chosen->Swivel;
			}
		}
	}

	// Speed cap from last frame's swivel (the short way round): a forced change of side sweeps instead of flipping.
	double Result = Target;
	if (bHasPrevious && MaxStep > 0.0)
	{
		Result = Previous + FMath::Clamp(RogueLimbSwivel::WrapAngle(Target - Previous), -MaxStep, MaxStep);
	}
	bOutAllowed = Violation(Result) <= RogueLimbSwivel::AllowedTolerance;
	return RogueLimbSwivel::WrapAngle(Result);
}
