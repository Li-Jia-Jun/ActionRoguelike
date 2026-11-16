// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Units/Highlevel/RigUnit_HighlevelBase.h"
#include "RogueLimbSwivelSolver.h"
#include "RigUnit_RogueClimbLimbIK.generated.h"

class URigHierarchy;

namespace RogueClimbRig
{
	/** A bone's length from the hierarchy's initial pose, scaled like the live chain - as the engine's Basic IK measures it. */
	float InitialBoneLength(const URigHierarchy* Hierarchy, const FCachedRigElement& From, const FCachedRigElement& To,
		const FTransform& LiveFrom);
}

/** How a partial Weight blends the limb between its FK pose and the IK solve. */
UENUM()
enum class ERogueClimbLimbIKBlend : uint8
{
	// Solve at full strength toward a target blended FK end -> Effector. Every in-between frame is a real single-chain pose,
	// so a blend never sweeps through an odd bend. Weight 0 reproduces the FK pose.
	Target,
	// The engine Basic IK behaviour: solve toward the Effector, then slerp each bone's rotation FK -> IK by Weight.
	Rotation
};

/** A capsule attached to a bone (its local space) that the mid joint and lower bone must stay out of - the character's own body. */
USTRUCT(BlueprintType)
struct FRogueLimbIKBodyCollider
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LimbIK")
	FRigElementKey Bone = FRigElementKey(NAME_None, ERigElementType::Bone);

	// Capsule segment ends in the bone's local space.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LimbIK")
	FVector Start = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LimbIK")
	FVector End = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LimbIK", meta = (ClampMin = "0.0"))
	float Radius = 12.0f;
};

USTRUCT()
struct FRogueClimbLimbIKDebugSettings
{
	GENERATED_BODY()

	// Draw the mid-joint circle (green = allowed swivel, red = blocked), the chosen and preferred mid joints, and any clamp.
	// In game this needs `ControlRig.EnableDrawInterfaceInGame 1`.
	UPROPERTY(EditAnywhere, meta = (Input), Category = "DebugSettings")
	bool bEnabled = false;

	// Also draw the colliders (body capsules, capsules, spheres; planes as a short normal).
	UPROPERTY(EditAnywhere, meta = (Input, EditCondition = "bEnabled"), Category = "DebugSettings")
	bool bDrawColliders = true;

	UPROPERTY(EditAnywhere, meta = (Input, EditCondition = "bEnabled"), Category = "DebugSettings")
	FTransform WorldOffset = FTransform::Identity;
};

/** Persistent per-node state (hidden): last frame's bend for the swivel speed cap / arc hysteresis, and the learned hinge. */
USTRUCT()
struct FRogueClimbLimbIKWorkData
{
	GENERATED_BODY()

	UPROPERTY()
	bool bHasPreviousBend = false;

	UPROPERTY()
	FVector PreviousBend = FVector::ZeroVector;

	UPROPERTY()
	bool bHasLearnedHinge = false;

	UPROPERTY()
	FVector LearnedHingeLocal = FVector::ZeroVector;  // in ItemA's local space
};

/**
 * Climb limb IK - a two-bone IK that picks the knee/elbow SWIVEL instead of taking a pole vector (Docs/climb/limb-ik.md).
 * Drop-in for Basic IK: same items, effector, axes and stretch. Bone lengths always come from the skeleton's reference pose
 * (what Basic IK does with its lengths at 0), so EffectorItem is required.
 *
 *  - phi = 0 is the FK clip's own bend, from the FK hinge axis carried onto the IK line - so it can't flip the way a pole
 *    at the FK elbow does (straight FK limb, target far from the FK end).
 *  - The target is clamped into the limb's distance range (Limits.MinMidAngle .. Limits.MaxReachRatio).
 *  - Colliders (BodyColliders on bones, plus rig-space Planes / Spheres / Capsules) block arcs of the mid-joint circle; the
 *    node takes the allowed swivel nearest PreferredSwivelOffset, keeps to its current allowed arc (Limits.ArcHysteresis),
 *    and never turns faster than MaxSwivelSpeed.
 *
 * Note: this node operates in global (rig) space, like Basic IK.
 */
USTRUCT(meta = (DisplayName = "Climb Limb IK", Category = "Hierarchy", Keywords = "TwoBone,IK,Swivel,Climb,Limb", NodeColor = "0 1 1"))
struct FRigUnit_RogueClimbLimbIK : public FRigUnit_HighlevelBaseMutable
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute() override;

	/** The first bone of the chain (thigh / upper arm). */
	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey ItemA = FRigElementKey(NAME_None, ERigElementType::Bone);

	/** The mid bone (calf / lower arm). */
	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey ItemB = FRigElementKey(NAME_None, ERigElementType::Bone);

	/** The effector bone (foot / hand) - required: it gives the second bone's length, and its FK position the clip's own bend. */
	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey EffectorItem = FRigElementKey(NAME_None, ERigElementType::Bone);

	/** The effector target transform (global / rig space). */
	UPROPERTY(meta = (Input))
	FTransform Effector = FTransform::Identity;

	/** The axis along each bone (as in Basic IK). */
	UPROPERTY(meta = (Input))
	FVector PrimaryAxis = FVector(1.0, 0.0, 0.0);

	/** The axis rolled toward the bend (as in Basic IK). */
	UPROPERTY(meta = (Input))
	FVector SecondaryAxis = FVector(0.0, 1.0, 0.0);

	UPROPERTY(meta = (Input))
	float SecondaryAxisWeight = 1.0f;

	/** How much IK to apply. */
	UPROPERTY(meta = (Input))
	float Weight = 1.0f;

	/** How a partial Weight blends FK and IK. */
	UPROPERTY(meta = (Input))
	ERogueClimbLimbIKBlend BlendMode = ERogueClimbLimbIKBlend::Target;

	/**
	 * The knee/elbow hinge in ItemA's local space, pointing along (upper x lower) for the limb's normal bend. Only a
	 * fallback while bLearnHingeAxis hasn't seen the FK limb bent yet (or always, with learning off).
	 */
	UPROPERTY(meta = (Input))
	FVector HingeAxis = FVector(0.0, 0.0, 1.0);

	/** Read the hinge off the FK pose whenever the limb is bent (> ~5 deg) and remember it for when it's straight. */
	UPROPERTY(meta = (Input))
	bool bLearnHingeAxis = true;

	/**
	 * Preferred swivel relative to the FK bend (degrees, right-handed about the root->target axis). The sign for
	 * "knee out" differs between the left and right limb.
	 */
	UPROPERTY(meta = (Input))
	float PreferredSwivelOffset = 0.0f;

	/** The fastest the swivel may turn (deg/s); a forced change of side sweeps instead of flipping. 0 = unlimited. */
	UPROPERTY(meta = (Input))
	float MaxSwivelSpeed = 720.0f;

	/** Fold / reach limits, joint thickness, sampling and arc hysteresis. */
	UPROPERTY(meta = (Input))
	FRogueLimbIKLimits Limits;

	/** Capsules on the character's own bones the knee/elbow must avoid (torso, pelvis, head, the other limb). */
	UPROPERTY(meta = (Input))
	TArray<FRogueLimbIKBodyCollider> BodyColliders;

	/** Rig-space half-spaces the knee/elbow must stay in front of (e.g. the climb wall, fed by the mover component). */
	UPROPERTY(meta = (Input))
	TArray<FRogueLimbIKPlane> Planes;

	/** Rig-space spheres to avoid (e.g. world-geometry blockers, fed by the mover component). */
	UPROPERTY(meta = (Input))
	TArray<FRogueLimbIKSphere> Spheres;

	/** Rig-space capsules to avoid. */
	UPROPERTY(meta = (Input))
	TArray<FRogueLimbIKCapsule> Capsules;

	UPROPERTY(meta = (Input))
	bool bEnableStretch = false;

	UPROPERTY(meta = (Input))
	float StretchStartRatio = 0.75f;

	UPROPERTY(meta = (Input))
	float StretchMaximumRatio = 1.25f;

	UPROPERTY(meta = (Input, Constant))
	bool bPropagateToChildren = true;

	UPROPERTY(meta = (Input, DetailsOnly))
	FRogueClimbLimbIKDebugSettings DebugSettings;

	/** The swivel used this frame, relative to the FK bend (degrees). */
	UPROPERTY(meta = (Output))
	float Swivel = 0.0f;

	/** False when no swivel satisfied the colliders (the least-violating one was used). */
	UPROPERTY(meta = (Output))
	bool bAllowed = true;

	/** How far the pose is from satisfying everything (cm): the distance clamp's shortfall + any collider penetration. */
	UPROPERTY(meta = (Output))
	float Strain = 0.0f;

	UPROPERTY()
	FCachedRigElement CachedItemA;

	UPROPERTY()
	FCachedRigElement CachedItemB;

	UPROPERTY()
	FCachedRigElement CachedEffectorItem;

	UPROPERTY()
	FRogueClimbLimbIKWorkData WorkData;
};
