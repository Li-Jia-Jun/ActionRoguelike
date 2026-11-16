// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Units/Highlevel/RigUnit_HighlevelBase.h"
#include "RogueClimbSurfaceMap.h"
#include "RigUnit_RogueClimbSurfaceWarp.generated.h"

USTRUCT()
struct FRogueClimbSurfaceWarpDebugSettings
{
	GENERATED_BODY()

	// Draw the clip's flat-wall effector -> the warped one (cyan; orange when the reach rule pulled it back), and the map's
	// surface points. In game this needs `ControlRig.EnableDrawInterfaceInGame 1`.
	UPROPERTY(EditAnywhere, meta = (Input), Category = "DebugSettings")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, meta = (Input, EditCondition = "bEnabled"), Category = "DebugSettings")
	bool bDrawSurfaceMap = false;

	UPROPERTY(EditAnywhere, meta = (Input, EditCondition = "bEnabled"), Category = "DebugSettings")
	FTransform WorldOffset = FTransform::Identity;
};

/** Persistent per-node state (hidden): the learned hinge, as in the Climb Limb IK. */
USTRUCT()
struct FRogueClimbSurfaceWarpWorkData
{
	GENERATED_BODY()

	UPROPERTY()
	bool bHasLearnedHinge = false;

	UPROPERTY()
	FVector LearnedHingeLocal = FVector::ZeroVector;
};

/**
 * Climb Surface Warp - a POSE layer: moves one limb of the clip's flat-wall pose onto the real surface, so a swinging hand or
 * foot follows a curved wall at the clip's own clearance instead of floating off it. Place it after the weight shift and
 * BEFORE that limb's Climb Limb IK: the IK then blends from this warped pose to the pin by the stance curve, unchanged.
 *
 *  - The effector (hand / foot bone): its (u, v) on the flat wall becomes the same distances ALONG the surface (the map is
 *    marched, so 60 cm right of the body lands 60 cm round a cylinder), and its height above the plane is kept, measured
 *    from the surface there along the local normal; its rotation turns from the plane normal to the local normal.
 *  - Reach rule: a warped contact (palm / ball) past the component's leash limit (the Reach pin, from FClimbLimbPin::Reach)
 *    or over a hole moves to the nearest spot on the map that has surface and is in reach - a wide reach on a convex wall
 *    lands at the edge of reach, still on the surface. Distance the clip itself put there is never undone.
 *  - The limb's root stays put; the knee / elbow is rebuilt with the skeleton's bone lengths (read from the reference pose,
 *    as Basic IK does with its lengths at 0) in the clip's own bend plane (the learned FK hinge), never folding tighter than
 *    MinMidAngle. Bone lengths never change.
 *
 * Feed SurfaceMap and Weight from the mover component (ClimbSurfaceMap, ClimbSurfaceWarpWeight). The map is WORLD space; the
 * node converts it with the rig's current component transform. Operates in global (rig) space, like Basic IK.
 */
USTRUCT(meta = (DisplayName = "Climb Surface Warp", Category = "Hierarchy", Keywords = "Climb,Warp,Surface,Conform,Curved", NodeColor = "0 1 1"))
struct FRigUnit_RogueClimbSurfaceWarp : public FRigUnit_HighlevelBaseMutable
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

	/** The effector bone (foot / hand) - the point that gets warped. */
	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey EffectorItem = FRigElementKey(NAME_None, ERigElementType::Bone);

	/** The mover component's ClimbSurfaceMap (world space). */
	UPROPERTY(meta = (Input))
	FClimbSurfaceMap SurfaceMap;

	/** How much to warp: the mover component's ClimbSurfaceWarpWeight (1 while climbing, eased; 0 off the wall). */
	UPROPERTY(meta = (Input))
	float Weight = 1.0f;

	/**
	 * Reach rule: this limb's pin's Reach (FClimbLimbPin::Reach) - the component's leash limit and the contact point it's
	 * measured to (palm / ball). The warped contact stays within Reach.Limit of ItemA, or within the clip's own distance if
	 * that's farther (only distance the warp adds is undone). Limit 0 = no reach rule.
	 */
	UPROPERTY(meta = (Input))
	FClimbLimbReach Reach;

	/** The rebuilt knee / elbow never folds tighter than this interior angle (degrees). */
	UPROPERTY(meta = (Input))
	float MinMidAngle = 30.0f;

	/** As in Basic IK: the axis along each bone, and the one rolled toward the bend. */
	UPROPERTY(meta = (Input))
	FVector PrimaryAxis = FVector(1.0, 0.0, 0.0);

	UPROPERTY(meta = (Input))
	FVector SecondaryAxis = FVector(0.0, 1.0, 0.0);

	UPROPERTY(meta = (Input))
	float SecondaryAxisWeight = 1.0f;

	/** The knee / elbow hinge in ItemA's local space - only a fallback until the FK limb has been seen bent (as in Climb Limb IK). */
	UPROPERTY(meta = (Input))
	FVector HingeAxis = FVector(0.0, 0.0, 1.0);

	UPROPERTY(meta = (Input))
	bool bLearnHingeAxis = true;

	UPROPERTY(meta = (Input, Constant))
	bool bPropagateToChildren = true;

	UPROPERTY(meta = (Input, DetailsOnly))
	FRogueClimbSurfaceWarpDebugSettings DebugSettings;

	/** How far the effector was moved this frame (cm). */
	UPROPERTY(meta = (Output))
	float Shift = 0.0f;

	/** The reach rule pulled the effector back toward the root (or it was left short at full reach). */
	UPROPERTY(meta = (Output))
	bool bReachLimited = false;

	UPROPERTY()
	FCachedRigElement CachedItemA;

	UPROPERTY()
	FCachedRigElement CachedItemB;

	UPROPERTY()
	FCachedRigElement CachedEffectorItem;

	UPROPERTY()
	FRogueClimbSurfaceWarpWorkData WorkData;
};
