// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "RogueLimbSwivelSolver.generated.h"

/*
 * Swivel-angle limb solver - the shared math behind the climb limb IK (see Docs/climb/limb-ik.md). Plain C++ so BOTH
 * callers run the same code:
 *   - the Control Rig unit (FRigUnit_RogueClimbLimbIK) - HOW the limb bends: picks the swivel on the current pose;
 *   - the mover component (later) - WHERE a target may go: "is there any allowed swivel for this target?", on last frame's pose.
 *
 * A two-bone limb has one free angle once the root->target distance d is fixed: the mid joint (knee/elbow) lies on a circle
 * around the root->target axis - centre C = Root + Axis * a, radius r, with a = L1 cos(alpha), r = L1 sin(alpha) from the law
 * of cosines. The SWIVEL phi picks the point on that circle: Mid(phi) = C + r (cos(phi) U + sin(phi) V), U = the reference
 * bend direction (phi = 0), V = Axis x U (right-handed about Axis). Every limit becomes a condition on d or on phi:
 *   - knee/elbow fold limit and max reach -> a range for d (the target is clamped into it);
 *   - planes / spheres / capsules the mid joint (and the lower bone) must stay out of -> blocked arcs of the circle.
 * The solver samples the circle, finds the allowed arcs, and picks the allowed swivel nearest the preferred one - with
 * hysteresis on WHICH side of a blocked region (so it doesn't hop between two allowed positions) and a speed cap (so it
 * never flips in a frame).
 */

/** A half-space the mid joint (knee/elbow) must stay in front of: Dot(Mid - Point, Normal) >= Margin. Solver space. */
USTRUCT(BlueprintType)
struct FRogueLimbIKPlane
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LimbIK")
	FVector Point = FVector::ZeroVector;

	// Points to the ALLOWED side (out of the wall).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LimbIK")
	FVector Normal = FVector::UpVector;

	// Required clearance of the mid joint's centre from the plane (cm) - include the joint's own thickness.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LimbIK")
	float Margin = 6.0f;
};

/** A ball the mid joint and the lower bone's middle must stay out of (e.g. a world-geometry blocker). Solver space. */
USTRUCT(BlueprintType)
struct FRogueLimbIKSphere
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LimbIK")
	FVector Center = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LimbIK", meta = (ClampMin = "0.0"))
	float Radius = 5.0f;
};

/** A capsule (segment + radius) the mid joint and the lower bone's middle must stay out of (e.g. the torso). Solver space. */
USTRUCT(BlueprintType)
struct FRogueLimbIKCapsule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LimbIK")
	FVector Start = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LimbIK")
	FVector End = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LimbIK", meta = (ClampMin = "0.0"))
	float Radius = 10.0f;
};

/** Per-limb solver limits. Angles in degrees, lengths in cm. */
USTRUCT(BlueprintType)
struct FRogueLimbIKLimits
{
	GENERATED_BODY()

	// The knee/elbow's tightest fold, as the INTERIOR angle between the two bones (0 = no limit: the limb may fold flat).
	// The fold is a function of d alone, so this is a minimum root->target distance: a closer target is pushed out to it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LimbIK", meta = (ClampMin = "0.0", ClampMax = "179.0", ForceUnits = "deg"))
	float MinMidAngle = 30.0f;

	// The target is clamped to this fraction of the straight limb (upper + lower). 1 = the solver's own straight limit;
	// raise it above 1 only when stretch is enabled. (The mover component's reach cap already softens over-reach.)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LimbIK", meta = (ClampMin = "0.05", ClampMax = "4.0"))
	float MaxReachRatio = 1.0f;

	// Thickness of the mid joint and the lower bone for the sphere / capsule tests (added to each blocker's radius).
	// Planes use their own Margin instead.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LimbIK", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float JointRadius = 6.0f;

	// Swivel samples around the circle (more = finer allowed-arc detection; each boundary is refined by bisection anyway).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LimbIK", meta = (ClampMin = "8", ClampMax = "128"))
	int32 NumSamples = 36;

	// Keep the limb on its current side of a blocked region unless the other side gets closer to the preferred swivel by
	// more than this - stops the knee hopping between two nearly equally good allowed positions.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LimbIK", meta = (ClampMin = "0.0", ClampMax = "180.0", ForceUnits = "deg"))
	float ArcHysteresis = 20.0f;
};

/** Non-owning views of the constraint primitives for one solve (all in solver space). */
struct FRogueLimbSwivelConstraints
{
	TConstArrayView<FRogueLimbIKPlane> Planes;
	TConstArrayView<FRogueLimbIKSphere> Spheres;
	TConstArrayView<FRogueLimbIKCapsule> Capsules;

	bool IsEmpty() const { return Planes.IsEmpty() && Spheres.IsEmpty() && Capsules.IsEmpty(); }
};

/** One solve: Prepare() builds the mid-joint circle for a root + target, then query it (feasibility) or choose a swivel. */
class FRogueLimbSwivelSolver
{
public:
	/**
	 * The swivel's phi = 0 direction, from the FK chain: the bend direction the FK hinge axis implies, carried from the
	 * FK root->end line onto the IK root->target line by the minimal rotation between them. Reproduces the FK mid joint
	 * exactly when the target is the FK end, and never degenerates (the hinge axis comes from the bone's ORIENTATION, so it
	 * is still well defined when the limb is straight). HingeWorld = (upper x lower) for a bent FK limb.
	 * Returns zero when the inputs are degenerate (Prepare then picks an arbitrary perpendicular).
	 */
	static FVector ComputeReferenceBend(const FVector& FKRoot, const FVector& FKEnd, const FVector& HingeWorld, const FVector& IKAxis);

	/**
	 * The knee/elbow hinge (world) of an FK chain: (upper x lower) whenever the chain is bent past ~5 deg (fully trusted
	 * past ~15 deg), remembered in the root bone's local space (InOutLearnedLocal) for when it's straight. Before anything
	 * is learned - or with bLearn off - FallbackLocalAxis (root-bone space). Shared by the Climb Limb IK and Climb Surface
	 * Warp rig nodes, each keeping its own memory.
	 */
	static FVector ResolveHinge(const FQuat& RootRotation, const FVector& Root, const FVector& Mid, const FVector& End,
		const FVector& FallbackLocalAxis, bool bLearn, bool& bInOutHasLearned, FVector& InOutLearnedLocal);

	/**
	 * Set up the circle. The target is clamped into the limb's distance range (MinMidAngle .. MaxReachRatio); the clamp
	 * shortfall is kept (GetShortfall). ReferenceBend = the phi = 0 direction (any vector; projected onto the circle's
	 * plane). The constraint views must outlive the solver's queries. Returns false on degenerate input.
	 */
	bool Prepare(const FVector& InRoot, const FVector& InTarget, double UpperLength, double LowerLength,
		const FVector& ReferenceBend, const FRogueLimbIKLimits& InLimits, const FRogueLimbSwivelConstraints& InConstraints);

	/** Total constraint penetration (cm) with the mid joint at Swivel (radians from U). 0 = allowed. */
	double Violation(double Swivel) const;

	/** Feasibility only (the component's question): is any swivel allowed? Always true with no constraints. */
	bool HasAllowedSwivel(double* OutMinViolation = nullptr) const;

	/**
	 * The rig's question: the allowed swivel nearest Preferred (radians from U), then moved from Previous by at most MaxStep
	 * (radians, <= 0 = unlimited).
	 *  - Preferred allowed and the short way to it from Previous clear -> Preferred.
	 *  - Otherwise the candidates are Preferred (if allowed) and every allowed/blocked edge. The best on the limb's current
	 *    side (reachable from Previous the short way without crossing a blocked region) wins unless another is closer to
	 *    Preferred by more than ArcHysteresis - so the limb keeps its side of a blocked region until the other side is
	 *    clearly better.
	 *  - Nothing allowed -> the least-violating swivel.
	 * bOutAllowed = the returned swivel violates nothing.
	 */
	double ChooseSwivel(double Preferred, bool bHasPrevious, double Previous, double MaxStep, bool& bOutAllowed) const;

	/** The swivel of a direction (e.g. last frame's bend), projected onto the circle's plane. false if it's along the axis. */
	bool SwivelOf(const FVector& Direction, double& OutSwivel) const;

	FVector BendAt(double Swivel) const;
	FVector MidAt(double Swivel) const { return Center + BendAt(Swivel) * Radius; }

	/** A pole far out along the bend at Swivel - hand it to a standard two-bone solve (e.g. AnimationCore::SolveTwoBoneIK). */
	FVector PoleAt(double Swivel) const { return Center + BendAt(Swivel) * (UpperLength + LowerLength); }

	bool IsValid() const { return bValid; }
	const FVector& GetRoot() const { return Root; }
	const FVector& GetClampedTarget() const { return ClampedTarget; }  // the target after the distance clamp
	const FVector& GetAxis() const { return Axis; }
	const FVector& GetCenter() const { return Center; }
	const FVector& GetU() const { return U; }
	const FVector& GetV() const { return V; }
	double GetRadius() const { return Radius; }
	double GetShortfall() const { return Shortfall; }  // how far the distance clamp moved the target (cm)

private:
	// Bisection between an allowed and a blocked swivel - returns an allowed swivel next to the boundary.
	double RefineBoundary(double Allowed, double Blocked) const;

	// Whether every swivel on the short way from From to To is allowed (sampled at the circle's sample spacing).
	bool IsSwivelPathAllowed(double From, double To) const;

	FRogueLimbIKLimits Limits;
	FRogueLimbSwivelConstraints Constraints;
	FVector Root = FVector::ZeroVector;
	FVector ClampedTarget = FVector::ZeroVector;
	FVector Axis = FVector::ForwardVector;
	FVector Center = FVector::ZeroVector;
	FVector U = FVector::RightVector;
	FVector V = FVector::UpVector;
	double UpperLength = 0.0;
	double LowerLength = 0.0;
	double Radius = 0.0;
	double Shortfall = 0.0;
	bool bValid = false;
};
