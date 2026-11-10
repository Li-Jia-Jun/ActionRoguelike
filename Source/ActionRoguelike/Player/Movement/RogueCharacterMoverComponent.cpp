// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueCharacterMoverComponent.h"

#include "RogueClimbMode.h"
#include "RogueClimbTransition.h"
#include "RogueMantleMode.h"
#include "RogueMantleTransition.h"
#include "RogueMantleDownMode.h"
#include "RogueMantleDownTransition.h"
#include "RogueCornerTurnMode.h"
#include "RogueCornerTurnTransition.h"
#include "RogueSharedGameplayTags.h"
#include "ActionSystem/RogueActionSystemComponent.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Player/Movement/RogueClimbContactMetaData.h"
#include "MotionWarpingComponent.h"
#include "MoveLibrary/MovementUtils.h"
#include "DefaultMovementSet/LayeredMoves/AnimRootMotionLayeredMove.h"
#include "CollisionShape.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Kismet/KismetMathLibrary.h"
#include "Curves/CurveFloat.h"

TAutoConsoleVariable<bool> CVarClimbingDebugDrawing(TEXT("game.climbing.DebugDraw"), true,
	TEXT("Enable climbing mover component debug rendering. (0 = off, 1 = enabled)"),
	ECVF_Cheat);


URogueCharacterMoverComponent::URogueCharacterMoverComponent()
{
	// Modes/transitions are intentionally NOT registered here - see InitializeComponent().
}

void URogueCharacterMoverComponent::InitializeComponent()
{
	// Register our custom modes/transitions HERE, not in the constructor. MovementModes/Transitions are Instanced,
	// EditAnywhere arrays that a Blueprint snapshots on save; constructor/CDO additions made AFTER that snapshot never
	// merge into the BP (that's why adding the mantle in the ctor silently didn't appear on the player BP). Adding at
	// InitializeComponent - before Super builds the state machine from these arrays (MoverComponent.cpp registers them
	// there) - makes C++ authoritative regardless of what any BP serialized, so a new mode never needs a BP reset.

	// Modes are keyed by name; add only if absent so any per-instance tuning on an existing mode instance is preserved.
	if (!MovementModes.Contains(URogueClimbMode::ModeName))
	{
		MovementModes.Add(URogueClimbMode::ModeName, NewObject<URogueClimbMode>(this));
	}
	if (!MovementModes.Contains(URogueMantleMode::ModeName))
	{
		MovementModes.Add(URogueMantleMode::ModeName, NewObject<URogueMantleMode>(this));
	}
	if (!MovementModes.Contains(URogueMantleDownMode::ModeName))
	{
		MovementModes.Add(URogueMantleDownMode::ModeName, NewObject<URogueMantleDownMode>(this));
	}
	if (!MovementModes.Contains(URogueCornerTurnMode::ModeName))
	{
		MovementModes.Add(URogueCornerTurnMode::ModeName, NewObject<URogueCornerTurnMode>(this));
	}

	// Global transitions are evaluated in array order (first match wins), so the mantle must come before the climb
	// transition (it wins over climb's exit-to-Falling at the lip). Drop any stale copies a BP may have snapshotted,
	// then insert ours at the front in priority order. (Mantle-down starts from Walking + a button, disjoint from the
	// others by start mode, so its order among them is not load-bearing; kept at the front for consistency.) The corner
	// turn sits after the mantle (an up-diagonal at a top corner prefers the mantle) and before climb (it must win over
	// climb's exit-to-Falling when the grid starts failing at the corner).
	Transitions.RemoveAll([](const TObjectPtr<UBaseMovementModeTransition>& Transition)
	{
		return Transition && (Transition->IsA<URogueMantleTransition>() || Transition->IsA<URogueMantleDownTransition>()
			|| Transition->IsA<URogueCornerTurnTransition>() || Transition->IsA<URogueClimbTransition>());
	});
	Transitions.Insert(NewObject<URogueMantleDownTransition>(this), 0);
	Transitions.Insert(NewObject<URogueMantleTransition>(this), 1);
	Transitions.Insert(NewObject<URogueCornerTurnTransition>(this), 2);
	Transitions.Insert(NewObject<URogueClimbTransition>(this), 3);

	Super::InitializeComponent();
}

void URogueCharacterMoverComponent::BeginPlay()
{
	Super::BeginPlay();

	CacheOwnerCharacter = Cast<ACharacter>(GetOwner());

	ClimbQueryParams.AddIgnoredActor(CacheOwnerCharacter);

	// Cache the owner's warping component (added on the pawn). UMoverComponent auto-wires its adapter to this.
	if (AActor* Owner = GetOwner())
	{
		MotionWarpingComp = Owner->FindComponentByClass<UMotionWarpingComponent>();

		// Listen for the mantle upright/tilt-blend gameplay events the montages fire via their "Send Gameplay Event" notify.
		if (URogueActionSystemComponent* ActionSystem = Owner->FindComponentByClass<URogueActionSystemComponent>())
		{
			ActionSystem->GameplayEventReceivedDelegate.AddDynamic(this, &URogueCharacterMoverComponent::OnMantleUprightBlendEvent);
			ActionSystem->GameplayEventReceivedDelegate.AddDynamic(this, &URogueCharacterMoverComponent::OnMantleDownTiltBlendEvent);
		}
	}

	OnPreSimulationTick.AddDynamic(this, &URogueCharacterMoverComponent::HandlePreSimulationTick);
}

// Mode queries go through the active mode's gameplay tags (Mover's own IsOnGround/IsAirborne pattern), not mode names:
// one mechanism for C++, the AnimBP (HasGameplayTag nodes), and future climb modes. The tag check reads the same sync-state
// buffer as GetMovementModeName(), so there's no tick of disagreement between the two.

bool URogueCharacterMoverComponent::IsInClimbState() const
{
	// Non-exact: the PARENT matches any child, i.e. any climb-family mode.
	return HasGameplayTag(RogueSharedGameplayTags::Mover_Climb, false);
}

bool URogueCharacterMoverComponent::IsClimbing() const
{
	return HasGameplayTag(RogueSharedGameplayTags::Mover_Climb_Sustained, true);
}

bool URogueCharacterMoverComponent::IsMantling() const
{
	return HasGameplayTag(RogueSharedGameplayTags::Mover_Climb_MantleUp, true);
}

bool URogueCharacterMoverComponent::IsMantlingDown() const
{
	return HasGameplayTag(RogueSharedGameplayTags::Mover_Climb_MantleDown, true);
}

bool URogueCharacterMoverComponent::IsCornerTurning() const
{
	return HasGameplayTag(RogueSharedGameplayTags::Mover_Climb_CornerTurn, true);
}

namespace
{
	// Straight reach of a limb chain in the reference pose: the bone segments from the tip up to (not past) the root, plus
	// a socket's offset when the tip is a socket. 0 if either is missing or the root isn't an ancestor of the tip.
	float ComputeLimbChainLength(const USkeletalMesh& Asset, FName RootBone, FName TipName)
	{
		const FReferenceSkeleton& RefSkeleton = Asset.GetRefSkeleton();
		float Length = 0.0f;
		FName TipBone = TipName;
		if (const USkeletalMeshSocket* Socket = Asset.FindSocket(TipName))
		{
			Length += static_cast<float>(Socket->RelativeLocation.Size());
			TipBone = Socket->BoneName;
		}
		const int32 RootIndex = RefSkeleton.FindBoneIndex(RootBone);
		int32 BoneIndex = RefSkeleton.FindBoneIndex(TipBone);
		if (RootIndex == INDEX_NONE)
		{
			return 0.0f;
		}
		while (BoneIndex != RootIndex)
		{
			if (BoneIndex == INDEX_NONE)
			{
				return 0.0f;
			}
			Length += static_cast<float>(RefSkeleton.GetRefBonePose()[BoneIndex].GetTranslation().Size());
			BoneIndex = RefSkeleton.GetParentIndex(BoneIndex);
		}
		return Length;
	}

	// Map a round (unit-circle) stick direction onto the unit square so full diagonals reach the blendspace
	// corners. Preserves push magnitude: the L-infinity norm (max component) of the result equals the input
	// length, so a half-pushed diagonal lands halfway out and nothing overshoots the axis range.
	FVector2D CircleToSquare(const FVector2D& In)
	{
		const float Len = In.Size();
		if (Len <= KINDA_SMALL_NUMBER)
		{
			return FVector2D::ZeroVector;
		}
		const FVector2D Dir = In / Len;                                          // unit direction on the circle
		const float MaxComp = FMath::Max(FMath::Abs(Dir.X), FMath::Abs(Dir.Y));  // > 0 for a unit vector
		return (Dir / MaxComp) * Len;                                            // push out to the square, keep length
	}

	// Round (pre-square) climb move intent in wall-relative axes (X = up/down the wall, Y = right/left), magnitude
	// 0..1. Shared by GetClimbMoveIntent (square-mapped for the blendspace AXES) and GetClimbMoveSpeedFraction
	// (its round magnitude for the PLAYRATE). Returns zero when there is no dominant surface.
	FVector2D ComputeRoundClimbWallIntent(const URogueCharacterMoverComponent& Comp)
	{
		const FVector WallNormal = Comp.GetClimbDominantSurfaceNormal();
		if (WallNormal.IsNearlyZero())
		{
			return FVector2D::ZeroVector;
		}

		// Same wall basis the climb mode uses, so the blendspace axes match the actual movement.
		const FVector Up = Comp.GetUpDirection();
		FVector WallUp = (Up - Up.ProjectOnToNormal(WallNormal)).GetSafeNormal();
		if (WallUp.IsNearlyZero())
		{
			WallUp = Up;
		}
		const FVector WallRight = FVector::CrossProduct(WallNormal, WallUp).GetSafeNormal();

		const FVector Intent = Comp.GetMovementIntent(); // world-space, magnitude 0-1
		return FVector2D(FVector::DotProduct(Intent, WallUp), FVector::DotProduct(Intent, WallRight));
	}
}

FVector2D URogueCharacterMoverComponent::GetClimbMoveIntent() const
{
	// The intent is a round (unit-circle) direction, but the blendspace is a square whose diagonal clips sit at
	// the corners. Remap so full diagonals reach the corners; without this they cap at 0.707 and never fully play.
	return CircleToSquare(ComputeRoundClimbWallIntent(*this));
}

float URogueCharacterMoverComponent::GetClimbMoveSpeedFraction() const
{
	// Locked to a single handhold (can cling but not traverse): report zero push so the anim holds. The climb mode
	// already zeros the along-wall velocity, so a non-zero fraction here would just skate the playrate against a body
	// that isn't moving. It also collapses the hang idle<->move blend to the (dead-hang) idle - the relaxed pose you
	// want when pinned - matching the body's held position.
	if (!bCanMoveOnWall)
	{
		return 0.0f;
	}

	// Round (pre-square) push amount, direction-independent: 1 at any full push, matching the body's constant
	// climb speed. Drives the blendspace PLAYRATE (Step A). Deliberately NOT |GetClimbMoveIntent()|, whose square
	// mapping reaches sqrt(2) on diagonals and would make diagonal climbing play too fast (foot skate).
	return FMath::Min(1.0f, ComputeRoundClimbWallIntent(*this).Size());
}

void URogueCharacterMoverComponent::HandlePreSimulationTick(const FMoverTimeStep& TimeStep, const FMoverInputCmdContext& InputCmd)
{
	// Sim time + step duration: the per-foot hang-flip debounce uses the time, the idle pin-weight ramp uses the step
	// (clamped so a first tick / resim jump can't produce a huge dt).
	{
		const double NowMs = TimeStep.BaseSimTimeMs;
		LastPreSimDeltaSeconds = (LastPreSimTimeMs > 0.0 && NowMs > LastPreSimTimeMs)
			? FMath::Min(static_cast<float>((NowMs - LastPreSimTimeMs) / 1000.0), 0.1f) : 0.0f;
		LastPreSimTimeMs = NowMs;
	}
	SweepAndStoreWallHits();
	RefreshClimbSurfaceInfo();
	RefreshClimbDistanceSpeed(); // along-wall speed from the authored ClimbDistance curve
	RefreshFootProbes();   // per-foot foothold probes + the both-feet gross-hang flag (self-gates on IsClimbing)
	RefreshLimbPins();     // per-limb world-locked plant/grip pins for the Control Rig (predictive climb, reactive corner)
	RefreshCornerProbes(); // left/right inner/outer corner detection + turn transforms (self-gates on IsClimbing)

	// The up-mantle probe matters while climbing (feeds the climb->mantle top-out); the mantle-down probe matters while
	// grounded (feeds the walk->mantle-down transition). They're mutually exclusive by state, so only one runs per tick.
	if (IsClimbing())
	{
		RefreshMantleProbe();
		bMantleDownAvailable = false;
	}
	else if (IsOnGround())
	{
		RefreshMantleDownProbe();
		bMantleLedgeAvailable = false;
	}
	else
	{
		bMantleLedgeAvailable = false;
		bMantleDownAvailable = false;
	}
}

void URogueCharacterMoverComponent::SweepAndStoreWallHits()
{
	// Sweeps a capsule just in front of the character and stores any wall hits into CurrentWallHits.
	
	const USceneComponent* Updated = GetUpdatedComponent();
	if (!Updated)
	{
		CurrentWallHits.Reset();
		return;
	}
	
	const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(ClimbDetectionCapsuleRadius, ClimbDetectionCapsuleHalfHeight);
	const FVector Forward = Updated->GetForwardVector();
	FVector Start = Updated->GetComponentLocation() + Forward * ClimbDetectionForwardOffset;
	FVector End = Start + Forward; // Sweep slightly ahead of the character
	
	if (IsClimbing())
	{
		// Bias up the WALL (not world up) so a tilted, receding surface is still caught. ClimbDominantSurfaceNormalCache
		// still holds the previous frame's plane here (this runs before RefreshClimbSurfaceInfo resets it).
		const FVector UpBias = GetClimbSampleUpBias(ClimbDominantSurfaceNormalCache);
		Start += UpBias;
		End += UpBias;
	}
	
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetOwner());

	TArray<FHitResult> Hits;
	const bool bHitWall = GetWorld()->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, ECC_WorldStatic, CapsuleShape, QueryParams);

	if (bHitWall)
	{
		CurrentWallHits = MoveTemp(Hits);
	}
	else
	{
		CurrentWallHits.Reset();
	}

#if ENABLE_DRAW_DEBUG
	const bool bDebugDrawEnable = CVarClimbingDebugDrawing.GetValueOnGameThread(); 
	if (bDebugDrawEnable)
	{
		const FColor CapsuleColor = (CurrentWallHits.Num() > 0) ? FColor::Green : FColor::White;
		DrawDebugCapsule(GetWorld(), Start, ClimbDetectionCapsuleHalfHeight, ClimbDetectionCapsuleRadius, FQuat::Identity, CapsuleColor, false, -1.0f, 0, 0.6f);

		for (const FHitResult& Hit : CurrentWallHits)
		{
			DrawDebugPoint(GetWorld(), Hit.ImpactPoint, 30.0f, FColor::Blue, false, -1.0f);
			DrawDebugDirectionalArrow(GetWorld(), Hit.ImpactPoint, Hit.ImpactPoint + Hit.ImpactNormal * 20.0f, 5.0f, FColor::Cyan, false, -1.0f, 0, 1.0f);
		}
	}
#endif
}

void URogueCharacterMoverComponent::RefreshClimbSurfaceInfo()
{
	// Keep the previous dominant plane so we can smooth toward the new grid average (damps curved-surface jitter).
	const FVector PrevNormal = ClimbDominantSurfaceNormalCache;
	const FVector PrevLocation = ClimbDominantSurfaceLocationCache;

	// Reset cached outputs each refresh. (bLowerBodyHanging is NOT reset here - RefreshFootProbes owns it, and freezes it
	// through a corner turn.)
	bFacingClimbableSurface = false;
	bCanMoveOnWall = false;
	ClimbDominantSurfaceNormalCache = FVector::ZeroVector;
	ClimbDominantSurfaceLocationCache = FVector::ZeroVector;
	LowerBodySupport = UpperBodySupport = LeftSupport = RightSupport = 0.0f;
	ClimbSurfaceSamplesCache.Reset();

	const USceneComponent* Updated = GetUpdatedComponent();
	// Cheap gate: if the capsule sweep found nothing ahead, skip the (up to Rows*Cols) grid traces entirely.
	if (!Updated || CurrentWallHits.Num() == 0)
	{
		return;
	}

	const int32 Rows = FMath::Max(1, ClimbGridRows);
	const int32 Cols = FMath::Max(1, ClimbGridColumns);
	
	// While climbing, bias the whole grid up the wall (hands reach high, legs curl). Follow the WALL's up-slope
	// (via PrevNormal — this frame's cache was reset above) so the samples stay on a tilted/receding surface. As a
	// world-space vector added to each Origin below, it shifts the grid without changing its spacing.
	const FVector ClimbUpBias = IsClimbing() ? GetClimbSampleUpBias(PrevNormal) : FVector::ZeroVector;

	// Even spacing over the grid. Divide the span by (count - 1) so the first/last samples land exactly on
	// the bottom/top (and left/right) bounds; step is 0 for a single row/column.
	const float RowStep = (Rows > 1) ? (ClimbGridTopOffset - ClimbGridBottomOffset) / (Rows - 1) : 0.0f;
	const float ColStep = (Cols > 1) ? (ClimbGridHalfWidth * 2.0f) / (Cols - 1) : 0.0f;

	const FVector Base = Updated->GetComponentLocation();
	const FVector Forward = Updated->GetForwardVector(); // trace direction; while climbing the tilted capsule already faces into the wall
	const FVector Up = GetUpDirection();

	// Grid STACKING axes. At entry the character is upright (world up/right are correct) AND there is no plane yet. While
	// climbing the capsule is TILTED to the wall, so stack the grid along the WALL (WallUp/WallRight from the cached plane)
	// - a world-up grid skews across a tilted face (the same class of bug as the mantle feet reference). GridRight uses the
	// climb mode's cross(Normal, WallUp) convention, so the left/right columns match on vertical AND tilted walls.
	FVector GridUp = Up;
	FVector GridRight = FVector::CrossProduct(Up, Forward).GetSafeNormal();
	if (IsClimbing() && !PrevNormal.IsNearlyZero())
	{
		GridUp = (Up - Up.ProjectOnToNormal(PrevNormal)).GetSafeNormal();
		if (GridUp.IsNearlyZero()) { GridUp = Up; }
		GridRight = FVector::CrossProduct(PrevNormal, GridUp).GetSafeNormal();
	}

	ClimbSurfaceSamplesCache.Reserve(Rows * Cols);

	int32 ValidCount = 0;
	FVector NormalSum = FVector::ZeroVector;
	FVector PointSum = FVector::ZeroVector;

	// Regional hit info (valid means climbable)
	int32 GripValid = 0, GripTotal = 0;		// Upper body
	int32 LowerValid = 0, LowerTotal = 0;
	int32 LeftValid = 0, LeftTotal = 0;
	int32 RightValid = 0, RightTotal = 0;

	// Hand purchase for the climbability / move gates (see below). The LEG hang is no longer derived from the grid -
	// each foot has its own foothold probe (RefreshFootProbes / ProbeFoot), so the bottom rows only feed the legacy
	// LowerBodySupport scalar now.
	int32 TopRowValid = 0;                  // valid cells in the hand (top) row

	// Grid-based line-tracing
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		const float VOffset = ClimbGridBottomOffset + Row * RowStep; // row 0 = bottom, last row = top
		const bool bLowerRow = (Row < ClimbLowerBodyRowCount);

		for (int32 Col = 0; Col < Cols; ++Col)
		{
			const float HOffset = -ClimbGridHalfWidth + Col * ColStep; // col 0 = left, last col = right

			const FVector Origin = Base + GridUp * VOffset + GridRight * HOffset + ClimbUpBias;
			const FVector End = Origin + Forward * ClimbSampleReach;

			FClimbSurfaceSample Sample;
			FHitResult Hit;
			const bool bTraceHit = GetWorld()->LineTraceSingleByChannel(Hit, Origin, End, ECC_WorldStatic, ClimbQueryParams);

			if (bTraceHit)
			{
				const float SteepnessDot = FVector::DotProduct(Hit.ImpactNormal, Up);
				const FVector HorizontalNormal = Hit.ImpactNormal.GetSafeNormal2D();
				// Facing is a HORIZONTAL question, so compare the FLATTENED forward - else while climbing the tilted
				// capsule's forward carries a vertical component that shrinks the dot and tightens the gate on steep walls.
				FVector FlatForward = Forward.GetSafeNormal2D();
				if (FlatForward.IsNearlyZero()) { FlatForward = Forward; }
				// Clamp before Acos: dot of two unit vectors can drift slightly outside [-1,1] and yield NaN.
				const float FacingDot = FMath::Clamp(FVector::DotProduct(FlatForward, -HorizontalNormal), -1.0f, 1.0f);
				const float FacingDegrees = FMath::RadiansToDegrees(FMath::Acos(FacingDot));

				if (IsSurfaceClimbable(SteepnessDot) && FacingDegrees <= MinHorizontalDegreesToStartClimbing)
				{
					Sample.bHit = true;
					Sample.Point = Hit.ImpactPoint;
					Sample.Normal = Hit.ImpactNormal;

					++ValidCount;
					NormalSum += Hit.ImpactNormal;
					PointSum += Hit.ImpactPoint;
				}
			}
			
			// Record each hit sample
			ClimbSurfaceSamplesCache.Add(Sample);

			// Record regional result
			const int32 ValidInc = Sample.bHit ? 1 : 0;
			if (bLowerRow) { ++LowerTotal; LowerValid += ValidInc; }
			else           { ++GripTotal;  GripValid  += ValidInc; }

			if (Cols > 1)
			{
				if (Col < Cols / 2)             { ++LeftTotal;  LeftValid  += ValidInc; }
				else if (Col >= (Cols + 1) / 2) { ++RightTotal; RightValid += ValidInc; }
			}

			// Hand row (top): counts only VALID (climbable) hits, for the hand-purchase / move gates.
			if (Sample.bHit && Row == Rows - 1)
			{
				++TopRowValid;
			}

#if ENABLE_DRAW_DEBUG
			if (CVarClimbingDebugDrawing.GetValueOnGameThread())
			{
				const FColor SampleColor = Sample.bHit ? FColor::Green : (bTraceHit ? FColor::Orange : FColor::Red);
				DrawDebugLine(GetWorld(), Origin, bTraceHit ? Hit.ImpactPoint : End, SampleColor, false, -1.0f, 0, 0.5f);
			}
#endif
		}
	}

	// Anim-facing support scalars, published whenever a wall is present (independent of the entry gate).
	LowerBodySupport = (LowerTotal > 0) ? static_cast<float>(LowerValid) / LowerTotal : 0.0f;
	UpperBodySupport = (GripTotal  > 0) ? static_cast<float>(GripValid)  / GripTotal  : 0.0f;
	LeftSupport      = (LeftTotal  > 0) ? static_cast<float>(LeftValid)  / LeftTotal  : 0.0f;
	RightSupport     = (RightTotal > 0) ? static_cast<float>(RightValid) / RightTotal : 0.0f;

	if (ValidCount > 0)
	{
		const FVector TargetNormal = NormalSum.GetSafeNormal();
		const FVector TargetLocation = PointSum / ValidCount;

		// Snap on the first frame we (re)acquire a surface (no prior value to interp from), otherwise ease toward
		// the new grid average to damp per-frame jitter on curved surfaces.
		const float DeltaSeconds = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;
		if (ClimbNormalSmoothingSpeed <= 0.0f || PrevNormal.IsNearlyZero() || DeltaSeconds <= 0.0f)
		{
			ClimbDominantSurfaceNormalCache = TargetNormal;
			ClimbDominantSurfaceLocationCache = TargetLocation;
		}
		else
		{
			ClimbDominantSurfaceNormalCache = FMath::VInterpTo(PrevNormal, TargetNormal, DeltaSeconds, ClimbNormalSmoothingSpeed).GetSafeNormal();
			ClimbDominantSurfaceLocationCache = FMath::VInterpTo(PrevLocation, TargetLocation, DeltaSeconds, ClimbNormalSmoothingSpeed);
		}
	}

	// Gate 1 - hand purchase: the top (hand) row must have >= ClimbTopRowMinHits cells on wall. This is the SOLE
	// coverage requirement (no torso/grip ratio): hands are what hold you on, legs are free to hang. Clamped to the
	// column count so a narrow grid can still satisfy it.
	const int32 TopRowHitsToClimb = FMath::Min(ClimbTopRowMinHits, Cols);
	const bool bHandPurchasePass = (TopRowValid >= TopRowHitsToClimb);

	// Gate 2 - consistency: every valid sample normal agrees with the average (rejects corners / fragments).
	bool bConsistencyPass = (ValidCount > 0);
	if (bConsistencyPass)
	{
		const float MinConsistencyDot = FMath::Cos(FMath::DegreesToRadians(MaxClimbNormalDeviationDegrees));
		for (const FClimbSurfaceSample& Sample : ClimbSurfaceSamplesCache)
		{
			if (Sample.bHit && FVector::DotProduct(Sample.Normal, ClimbDominantSurfaceNormalCache) < MinConsistencyDot)
			{
				bConsistencyPass = false;
				break;
			}
		}
	}

	bFacingClimbableSurface = bHandPurchasePass && bConsistencyPass;

	// Movement needs more hand purchase than clinging: below the move threshold the character holds position (the climb
	// mode zeros the along-wall intent) so a single handhold reads as an idle cling rather than a slide.
	const int32 TopRowHitsToMove = FMath::Min(ClimbTopRowMinHitsToMove, Cols);
	bCanMoveOnWall = bFacingClimbableSurface && (TopRowValid >= TopRowHitsToMove);

	// bLowerBodyHanging is set from the per-foot probes (RefreshFootProbes), not the grid: it's true only when BOTH
	// feet lack a reachable foothold. A single foot over a void stays braced and is handled per-foot (tuck / IK).

#if ENABLE_DRAW_DEBUG
	if (CVarClimbingDebugDrawing.GetValueOnGameThread() && ValidCount > 0)
	{
		const FColor PlaneColor = bFacingClimbableSurface ? FColor::Green : FColor::Red;
		DrawDebugDirectionalArrow(GetWorld(), ClimbDominantSurfaceLocationCache, ClimbDominantSurfaceLocationCache + ClimbDominantSurfaceNormalCache * 40.0f, 8.0f, PlaneColor, false, -1.0f, 0, 2.0f);
	}
#endif
}

bool URogueCharacterMoverComponent::IsSurfaceClimbable(float SteepnessDotProduct) const
{
	// SteepnessDotProduct = dot(surfaceNormal, up): ~0 for a vertical wall, ~+1 for a floor, ~-1 for a ceiling.
	// A surface is a WALL when (1) it is near-vertical - leaning no more than MaxWallTiltFromVerticalDegrees from vertical,
	// tested as |dot| = sin(tilt-from-vertical) <= sin(MaxWallTiltFromVerticalDegrees) - AND (2) it is NOT a walkable top
	// (dot < MantleMinWalkableDot). Clause (2) is the explicit walk/wall gate: it keeps a walkable surface from ever
	// counting as a wall even if the tilt limit is (mis)set above the walkable slope. With the default 40deg (< ~44deg
	// walkable) it is redundant but free insurance.
	const float MaxSteepnessDot = FMath::Sin(FMath::DegreesToRadians(MaxWallTiltFromVerticalDegrees));
	return FMath::Abs(SteepnessDotProduct) <= MaxSteepnessDot && SteepnessDotProduct < MantleMinWalkableDot;
}

FVector URogueCharacterMoverComponent::GetClimbSampleUpBias(const FVector& PlaneNormal) const
{
	// Project world-up onto the wall plane so the climb sampling shift follows the wall's up-slope. On a vertical
	// wall this equals world up; on a back-tilted wall it gains the forward component that keeps the sweep/grid on
	// the receding surface (a pure world-up shift would miss it and drop the climb).
	const FVector Up = GetUpDirection();
	const FVector WallUp = (Up - FVector::DotProduct(Up, PlaneNormal) * PlaneNormal).GetSafeNormal();
	return (WallUp.IsNearlyZero() ? Up : WallUp) * OnClimbAdditionalOffset;
}

void URogueCharacterMoverComponent::RefreshClimbDistanceSpeed()
{
	// Only the sustained climb consumes this; reset otherwise so the next climb seeds cleanly.
	if (!IsClimbing())
	{
		bClimbDistanceSpeedValid = false;
		bHasClimbDistancePrev = false;
		ClimbDistanceStride = 0.0f;
		ClimbAlongWallSpeedThisTick = 0.0f;
		return;
	}

	// Blended ClimbDistance (cumulative along-wall travel) at the current anim position. Absent -> invalid (solver speed).
	float DistNow = 0.0f;
	if (!GetStanceCurveValue(ClimbDistanceCurveName, DistNow))
	{
		bClimbDistanceSpeedValid = false;
		bHasClimbDistancePrev = false;
		return;
	}

	if (!bHasClimbDistancePrev)
	{
		// First climbing tick (or just-authored): seed, no movement attributed this tick.
		ClimbDistancePrev = DistNow;
		ClimbDistanceStride = DistNow;
		ClimbAlongWallSpeedThisTick = 0.0f;
		bHasClimbDistancePrev = true;
		bClimbDistanceSpeedValid = true;
		return;
	}

	// Update the running loop stride first (running max of the curve) so the wrap test below has the true stride.
	ClimbDistanceStride = FMath::Max(ClimbDistanceStride, DistNow);

	// Per-tick advance along the wall. Two kinds of decrease can happen and they are NOT the same:
	//   1. A real LOOP-SEAM wrap: the curve jumps ~a full stride down in ONE tick (stride -> 0). True advance = delta + stride.
	//   2. A GRADUAL decrease from BLENDING: when the move clip crossfades out to idle (or across diagonal clips at different
	//      phases), the blended ClimbDistance eases DOWN over the fade. The old "any negative = wrap" rule turned each small
	//      dip into a near-full-stride phantom advance EVERY tick of the blend-out -> the body shot forward a long way after
	//      release (not a few-dt creep - strides' worth). So only compensate a LARGE drop as a wrap; a gradual dip = no advance.
	float DeltaDist = DistNow - ClimbDistancePrev;
	if (DeltaDist < -0.5f * ClimbDistanceStride)
	{
		DeltaDist += ClimbDistanceStride; // genuine seam wrap
	}
	DeltaDist = FMath::Max(0.0f, DeltaDist); // a blend-induced dip (or noise) contributes no forward travel

	ClimbDistancePrev = DistNow; // track every tick (even when gated below) so resuming from idle doesn't spike the delta

	// Gate on LIVE INPUT. Even with the wrap fix, the curve keeps GENUINELY advancing while the move clip plays out its
	// blend after the key is released - driving the body from it then glides it forward with no input. Let the curve drive
	// the body only while the player is actively commanding a stride; when idle, report invalid so the climb mode's solver
	// deceleration brakes the body to a stop (the release feel from before the distance swap). Same idle threshold the limb
	// IK uses, so the body stops exactly as the limbs go idle.
	if (GetClimbMoveSpeedFraction() <= FootPlantIdleInputDeadzone)
	{
		ClimbAlongWallSpeedThisTick = 0.0f;
		bClimbDistanceSpeedValid = false;
		return;
	}

	ClimbAlongWallSpeedThisTick = (LastPreSimDeltaSeconds > 0.0f) ? FMath::Max(0.0f, DeltaDist) / LastPreSimDeltaSeconds : 0.0f;
	bClimbDistanceSpeedValid = true;
}

FVector URogueCharacterMoverComponent::ResolveClimbMoveInput(const FVector& RawInput)
{
	// Only the sustained climb commits (a corner turn reads the raw stick for its own playhead). Off it, drop the latch so a
	// stale input can't be replayed after a corner turn or a re-entry.
	if (!IsClimbing())
	{
		ClimbLastMoveInput = FVector::ZeroVector;
		bClimbStrikeCommitting = false;
		return RawInput;
	}

	// Commanding a stride: pass it through and remember it.
	if (FVector2D(RawInput.X, RawInput.Y).Size() > FootPlantIdleInputDeadzone)
	{
		ClimbLastMoveInput = RawInput;
		bClimbStrikeCommitting = false;
		return RawInput;
	}

	// Released. While the strike in progress is past its commit marker, keep feeding the last stride - the clip, the body
	// (ClimbDistance) and the limb IK all carry on exactly as if the stick were still held - until the clip crosses that
	// strike's end marker. Then drop the latch for good (until the next press), so an idle phase can't re-trigger a commit.
	// The first evaluation after a release logs its decision (debug cvar), so marker placement can be checked per release.
	const bool bWasCommitting = bClimbStrikeCommitting;
	const bool bDebugLog = CVarClimbingDebugDrawing.GetValueOnGameThread();
	bClimbStrikeCommitting = bCommitClimbStrikes && !ClimbLastMoveInput.IsNearlyZero()
		&& IsPastClimbStrikeCommit(/*bLogDecision*/ bDebugLog && !bWasCommitting);
	if (!bClimbStrikeCommitting)
	{
		ClimbLastMoveInput = FVector::ZeroVector;
	}

	if (bWasCommitting && !bClimbStrikeCommitting && bDebugLog)
	{
		UE_LOG(LogTemp, Log, TEXT("[ClimbStrike] reached the strike end - releasing to idle"));
	}

	return bClimbStrikeCommitting ? ClimbLastMoveInput : RawInput;
}

bool URogueCharacterMoverComponent::IsPastClimbStrikeCommit(bool bLogDecision)
{
	// Where the dominant clip is: its live (blended) ClimbPhase. While committing there is no crossfade, so it's exact.
	float DominantWeight = 0.0f;
	const UAnimSequence* Clip = GetDominantClimbClip(DominantWeight);
	float Phase01 = 0.0f;
	if (!Clip || !GetStanceCurveValue(ClimbPhaseCurveName, Phase01))
	{
		if (bLogDecision)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ClimbStrike] released - no dominant climb clip or no '%s' curve, can't commit"), *ClimbPhaseCurveName.ToString());
		}
		return false;
	}
	const float PhaseTime = Phase01 * Clip->GetPlayLength();

	// The most recent strike marker at/before the playhead decides: a commit marker = the strike in progress is committed,
	// an end marker = it isn't (still reaching, or already finished). The clip loops, so before its first marker the most
	// recent one is the previous lap's LAST marker. Markers may be authored as SYNC markers (which also phase-align the
	// blendspace's samples) or as plain NOTIFIES of the same names - both are read.
	bool bHasRecent = false, bRecentIsCommit = false;
	float RecentTime = -1.0f;
	bool bHasAny = false, bLastIsCommit = false, bHasEnd = false;
	float LastTime = -1.0f;
	auto Consider = [&](FName Name, float Time)
	{
		const bool bCommit = Name == ClimbStrikeCommitMarkerName;
		if (!bCommit && Name != ClimbStrikeEndMarkerName)
		{
			return;
		}
		bHasEnd |= !bCommit;
		if (Time <= PhaseTime && Time > RecentTime)
		{
			bHasRecent = true;
			RecentTime = Time;
			bRecentIsCommit = bCommit;
		}
		if (Time > LastTime)
		{
			bHasAny = true;
			LastTime = Time;
			bLastIsCommit = bCommit;
		}
	};
	for (const FAnimSyncMarker& Marker : Clip->AuthoredSyncMarkers)
	{
		Consider(Marker.MarkerName, Marker.Time);
	}
	for (const FAnimNotifyEvent& Notify : Clip->Notifies)
	{
		Consider(Notify.NotifyName, Notify.GetTriggerTime());
	}
	// A commit with NO end marker anywhere in the clip would read as committed forever (the latch never lets go and the climb
	// runs on by itself) - an authoring slip, so it never commits instead.
	const bool bCommitted = bHasEnd && (bHasRecent ? bRecentIsCommit : (bHasAny && bLastIsCommit));

	if (bLogDecision)
	{
		if (!bHasAny)
		{
			// Normal for an untagged clip - only the authored Commit->End segments auto-forward.
			UE_LOG(LogTemp, Log, TEXT("[ClimbStrike] released on '%s' at %.2fs - no strike markers, going idle"),
				*Clip->GetName(), PhaseTime);
		}
		else if (!bHasEnd)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ClimbStrike] '%s' has '%s' but no '%s' marker - ignoring its commits (they'd never end)"),
				*Clip->GetName(), *ClimbStrikeCommitMarkerName.ToString(), *ClimbStrikeEndMarkerName.ToString());
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[ClimbStrike] released on '%s' at %.2fs - %s"), *Clip->GetName(), PhaseTime,
				bCommitted ? TEXT("past the commit marker, finishing the strike") : TEXT("before the commit marker, going idle"));
		}
	}
	return bCommitted;
}

void URogueCharacterMoverComponent::RefreshFootProbes()
{
	// Corner turn (a climb -> climb traversal): FREEZE the per-foot results and the gross hang as they were when the turn
	// began, so IsLowerBodyHanging() / the foot flags stay true to the climb through the turn instead of reading "braced".
	// The probes can't run meaningfully mid-turn anyway (they sweep toward one plane while the body straddles two, from
	// foot bones the clip is swinging); they resume on the first Climbing tick after the hand-off.
	if (IsCornerTurning())
	{
		return;
	}

	// Carry each foot's previous result across the reset: the hanging bool feeds the release hysteresis in ProbeFoot, and
	// the full previous result lets the hang-flip debounce (below) restore it when a flip is suppressed.
	const FClimbFootProbeResult PrevLeftFootProbe = LeftFootProbe;
	const FClimbFootProbeResult PrevRightFootProbe = RightFootProbe;
	const bool bLeftWasHanging = PrevLeftFootProbe.bHanging;
	const bool bRightWasHanging = PrevRightFootProbe.bHanging;

	// Reset each tick; only meaningful while climbing against a known plane (the probe sweeps toward it).
	LeftFootProbe = FClimbFootProbeResult();
	RightFootProbe = FClimbFootProbeResult();

	if (!IsClimbing() || ClimbDominantSurfaceNormalCache.IsNearlyZero())
	{
		bLowerBodyHanging = false;
		return;
	}

	LeftFootProbe = ProbeFoot(LeftFootSocketName, bLeftWasHanging);
	RightFootProbe = ProbeFoot(RightFootSocketName, bRightWasHanging);

	// Idle latch: while the player isn't driving movement, don't let a hanging foot RE-PLANT. That removes the
	// hang->plant edge of the plant/hang limit cycle (the probe rides the animated foot, whose pose depends on the very
	// decision it drives), so an idle foot at an edge settles into a stationary dangle near the surface - the only place
	// the flicker is visible, and the acceptable pose - instead of pawing at the void. Moving re-enables re-planting.
	if (GetClimbMoveSpeedFraction() <= FootPlantIdleInputDeadzone)
	{
		if (bLeftWasHanging && !LeftFootProbe.bHanging)   { LeftFootProbe = FClimbFootProbeResult();  LeftFootProbe.bHanging = true; }
		if (bRightWasHanging && !RightFootProbe.bHanging) { RightFootProbe = FClimbFootProbeResult(); RightFootProbe.bHanging = true; }
	}

	// Temporal debounce (FootHangCooldownSeconds): once a foot's plant<->hang state flips, hold it for the cooldown before
	// it may flip again, so the state can't switch too frequently at a ledge edge. Restore the WHOLE previous result (not
	// just the bool) when suppressed, so bHanging and the foothold target stay consistent - the pin layer then reads one
	// stable decision. (A time rule, separate from the position-based idle latch above.)
	if (FootHangCooldownSeconds > 0.0f)
	{
		if (LeftFootProbe.bHanging != bLeftWasHanging)
		{
			if (LastPreSimTimeMs < LeftFootHangUnlockSimTimeMs) { LeftFootProbe = PrevLeftFootProbe; }
			else { LeftFootHangUnlockSimTimeMs = LastPreSimTimeMs + FootHangCooldownSeconds * 1000.0; }
		}
		if (RightFootProbe.bHanging != bRightWasHanging)
		{
			if (LastPreSimTimeMs < RightFootHangUnlockSimTimeMs) { RightFootProbe = PrevRightFootProbe; }
			else { RightFootHangUnlockSimTimeMs = LastPreSimTimeMs + FootHangCooldownSeconds * 1000.0; }
		}
	}

	// Gross lower-body hang (drives the dead-hang UPPER body) only when BOTH feet lack a foothold. A single foot over a
	// void stays braced and is resolved per-foot (tuck / IK) - this is the OR->both flip enabled by the per-foot probes.
	bLowerBodyHanging = bFacingClimbableSurface && LeftFootProbe.bHanging && RightFootProbe.bHanging;
}

FClimbFootProbeResult URogueCharacterMoverComponent::ProbeFoot(FName FootSocketName, bool bWasHanging) const
{
	// Dynamic per-foot probe: sphere-sweep from the ANIMATED foot bone toward the dominant climb plane to find a
	// reachable climbable foothold. Independent of the grid's leg rows (the grid only supplies the plane). Because it
	// follows the live foot, the plant/hang decision - and later the IK snap - can happen during motion.
	FClimbFootProbeResult Result;
	Result.bHanging = true;   // assume no foothold until one is found within reach

	const USkeletalMeshComponent* Mesh = CacheOwnerCharacter ? CacheOwnerCharacter->GetMesh() : nullptr;
	if (!Mesh || !GetWorld())
	{
		return Result;
	}

	const FVector WallNormal = ClimbDominantSurfaceNormalCache; // points out of the wall
	const FVector FootLoc = Mesh->GetSocketLocation(FootSocketName);

	// Release hysteresis: a foot that was already PLANTED searches wider (radius + reach) so a clearly-gone foothold is
	// needed to switch to hanging; a hanging foot uses the base extents to acquire. Stops the flicker at ledge edges.
	const float Hysteresis = bWasHanging ? 0.0f : FootProbeReleaseHysteresis;
	const float Radius = FootProbeRadius + Hysteresis;
	const float Reach = FootProbeReach + Hysteresis;

	// Start OFF the wall (in free space in front of the face) and sweep back toward it, so the search always begins
	// outside the surface no matter how the foot is posed. Reachable = a climbable hit within Reach.
	const FVector Origin = FootLoc + WallNormal * FootProbeBackoff;
	const FVector End = Origin - WallNormal * (FootProbeBackoff + Reach);

	const FCollisionShape Sphere = FCollisionShape::MakeSphere(Radius);
	FHitResult Hit;
	const bool bHit = GetWorld()->SweepSingleByChannel(Hit, Origin, End, FQuat::Identity, ECC_WorldStatic, Sphere, ClimbQueryParams);

	if (bHit && !Hit.bStartPenetrating)
	{
		const float SteepnessDot = FVector::DotProduct(Hit.ImpactNormal, GetUpDirection());
		// A foothold must be a climbable wall (not a floor/ceiling) and roughly on the same face as the climb plane
		// (rejects a perpendicular inner-corner wall that happens to be within reach).
		const bool bSameFace = FVector::DotProduct(Hit.ImpactNormal, WallNormal) > 0.3f;
		if (IsSurfaceClimbable(SteepnessDot) && bSameFace)
		{
			Result.bHanging = false;
			Result.bHasTarget = true;
			Result.TargetLocation = Hit.ImpactPoint;
			Result.TargetNormal = Hit.ImpactNormal;
		}
	}

#if ENABLE_DRAW_DEBUG
	if (CVarClimbingDebugDrawing.GetValueOnGameThread())
	{
		const FColor C = Result.bHanging ? FColor::Red : FColor::Green;
		const FVector Marker = Result.bHasTarget ? Result.TargetLocation : End;
		DrawDebugLine(GetWorld(), Origin, Marker, C, false, -1.0f, 0, 0.5f);
		DrawDebugSphere(GetWorld(), Marker, Radius, 12, C, false, -1.0f, 0, 0.5f);
	}
#endif

	return Result;
}

FClimbHandGrip URogueCharacterMoverComponent::ProbeContactToward(const FVector& FromLoc, const FVector& Normal,
	const FVector& PlanePoint, float Radius, float Reach, float Backoff) const
{
	// Default (no hit) to FromLoc projected onto the (Normal, PlanePoint) plane rather than a zero vector, so Location is
	// always a sensible nearby point - a consumer that blends on bFound never sees a world-origin target mid-blend.
	FClimbHandGrip Grip;
	Grip.Location = FromLoc - Normal * FVector::DotProduct(FromLoc - PlanePoint, Normal);
	Grip.Normal = Normal;
	if (!GetWorld())
	{
		return Grip;
	}

	// Start OFF the face and sweep toward it (origin in free space). A climbable, same-face hit within reach = contact.
	const FVector Origin = FromLoc + Normal * Backoff;
	const FVector End = Origin - Normal * (Backoff + Reach);

	const FCollisionShape Sphere = FCollisionShape::MakeSphere(Radius);
	FHitResult Hit;
	if (GetWorld()->SweepSingleByChannel(Hit, Origin, End, FQuat::Identity, ECC_WorldStatic, Sphere, ClimbQueryParams)
		&& !Hit.bStartPenetrating)
	{
		const float SteepnessDot = FVector::DotProduct(Hit.ImpactNormal, GetUpDirection());
		const bool bSameFace = FVector::DotProduct(Hit.ImpactNormal, Normal) > 0.3f;
		if (IsSurfaceClimbable(SteepnessDot) && bSameFace)
		{
			Grip.bFound = true;
			// Raw surface hit; the pin's LimbPinSurfaceOffset (the "lip" standoff) is applied once at consume (RefreshLimbPins).
			Grip.Location = Hit.ImpactPoint;
			Grip.Normal = Hit.ImpactNormal;
		}
	}

#if ENABLE_DRAW_DEBUG
	if (CVarClimbingDebugDrawing.GetValueOnGameThread())
	{
		const FColor C = Grip.bFound ? FColor::Magenta : FColor::Silver;
		const FVector Marker = Grip.bFound ? Grip.Location : End;
		DrawDebugLine(GetWorld(), Origin, Marker, C, false, -1.0f, 0, 0.5f);
		DrawDebugSphere(GetWorld(), Marker, Radius, 12, C, false, -1.0f, 0, 0.5f);
	}
#endif

	return Grip;
}

bool URogueCharacterMoverComponent::GetStanceCurveValue(FName CurveName, float& OutValue) const
{
	OutValue = 0.0f;
	if (CurveName.IsNone())
	{
		return false;
	}
	const USkeletalMeshComponent* Mesh = CacheOwnerCharacter ? CacheOwnerCharacter->GetMesh() : nullptr;
	const UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return false;
	}
	// False when the curve isn't in the current pose -> the caller leaves that limb's pin dormant (per-limb opt-in).
	return AnimInstance->GetCurveValue(CurveName, OutValue);
}

void URogueCharacterMoverComponent::RefreshLimbPins()
{
	const USceneComponent* Updated = GetUpdatedComponent();
	const USkeletalMeshComponent* Mesh = CacheOwnerCharacter ? CacheOwnerCharacter->GetMesh() : nullptr;
	FClimbLimbPin* AllPins[] = { &LeftFootPin, &RightFootPin, &LeftHandPin, &RightHandPin };

	// Pins run in BOTH the sustained climb and the corner turn. Climb uses the dominant plane; the corner uses its two
	// cached faces. Anything else (falling, mantles) releases.
	const bool bClimb = IsClimbing() && !ClimbDominantSurfaceNormalCache.IsNearlyZero() && Updated;
	const bool bCorner = IsCornerTurning() && Updated
		&& !CornerTurn.OldWallNormal.IsNearlyZero() && !CornerTurn.NewWallNormal.IsNearlyZero();

	if (!bClimb && !bCorner)
	{
		// Not in a pin state (falling, mantles): ease every pin OUT. Keep each Location FROZEN at its last pin (never zero
		// it) so a Control Rig effector still blending out isn't dragged toward origin. bPinned/Weight -> 0 so the CR drops
		// the IK; re-entry acquires a fresh pin on the next stance.
		for (FClimbLimbPin* Pin : AllPins)
		{
			Pin->bPinned = false;
			Pin->bApproaching = false;
			Pin->Weight = 0.0f;
			Pin->bClipSwitchRegripped = false;
		}
		ResetIdleRetreat();
		LastDominantClimbClip.Reset();
	}
	else if (bCorner)
	{
		// Corner turn (REACTIVE - the corner clips carry no baked contacts): probe BOTH faces from each live limb and pin to
		// the nearest valid hit, so a limb auto-selects the face it's actually on as the turn carries it old -> new (and the
		// 4-limb straddle at the rest). The stance curves authored on the corner clip gate pin/release; weight = that curve.
		const FVector N1 = CornerTurn.OldWallNormal;
		const FVector P1 = CornerTurn.OldWallLocation;
		const FVector N2 = CornerTurn.NewWallNormal;
		const FVector P2 = CornerTurn.NewWallLocation;
		const FVector Center = Updated->GetComponentLocation();

		auto ProbeCorner = [this, N1, P1, N2, P2](const FVector& FromLoc, float Radius, float Reach, float Backoff) -> FClimbHandGrip
		{
			const FClimbHandGrip G1 = ProbeContactToward(FromLoc, N1, P1, Radius, Reach, Backoff);
			const FClimbHandGrip G2 = ProbeContactToward(FromLoc, N2, P2, Radius, Reach, Backoff);
			if (G1.bFound && G2.bFound)
			{
				return FVector::DistSquared(G1.Location, FromLoc) <= FVector::DistSquared(G2.Location, FromLoc) ? G1 : G2;
			}
			return G2.bFound ? G2 : G1; // one found -> it; neither -> G1's default projection (a void target for the fallback)
		};

		const FVector LeftFootLoc  = Mesh ? Mesh->GetSocketLocation(LeftFootSocketName)  : Center;
		const FVector RightFootLoc = Mesh ? Mesh->GetSocketLocation(RightFootSocketName) : Center;
		const FVector LeftHandLoc  = Mesh ? Mesh->GetSocketLocation(LeftHandSocketName)  : Center;
		const FVector RightHandLoc = Mesh ? Mesh->GetSocketLocation(RightHandSocketName) : Center;

		const FClimbHandGrip LF = ProbeCorner(LeftFootLoc,  FootProbeRadius, FootProbeReach, FootProbeBackoff);
		const FClimbHandGrip RF = ProbeCorner(RightFootLoc, FootProbeRadius, FootProbeReach, FootProbeBackoff);
		const FClimbHandGrip LH = ProbeCorner(LeftHandLoc,  HandProbeRadius, HandProbeReach, HandProbeBackoff);
		const FClimbHandGrip RH = ProbeCorner(RightHandLoc, HandProbeRadius, HandProbeReach, HandProbeBackoff);

		// Reach leash first: a held pin the turn is carrying the body away from is dragged along its face, within reach.
		LeashLimbTargetsWithinReach(Mesh, /*bPredictive*/ false);

		ResolveLimbPin(LeftFootPin,  LeftFootStanceCurveName,  LF.bFound, LF.Location, LF.Normal, LF.Location, LF.Normal, /*bIsArm*/ false);
		ResolveLimbPin(RightFootPin, RightFootStanceCurveName, RF.bFound, RF.Location, RF.Normal, RF.Location, RF.Normal, /*bIsArm*/ false);
		ResolveLimbPin(LeftHandPin,  LeftHandStanceCurveName,  LH.bFound, LH.Location, LH.Normal, LH.Location, LH.Normal, /*bIsArm*/ true);
		ResolveLimbPin(RightHandPin, RightHandStanceCurveName, RH.bFound, RH.Location, RH.Normal, RH.Location, RH.Normal, /*bIsArm*/ true);

		ResetIdleRetreat(); // a turn that settles with no input commits to idle only IdleRetreatDelaySeconds after it ends
		LastDominantClimbClip.Reset();
	}
	else
	{
		const FVector WallNormal = ClimbDominantSurfaceNormalCache;
		const FVector PlanePoint = ClimbDominantSurfaceLocationCache;
		const FTransform MeshXform = Mesh ? Mesh->GetComponentTransform() : FTransform::Identity;

		// Idle = no climb push (at/below the deadzone). Moving: drop any idle-retreat plan (limbs it hasn't released yet keep
		// their grips - a seamless resume) and keep the committed-idle timer armed, so it counts from the last moving tick.
		const bool bIdle = GetClimbMoveSpeedFraction() <= FootPlantIdleInputDeadzone;
		if (!bIdle)
		{
			ResetIdleRetreat();
		}
		if (bIdle != bLimbPinsWasIdle)
		{
			// Idle<->move edge: every limb gets a fresh clip-switch re-grip in the new regime.
			for (FClimbLimbPin* Pin : AllPins)
			{
				Pin->bClipSwitchRegripped = false;
			}
			bLimbPinsWasIdle = bIdle;
		}

		// PREDICTIVE: aim each limb at its BAKED landing for the current stance (resolved against the live mesh root and traced
		// to the surface), re-resolved through the approach then locked at contact. The destination (dominant) clip is the top
		// BS sample while MOVING, and the authored idle cling clip while IDLE (so the limbs settle to its baked grips). With no
		// idle clip assigned, ResolveLimbPinPredictive just HOLDS the last pins at idle instead.
		float DominantWeight = 0.0f;
		const UAnimSequence* DominantClip = bIdle ? ClimbIdleClip.Get() : GetDominantClimbClip(DominantWeight);

		// Blendspace CLIP SWITCH while moving (a direction change): every limb's destination just changed. Open the blend window
		// (the resolver eases weights instead of dropping them) and let each limb's target ease past the spring's teleport guard.
		// Same-clip progress never lands here (the clip doesn't change), nor do idle<->move edges (the last clip is cleared at idle).
		if (!bIdle && DominantClip && LastDominantClimbClip.IsValid() && LastDominantClimbClip.Get() != DominantClip)
		{
			ClimbClipSwitchMs = LastPreSimTimeMs;
			for (FClimbLimbPin* Pin : AllPins)
			{
				Pin->bClipSwitchEase = true;
			}
			if (CVarClimbingDebugDrawing.GetValueOnGameThread())
			{
				UE_LOG(LogTemp, Log, TEXT("[ClimbIK] blendspace clip switch: %s -> %s (limbs blend over %.2fs, from the blendspace)"),
					*LastDominantClimbClip->GetName(), *DominantClip->GetName(), GetClimbClipSwitchBlendTime());
			}
			// Locked limbs the new clip also plants elsewhere re-grip onto its holds now (before the resolvers re-lock them).
			RegripLockedLimbsOnClipSwitch(DominantClip, MeshXform, WallNormal, PlanePoint);
		}
		LastDominantClimbClip = bIdle ? nullptr : DominantClip;

		// REACH LEASH (moving and idle alike): no pinned or fading target is left past LimbReachLeashRatio x its limb's reach -
		// it's dragged back along the wall by just the excess, so a direction change, the blend or the idle clip reclaiming the
		// torso drags a limb instead of stretching it straight. Before the idle block (the retreat plan waits while the body is
		// still dragging pins) and before the resolvers (a limb it can't keep on its face re-grips onto this tick's plant).
		const bool bLeashedPins = LeashLimbTargetsWithinReach(Mesh, /*bPredictive*/ true);

		// Staged idle retreat: the move->idle edge releases nothing - gripping limbs HOLD their move grips (a quick resume is
		// seamless), while a limb caught mid-swing is already unpinned, so it approaches its idle grip at once. Once idle is
		// COMMITTED (IdleRetreatDelaySeconds with no move input), the still-gripping limbs retreat to the authored idle pose
		// as a SEQUENCE - never both hands or both feet at once - planned once, then stepped each tick BEFORE the resolvers
		// so a limb released this tick re-locks at its idle grip in the same tick. Needs the idle clip (it holds the grips).
		if (bIdle && ClimbIdleClip)
		{
			// The plan waits until the leash has stopped dragging pins (the idle clip still pulling the torso back over the
			// capsule), so it measures from settled grips; it then moves the limbs one support at a time.
			if (!bIdleRetreatPlanned && !bLeashedPins
				&& LastPreSimTimeMs - IdleRetreatArmSimTimeMs >= IdleRetreatDelaySeconds * 1000.0)
			{
				BuildIdleRetreatPlan(ClimbIdleClip.Get(), MeshXform, WallNormal, PlanePoint);
				bIdleRetreatPlanned = true;
			}
			AdvanceIdleRetreat();
		}

		ResolveLimbPinPredictive(LeftFootPin,  LeftFootStanceCurveName,  DominantClip, MeshXform, WallNormal, PlanePoint, FootProbeRadius, FootProbeReach, FootProbeBackoff, bIdle);
		ResolveLimbPinPredictive(RightFootPin, RightFootStanceCurveName, DominantClip, MeshXform, WallNormal, PlanePoint, FootProbeRadius, FootProbeReach, FootProbeBackoff, bIdle);
		ResolveLimbPinPredictive(LeftHandPin,  LeftHandStanceCurveName,  DominantClip, MeshXform, WallNormal, PlanePoint, HandProbeRadius, HandProbeReach, HandProbeBackoff, bIdle);
		ResolveLimbPinPredictive(RightHandPin, RightHandStanceCurveName, DominantClip, MeshXform, WallNormal, PlanePoint, HandProbeRadius, HandProbeReach, HandProbeBackoff, bIdle);
	}

	// Convert each world pin into the MESH COMPONENT's space for the Control Rig (its effectors are component-space;
	// feeding a world position flings the rig). Done every tick in both paths, so a held pin stays world-fixed while its
	// component-space value tracks the moving mesh, and a pin blending out still resolves to its frozen world spot.
	if (Mesh)
	{
		const FTransform MeshXform = Mesh->GetComponentTransform();
		const float Dt = LastPreSimDeltaSeconds;
		for (int32 Limb = 0; Limb < RogueClimbLimb::Count; ++Limb)
		{
			FClimbLimbPin* Pin = AllPins[Limb];

			// Raw Control Rig target in WORLD: the on-surface pin pushed out by the "lip" standoff. Applied here, ONCE, from
			// the raw pin so it can't accumulate on a held pin (world Location stays on the surface, the CR target sits proud).
			const FVector RawWorld = Pin->Location + Pin->Normal * LimbPinSurfaceOffset;

			FVector SmoothWorld;
			FVector SmoothNormal = Pin->Normal;
			if (Pin->bRegripSwing && bRegripArc && Pin->Weight > KINDA_SMALL_NUMBER)
			{
				// An re-grip SWING: the lift-travel-place arc IS the motion (smooth by construction), so it replaces the
				// spring, which would lag behind it and flatten it. Park the spring at rest on the arc point so it takes over
				// seamlessly when the swing lands (the arc ends exactly on the target).
				SmoothWorld = EvaluateRegripSwing(*Pin, RawWorld, SmoothNormal);
				Pin->SpringSmoothedWorld = SmoothWorld;
				Pin->SpringPrevTarget = RawWorld;
				Pin->SpringVelocity = FVector::ZeroVector;
				Pin->bSpringValid = false;
			}
			else
			{
				// Spring-smooth the WORLD target so a pin that ACQUIRES or RELOCATES (re-grab, corner face-switch) eases to its
				// new spot over a few frames instead of popping. It smooths the WORLD value, which is constant while a pin is
				// held, so a settled plant gets ZERO added lag - the spring only absorbs target JUMPS. SNAP (and reset the spring)
				// when the limb isn't visibly IK'd (Weight ~ 0), the spring is off, there's no dt, or the jump is a teleport
				// (bigger than LimbIKSpringSnapDistance - a far relocation / a stale first acquire), so we never ease across a jump
				// we don't want to. A re-grip move (arc off) is exempt from the teleport guard: it's an INTENDED long move
				// starting from the limb's live (fresh) target, so it eases however far it goes.
				const bool bTeleport = !Pin->bRegripSwing && !Pin->bClipSwitchEase
					&& FVector::DistSquared(RawWorld, Pin->SpringSmoothedWorld) > FMath::Square(LimbIKSpringSnapDistance);
				const bool bSnap = !bUseLimbIKSpring || Pin->Weight <= KINDA_SMALL_NUMBER || Dt <= 0.0f || bTeleport;

				if (bSnap)
				{
					SmoothWorld = RawWorld;
					Pin->SpringSmoothedWorld = RawWorld;
					Pin->SpringPrevTarget = RawWorld;
					Pin->SpringVelocity = FVector::ZeroVector;
					Pin->bSpringValid = false;
				}
				else
				{
					// Rebuild the engine spring state from our mirrored fields (keeps KismetMathLibrary out of the header).
					FVectorSpringState State;
					State.PrevTarget = Pin->SpringPrevTarget;
					State.Velocity = Pin->SpringVelocity;
					State.bPrevTargetValid = Pin->bSpringValid;
					// TargetVelocityAmount = 0: we are smoothing a jumpy signal, not chasing a target's velocity (per the API docs).
					SmoothWorld = UKismetMathLibrary::VectorSpringInterp(Pin->SpringSmoothedWorld, RawWorld, State,
						LimbIKSpringStiffness, LimbIKSpringCriticalDamping, Dt, /*Mass*/ 1.0f, /*TargetVelocityAmount*/ 0.0f);
					Pin->SpringPrevTarget = State.PrevTarget;
					Pin->SpringVelocity = State.Velocity;
					Pin->bSpringValid = State.bPrevTargetValid;
					Pin->SpringSmoothedWorld = SmoothWorld;
				}

				// A clip-switch ease ends once the eased target has caught up (or the limb isn't visibly IK'd any more).
				if (Pin->bClipSwitchEase && (bSnap || FVector::DistSquared(SmoothWorld, RawWorld) <= FMath::Square(1.0f)))
				{
					Pin->bClipSwitchEase = false;
				}

				// A re-gripping limb (arc off) has ARRIVED once its eased target reaches its new grip (or there was nothing to
				// ease) - the idle-retreat sequencer waits on this before starting its next step.
				if (Pin->bRegripSwing
					&& (bSnap || FVector::DistSquared(SmoothWorld, RawWorld) <= FMath::Square(RegripArrivalTolerance)))
				{
					Pin->bRegripSwing = false;
				}
			}

			// Reach cap: never hand the rig a point the limb can't reach (bCapLimbReach). Visual only - the spring above and the
			// pin keep the real target, so the cap fades out by itself as the target comes back in reach. Only for a limb the IK
			// actually drives (Weight > 0): a frozen pin off the climb (walking, falling, mantles) or a limb the clip is swinging
			// isn't shown, and capping it would only draw a stale target.
			const FVector ShownWorld = (Pin->Weight > KINDA_SMALL_NUMBER) ? CapToLimbReach(Mesh, Limb, SmoothWorld) : SmoothWorld;
			Pin->DisplayedWorld = ShownWorld;

#if ENABLE_DRAW_DEBUG
			// WHITE = what the rig is actually given (after the spring, a swing and the reach cap), sized by the IK weight, with
			// a line to the pin while they differ. The cyan/green sphere is the LOGICAL pin: the limb trails it while the weight
			// fades in, the spring eases, a swing is in flight, or the cap holds it short.
			if (CVarClimbingDebugDrawing.GetValueOnGameThread() && GetWorld() && Pin->Weight > KINDA_SMALL_NUMBER)
			{
				DrawDebugSphere(GetWorld(), ShownWorld, 1.5f + 3.5f * Pin->Weight, 6, FColor::White, false, -1.0f, 0, 0.5f);
				if (FVector::DistSquared(ShownWorld, RawWorld) > 1.0f)
				{
					DrawDebugLine(GetWorld(), ShownWorld, RawWorld, FColor::White, false, -1.0f, 0, 0.5f);
				}
			}
#endif
			Pin->LocalLocation = MeshXform.InverseTransformPosition(ShownWorld);
			Pin->LocalNormal = MeshXform.InverseTransformVectorNoScale(SmoothNormal);
		}
	}

	// After the pins (a re-grip swing that landed this tick has re-loaded its limb): the body's weight shift.
	RefreshClimbWeightShift(Mesh);
}

void URogueCharacterMoverComponent::ResolveLimbPin(FClimbLimbPin& Pin, FName StanceCurveName, bool bSurfaceFound,
	const FVector& SurfaceLocation, const FVector& SurfaceNormal, const FVector& VoidTarget, const FVector& WallNormal,
	bool bIsArm) const
{
#if ENABLE_DRAW_DEBUG
	const bool bWasPinned = Pin.bPinned; // captured before any mutation, so the debug draw can show held vs (re)acquired
#endif

	// Stance source: the limb's authored curve on the corner clip (an ABSENT curve stays dormant - the per-limb opt-in).
	float Alpha = 0.0f;
	if (!GetStanceCurveValue(StanceCurveName, Alpha))
	{
		Pin.bPinned = false;
		Pin.Weight = 0.0f;
		return;
	}
	const bool bStance = Alpha > LimbStanceThreshold;

	if (!bStance)
	{
		// Swing: release the pin and let the limb follow the clip. Keep the last pin frozen (Location untouched) for the
		// arm fallback / a clean blend-out; the stance curve has already ramped Alpha toward 0, so Weight follows.
		Pin.bPinned = false;
		Pin.Weight = 0.0f;
		return;
	}

	// In a stance window, a held pin stays world-locked (one-way ratchet: never re-probe it mid-stance - re-probing would read
	// the limb the IK already moved and the pin walks off up the wall; locking is the whole point) until the stance ends. The
	// reach leash (run just before) drags it along its face if the turn would carry the body out of its reach.

	// Not holding: acquire a fresh pin.
	if (!Pin.bPinned)
	{
		if (bSurfaceFound)
		{
			// Acquire: world-lock the pin at the surface hit and remember it (arm grace-hold / freeze use the last pin).
			Pin.bPinned = true;
			Pin.Location = SurfaceLocation;
			Pin.Normal = SurfaceNormal;
			Pin.bHasLastPin = true;
			Pin.LastPinLocation = SurfaceLocation;
			Pin.LastPinNormal = SurfaceNormal;
		}
		else if (bIsArm && Pin.bHasLastPin)
		{
			// Arm over a void in its stance: edge-search back toward the last grip for the nearest surface; else grace-hold.
			FVector FallbackLoc, FallbackNormal;
			if (FindHandFallbackGrip(VoidTarget, Pin.LastPinLocation, WallNormal, FallbackLoc, FallbackNormal))
			{
				Pin.bPinned = true;
				Pin.Location = FallbackLoc;
				Pin.Normal = FallbackNormal;
			}
		}
		// Else (leg over a void, or an arm with no last grip): leave unpinned - the leg hangs/tucks, the arm plays the clip.
	}

	// Weight: the authored stance curve while pinned - its edge ramps ease the IK in and out.
	Pin.Weight = Pin.bPinned ? Alpha : 0.0f;

#if ENABLE_DRAW_DEBUG
	if (Pin.bPinned && CVarClimbingDebugDrawing.GetValueOnGameThread() && GetWorld())
	{
		// GREEN = the pin HELD this tick (world-locked, correct); RED = it (re)acquired this tick.
		const FColor PinColor = (bWasPinned && Pin.bPinned) ? FColor::Green : FColor::Red;
		DrawDebugSphere(GetWorld(), Pin.Location, 6.0f, 10, PinColor, false, -1.0f, 0, 0.75f);
	}
#endif
}

UAnimSequence* URogueCharacterMoverComponent::GetDominantClimbClip(float& OutWeight)
{
	OutWeight = 0.0f;
	if (!ClimbBlendSpace)
	{
		return nullptr;
	}

	// The component drives the blendspace input, so querying the BS asset with the same values reproduces its active samples
	// + weights (no AnimBP plumbing). AXIS ORDER: GetClimbMoveIntent is (X = up/down, Y = right/left), but the blendspace
	// input is (horizontal, vertical) = (right/left, up/down) - so the components SWAP: BlendInput.X = Intent.Y,
	// BlendInput.Y = Intent.X. (Confirmed by the dominant-clip log: unswapped, climbing Up picked the Right clip.)
	const FVector2D Intent = GetClimbMoveIntent();
	TArray<FBlendSampleData> Samples;
	if (!ClimbBlendSpace->GetSamplesFromBlendInput(FVector(Intent.Y, Intent.X, 0.0), Samples, ClimbBSTriangulationCache, /*bCombineAnimations*/ true))
	{
		return nullptr;
	}

	UAnimSequence* Best = nullptr;
	for (const FBlendSampleData& Sample : Samples)
	{
		const float Weight = Sample.GetClampedWeight();
		if (Sample.Animation && Weight > OutWeight)
		{
			OutWeight = Weight;
			Best = Sample.Animation;
		}
	}

	return Best;
}

float URogueCharacterMoverComponent::GetClimbClipSwitchBlendTime() const
{
	if (!bClipSwitchBlend || !ClimbBlendSpace)
	{
		return 0.0f;
	}

	// Sample Smoothing ("Weight Speed") crossfades the sample weights directly - the clip-switch case. Linear: a weight goes
	// 0 -> 1 in 1 / speed. With "Smoothing" (ease in/out) the engine runs a critically damped spring with smoothing time
	// 1 / (e x speed) (UBlendSpace SmoothWeight / SmoothingTimeFromSpeed), which is ~95% there after 4.74 / (2 / smoothing
	// time) = ~0.87 / speed.
	const float WeightSpeed = ClimbBlendSpace->TargetWeightInterpolationSpeedPerSec;
	if (WeightSpeed > KINDA_SMALL_NUMBER)
	{
		const float EaseInOut95 = 4.744f / (2.0f * UE_EULERS_NUMBER);
		return (ClimbBlendSpace->bTargetWeightInterpolationEaseInOut ? EaseInOut95 : 1.0f) / WeightSpeed;
	}

	// Else the per-axis Input Interpolation smooths the input itself (the pose passes through the samples in between); each
	// Smoothing Type treats its time differently, but all settle in roughly that time. Neither set -> the pose switches
	// instantly, and so does the IK.
	return FMath::Max(ClimbBlendSpace->InterpolationParam[0].InterpolationTime, ClimbBlendSpace->InterpolationParam[1].InterpolationTime);
}

const FRogueClimbLimbContact* URogueCharacterMoverComponent::GetLimbContact(const UAnimSequence* Clip, FName LimbId) const
{
	if (!Clip)
	{
		return nullptr;
	}

	const URogueClimbContactMetaData* ContactMeta = nullptr;
	for (const UAnimMetaData* Meta : Clip->GetMetaData())
	{
		if (const URogueClimbContactMetaData* C = Cast<URogueClimbContactMetaData>(Meta))
		{
			ContactMeta = C;
			break;
		}
	}
	return ContactMeta ? ContactMeta->FindContact(LimbId) : nullptr;
}

void URogueCharacterMoverComponent::ResolveLimbPinPredictive(FClimbLimbPin& Pin, FName StanceCurveName,
	const UAnimSequence* DominantClip, const FTransform& MeshXform, const FVector& WallNormal, const FVector& PlanePoint,
	float ProbeRadius, float ProbeReach, float ProbeBackoff, bool bIdle) const
{
	// Idle with NO idle clip assigned: HOLD the current pin state untouched (the simple behavior) - a planted limb stays
	// gripped where it last was while the body settles. With an idle clip assigned it is passed in as DominantClip, so we
	// fall through and resolve against IT: a limb that is unpinned (caught mid-swing, or released by the idle-retreat
	// sequencer) RE-APPROACHES its idle grip, eased by the spring; a still-gripping limb holds its move grip until released.
	if (bIdle && !DominantClip)
	{
		return;
	}

	// Weight easing. Normally PredictiveWeightInterpSpeed; for the pose's crossfade time after a blendspace clip switch, a rate
	// that covers ~95% in that time (FInterpTo: 3 time constants). RELEASE normally drops the weight at once - an authored
	// stance ramp has already brought it to ~0 by then - but right after a switch the NEW clip's curve can drop a limb held
	// at full weight, so it eases out instead (its Location stays frozen meanwhile - dragged by the reach leash only if the body
	// would leave it out of reach - so it fades from where it was aimed).
	const float WeightDt = LastPreSimDeltaSeconds;
	const float ClipSwitchBlendTime = GetClimbClipSwitchBlendTime();
	const bool bInClipSwitchBlend = ClipSwitchBlendTime > 0.0f
		&& (LastPreSimTimeMs - ClimbClipSwitchMs) < ClipSwitchBlendTime * 1000.0;
	const float WeightSpeed = bInClipSwitchBlend ? 3.0f / ClipSwitchBlendTime : PredictiveWeightInterpSpeed;
	auto Release = [&Pin, bInClipSwitchBlend, WeightDt, WeightSpeed]()
	{
		Pin.bPinned = false;
		Pin.bApproaching = false;
		Pin.bClipSwitchRegripped = false; // the limb left its plant (swing): its next plant gets a fresh clip-switch re-grip
		Pin.Weight = (bInClipSwitchBlend && WeightDt > 0.0f) ? FMath::FInterpTo(Pin.Weight, 0.0f, WeightDt, WeightSpeed) : 0.0f;
	};

	// The baked contacts for this limb on the dominant (destination) clip. No data (clip unbaked / limb not authored) ->
	// dormant no-op, so turning predictive on before a clip is baked just leaves that limb's IK off rather than misbehaving.
	const FRogueClimbLimbContact* Contact = GetLimbContact(DominantClip, StanceCurveName);
	const int32 NumPlants = (Contact && DominantClip) ? Contact->Plants.Num() : 0;
	if (NumPlants == 0)
	{
		Release();
		return;
	}

	// PHASE comes from the LIVE blended pose (the real playhead - the one signal that must reflect the blend). During a state
	// crossfade it is suppressed toward 0 (the incoming clip is at its start, the outgoing idle state carries no phase) - which
	// is exactly what we want below.
	float Phase01 = 0.0f;
	GetStanceCurveValue(ClimbPhaseCurveName, Phase01);
	const float PhaseTime = Phase01 * static_cast<float>(DominantClip->GetPlayLength());

	// WEIGHT comes from the DOMINANT (destination) clip's OWN stance curve at that phase - NOT the blended pose. So a crossfade
	// from a planted idle cling (whose Stance_FootL ~ 1) can't drive this; W is the destination clip's authored value at where
	// the clip actually is (0 at its frame 0), so the foot rides the crossfade and only engages as the clip advances into a
	// plant. This is "transition-to decides": the regime is chosen by code (bIdle / mode), the weight by the destination clip's
	// own curve. An absent stance curve evaluates to 0 -> W 0 -> dormant below (the per-limb opt-in still holds).
	const float W = DominantClip->EvaluateCurveData(StanceCurveName, PhaseTime);

	const bool bInStance = W > FootPlantIdleInputDeadzone;
	if (!bInStance)
	{
		// Swing / not yet planted per the destination clip: release and let the clip play.
		Release();
		return;
	}

	// The plant for this phase (big/small strike), picked by NEAREST-in-phase (circular) so a seam-wrapped hold resolves to
	// its own plant (the end-of-clip one), not the next interior plant. Robust to where in the cycle climb was entered.
	const FRogueClimbLimbPlant* Target = Contact->FindNearestPlant(PhaseTime, static_cast<float>(DominantClip->GetPlayLength()));
	if (!Target)
	{
		Release();
		return;
	}

	// HOLD: world-locked; Location stays frozen while the body climbs. The reach leash (before the resolvers) drags it along
	// the wall only if the body would leave it out of reach, and re-grips it on the arc - unpinned here, re-locked below this
	// same tick - if it can't keep it on its face.
	if (!Pin.bPinned)
	{
		Pin.PredictSelectedPlant = static_cast<int32>(Target - Contact->Plants.GetData());

		// APPROACH: resolve the baked (root-relative) landing against the LIVE mesh root, then trace onto the real surface.
		// Re-resolved each frame so the body's forward travel is folded in (no fall-short) - it converges to the true plant.
		const FClimbHandGrip PlantTarget = ResolvePlantTarget(*Target, MeshXform, WallNormal, PlanePoint, ProbeRadius, ProbeReach, ProbeBackoff);
		Pin.Location = PlantTarget.Location;
		Pin.Normal = PlantTarget.Normal;
		Pin.bApproaching = true;

		// Lock at full contact: freeze the world point so the planted limb holds while the body climbs past it.
		if (W >= PredictiveLockStanceValue)
		{
			Pin.bPinned = true;
			Pin.bApproaching = false;
			Pin.bHasLastPin = true;
			Pin.LastPinLocation = Pin.Location;
			Pin.LastPinNormal = Pin.Normal;
		}
	}

	// Weight comes from the authored stance curve (the blend profile), but SMOOTHED toward it: a discontinuity in W (the clip
	// playhead jumping on an idle<->move transition, or a held approach resuming) then eases the foot in instead of snapping.
	// Authored ramps are followed closely at a high speed; only jumps are capped. Lock/release above use the RAW W.
	Pin.Weight = (WeightSpeed > 0.0f && WeightDt > 0.0f)
		? FMath::FInterpTo(Pin.Weight, W, WeightDt, WeightSpeed)
		: W;

#if ENABLE_DRAW_DEBUG
	if (CVarClimbingDebugDrawing.GetValueOnGameThread() && GetWorld())
	{
		// CYAN = approaching (target re-resolving toward the plant); GREEN = locked/held.
		const FColor PinColor = Pin.bPinned ? FColor::Green : FColor::Cyan;
		DrawDebugSphere(GetWorld(), Pin.Location, 6.0f, 10, PinColor, false, -1.0f, 0, 0.75f);
	}
#endif
}

FClimbHandGrip URogueCharacterMoverComponent::ResolvePlantTarget(const FRogueClimbLimbPlant& Plant, const FTransform& MeshXform,
	const FVector& WallNormal, const FVector& PlanePoint, float ProbeRadius, float ProbeReach, float ProbeBackoff) const
{
	const FVector AnchorWorld = MeshXform.TransformPosition(Plant.Landing.GetLocation());
	FClimbHandGrip Grip = ProbeContactToward(AnchorWorld, WallNormal, PlanePoint, ProbeRadius, ProbeReach, ProbeBackoff);
	if (!Grip.bFound)
	{
		// Miss (over a void): aim at the predicted anchor itself.
		Grip.Location = AnchorWorld;
		Grip.Normal = WallNormal;
	}
	return Grip;
}

FClimbLimbPin& URogueCharacterMoverComponent::GetLimbPin(int32 Limb)
{
	switch (Limb)
	{
	case RogueClimbLimb::LeftFoot:  return LeftFootPin;
	case RogueClimbLimb::RightFoot: return RightFootPin;
	case RogueClimbLimb::LeftHand:  return LeftHandPin;
	default:                        return RightHandPin;
	}
}

const FClimbLimbPin& URogueCharacterMoverComponent::GetLimbPin(int32 Limb) const
{
	return const_cast<URogueCharacterMoverComponent*>(this)->GetLimbPin(Limb);
}

FName URogueCharacterMoverComponent::GetLimbStanceCurveName(int32 Limb) const
{
	switch (Limb)
	{
	case RogueClimbLimb::LeftFoot:  return LeftFootStanceCurveName;
	case RogueClimbLimb::RightFoot: return RightFootStanceCurveName;
	case RogueClimbLimb::LeftHand:  return LeftHandStanceCurveName;
	default:                        return RightHandStanceCurveName;
	}
}

void URogueCharacterMoverComponent::ResetIdleRetreat()
{
	// Limbs already released keep easing to wherever their pin now is (bRegripSwing clears itself on arrival); only the
	// plan is dropped, so limbs it hadn't reached yet keep their grips.
	RetreatPlan.Reset();
	RetreatStepIndex = INDEX_NONE;
	bIdleRetreatPlanned = false;
	bRetreatLeadReleased = false;
	bRetreatFollowReleased = false;
	IdleRetreatArmSimTimeMs = LastPreSimTimeMs;
}

void URogueCharacterMoverComponent::BuildIdleRetreatPlan(const UAnimSequence* IdleClip, const FTransform& MeshXform,
	const FVector& WallNormal, const FVector& PlanePoint)
{
	RetreatPlan.Reset();
	RetreatStepIndex = INDEX_NONE;
	bRetreatLeadReleased = false;
	bRetreatFollowReleased = false;
	if (!IdleClip)
	{
		return;
	}

	// The idle grips, picked + resolved exactly as the predictive resolver will (same plant at the live phase, same target
	// trace), so the spot measured to here is where a released limb re-locks.
	float Phase01 = 0.0f;
	GetStanceCurveValue(ClimbPhaseCurveName, Phase01);
	const float ClipLength = static_cast<float>(IdleClip->GetPlayLength());
	const float PhaseTime = Phase01 * ClipLength;

	// 1) Which limbs need to move, and how far. Only a GRIPPING limb (still pinned at its move grip) is a candidate - a limb
	//    caught mid-swing already retreated on the idle edge, so by now it's pinned at its idle grip (displacement ~0).
	float Displacement[RogueClimbLimb::Count] = {};
	FVector IdleGripLocation[RogueClimbLimb::Count];
	for (int32 Limb = 0; Limb < RogueClimbLimb::Count; ++Limb)
	{
		const FClimbLimbPin& Pin = GetLimbPin(Limb);
		const FRogueClimbLimbContact* Contact = GetLimbContact(IdleClip, GetLimbStanceCurveName(Limb));
		const FRogueClimbLimbPlant* Plant = Contact ? Contact->FindNearestPlant(PhaseTime, ClipLength) : nullptr;
		if (!Pin.bPinned || !Plant)
		{
			continue;
		}

		const bool bFoot = Limb == RogueClimbLimb::LeftFoot || Limb == RogueClimbLimb::RightFoot;
		const FClimbHandGrip IdleGrip = ResolvePlantTarget(*Plant, MeshXform, WallNormal, PlanePoint,
			bFoot ? FootProbeRadius : HandProbeRadius, bFoot ? FootProbeReach : HandProbeReach, bFoot ? FootProbeBackoff : HandProbeBackoff);
		IdleGripLocation[Limb] = IdleGrip.Location;

		const float Distance = FVector::Dist(Pin.Location, IdleGrip.Location);
		if (Distance > LimbRegripMinDistance)
		{
			Displacement[Limb] = Distance;
		}
	}

	// 2) Group into steps. A CONTRALATERAL pair (a hand + the opposite foot) when BOTH of that diagonal need to move - the
	//    other diagonal stays planted, a stable support. The pairing is forced by contralaterality, never chosen; any leftover
	//    limb (its diagonal partner doesn't need to move) is a single, so two hands or two feet are always separate steps.
	static constexpr int32 Diagonals[2][2] = {
		{ RogueClimbLimb::LeftHand,  RogueClimbLimb::RightFoot },
		{ RogueClimbLimb::RightHand, RogueClimbLimb::LeftFoot } };
	bool bGrouped[RogueClimbLimb::Count] = {};
	for (const auto& Diagonal : Diagonals)
	{
		const int32 Hand = Diagonal[0];
		const int32 Foot = Diagonal[1];
		if (Displacement[Hand] > 0.0f && Displacement[Foot] > 0.0f)
		{
			// Lead = the hand (default) or the more-displaced limb; either way the follower trails by only IdleRetreatPairStagger.
			const bool bHandLeads = bIdleRetreatPairHandLeads || Displacement[Hand] >= Displacement[Foot];
			FClimbRetreatStep& Step = RetreatPlan.AddDefaulted_GetRef();
			Step.Lead = bHandLeads ? Hand : Foot;
			Step.Follow = bHandLeads ? Foot : Hand;
			Step.Displacement = Displacement[Hand] + Displacement[Foot];
			bGrouped[Hand] = bGrouped[Foot] = true;
		}
	}
	for (int32 Limb = 0; Limb < RogueClimbLimb::Count; ++Limb)
	{
		if (!bGrouped[Limb] && Displacement[Limb] > 0.0f)
		{
			FClimbRetreatStep& Step = RetreatPlan.AddDefaulted_GetRef();
			Step.Lead = Limb;
			Step.Displacement = Displacement[Limb];
		}
	}

	// 3) Rank: most-displaced step first (the body fixes the worst thing first). The order can never strand the body - every
	//    step is a single or a contralateral pair, so whichever runs first, a planted diagonal remains.
	RetreatPlan.StableSort([](const FClimbRetreatStep& A, const FClimbRetreatStep& B) { return A.Displacement > B.Displacement; });

	if (RetreatPlan.Num() > 0)
	{
		RetreatStepIndex = 0;
		RetreatStepStartMs = LastPreSimTimeMs;
	}

	if (CVarClimbingDebugDrawing.GetValueOnGameThread())
	{
		static const TCHAR* LimbNames[RogueClimbLimb::Count] = { TEXT("LF"), TEXT("RF"), TEXT("LH"), TEXT("RH") };
		FString PlanText;
		for (int32 Index = 0; Index < RetreatPlan.Num(); ++Index)
		{
			const FClimbRetreatStep& Step = RetreatPlan[Index];
			PlanText += (Step.Follow == INDEX_NONE)
				? FString::Printf(TEXT("  %d) %s %.0fcm"), Index + 1, LimbNames[Step.Lead], Step.Displacement)
				: FString::Printf(TEXT("  %d) %s+%s %.0fcm"), Index + 1, LimbNames[Step.Lead], LimbNames[Step.Follow], Step.Displacement);

#if ENABLE_DRAW_DEBUG
			// Each moving limb: a line from its move grip to its idle grip, labelled with its step number.
			for (const int32 Limb : { Step.Lead, Step.Follow })
			{
				if (Limb != INDEX_NONE && GetWorld())
				{
					const FVector From = GetLimbPin(Limb).Location;
					DrawDebugLine(GetWorld(), From, IdleGripLocation[Limb], FColor::Orange, false, 3.0f, 0, 1.0f);
					DrawDebugString(GetWorld(), From, FString::Printf(TEXT("%d %s"), Index + 1, LimbNames[Limb]), nullptr, FColor::Orange, 3.0f);
				}
			}
#endif
		}
		UE_LOG(LogTemp, Log, TEXT("[ClimbRetreat] plan:%s"), PlanText.IsEmpty() ? TEXT(" nothing to move") : *PlanText);
	}
}

void URogueCharacterMoverComponent::AdvanceIdleRetreat()
{
	if (!RetreatPlan.IsValidIndex(RetreatStepIndex))
	{
		return;
	}

	const FClimbRetreatStep& Step = RetreatPlan[RetreatStepIndex];
	const double Elapsed = (LastPreSimTimeMs - RetreatStepStartMs) * 0.001;
	if (Elapsed < 0.0)
	{
		return; // still in the gap before this step
	}

	// The follower trails the lead by the stagger - overlapping, never simultaneous (BeginRegripSwing: unpin + swing).
	// The weight shift leads the lift: the step's limbs UNLOAD at its start (IsLimbUnloaded - the body starts moving onto the
	// others) and only lift WeightShiftLead later. 0, or the shift off, = lift at once.
	const double LiftDelay = bIdleWeightShift ? WeightShiftLead : 0.0;
	if (!bRetreatLeadReleased && Elapsed >= LiftDelay)
	{
		BeginRegripSwing(GetLimbPin(Step.Lead));
		bRetreatLeadReleased = true;
	}
	const bool bPair = Step.Follow != INDEX_NONE;
	if (bPair && !bRetreatFollowReleased && Elapsed >= LiftDelay + IdleRetreatPairStagger)
	{
		BeginRegripSwing(GetLimbPin(Step.Follow));
		bRetreatFollowReleased = true;
	}

	// The step is done once all its limbs are released AND have arrived (or it timed out); the next starts after the gap.
	const bool bAllReleased = bRetreatLeadReleased && (!bPair || bRetreatFollowReleased);
	const bool bArrived = !GetLimbPin(Step.Lead).bRegripSwing && (!bPair || !GetLimbPin(Step.Follow).bRegripSwing);
	if (bAllReleased && (bArrived || Elapsed >= LiftDelay + IdleRetreatStepTimeout))
	{
		++RetreatStepIndex;
		RetreatStepStartMs = LastPreSimTimeMs + IdleRetreatStepGap * 1000.0;
		bRetreatLeadReleased = false;
		bRetreatFollowReleased = false;
	}
}

bool URogueCharacterMoverComponent::LeashLimbTargetsWithinReach(const USkeletalMeshComponent* Mesh, bool bPredictive)
{
	if (!bLimbReachLeash || !Mesh)
	{
		return false;
	}

	static const TCHAR* LimbNames[RogueClimbLimb::Count] = { TEXT("LF"), TEXT("RF"), TEXT("LH"), TEXT("RH") };
	bool bDraggedPinned = false;
	for (int32 Limb = 0; Limb < RogueClimbLimb::Count; ++Limb)
	{
		FClimbLimbPin& Pin = GetLimbPin(Limb);

		// HELD targets only: a pinned one, or one still fading out on its frozen grip (a clip switch eases a released limb's
		// weight out). Not one approaching a plant (the resolver re-resolves it from the live body every tick) or travelling a
		// re-grip arc (it lands on a fresh grip).
		if (Pin.bRegripSwing || (!Pin.bPinned && (Pin.bApproaching || Pin.Weight <= KINDA_SMALL_NUMBER)))
		{
			continue;
		}
		const FName RootBone = GetLimbRootBoneName(Limb);
		const float Reach = GetLimbReachLength(Mesh, Limb);
		const FVector N = Pin.Normal.GetSafeNormal();
		if (Reach <= KINDA_SMALL_NUMBER || N.IsNearlyZero() || Mesh->GetBoneIndex(RootBone) == INDEX_NONE)
		{
			continue;
		}

		// Measured like the reach cap: the limb's root (shoulder / hip, last evaluated pose - so it includes the weight shift)
		// to the IK target (the pin pushed out by the lip standoff - what the rig aims the contact tip at).
		const FVector Root = Mesh->GetBoneLocation(RootBone);
		const FVector Target = Pin.Location + N * LimbPinSurfaceOffset;
		const float Limit = LimbReachLeashRatio * Reach;
		if (FVector::DistSquared(Root, Target) <= FMath::Square(Limit))
		{
			continue; // within reach - untouched
		}

		// The reachable part of the target's plane is a disc: centred on the root's projection, radius sqrt(Limit^2 - H^2)
		// with H = the root's distance from the plane. Drag the target to the disc's nearest point - along the face, the
		// shortest way back into reach, by exactly the excess - then re-trace onto the real surface there: a bumpy face is
		// followed, and past a wall's edge (or into a corner) there's no same-face surface, so a limb is never dragged off it.
		const float H = static_cast<float>(FVector::DotProduct(Root - Target, N));
		bool bDragged = false;
		if (FMath::Abs(H) < Limit)
		{
			const FVector Centre = Root - N * H;
			const FVector FromCentre = Target - Centre;
			const float FromCentreLength = static_cast<float>(FromCentre.Size());
			const float Radius = FMath::Sqrt(FMath::Square(Limit) - FMath::Square(H));
			if (FromCentreLength > KINDA_SMALL_NUMBER)
			{
				const FVector Surface = Centre + FromCentre * (FMath::Min(Radius, FromCentreLength) / FromCentreLength) - N * LimbPinSurfaceOffset;
				const bool bFoot = Limb == RogueClimbLimb::LeftFoot || Limb == RogueClimbLimb::RightFoot;
				const FClimbHandGrip OnFace = ProbeContactToward(Surface, N, Surface, bFoot ? FootProbeRadius : HandProbeRadius,
					bFoot ? FootProbeReach : HandProbeReach, bFoot ? FootProbeBackoff : HandProbeBackoff);
				if (OnFace.bFound)
				{
					// The spring state moves with the pin: the leash is a continuous constraint, not a jump to absorb - easing it
					// would lag the shown limb behind the dragged target, back out of reach.
					const FVector Delta = (OnFace.Location + OnFace.Normal * LimbPinSurfaceOffset) - Target;
					Pin.Location = OnFace.Location;
					Pin.Normal = OnFace.Normal;
					Pin.SpringSmoothedWorld += Delta;
					Pin.SpringPrevTarget += Delta;
					if (Pin.bPinned)
					{
						Pin.LastPinLocation = Pin.Location;
						Pin.LastPinNormal = Pin.Normal;
						bDraggedPinned = true;
					}
					bDragged = true;
#if ENABLE_DRAW_DEBUG
					// MAGENTA = the leash, taut (limb root -> the dragged target) while it drags the limb.
					if (CVarClimbingDebugDrawing.GetValueOnGameThread() && GetWorld())
					{
						DrawDebugLine(GetWorld(), Root, Target + Delta, FColor::Magenta, false, -1.0f, 0, 1.0f);
					}
#endif
				}
			}
		}
		if (bDragged || !Pin.bPinned)
		{
			continue; // dragged; or a fading target that can't be - it just finishes fading (the reach cap bends it meanwhile)
		}

		// A PINNED limb the leash can't keep on its face within reach (the face has no reachable point left - the body turned
		// away from it or pushed off - or that point is off the surface). Climb: let go and re-grip on the arc onto this tick's
		// plant (the resolver re-locks it right after; taken out of any retreat plan). Corner turn: hold - a reactive re-acquire
		// would land on the same unreachable face; the corner clip's stance ends it, and the reach cap bends it meanwhile.
		if (bPredictive)
		{
			BeginRegripSwing(Pin);
			DropLimbFromRetreatPlan(Limb);
			if (CVarClimbingDebugDrawing.GetValueOnGameThread())
			{
				UE_LOG(LogTemp, Log, TEXT("[ClimbReach] %s can't stay on its face within reach (%.0f%% of its reach) - re-grips"),
					LimbNames[Limb], FVector::Dist(Root, Target) / Reach * 100.0);
			}
		}
#if ENABLE_DRAW_DEBUG
		// RED = a pinned limb the leash couldn't keep in reach (2 s for a climb re-grip; per tick while a corner pin holds).
		if (CVarClimbingDebugDrawing.GetValueOnGameThread() && GetWorld())
		{
			DrawDebugLine(GetWorld(), Root, Target, FColor::Red, false, bPredictive ? 2.0f : -1.0f, 0, 1.5f);
		}
#endif
	}
	return bDraggedPinned;
}

void URogueCharacterMoverComponent::RegripLockedLimbsOnClipSwitch(const UAnimSequence* Clip, const FTransform& MeshXform,
	const FVector& WallNormal, const FVector& PlanePoint)
{
	if (!bRegripOnClipSwitch || !Clip)
	{
		return;
	}

	// Where the NEW clip is (its samples share the blendspace's normalized time, so the live phase is its phase too), and for
	// each locked limb the plant it has there - picked + resolved exactly as the resolver will re-lock it.
	float Phase01 = 0.0f;
	GetStanceCurveValue(ClimbPhaseCurveName, Phase01);
	const float ClipLength = static_cast<float>(Clip->GetPlayLength());
	const float PhaseTime = Phase01 * ClipLength;

	static const TCHAR* LimbNames[RogueClimbLimb::Count] = { TEXT("LF"), TEXT("RF"), TEXT("LH"), TEXT("RH") };
	for (int32 Limb = 0; Limb < RogueClimbLimb::Count; ++Limb)
	{
		FClimbLimbPin& Pin = GetLimbPin(Limb);
		if (!Pin.bPinned || Pin.bRegripSwing || Pin.bClipSwitchRegripped)
		{
			continue;
		}

		// Only a limb the new clip also has in STANCE - one it has mid-swing is released by the resolver instead (fades out).
		const FName StanceCurveName = GetLimbStanceCurveName(Limb);
		const FRogueClimbLimbContact* Contact = GetLimbContact(Clip, StanceCurveName);
		const FRogueClimbLimbPlant* Plant = Contact ? Contact->FindNearestPlant(PhaseTime, ClipLength) : nullptr;
		if (!Plant || Clip->EvaluateCurveData(StanceCurveName, PhaseTime) <= FootPlantIdleInputDeadzone)
		{
			continue;
		}

		const bool bFoot = Limb == RogueClimbLimb::LeftFoot || Limb == RogueClimbLimb::RightFoot;
		const FClimbHandGrip NewGrip = ResolvePlantTarget(*Plant, MeshXform, WallNormal, PlanePoint,
			bFoot ? FootProbeRadius : HandProbeRadius, bFoot ? FootProbeReach : HandProbeReach, bFoot ? FootProbeBackoff : HandProbeBackoff);
		const float Distance = static_cast<float>(FVector::Dist(NewGrip.Location, Pin.Location));
		if (Distance <= LimbRegripMinDistance)
		{
			continue; // close enough - keep the hold
		}

		BeginRegripSwing(GetLimbPin(Limb));
		Pin.bClipSwitchRegripped = true;
		if (CVarClimbingDebugDrawing.GetValueOnGameThread())
		{
			UE_LOG(LogTemp, Log, TEXT("[ClimbIK] %s re-grips onto %s's plant (%.0fcm away)"), LimbNames[Limb], *Clip->GetName(), Distance);
		}
	}
}

void URogueCharacterMoverComponent::BeginRegripSwing(FClimbLimbPin& Pin) const
{
	// Release = unpin. The predictive resolver (run right after this) re-locks the limb in this same tick - at its idle grip
	// at idle (the idle clip's flat stance gives W = 1), on the current clip's plant while moving; bRegripSwing marks it in
	// flight until it lands.
	Pin.bPinned = false;
	Pin.bRegripSwing = true;

	// Start of the swing arc: the IK target as last SHOWN (after the reach cap - so a limb released because it was out of
	// reach starts from where it really was, not from the unreachable grip) and that grip's normal (the lift-off direction).
	// The end, duration and lift are sized on the swing's first tick, once the resolver has re-locked the target this tick.
	Pin.RegripSwingFrom = Pin.DisplayedWorld.IsZero() ? Pin.SpringSmoothedWorld : Pin.DisplayedWorld;
	Pin.RegripSwingFromNormal = Pin.Normal;
	Pin.RegripSwingStartMs = LastPreSimTimeMs;
	Pin.RegripSwingDuration = 0.0f;
}

void URogueCharacterMoverComponent::DropLimbFromRetreatPlan(int32 Limb)
{
	// A limb that left out of turn (the reach leash's last-resort re-grip). The current step: mark it released so the
	// sequencer doesn't lift it again (it waits for its landing as usual).
	if (RetreatPlan.IsValidIndex(RetreatStepIndex))
	{
		const FClimbRetreatStep& Current = RetreatPlan[RetreatStepIndex];
		bRetreatLeadReleased |= Current.Lead == Limb;
		bRetreatFollowReleased |= Current.Follow == Limb;
	}

	// Later steps: take the limb out - a pair keeps its other limb as a single; a single becomes empty and is removed.
	for (int32 Index = RetreatPlan.Num() - 1; Index > RetreatStepIndex; --Index)
	{
		FClimbRetreatStep& Step = RetreatPlan[Index];
		if (Step.Follow == Limb)
		{
			Step.Follow = INDEX_NONE;
		}
		else if (Step.Lead == Limb)
		{
			if (Step.Follow != INDEX_NONE)
			{
				Step.Lead = Step.Follow;
				Step.Follow = INDEX_NONE;
			}
			else
			{
				RetreatPlan.RemoveAt(Index);
			}
		}
	}
}

FName URogueCharacterMoverComponent::GetLimbRootBoneName(int32 Limb) const
{
	switch (Limb)
	{
	case RogueClimbLimb::LeftFoot:  return LeftFootRootBoneName;
	case RogueClimbLimb::RightFoot: return RightFootRootBoneName;
	case RogueClimbLimb::LeftHand:  return LeftHandRootBoneName;
	default:                        return RightHandRootBoneName;
	}
}

FVector URogueCharacterMoverComponent::CapToLimbReach(const USkeletalMeshComponent* Mesh, int32 Limb, const FVector& Target)
{
	if (!bCapLimbReach || !Mesh)
	{
		return Target;
	}
	const FName RootBone = GetLimbRootBoneName(Limb);
	const float Reach = GetLimbReachLength(Mesh, Limb);
	if (Reach <= KINDA_SMALL_NUMBER || Mesh->GetBoneIndex(RootBone) == INDEX_NONE)
	{
		return Target;
	}

	const FVector Root = Mesh->GetBoneLocation(RootBone);
	const FVector ToTarget = Target - Root;
	const float Distance = static_cast<float>(ToTarget.Size());
	const float Soft = LimbReachSoftStart * Reach;
	const float Limit = LimbReachLimit * Reach;
	if (Distance <= FMath::Min(Soft, Limit) || Distance <= KINDA_SMALL_NUMBER)
	{
		return Target;
	}

	// Past the soft start: d' = s + (M - s)(1 - e^-((d - s)/(M - s))) - slope 1 where it begins (no kink), asymptotic to the
	// limit M. No soft band (M <= s) = a hard cap at M.
	const float Band = Limit - Soft;
	const float Capped = (Band > KINDA_SMALL_NUMBER)
		? Soft + Band * (1.0f - FMath::Exp(-(Distance - Soft) / Band))
		: Limit;
	const FVector Shown = Root + ToTarget * (Capped / Distance);

#if ENABLE_DRAW_DEBUG
	// YELLOW = the part of the target the limb can't reach (shown point -> real target).
	if (CVarClimbingDebugDrawing.GetValueOnGameThread() && GetWorld())
	{
		DrawDebugLine(GetWorld(), Shown, Target, FColor::Yellow, false, -1.0f, 0, 1.0f);
	}
#endif
	return Shown;
}

FName URogueCharacterMoverComponent::GetLimbReachTipName(int32 Limb) const
{
	switch (Limb)
	{
	case RogueClimbLimb::LeftFoot:  return LeftFootReachTipName;
	case RogueClimbLimb::RightFoot: return RightFootReachTipName;
	case RogueClimbLimb::LeftHand:  return LeftHandSocketName;
	default:                        return RightHandSocketName;
	}
}

float URogueCharacterMoverComponent::GetLimbReachLength(const USkeletalMeshComponent* Mesh, int32 Limb)
{
	const USkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset)
	{
		return 0.0f;
	}
	if (LimbReachLengthCacheMesh.Get() != Asset)
	{
		LimbReachLengthCacheMesh = Asset;
		for (int32 Index = 0; Index < RogueClimbLimb::Count; ++Index)
		{
			LimbReachLengthCache[Index] = ComputeLimbChainLength(*Asset, GetLimbRootBoneName(Index), GetLimbReachTipName(Index));
		}
	}
	return LimbReachLengthCache[Limb] * static_cast<float>(Mesh->GetComponentScale().X);
}

bool URogueCharacterMoverComponent::IsLimbUnloaded(int32 Limb) const
{
	// Unloaded = in the current step (once it has started, not during the gap before it), from the step's start until the
	// limb lands: not yet released (the lead-in, when only the body moves), or released and still swinging.
	if (!RetreatPlan.IsValidIndex(RetreatStepIndex) || LastPreSimTimeMs < RetreatStepStartMs)
	{
		return false;
	}
	const FClimbRetreatStep& Step = RetreatPlan[RetreatStepIndex];
	if (Limb != Step.Lead && Limb != Step.Follow)
	{
		return false;
	}
	const bool bReleased = (Limb == Step.Lead) ? bRetreatLeadReleased : bRetreatFollowReleased;
	return !bReleased || GetLimbPin(Limb).bRegripSwing;
}

void URogueCharacterMoverComponent::RefreshClimbWeightShift(const USkeletalMeshComponent* Mesh)
{
	// Off the wall (falling, mantles): no shift, and nothing left to ease back from.
	if (!IsClimbing() && !IsCornerTurning())
	{
		BodyShiftWorld = FVector::ZeroVector;
		BodyShiftRate = FVector::ZeroVector;
		ClimbBodyShift = FVector::ZeroVector;
		ClimbChestRotation = FRotator::ZeroRotator;
		return;
	}

	// Wall frame: WallUp = gravity-up flattened onto the wall, WallRight = along the wall, horizontal (sign irrelevant - the
	// shift projects onto it and back, and the chest tilt is built from the same axis).
	const FVector N = ClimbDominantSurfaceNormalCache.GetSafeNormal();
	const FVector Up = GetUpDirection();
	const FVector WallUp = N.IsZero() ? FVector::ZeroVector : FVector::VectorPlaneProject(Up, N).GetSafeNormal();
	const FVector WallRight = FVector::CrossProduct(N, WallUp).GetSafeNormal();

	// Target: zero unless the idle-retreat sequencer has a limb unloaded (moving, corner turns and rest all ease back to 0).
	FVector Target = FVector::ZeroVector;
	FVector LoadCentroid = FVector::ZeroVector;
	FVector FullCentroid = FVector::ZeroVector;
	bool bAnyUnloaded = false;
	if (bIdleWeightShift && IsClimbing() && !WallUp.IsZero())
	{
		FVector LoadSum = FVector::ZeroVector;
		int32 LoadCount = 0;
		bool bHandUnloaded = false;
		for (int32 Limb = 0; Limb < RogueClimbLimb::Count; ++Limb)
		{
			const FClimbLimbPin& Pin = GetLimbPin(Limb);
			const bool bUnloaded = IsLimbUnloaded(Limb);

			// The grip the limb bears (or bore) weight on. A released retreating limb's pin is already re-locked at its idle
			// grip, so it counts from where it left (only the along-wall part matters, so the swing's standoff is harmless).
			const FVector Grip = (bUnloaded && Pin.bRegripSwing) ? Pin.RegripSwingFrom : Pin.Location;
			FullCentroid += Grip;
			if (bUnloaded)
			{
				bAnyUnloaded = true;
				bHandUnloaded |= Limb == RogueClimbLimb::LeftHand || Limb == RogueClimbLimb::RightHand;
			}
			else
			{
				LoadSum += Grip;
				++LoadCount;
			}
		}
		FullCentroid /= static_cast<double>(RogueClimbLimb::Count);

		if (bAnyUnloaded && LoadCount > 0)
		{
			// Sit over the holds still bearing weight: shift by how far their centroid sits from the all-four centroid, split
			// along the wall (sideways / up-down get their own gains), capped so a planted limb stays within reach.
			LoadCentroid = LoadSum / LoadCount;
			const FVector Delta = LoadCentroid - FullCentroid;
			const FVector AlongWall = WallRight * (FVector::DotProduct(Delta, WallRight) * WeightShiftLateralGain)
				+ WallUp * (FVector::DotProduct(Delta, WallUp) * WeightShiftVerticalGain);
			Target = AlongWall.GetClampedToMaxSize(WeightShiftMaxDistance);

			// Hip-in while a hand is off: hips toward the wall, faded out as the wall leans back into a slab (N . Up > 0).
			if (bHandUnloaded)
			{
				const float SlabFade = 1.0f - FMath::SmoothStep(0.0f,
					FMath::Sin(FMath::DegreesToRadians(WeightShiftHipInSlabFadeAngle)), static_cast<float>(FVector::DotProduct(N, Up)));
				Target -= N * (WeightShiftHipInDepth * SlabFade);
			}
		}
	}

	// Critically damped: eases onto the holds and back without overshoot (the target steps as limbs unload / land). Then the
	// speed cap: limit this tick's move, and the smoother's own rate with it, so it can't build up speed it isn't allowed
	// to use (a big target jump then plays out as an even glide that still eases into the end).
	const FVector PrevShift = BodyShiftWorld;
	FMath::CriticallyDampedSmoothing(BodyShiftWorld, BodyShiftRate, Target, FVector::ZeroVector,
		LastPreSimDeltaSeconds, WeightShiftSmoothingTime);
	if (WeightShiftMaxSpeed > 0.0f && LastPreSimDeltaSeconds > 0.0f)
	{
		BodyShiftWorld = PrevShift + (BodyShiftWorld - PrevShift).GetClampedToMaxSize(WeightShiftMaxSpeed * LastPreSimDeltaSeconds);
		BodyShiftRate = BodyShiftRate.GetClampedToMaxSize(WeightShiftMaxSpeed);
	}

	// Chest, read back off the SMOOTHED shift so it moves exactly with the body. Rotating WallUp about (WallUp x Dir) by a
	// positive angle swings it toward Dir:
	//   (a) tilt - the chest's top toward the sideways shift (axis WallUp x WallRight = the wall normal);
	//   (b) lean - the chest's top back off the wall by the angle that keeps the shoulders put as the hips press in.
	FQuat ChestWorld = FQuat::Identity;
	if (!WallUp.IsZero())
	{
		const float Sideways = static_cast<float>(FVector::DotProduct(BodyShiftWorld, WallRight));
		const float HipIn = FMath::Max(0.0f, -static_cast<float>(FVector::DotProduct(BodyShiftWorld, N)));
		const FQuat Tilt(FVector::CrossProduct(WallUp, WallRight), FMath::DegreesToRadians(WeightShiftChestTiltPerCm * Sideways));
		const FQuat Lean(FVector::CrossProduct(WallUp, N), WeightShiftChestLeanScale * FMath::Atan2(HipIn, WeightShiftShoulderHeight));
		ChestWorld = Lean * Tilt;
	}

	// Publish in mesh space for the Control Rig (a world rotation R is M^-1 * R * M in the component's frame).
	if (Mesh)
	{
		const FTransform MeshXform = Mesh->GetComponentTransform();
		const FQuat MeshQuat = MeshXform.GetRotation();
		ClimbBodyShift = MeshXform.InverseTransformVector(BodyShiftWorld);
		ClimbChestRotation = (MeshQuat.Inverse() * ChestWorld * MeshQuat).Rotator();
	}

#if ENABLE_DRAW_DEBUG
	// Load-bearing centroid (amber) vs all-four centroid (white) on the wall, and the shift from the capsule centre.
	if (CVarClimbingDebugDrawing.GetValueOnGameThread() && GetWorld() && GetUpdatedComponent())
	{
		if (bAnyUnloaded)
		{
			DrawDebugPoint(GetWorld(), LoadCentroid, 10.0f, FColor::Orange, false, -1.0f);
			DrawDebugPoint(GetWorld(), FullCentroid, 10.0f, FColor::White, false, -1.0f);
		}
		if (!BodyShiftWorld.IsNearlyZero(0.1f))
		{
			const FVector Center = GetUpdatedComponent()->GetComponentLocation();
			DrawDebugLine(GetWorld(), Center, Center + BodyShiftWorld, FColor::Orange, false, -1.0f, 0, 1.0f);
			DrawDebugSphere(GetWorld(), Center + BodyShiftWorld, 3.0f, 8, FColor::Orange, false, -1.0f, 0, 0.5f);
		}
	}
#endif
}

FVector URogueCharacterMoverComponent::EvaluateRegripSwing(FClimbLimbPin& Pin, const FVector& To, FVector& OutNormal) const
{
	const FVector From = Pin.RegripSwingFrom;

	// First tick of the swing (the resolver re-locked the target this tick, so the end is known): size it from the distance -
	// a short re-grip is a quick little hop, a long one a slower, higher arc.
	if (Pin.RegripSwingDuration <= 0.0f)
	{
		const float Distance = FVector::Dist(From, To);
		Pin.RegripSwingDuration = FMath::Max(RegripSwingMinDuration, Distance / RegripSwingSpeed);
		Pin.RegripSwingLift = FMath::Min(Distance * RegripSwingLiftRatio, RegripSwingMaxLift);

		// Peel direction: from the body out to the swing, flattened onto the wall - fixed for the swing (the body holds still
		// at idle), so a limb peels the same way whichever way it travels.
		const USceneComponent* Updated = GetUpdatedComponent();
		const FVector Away = Updated ? 0.5f * (From + To) - Updated->GetComponentLocation() : FVector::ZeroVector;
		Pin.RegripSwingPeelDir = FVector::VectorPlaneProject(Away, Pin.Normal).GetSafeNormal();
	}

	// Progress: time through the swing, shaped by the authored profile (or a smoothstep ease-in/out).
	const float Time01 = FMath::Clamp(static_cast<float>((LastPreSimTimeMs - Pin.RegripSwingStartMs) * 0.001 / Pin.RegripSwingDuration), 0.0f, 1.0f);
	const float U = RegripSwingProfile
		? FMath::Clamp(RegripSwingProfile->GetFloatValue(Time01), 0.0f, 1.0f)
		: FMath::SmoothStep(0.0f, 1.0f, Time01);

	// Cubic Bezier with both handles straight out along the surface normals: the limb peels straight OFF the start grip,
	// travels across, and comes straight DOWN onto the target. A cubic's midpoint only reaches 3/4 of its handle height, so
	// the handles are scaled to make RegripSwingLift the arc's PEAK. P3 is the live target, so the arc tracks it if it moves.
	const float Handle = Pin.RegripSwingLift / 0.75f;
	const FVector P1 = From + Pin.RegripSwingFromNormal * Handle;
	const FVector P2 = To + Pin.Normal * Handle;
	const float V = 1.0f - U;
	const FVector Point = From * (V * V * V) + P1 * (3.0f * V * V * U) + P2 * (3.0f * V * U * U) + To * (U * U * U);
	OutNormal = FMath::Lerp(Pin.RegripSwingFromNormal, Pin.Normal, U).GetSafeNormal();

	// Peel: lean the normal toward the peel direction by an angle that follows the lift (the arc's height is 3UV of the
	// handle, so 4UV is the lift normalised to 1 at its peak) - flush on both grips, most tilted at the top of the arc.
	const bool bIsHand = &Pin == &LeftHandPin || &Pin == &RightHandPin;
	const float PeelRad = FMath::DegreesToRadians(bIsHand ? RegripHandPeelAngle : RegripFootPeelAngle) * 4.0f * U * V;
	const FVector PeelDir = FVector::VectorPlaneProject(Pin.RegripSwingPeelDir, OutNormal).GetSafeNormal();
	if (!PeelDir.IsZero() && !FMath::IsNearlyZero(PeelRad))
	{
		OutNormal = OutNormal * FMath::Cos(PeelRad) + PeelDir * FMath::Sin(PeelRad);
	}

#if ENABLE_DRAW_DEBUG
	if (CVarClimbingDebugDrawing.GetValueOnGameThread() && GetWorld())
	{
		DrawDebugSphere(GetWorld(), Point, 4.0f, 8, FColor::Orange, false, -1.0f, 0, 0.5f);
		DrawDebugLine(GetWorld(), Point, Point + OutNormal * 15.0f, FColor::Orange, false, -1.0f, 0, 0.5f);
	}
#endif

	// Landed: the sequencer's arrival for this limb (the spring, parked on the arc, takes over at rest).
	if (Time01 >= 1.0f)
	{
		Pin.bRegripSwing = false;
	}
	return Point;
}

bool URogueCharacterMoverComponent::FindHandFallbackGrip(const FVector& VoidTarget, const FVector& LastGrip,
	const FVector& WallNormal, FVector& OutLocation, FVector& OutNormal) const
{
	// Default to grace-holding the last grip (the segment's far, on-wall end) - always a valid hold when we get here.
	OutLocation = LastGrip;
	OutNormal = WallNormal;
	if (!GetWorld())
	{
		return true;
	}

	// Sample a couple of points between the intended (void) grip and the last good grip; take the hit CLOSEST to the void
	// (most reach toward where the clip wanted the hand), so check the near fraction first. Each sample sphere-sweeps
	// toward the wall from just off it (the same pattern as the hand grip probe).
	const float Fractions[2] = { HandFallbackSampleNear, HandFallbackSampleFar };
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(HandProbeRadius);
	for (const float T : Fractions)
	{
		const FVector Sample = FMath::Lerp(VoidTarget, LastGrip, T);
		const FVector Origin = Sample + WallNormal * HandProbeBackoff;
		const FVector End = Origin - WallNormal * (HandProbeBackoff + HandProbeReach);
		FHitResult Hit;
		if (GetWorld()->SweepSingleByChannel(Hit, Origin, End, FQuat::Identity, ECC_WorldStatic, Sphere, ClimbQueryParams)
			&& !Hit.bStartPenetrating)
		{
			const float SteepnessDot = FVector::DotProduct(Hit.ImpactNormal, GetUpDirection());
			const bool bSameFace = FVector::DotProduct(Hit.ImpactNormal, WallNormal) > 0.3f;
			if (IsSurfaceClimbable(SteepnessDot) && bSameFace)
			{
				OutLocation = Hit.ImpactPoint; // raw; LimbPinSurfaceOffset is applied at consume (RefreshLimbPins)
				OutNormal = Hit.ImpactNormal;
				return true;
			}
		}
	}
	return true; // nothing between hit -> grace-hold LastGrip (already set)
}

void URogueCharacterMoverComponent::RefreshCornerProbes()
{
	LeftCorner = FClimbCornerProbeResult();
	RightCorner = FClimbCornerProbeResult();

	if (!IsClimbing() || ClimbDominantSurfaceNormalCache.IsNearlyZero() || !GetUpdatedComponent())
	{
		return;
	}

	// Both sides every tick, independent of the stick: availability is published like the mantle-down affordance, and
	// whichever design consumes it (montage traversal or input-driven strikes) just reads the side it needs.
	LeftCorner = ProbeCorner(-1.0f);
	RightCorner = ProbeCorner(1.0f);
}

FClimbCornerProbeResult URogueCharacterMoverComponent::ProbeCorner(float Side) const
{
	FClimbCornerProbeResult Result;

	const USceneComponent* Updated = GetUpdatedComponent();
	const UWorld* World = GetWorld();
	if (!Updated || !World)
	{
		return Result;
	}

#if ENABLE_DRAW_DEBUG
	const bool bDebug = CVarClimbingDebugDrawing.GetValueOnGameThread();
#endif

	float CapR = ClimbDetectionCapsuleRadius;
	float CapHH = ClimbDetectionCapsuleHalfHeight;
	if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Updated))
	{
		CapR = Capsule->GetScaledCapsuleRadius();
		CapHH = Capsule->GetScaledCapsuleHalfHeight();
	}
	const float Standoff = (CornerWallStandoff > 0.0f) ? CornerWallStandoff : CapR;

	// Current wall basis - same convention as the climb mode, so "right" here is the stick's +Y while climbing.
	const FVector N1 = ClimbDominantSurfaceNormalCache;
	const FVector P1 = ClimbDominantSurfaceLocationCache;
	const FVector Up = GetUpDirection();
	FVector WallUp = (Up - Up.ProjectOnToNormal(N1)).GetSafeNormal();
	if (WallUp.IsNearlyZero()) { WallUp = Up; }
	const FVector WallRight = FVector::CrossProduct(N1, WallUp).GetSafeNormal();
	const FVector SideDir = WallRight * Side;
	const FVector Center = Updated->GetComponentLocation();
	const float SameFaceDot = FMath::Cos(FMath::DegreesToRadians(MaxClimbNormalDeviationDegrees));

	// --- Stage 1: find the side face at the HAND ROW ---
	// Same height as the coverage grid's top row (ClimbGridTopOffset + the climb up-bias along the wall), so the probe and
	// the climb agree on where the hands are. Only the hands matter: a valid climb needs just the hand row, and while
	// hanging both faces may exist only up there - any lower probe would read the void as "no corner" (or a false edge).
	const FVector Origin = Center + WallUp * ClimbGridTopOffset + GetClimbSampleUpBias(N1);
	const float Gap = FVector::DotProduct(Origin - P1, N1); // perpendicular distance to the current face at the hands

	ERogueClimbCornerType Type = ERogueClimbCornerType::None;
	FHitResult FaceHit;

	// (a) INNER: a side ray, in front of the face. Anything it hits is either the concave corner's wall (it must face back
	// at us and be climbable) or an obstruction - and an obstruction rules out an outer corner on this side too.
	const FVector InnerEnd = Origin + SideDir * (CapR + CornerInnerArmDistance);
	FHitResult InnerHit;
	if (World->LineTraceSingleByChannel(InnerHit, Origin, InnerEnd, ECC_WorldStatic, ClimbQueryParams))
	{
		const bool bValid = IsSurfaceClimbable(FVector::DotProduct(InnerHit.ImpactNormal, Up))
			&& FVector::DotProduct(InnerHit.ImpactNormal, SideDir) < 0.0f;
#if ENABLE_DRAW_DEBUG
		if (bDebug)
		{
			DrawDebugLine(World, Origin, InnerHit.ImpactPoint, bValid ? FColor::Yellow : FColor::Red, false, -1.0f, 0, 1.0f);
		}
#endif
		if (!bValid)
		{
			return Result;
		}
		Type = ERogueClimbCornerType::Inner;
		FaceHit = InnerHit;
	}
	else
	{
#if ENABLE_DRAW_DEBUG
		if (bDebug)
		{
			DrawDebugLine(World, Origin, InnerEnd, FColor::Silver, false, -1.0f, 0, 0.5f);
		}
#endif
		// (b) OUTER edge test: a forward ray at the arm distance. If the SAME face is still there, the wall continues past
		// our reach on this side - no corner. (Cheap gate: the wrap ray below only fires at an actual edge.) We're climbing,
		// so our own face exists at the hands - a miss here really is the face ending beside us, not a void below.
		const FVector EdgeOrigin = Origin + SideDir * CornerOuterArmDistance;
		const FVector EdgeEnd = EdgeOrigin - N1 * (Gap + CornerOuterWrapDepth);
		FHitResult EdgeHit;
		const bool bEdgeHit = World->LineTraceSingleByChannel(EdgeHit, EdgeOrigin, EdgeEnd, ECC_WorldStatic, ClimbQueryParams);
		const bool bFaceContinues = bEdgeHit && FVector::DotProduct(EdgeHit.ImpactNormal, N1) >= SameFaceDot;
#if ENABLE_DRAW_DEBUG
		if (bDebug)
		{
			DrawDebugLine(World, EdgeOrigin, bEdgeHit ? EdgeHit.ImpactPoint : EdgeEnd, bFaceContinues ? FColor::Silver : FColor::Orange, false, -1.0f, 0, 0.5f);
		}
#endif
		if (bFaceContinues)
		{
			return Result;
		}

		// (c) OUTER wrap ray - the mantle-down backward-ray trick turned 90deg: start PAST the edge and BEHIND the face's
		// plane, fire back toward the body. The first climbable hit is the side face; its normal points the way we were
		// moving (+SideDir), which is what makes it convex.
		const FVector WrapStart = EdgeEnd + SideDir * CornerOuterWrapOut;
		const FVector WrapEnd = WrapStart - SideDir * (CornerOuterArmDistance + CornerOuterWrapOut);
		FHitResult WrapHit;
		const bool bWrapHit = World->LineTraceSingleByChannel(WrapHit, WrapStart, WrapEnd, ECC_WorldStatic, ClimbQueryParams);
		const bool bValid = bWrapHit && !WrapHit.bStartPenetrating
			&& IsSurfaceClimbable(FVector::DotProduct(WrapHit.ImpactNormal, Up))
			&& FVector::DotProduct(WrapHit.ImpactNormal, SideDir) > 0.0f;
#if ENABLE_DRAW_DEBUG
		if (bDebug)
		{
			DrawDebugLine(World, WrapStart, bWrapHit ? WrapHit.ImpactPoint : WrapEnd, bValid ? FColor::Yellow : FColor::Red, false, -1.0f, 0, 1.0f);
		}
#endif
		if (!bValid)
		{
			return Result;
		}
		Type = ERogueClimbCornerType::Outer;
		FaceHit = WrapHit;
	}

	const FVector N2 = FaceHit.ImpactNormal;
	const FVector P2 = FaceHit.ImpactPoint;
	Result.Type = Type;
	Result.NewWallNormal = N2;
	Result.NewWallLocation = P2;
	Result.TurnDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(N1, N2), -1.0f, 1.0f)));

	// --- Stage 2: the corner frame ---
	// The corner is the exact intersection line of the two planes (no edge scan needed). Orient it upward and slide it to
	// the character's height, so the turn happens where we are.
	FVector LinePoint, LineDir;
	if (Result.TurnDegrees < CornerMinTurnDegrees || Result.TurnDegrees > CornerMaxTurnDegrees
		|| !FMath::IntersectPlanes2(LinePoint, LineDir, FPlane(P1, N1), FPlane(P2, N2)))
	{
#if ENABLE_DRAW_DEBUG
		if (bDebug)
		{
			DrawDebugString(World, P2, FString::Printf(TEXT("corner %.0f deg (out of range)"), Result.TurnDegrees), nullptr, FColor::Red, 0.0f);
		}
#endif
		return Result;
	}
	if (FVector::DotProduct(LineDir, Up) < 0.0f)
	{
		LineDir = -LineDir;
	}
	const FVector Corner = LinePoint + LineDir * FVector::DotProduct(Center - LinePoint, LineDir);
	Result.CornerPoint = Corner;
	Result.CornerDirection = LineDir;

	// Same orientation rule as the climb mode: up follows the face's up-slope, forward faces into it.
	auto MakeFacingQuat = [&Up](const FVector& FaceNormal)
	{
		FVector FaceUp = (Up - Up.ProjectOnToNormal(FaceNormal)).GetSafeNormal();
		if (FaceUp.IsNearlyZero()) { FaceUp = Up; }
		return FRotationMatrix::MakeFromZX(FaceUp, -FaceNormal).ToQuat();
	};

	// End (strike 2): on the new face, Standoff off it, and EndOffset along it AWAY from the corner. Face 2 extends out in
	// front of the old face for an inner corner (+N1) and behind it for an outer one (-N1); the in-plane direction is
	// built from the line (not N1 directly) so it stays on the face for non-90deg corners and tilted walls.
	FVector Away2 = FVector::CrossProduct(LineDir, N2).GetSafeNormal();
	const FVector Extent2 = (Type == ERogueClimbCornerType::Inner) ? N1 : -N1;
	if (FVector::DotProduct(Away2, Extent2) < 0.0f)
	{
		Away2 = -Away2;
	}
	const float EndOffset = (Type == ERogueClimbCornerType::Inner) ? CornerInnerEndOffset : CornerOuterEndOffset;
	const FQuat EndQuat = MakeFacingQuat(N2);
	const FVector EndLoc = Corner + N2 * Standoff + Away2 * EndOffset;
	Result.EndTransform = FTransform(EndQuat, EndLoc);

	// Mid (strike 1): rounding the corner, facing into the bisector. Inner: Standoff off BOTH faces (nestled in the
	// corner) -> distance Standoff / cos(theta/2) along the bisector. Outer: orbiting the edge line at radius Standoff.
	const FVector Bisector = (N1 + N2).GetSafeNormal();
	const float HalfCos = FMath::Cos(FMath::DegreesToRadians(Result.TurnDegrees * 0.5f));
	const float MidDistance = (Type == ERogueClimbCornerType::Inner) ? Standoff / FMath::Max(HalfCos, 0.1f) : Standoff;
	const FQuat MidQuat = MakeFacingQuat(Bisector);
	const FVector MidLoc = Corner + Bisector * MidDistance;
	Result.MidTransform = FTransform(MidQuat, MidLoc);

	// --- Stage 3: validation (the always-succeed contract) ---
	// (a) Both poses fit. Shrunk by a skin so resting one radius off a face isn't an overlap.
	const FCollisionShape FitCapsule = FCollisionShape::MakeCapsule(FMath::Max(CapR - CornerFitSkin, 1.0f), FMath::Max(CapHH - CornerFitSkin, CapR - CornerFitSkin));
	const bool bMidFits = !World->OverlapAnyTestByChannel(MidLoc, MidQuat, ECC_WorldStatic, FitCapsule, ClimbQueryParams);
	const bool bEndFits = !World->OverlapAnyTestByChannel(EndLoc, EndQuat, ECC_WorldStatic, FitCapsule, ClimbQueryParams);

	// (b) Hand purchase on the new face: replay the coverage grid's TOP (hand) row from the End pose, so the climb grid is
	// guaranteed to accept the face at hand-off - and require the MOVE threshold, so a turn never strands you in a cling.
	const int32 Cols = FMath::Max(1, ClimbGridColumns);
	const float ColStep = (Cols > 1) ? (ClimbGridHalfWidth * 2.0f) / (Cols - 1) : 0.0f;
	const FVector EndUp = EndQuat.GetUpVector();
	const FVector EndRight = FVector::CrossProduct(N2, EndUp).GetSafeNormal();
	const FVector RowBase = EndLoc + EndUp * ClimbGridTopOffset + GetClimbSampleUpBias(N2);
	int32 HandHits = 0;
	for (int32 Col = 0; Col < Cols; ++Col)
	{
		const FVector RowOrigin = RowBase + EndRight * (-ClimbGridHalfWidth + Col * ColStep);
		const FVector RowEnd = RowOrigin - N2 * ClimbSampleReach;
		FHitResult RowHit;
		const bool bRowHit = World->LineTraceSingleByChannel(RowHit, RowOrigin, RowEnd, ECC_WorldStatic, ClimbQueryParams);
		const bool bRowValid = bRowHit && IsSurfaceClimbable(FVector::DotProduct(RowHit.ImpactNormal, Up))
			&& FVector::DotProduct(RowHit.ImpactNormal, N2) >= SameFaceDot;
		HandHits += bRowValid ? 1 : 0;
#if ENABLE_DRAW_DEBUG
		if (bDebug)
		{
			DrawDebugLine(World, RowOrigin, bRowHit ? RowHit.ImpactPoint : RowEnd, bRowValid ? FColor::Green : FColor::Red, false, -1.0f, 0, 0.5f);
		}
#endif
	}
	const bool bHandPurchase = HandHits >= FMath::Min(ClimbTopRowMinHitsToMove, Cols);

	Result.bAvailable = bMidFits && bEndFits && bHandPurchase;

#if ENABLE_DRAW_DEBUG
	if (bDebug)
	{
		// Corner line (white), Mid ghost (cyan / red if blocked), End ghost (green = available, orange = fits but the new
		// face lacks hand purchase, red = blocked), facing arrows, and a type/angle label above the corner.
		DrawDebugLine(World, Corner - LineDir * 100.0f, Corner + LineDir * 100.0f, FColor::White, false, -1.0f, 0, 2.0f);
		DrawDebugCapsule(World, MidLoc, CapHH, CapR, MidQuat, bMidFits ? FColor::Cyan : FColor::Red, false, -1.0f, 0, 1.0f);
		DrawDebugDirectionalArrow(World, MidLoc, MidLoc - Bisector * 40.0f, 8.0f, FColor::Cyan, false, -1.0f, 0, 1.5f);
		const FColor EndColor = Result.bAvailable ? FColor::Green : (bEndFits ? FColor::Orange : FColor::Red);
		DrawDebugCapsule(World, EndLoc, CapHH, CapR, EndQuat, EndColor, false, -1.0f, 0, 1.0f);
		DrawDebugDirectionalArrow(World, EndLoc, EndLoc - N2 * 40.0f, 8.0f, EndColor, false, -1.0f, 0, 1.5f);
		const FString Label = FString::Printf(TEXT("%s %s %.0f deg"), Side > 0.0f ? TEXT("R") : TEXT("L"),
			Type == ERogueClimbCornerType::Inner ? TEXT("INNER") : TEXT("OUTER"), Result.TurnDegrees);
		DrawDebugString(World, Corner + LineDir * 110.0f, Label, nullptr, EndColor, 0.0f);
	}
#endif

	return Result;
}

UAnimSequence* URogueCharacterMoverComponent::GetCornerTurnAnimationFor(ERogueClimbCornerType Type, bool bRight) const
{
	switch (Type)
	{
	case ERogueClimbCornerType::Inner: return bRight ? CornerInnerRightAnimation : CornerInnerLeftAnimation;
	case ERogueClimbCornerType::Outer: return bRight ? CornerOuterRightAnimation : CornerOuterLeftAnimation;
	default:                           return nullptr;
	}
}

bool URogueCharacterMoverComponent::CanBeginCornerTurn(bool bRight) const
{
	const FClimbCornerProbeResult& Corner = bRight ? RightCorner : LeftCorner;
	return Corner.bAvailable && GetCornerTurnAnimationFor(Corner.Type, bRight) && CacheOwnerCharacter && CacheOwnerCharacter->GetMesh();
}

bool URogueCharacterMoverComponent::BeginCornerTurn(bool bRight)
{
	const FClimbCornerProbeResult Corner = bRight ? RightCorner : LeftCorner;
	UAnimSequence* Animation = GetCornerTurnAnimationFor(Corner.Type, bRight);
	const USceneComponent* Updated = GetUpdatedComponent();
	const USkeletalMeshComponent* Mesh = CacheOwnerCharacter ? CacheOwnerCharacter->GetMesh() : nullptr;
	if (!Corner.bAvailable || !Animation || !Updated || !Mesh)
	{
		return false;
	}

	CornerTurn = FRogueCornerTurnState();
	CornerTurn.Animation = Animation;
	CornerTurn.Type = Corner.Type;
	CornerTurn.Side = bRight ? 1.0f : -1.0f;
	CornerTurn.MeshRelative = Mesh->GetRelativeTransform();
	// (The hang state needs no latch here: RefreshFootProbes freezes it - and the foot flags - for the whole turn.)

	// Rests: clip start (= the climb idle), the authored 4-limb corner rest (named notify), clip end (= climb on the new face).
	const double Length = Animation->GetPlayLength();
	double RestTime = Length * 0.5;
	bool bFoundRest = false;
	for (const FAnimNotifyEvent& Notify : Animation->Notifies)
	{
		if (Notify.NotifyName == CornerTurnRestNotifyName)
		{
			RestTime = Notify.GetTriggerTime();
			bFoundRest = true;
			break;
		}
	}
	if (!bFoundRest)
	{
		UE_LOG(LogTemp, Warning, TEXT("Corner turn: '%s' has no '%s' notify - using mid-clip as the corner rest."), *GetNameSafe(Animation), *CornerTurnRestNotifyName.ToString());
	}
	RestTime = FMath::Clamp(RestTime, UE_KINDA_SMALL_NUMBER, Length - UE_KINDA_SMALL_NUMBER);

	CornerTurn.RestTimes[0] = 0.0;
	CornerTurn.RestTimes[1] = RestTime;
	CornerTurn.RestTimes[2] = Length;
	CornerTurn.CommitTimes[0] = FMath::Lerp(CornerTurn.RestTimes[0], CornerTurn.RestTimes[1], static_cast<double>(CornerTurnStrike1CommitFraction));
	CornerTurn.CommitTimes[1] = FMath::Lerp(CornerTurn.RestTimes[1], CornerTurn.RestTimes[2], static_cast<double>(CornerTurnStrike2CommitFraction));

	CornerTurn.OldWallNormal = ClimbDominantSurfaceNormalCache;
	CornerTurn.OldWallLocation = ClimbDominantSurfaceLocationCache;
	CornerTurn.NewWallNormal = Corner.NewWallNormal;
	CornerTurn.NewWallLocation = Corner.NewWallLocation;

	// Per-strike warp, fixed for the whole turn: strike 1 = here -> Mid, strike 2 = Mid -> End. Cache where the UNWARPED
	// clip would end each strike (and how far it rotates) - the warp maps that onto the target.
	CornerTurn.StrikeStart[0] = FTransform(Updated->GetComponentQuat(), Updated->GetComponentLocation());
	CornerTurn.StrikeTarget[0] = Corner.MidTransform;
	CornerTurn.StrikeStart[1] = Corner.MidTransform;
	CornerTurn.StrikeTarget[1] = Corner.EndTransform;
	for (int32 Strike = 0; Strike < 2; ++Strike)
	{
		CornerTurn.StrikeClipEnd[Strike] = ComputeCornerClipCapsule(Strike, CornerTurn.RestTimes[Strike + 1]);
		CornerTurn.StrikeClipAngle[Strike] = CornerTurn.StrikeStart[Strike].GetRotation().AngularDistance(CornerTurn.StrikeClipEnd[Strike].GetRotation());
	}

	return true;
}

FTransform URogueCharacterMoverComponent::ComputeCornerClipCapsule(int32 Strike, double Time) const
{
	const FRogueCornerTurnState& State = CornerTurn;
	if (!State.Animation)
	{
		return State.StrikeStart[Strike];
	}

	// Root motion is a MESH-space delta (relative to the root at the strike start): with the root locked, the mesh moves by
	// it - MeshWorld(t) = Delta * MeshWorld(start) - and MeshWorld = MeshRelative * Capsule. Solving for the capsule:
	//   Capsule(t) = MeshRelative^-1 * Delta * MeshRelative * Capsule(start)
	// (conjugating by MeshRelative handles the mesh's -90deg yaw and its pivot below the capsule centre).
	const FTransform Delta = State.Animation->ExtractRootMotionFromRange(State.RestTimes[Strike], Time, FAnimExtractContext());
	return State.MeshRelative.Inverse() * Delta * State.MeshRelative * State.StrikeStart[Strike];
}

FTransform URogueCharacterMoverComponent::EvaluateCornerTurnStrike(int32 Strike, double Time) const
{
	const FRogueCornerTurnState& State = CornerTurn;
	const FTransform& Start = State.StrikeStart[Strike];
	const FTransform& Target = State.StrikeTarget[Strike];
	const FTransform& ClipEnd = State.StrikeClipEnd[Strike];
	const FTransform Clip = ComputeCornerClipCapsule(Strike, Time);

	const double StrikeStartTime = State.RestTimes[Strike];
	const double Span = State.RestTimes[Strike + 1] - StrikeStartTime;
	const double TimeAlpha = (Span > UE_KINDA_SMALL_NUMBER) ? FMath::Clamp((Time - StrikeStartTime) / Span, 0.0, 1.0) : 1.0;

	// Translation - SkewWarp's idea as a pure function of time: a rank-one linear map that takes the clip's total strike
	// translation onto the target's. Each instant is corrected in proportion to its progress ALONG the clip's own motion,
	// so the correction rides the clip (none while the body is still during a reach) and lands exactly on the target.
	const FVector StartLocation = Start.GetLocation();
	const FVector ClipDelta = Clip.GetLocation() - StartLocation;
	const FVector ClipTotal = ClipEnd.GetLocation() - StartLocation;
	const FVector TargetTotal = Target.GetLocation() - StartLocation;
	const double ClipTotalSq = ClipTotal.SizeSquared();
	const double Alpha = (ClipTotalSq > 1.0) ? FVector::DotProduct(ClipDelta, ClipTotal) / ClipTotalSq : TimeAlpha;
	const FVector Location = StartLocation + ClipDelta + (TargetTotal - ClipTotal) * Alpha;

	// Rotation - the clip's own turn, plus the residual (target vs the unwarped clip end) blended in by rotational progress.
	const FQuat Correction = Target.GetRotation() * ClipEnd.GetRotation().Inverse();
	const double Beta = (State.StrikeClipAngle[Strike] > 0.01f)
		? FMath::Clamp(Start.GetRotation().AngularDistance(Clip.GetRotation()) / State.StrikeClipAngle[Strike], 0.0, 1.0)
		: TimeAlpha;
	const FQuat Rotation = FQuat::Slerp(FQuat::Identity, Correction, Beta) * Clip.GetRotation();

	return FTransform(Rotation.GetNormalized(), Location);
}

ERogueCornerTurnStep URogueCharacterMoverComponent::AdvanceCornerTurn(float StickY, float DeltaSeconds, FTransform& OutCapsuleTarget)
{
	FRogueCornerTurnState& State = CornerTurn;
	const USceneComponent* Updated = GetUpdatedComponent();
	if (!State.Animation || !Updated)
	{
		if (Updated)
		{
			OutCapsuleTarget = FTransform(Updated->GetComponentQuat(), Updated->GetComponentLocation());
		}
		return ERogueCornerTurnStep::FinishedOnOldFace;
	}

	constexpr double RestTolerance = 1e-4;
	const double Rest0 = State.RestTimes[0];
	const double Rest1 = State.RestTimes[1];
	const double Rest2 = State.RestTimes[2];

	// + = forward through the turn. "Right" stays right relative to the character all the way round, so holding the same
	// stick direction that entered the corner keeps going.
	const float Push = StickY * State.Side;
	const bool bDriving = FMath::Abs(Push) > CornerTurnInputDeadzone && (Push > 0.0f || bCornerTurnAllowReverse);

	double Time = State.Playhead;
	if (bDriving)
	{
		// The stick drives the playhead (partial push = slower, like the climb playrate). Body and pose both follow it.
		Time += Push * CornerTurnPlayRate * DeltaSeconds;
	}
	else if (FMath::Abs(Time - Rest1) > RestTolerance && Time > Rest0 && Time < Rest2)
	{
		// Released mid-strike: settle to the nearest rest, forward or back depending on the strike's commit point.
		const bool bFirstStrike = Time < Rest1;
		const double Commit = State.CommitTimes[bFirstStrike ? 0 : 1];
		const double Step = CornerTurnSettlePlayRate * DeltaSeconds;
		Time = (Time >= Commit)
			? FMath::Min(Time + Step, bFirstStrike ? Rest1 : Rest2)
			: FMath::Max(Time - Step, bFirstStrike ? Rest0 : Rest1);
	}
	Time = FMath::Clamp(Time, Rest0, Rest2);

	State.Playhead = Time;
	State.bHolding = !bDriving && FMath::Abs(Time - Rest1) <= RestTolerance;

	// Strike 1 covers [Rest0, Rest1], strike 2 [Rest1, Rest2]; both evaluate to the Mid target exactly at Rest1.
	const int32 Strike = (Time <= Rest1) ? 0 : 1;
	OutCapsuleTarget = EvaluateCornerTurnStrike(Strike, Time);

#if ENABLE_DRAW_DEBUG
	if (CVarClimbingDebugDrawing.GetValueOnGameThread())
	{
		float CapR = ClimbDetectionCapsuleRadius;
		float CapHH = ClimbDetectionCapsuleHalfHeight;
		if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Updated))
		{
			CapR = Capsule->GetScaledCapsuleRadius();
			CapHH = Capsule->GetScaledCapsuleHalfHeight();
		}
		// Warp targets (cyan Mid, green End) vs where the UNWARPED clip would end each strike (silver): the gap between a
		// target and its silver ghost is how much the warp is correcting - tune the probe offsets to shrink it.
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FTransform& StrikeTarget = State.StrikeTarget[Index];
			const FTransform& ClipEnd = State.StrikeClipEnd[Index];
			DrawDebugCapsule(GetWorld(), StrikeTarget.GetLocation(), CapHH, CapR, StrikeTarget.GetRotation(), Index == 0 ? FColor::Cyan : FColor::Green, false, -1.0f, 0, 1.0f);
			DrawDebugCapsule(GetWorld(), ClipEnd.GetLocation(), CapHH, CapR, ClipEnd.GetRotation(), FColor::Silver, false, -1.0f, 0, 0.5f);
			DrawDebugLine(GetWorld(), ClipEnd.GetLocation(), StrikeTarget.GetLocation(), FColor::Silver, false, -1.0f, 0, 0.5f);
		}
		DrawDebugCapsule(GetWorld(), OutCapsuleTarget.GetLocation(), CapHH, CapR, OutCapsuleTarget.GetRotation(), State.bHolding ? FColor::Magenta : FColor::Yellow, false, -1.0f, 0, 1.5f);
	}
#endif

	if (Time >= Rest2 - RestTolerance)
	{
		return ERogueCornerTurnStep::FinishedOnNewFace;
	}
	if (Time <= Rest0 + RestTolerance && !(bDriving && Push > 0.0f))
	{
		return ERogueCornerTurnStep::FinishedOnOldFace;
	}
	return ERogueCornerTurnStep::InProgress;
}

void URogueCharacterMoverComponent::FinishCornerTurn(bool bOntoNewFace)
{
	// Seed (snap) the climb plane to the face we're resuming on. Without this, RefreshClimbSurfaceInfo would VInterp from
	// whatever it sampled mid-turn (visibly swinging the capsule after the clip already turned it) and stack the first
	// grid on a stale plane - a false "can't climb" there trips the climb transition's exit-to-Falling.
	ClimbDominantSurfaceNormalCache = bOntoNewFace ? CornerTurn.NewWallNormal : CornerTurn.OldWallNormal;
	ClimbDominantSurfaceLocationCache = bOntoNewFace ? CornerTurn.NewWallLocation : CornerTurn.OldWallLocation;
	CornerTurn.bHolding = false;
}

void URogueCharacterMoverComponent::RefreshMantleProbe()
{
	bMantleLedgeAvailable = false;

	const USceneComponent* Updated = GetUpdatedComponent();
	if (!Updated)
	{
		return;
	}

	// Build the probe basis off the cached (smoothed) climb plane.
	const FVector WallNormal = ClimbDominantSurfaceNormalCache;
	const FVector IntoWall = (-WallNormal).GetSafeNormal2D();
	if (IntoWall.IsNearlyZero())
	{
		return;
	}
	
	// Use the real capsule for the fit test
	float CapR = ClimbDetectionCapsuleRadius;
	float CapHH = ClimbDetectionCapsuleHalfHeight;
	if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Updated))
	{
		CapR = Capsule->GetScaledCapsuleRadius();
		CapHH = Capsule->GetScaledCapsuleHalfHeight();
	}
	
	const FVector Up = GetUpDirection();
	FVector WallUp = (Up - Up.ProjectOnToNormal(WallNormal)).GetSafeNormal();
	if (WallUp.IsNearlyZero()) { WallUp = Up; }
	const FVector UpBias = GetClimbSampleUpBias(ClimbDominantSurfaceNormalCache);
	// [Fix A] Feet = the TILTED climbing capsule's real bottom, along its axis WallUp - NOT world-down. Using world Up*CapHH
	// put Feet at perpendicular distance CapR - CapHH*sin(tilt) from the face, which crosses 0 near 40deg (CapR/CapHH), so
	// the perpendicular probes here (and the precise lip scan) start on/behind the face and miss on steep walls. WallUp*CapHH
	// keeps Feet a constant CapR off the face at any tilt (a no-op on vertical walls, where WallUp == Up).
	const FVector Feet = Updated->GetComponentLocation() - WallUp * CapHH + UpBias;
	const float ForwardOffset = CapR + MantleForwardReach;

	// (a) Overhead test: probe PERPENDICULAR into the wall (-WallNormal), NOT horizontally, at the top of the band. A
	// horizontal (IntoWall) ray keeps a constant world height, so its gap to the face shrinks on a toward-lean and grows
	// on a back-lean; going perpendicular keeps the clearance a consistent ~CapR across tilts (OverheadStart sits on the
	// climbing line, ~CapR off the face, and WallUp keeps it there). Only a STEEP (non-walkable) hit blocks - the face
	// still climbing past the band (a tall wall, no lip); empty or a walkable hit both mean the face has ended into a lip.
	const FVector OverheadStart = Feet + WallUp * MantleMaxLedgeHeightFromFeet;
	const FVector OverheadEnd = OverheadStart - WallNormal * ForwardOffset;
	FHitResult OverheadHit;
	const bool bOverheadHit = GetWorld()->LineTraceSingleByChannel(OverheadHit, OverheadStart, OverheadEnd, ECC_WorldStatic, ClimbQueryParams);
	const bool bOverheadBlocked = bOverheadHit && (FVector::DotProduct(OverheadHit.ImpactNormal, Up) < MantleMinWalkableDot);

	// (b) Down test: from beyond the lip, trace down across the whole band to find the walkable top (expect a hit and surface not too steep).
	const FVector DownStart = OverheadStart + IntoWall * ForwardOffset;
	const FVector DownEnd = DownStart - WallUp * MantleDownReach;
	FHitResult DownHit;
	const bool bLandingHit = GetWorld()->LineTraceSingleByChannel(DownHit, DownStart, DownEnd, ECC_WorldStatic, ClimbQueryParams);
	const bool bWalkable = bLandingHit && (FVector::DotProduct(DownHit.ImpactNormal, Up) >= MantleMinWalkableDot);

	// (c) Fit + height via a downward capsule SWEEP (not a static overlap): drop the real capsule onto the landing and
	// find where it RESTS. This settles onto a tilted walk surface without the false self-overlap a fixed vertical
	// capsule hits on a slope, still stops on real obstacles in the descent, and gives the real rest position. The rest
	// is found WORLD-VERTICAL (the character stands upright); the height GATE below then measures the rise ALONG WallUp
	// so the trigger window is a tilt-invariant along-wall span - see the LedgeRise comment.
	bool bHeightOk = false;
	bool bFits = false;
	FVector RestCenter = FVector::ZeroVector;   // capsule center where it comes to rest
	FVector RestFeet = FVector::ZeroVector;      // capsule bottom = the standing feet position (the landing warp target)
	if (bWalkable)
	{
		RestCenter = DownHit.ImpactPoint + Up * CapHH;   // nominal flat-rest (debug fallback until the sweep resolves)
		RestFeet = DownHit.ImpactPoint;

		const FCollisionShape FitCapsule = FCollisionShape::MakeCapsule(CapR, CapHH);
		// Start above the slope in FREE space (a start-penetration would read as a false blocker), sweep straight DOWN.
		const FVector SweepStart = DownHit.ImpactPoint + Up * (CapHH + MantleFitSweepClearance);
		const FVector SweepEnd   = DownHit.ImpactPoint + Up * (CapHH - MantleFitSweepClearance);
		FHitResult SweepHit;
		const bool bSweepHit = GetWorld()->SweepSingleByChannel(SweepHit, SweepStart, SweepEnd, FQuat::Identity, ECC_WorldStatic, FitCapsule, ClimbQueryParams);

		if (bSweepHit && !SweepHit.bStartPenetrating)
		{
			RestCenter = SweepHit.Location;          // capsule center at its settled resting position
			RestFeet = RestCenter - Up * CapHH;

			// Fits only if it settled on a WALKABLE surface (not caught mid-descent on an obstacle / steep face) ...
			bFits = (FVector::DotProduct(SweepHit.ImpactNormal, Up) >= MantleMinWalkableDot);
			// ... and the settled feet land within the height band, measured ALONG THE WALL (WallUp), not world-up. The
			// gate is really "has the character climbed up to the lip" - an along-wall progression - so an along-wall span
			// stays consistent across tilts, whereas world-up compresses the trigger window on a steep (e.g. 40deg) lean.
			// (The rest itself is still found world-vertical by the sweep above; only this gate's measure is along-wall.)
			const float LedgeRise = FVector::DotProduct(RestFeet - Feet, WallUp);
			bHeightOk = (LedgeRise >= MantleMinLedgeHeightFromFeet) && (LedgeRise <= MantleMaxLedgeHeightFromFeet);
		}
	}
	
	bMantleLedgeAvailable = !bOverheadBlocked && bWalkable && bHeightOk && bFits;
	// UE_LOG(LogTemp, Warning, TEXT("Mantle Fits: %s, HeightOk: %s, Walkable: %s, OverheadBlocked: %s"), bFits ? TEXT("Yes") : TEXT("No"), bHeightOk ? TEXT("Yes") : TEXT("No"), bWalkable ? TEXT("Yes") : TEXT("No"), bOverheadBlocked ? TEXT("Yes") : TEXT("No"));

	if (bMantleLedgeAvailable)
	{
		// Face away from the wall (onto the ledge), kept vertical - same convention as the climb mode's orientation.
		const FRotator LandingRot = UMovementUtils::ApplyGravityToOrientationIntent((-WallNormal).ToOrientationRotator(), GetWorldToGravityTransform(), true);
		const FQuat LandingQuat = LandingRot.Quaternion();

		// Two warp targets for the two-phase (up-then-over) path (both FEET targets via WarpToFeetLocation):
		//  - Landing ("forward"): the swept capsule's rest feet on top - where the character actually ends up standing.
		//  - Edge ("up"): at ledge height, only MantleEdgeBackward in front of the FEET, so phase 1 goes mostly straight
		//    UP to just inside the lip rather than diagonally out over it (which would clip the wall corner).
		// Along-wall rise (WallUp), matching LedgeRise - so Feet + WallUp*EdgeUp lands at the real lip height on a tilt
		// (a world-up EdgeUp used along WallUp would fall short by cos(tilt)). This is the cheap fallback edge; BeginMantle's
		// precise scan overrides it at commit.
		// [Fix C] To the LIP, not the inland landing: the down-hit sits ForwardOffset*dot(IntoWall,WallUp) higher along the
		// wall than the lip on a tilt (leaning away), so subtract it - else this fallback edge (used when the precise scan
		// fails) floats that much up in the air over the ledge, and the character mantles into it. 0 on vertical walls.
		const float EdgeUp = FVector::DotProduct(DownHit.ImpactPoint - Feet, WallUp) - ForwardOffset * FVector::DotProduct(IntoWall, WallUp);
		MantleLandingTransformCache = FTransform(LandingQuat, RestFeet);
		MantleEdgeTransformCache = FTransform(LandingQuat, Feet + WallUp * EdgeUp + WallUp * MantleEdgeUp - IntoWall * MantleEdgeBackward);
#if ENABLE_DRAW_DEBUG
		if (CVarClimbingDebugDrawing.GetValueOnGameThread())
		{
			// Landing (MantleForward) target. The MantleUp/edge target is drawn by the precise scan (cyan) at commit;
			// MantleEdgeTransformCache (set above) is now only a silent fallback, so it isn't drawn.
			DrawDebugSphere(GetWorld(), MantleLandingTransformCache.GetLocation(), 12.0f, 10, FColor::Purple, false, -1, 0, 2.0f);
		}
#endif
	}

#if ENABLE_DRAW_DEBUG
	if (CVarClimbingDebugDrawing.GetValueOnGameThread())
	{
		DrawDebugLine(GetWorld(), OverheadStart, bOverheadBlocked ? OverheadHit.ImpactPoint : OverheadEnd, bOverheadBlocked ? FColor::Red : FColor::Green, false, -1.0f, 0, 2.0f);
		DrawDebugLine(GetWorld(), DownStart, bLandingHit ? DownHit.ImpactPoint : DownEnd, bWalkable ? FColor::Green : FColor::Orange, false, -1.0f, 0, 1.0f);
		if (bWalkable)
		{
			DrawDebugPoint(GetWorld(), DownHit.ImpactPoint, 20.0f, FColor::Cyan, false, -1.0f);
			const FColor FitColor = (bFits && bHeightOk) ? FColor::Green : FColor::Red;
			DrawDebugCapsule(GetWorld(), RestCenter, CapHH, CapR, FQuat::Identity, FitColor, false, -1.0f, 0, 1.0f);
			if (bMantleLedgeAvailable)
			{
				DrawDebugDirectionalArrow(GetWorld(), RestCenter, RestCenter + IntoWall * 40.0f, 8.0f, FColor::Yellow, false, -1.0f, 0, 2.0f);
			}
		}
	}
#endif
}

bool URogueCharacterMoverComponent::ComputePreciseMantleEdge(FTransform& OutEdge) const
{
	const USceneComponent* Updated = GetUpdatedComponent();
	if (!Updated)
	{
		return false;
	}

	// Same tilt-aware basis as the cheap probe. March UP THE WALL FACE (WallUp), not world-up, so the ray starts stay
	// glued to a receding tilted face instead of drifting off it (a straight-up march would out-run the face's recession).
	const FVector WallNormal = ClimbDominantSurfaceNormalCache;
	const FVector IntoWall = (-WallNormal).GetSafeNormal2D();
	if (IntoWall.IsNearlyZero())
	{
		return false;
	}

	const FVector Up = GetUpDirection();
	FVector WallUp = (Up - Up.ProjectOnToNormal(WallNormal)).GetSafeNormal();
	if (WallUp.IsNearlyZero())
	{
		WallUp = Up;
	}
	const FVector WallRight = FVector::CrossProduct(WallNormal, WallUp).GetSafeNormal();

	float CapR = ClimbDetectionCapsuleRadius;
	float CapHH = ClimbDetectionCapsuleHalfHeight;
	if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Updated))
	{
		CapR = Capsule->GetScaledCapsuleRadius();
		CapHH = Capsule->GetScaledCapsuleHalfHeight();
	}
	// Include the SAME UpBias the cheap probe / overhead gate use for their Feet, so this scan's [Min, Max] band lines up
	// with the band the overhead already validated. Without it the scan's band sat UpBias (= OnClimbAdditionalOffset)
	// LOWER, so a lip near the top of the band fell above the scan's Hi=Max and the search pegged ~UpBias below the lip
	// regardless of iteration count.
	// [Fix A] WallUp*CapHH (the tilted climbing capsule's real bottom), NOT world Up*CapHH - see the RefreshMantleProbe Feet
	// note. With world Up the scan-ray origins sit CapR - CapHH*sin(tilt) off the face, going ~behind it near 40deg, so no
	// ray registers an on-face hit and the scan returns false (no cyan/white spheres). WallUp keeps them a constant CapR off.
	const FVector Feet = Updated->GetComponentLocation() - WallUp * CapHH + GetClimbSampleUpBias(WallNormal);
	const float Reach = CapR + MantleForwardReach;

	const int32 Iterations = FMath::Max(1, MantleLipScanIterations);
	const int32 LateralSamples = FMath::Max(1, MantleLipLateralSamples);

	// Bracket is the same [min, max] band the overhead gate validated: at min the face is present (hit), at max it is
	// empty (miss). The lip is the hit->miss boundary; binary-search it, averaged across a few lateral columns.
	FVector LipSum = FVector::ZeroVector;
	int32 LipCount = 0;

	for (int32 s = 0; s < LateralSamples; ++s)
	{
		const float Frac = (LateralSamples > 1) ? ((static_cast<float>(s) / (LateralSamples - 1)) * 2.0f - 1.0f) : 0.0f;
		const FVector ColBase = Feet + WallRight * (Frac * MantleLipScanHalfWidth);

		// [Fix B] The arming gate measures to the LANDING, which sits Reach*dot(IntoWall,WallUp) along the wall ABOVE the lip
		// on a tilt (leaning away) or below it (overhang). So the true lip falls outside the raw [Min,Max] band - widen the
		// bracket in the tilt's direction so it still straddles the lip (on-face at Lo, above the lip at Hi). 0 on vertical.
		const float LipBandShift = Reach * FVector::DotProduct(IntoWall, WallUp);
		float Lo = MantleMinLedgeHeightFromFeet - FMath::Max(0.0f, LipBandShift); // along-wall: known to still be on the face (steep hit)
		float Hi = MantleMaxLedgeHeightFromFeet - FMath::Min(0.0f, LipBandShift); // along-wall: known to be above the lip (empty, or a walkable top)
		FVector ColLip = FVector::ZeroVector;
		bool bColFound = false;

		for (int32 i = 0; i < Iterations; ++i)
		{
			const float Mid = (Lo + Hi) * 0.5f;
			const FVector Start = ColBase + WallUp * Mid;
			// Probe PERPENDICULAR into the face (-WallNormal), matching the overhead - NOT horizontal IntoWall. A flat
			// IntoWall ray passes UNDER a toward-leaning (overhang) face (the face is up-and-away from Start, the ray
			// goes away-but-flat), so it misses at higher Mid and the search caps low (lip pulled toward Min). The
			// perpendicular ray hits at ~CapR from the climbing line for any tilt; == IntoWall on a vertical wall.
			const FVector End = Start - WallNormal * Reach;

			FHitResult Hit;
			const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, ClimbQueryParams);
			// "On the face" only if we hit a NON-walkable (steep / overhanging) surface. A hit on a WALKABLE top means
			// we're already above the lip: essential for walls that lean TOWARD the character, where the forward ray
			// above the lip strikes the (higher) landing instead of missing - otherwise the search pegs at max and the
			// lip collapses onto the landing. Same walkable threshold the landing test uses.
			const bool bOnFace = bHit && (FVector::DotProduct(Hit.ImpactNormal, Up) < MantleMinWalkableDot);
			if (bOnFace)
			{
				Lo = Mid;                    // still on the face -> the lip is higher
				ColLip = Hit.ImpactPoint;    // best-known top-of-face point so far
				bColFound = true;
			}
			else
			{
				Hi = Mid;                    // miss OR walkable top -> above the lip, come back down
			}
		}

		if (bColFound)
		{
			// The recorded hit sits at Lo (the on-face side of the bracket); the true lip is in [Lo, Hi]. Lift it along
			// the face to the bracket MIDPOINT so the estimate is centred on the lip (error ±half-residual) instead of
			// the systematic ~residual-low it is at Lo. Zero extra traces; iterations then just tighten the spread.
			LipSum += ColLip + WallUp * ((Hi - Lo) * 0.5f);
			++LipCount;
		}
	}

	if (LipCount == 0)
	{
		return false;
	}

	const FVector LipPoint = LipSum / LipCount;

	// Feet warp target: at the lip, nudged onto the ledge by MantleEdgeBackward so the feet clear the corner. Face onto
	// the ledge, kept vertical (same convention as the landing / climb orientation).
	const FRotator LandingRot = UMovementUtils::ApplyGravityToOrientationIntent((-WallNormal).ToOrientationRotator(), GetWorldToGravityTransform(), true);
	OutEdge = FTransform(LandingRot.Quaternion(), LipPoint + WallUp * MantleEdgeUp - IntoWall * MantleEdgeBackward);

#if ENABLE_DRAW_DEBUG
	if (CVarClimbingDebugDrawing.GetValueOnGameThread())
	{
		DrawDebugSphere(GetWorld(), OutEdge.GetLocation(), 10.0f, 12, FColor::Cyan, false, 3.0f, 0, 2.0f);  // precise lip (feet target)
		DrawDebugSphere(GetWorld(), LipPoint, 5.0f, 8, FColor::White, false, 3.0f, 0, 1.5f);                // raw averaged lip hit on the face
	}
#endif

	return true;
}

void URogueCharacterMoverComponent::RefreshMantleDownProbe()
{
	bMantleDownAvailable = false;

	const USceneComponent* Updated = GetUpdatedComponent();
	if (!Updated || !MantleDownMontage)
	{
		return;
	}

	// Standing basis: upright, forward = facing (toward the potential edge/drop). Flatten forward to horizontal so a
	// tiny capsule tilt doesn't skew the probe.
	const FVector Up = GetUpDirection();
	FVector Forward = (Updated->GetForwardVector() - Updated->GetForwardVector().ProjectOnToNormal(Up)).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return;
	}

	float CapR = ClimbDetectionCapsuleRadius;
	float CapHH = ClimbDetectionCapsuleHalfHeight;
	if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Updated))
	{
		CapR = Capsule->GetScaledCapsuleRadius();
		CapHH = Capsule->GetScaledCapsuleHalfHeight();
	}

	const FVector Location = Updated->GetComponentLocation();
	const FVector Feet = Location - Up * CapHH;

	// --- Stage 1: cheap arming gate. Is there air below-and-ahead (the ground has ended into an edge)? ---
	// A small capsule in the lower front; collision-free => at an edge. If it's blocked there's still ground ahead.
	const FVector GateCenter = Feet + Forward * MantleDownGateForwardOffset - Up * MantleDownGateDownOffset;
	const FCollisionShape GateShape = FCollisionShape::MakeCapsule(MantleDownGateRadius, MantleDownGateHalfHeight);
	const bool bGateBlocked = GetWorld()->OverlapAnyTestByChannel(GateCenter, FQuat::Identity, ECC_WorldStatic, GateShape, ClimbQueryParams);

#if ENABLE_DRAW_DEBUG
	const bool bDebugDraw = CVarClimbingDebugDrawing.GetValueOnGameThread();
	if (bDebugDraw)
	{
		DrawDebugCapsule(GetWorld(), GateCenter, MantleDownGateHalfHeight, MantleDownGateRadius, FQuat::Identity, bGateBlocked ? FColor::Red : FColor::Green, false, -1.0f, 0, 0.6f);
	}
#endif

	if (bGateBlocked)
	{
		return; // ground still ahead -> not at an edge
	}

	// --- Stage 2: find the descendable face with BACKWARD rays. ---
	// Origins sit out-front-and-below the lip (over the drop, alongside the face); each fires back toward the wall.
	// The first near-vertical (climbable) hit is the face; its normal points back toward us (~+Forward). Averaging the
	// hits gives the wall plane; the topmost hit approximates the lip, the span confirms a hangable face.
	const int32 Samples = FMath::Max(2, MantleDownFaceProbeCount);
	const float FrontDist = CapR + MantleDownFaceSearchForward;

	FVector NormalSum = FVector::ZeroVector;
	int32 HitCount = 0;
	FVector TopFaceHit = FVector::ZeroVector;    // highest (nearest-top) climbable hit -> ~the lip
	FVector BottomFaceHit = FVector::ZeroVector; // lowest climbable hit -> face extent
	bool bHasFace = false;

	for (int32 i = 0; i < Samples; ++i)
	{
		const float Depth = FMath::Lerp(MantleDownFaceProbeTopInset, MantleDownMaxDepth, static_cast<float>(i) / (Samples - 1));
		const FVector Origin = Feet + Forward * FrontDist - Up * Depth;
		const FVector End = Feet - Forward * CapR - Up * Depth; // fire back toward (and just past) the character line
		FHitResult Hit;
		const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Origin, End, ECC_WorldStatic, ClimbQueryParams);
		const bool bFace = bHit && IsSurfaceClimbable(FVector::DotProduct(Hit.ImpactNormal, Up));
		if (bFace)
		{
			NormalSum += Hit.ImpactNormal;
			++HitCount;
			if (!bHasFace) { TopFaceHit = Hit.ImpactPoint; bHasFace = true; } // first climbable hit = highest
			BottomFaceHit = Hit.ImpactPoint;                                  // last climbable hit = lowest
		}

#if ENABLE_DRAW_DEBUG
		if (bDebugDraw)
		{
			DrawDebugLine(GetWorld(), Origin, bHit ? Hit.ImpactPoint : End, bFace ? FColor::Green : (bHit ? FColor::Orange : FColor::Red), false, -1.0f, 0, 0.5f);
		}
#endif
	}

	if (HitCount < 2) // need a spanning face, not a lone hit
	{
		return;
	}

	const FVector WallNormal = NormalSum.GetSafeNormal();
	FVector WallUp = (Up - Up.ProjectOnToNormal(WallNormal)).GetSafeNormal(); // follows the wall's up-slope (tilt-consistent)
	if (WallUp.IsNearlyZero()) { WallUp = Up; }

	// Gates evaluated as inline bools (NOT early returns) so the debug draw below always runs once we have a face - a
	// green gate + green rays that still doesn't arm then shows WHICH gate failed, instead of silently drawing nothing.
	// Gate A - facing (loose): the character must roughly face the face (out over the drop); the warp snaps the rest.
	// Compare in the HORIZONTAL plane (both operands flattened) so the tolerance is a true horizontal angle at any tilt.
	// Forward is already flattened above; flatten WallNormal too, else its vertical (tilt) component shrinks the dot and
	// tightens the window on steep walls. A climbable face is near-vertical, so its 2D normal is well-defined (a
	// degenerate zero would simply fail the gate, which is safe).
	const FVector WallNormalHoriz = WallNormal.GetSafeNormal2D();
	const float FacingDot = FVector::DotProduct(Forward, WallNormalHoriz);
	const bool bFacingOk = FacingDot >= FMath::Cos(FMath::DegreesToRadians(MantleDownFacingToleranceDegrees));
	// Gate B - min face height: the top-most and bottom-most face hits must span at least this along the wall (else it's a step).
	const float FaceSpan = FVector::DotProduct(TopFaceHit - BottomFaceHit, WallUp);
	const bool bHeightOk = FaceSpan >= MantleDownMinFaceHeight;

	// Combined clearance corridor: ONE capsule lying PARALLEL to the wall, from the hang feet (bottom, = the purple warp
	// target) up along the wall to a configurable amount above the lip (top). This is the whole volume the body occupies
	// from the swing-over down into the hang, in a single test - it replaces the separate hang-fit + swing-clearance
	// capsules. Anchored at the hang feet (a real, stable point), so it doesn't drift like the old floating swing box;
	// wall-parallel because that's how the body lies against the face through the move.
	// Find the TRUE lip (top edge of the face) by marching UP the face from the topmost ray hit until a perpendicular
	// probe stops hitting the climbable face - the last on-face point is the edge. (An earlier version extrapolated the
	// face plane up to the FEET height, but that assumed the lip sits at the stance height; on a forward-leaning face the
	// lip can be below the stance, so it overshot ABOVE the lip. A local scan finds the real edge regardless of tilt or of
	// where the lip sits relative to the feet.) TopFaceHit sits ~MantleDownFaceProbeTopInset below the top, so the march
	// only needs to cover that inset plus a small margin.
	FVector LipPoint = TopFaceHit;
	{
		const float ScanReach = CapR + MantleDownFaceSearchForward;
		const float ScanStep = 4.0f;
		const int32 ScanSteps = FMath::Clamp(FMath::CeilToInt((MantleDownFaceProbeTopInset + 20.0f) / ScanStep), 1, 20);
		for (int32 s = 1; s <= ScanSteps; ++s)
		{
			const FVector Mid = TopFaceHit + WallUp * (ScanStep * s);
			// Perpendicular into the face (-WallNormal), matching the face probe: consistent ~CapR clearance at any tilt.
			FHitResult ScanHit;
			const bool bScanHit = GetWorld()->LineTraceSingleByChannel(ScanHit, Mid + WallNormal * ScanReach, Mid - WallNormal * CapR, ECC_WorldStatic, ClimbQueryParams);
			if (bScanHit && IsSurfaceClimbable(FVector::DotProduct(ScanHit.ImpactNormal, Up)))
			{
				LipPoint = ScanHit.ImpactPoint; // still on the face -> the lip is at least this high
			}
			else
			{
				break; // marched past the top edge
			}
		}
	}

	const FQuat HangQuat = FRotationMatrix::MakeFromZX(WallUp, -WallNormal).ToQuat();  // Z = WallUp -> capsule axis runs along the wall
	const FVector HangCenter = LipPoint - WallUp * MantleDownHangDrop + WallNormal * CapR;
	const FVector HangFeet = HangCenter - WallUp * CapHH;                               // corridor BOTTOM (= the purple hang target)
	const FVector CorridorTop = LipPoint + WallUp * MantleDownSwingClearanceUp + WallNormal * CapR; // TOP: above the lip, same one-radius face offset
	const FVector CorridorCenter = (HangFeet + CorridorTop) * 0.5f;
	const float CorridorRadius = FMath::Max(1.0f, CapR - MantleDownFitSkin);
	const float CorridorHalfHeight = FMath::Max(CorridorRadius, (CorridorTop - HangFeet).Size() * 0.5f); // tips land on HangFeet / CorridorTop
	const FCollisionShape CorridorCapsule = FCollisionShape::MakeCapsule(CorridorRadius, CorridorHalfHeight);
	const bool bCorridorClear = !GetWorld()->OverlapAnyTestByChannel(CorridorCenter, HangQuat, ECC_WorldStatic, CorridorCapsule, ClimbQueryParams);

	bMantleDownAvailable = bFacingOk && bHeightOk && bCorridorClear;

	if (bMantleDownAvailable)
	{
		MantleDownWallNormalCache = WallNormal;

		// "Down" warp target: the hang FEET on the face (= HangCenter - WallUp*CapHH), tilted, facing into the wall - the
		// climb hand-off pose. Because the mode rotates the capsule ABOUT ITS CENTER, the final center is frozen at
		// feetTarget + (capsule axis when the warp lands)*CapHH. So this on-face target is correct precisely when the
		// capsule has already tilted to the wall (axis = WallUp) by the moment the down-warp pins the feet: then
		// center = HangFeet + WallUp*CapHH = HangCenter, exactly CapR off the face on every tilt -> flush. (If the tilt
		// blend has NOT completed by then, the capsule is still partly upright and the center lands off by up to
		// CapHH*dot(WallNormal,Up) -> hang in air / into wall; the fix there is to finish the tilt before the warp lands,
		// not to move this target.)
		MantleDownHangTransformCache = FTransform(HangQuat, HangFeet);

		// "Forward" warp target: the true lip pivot, nudged back onto the top (HORIZONTALLY, so the nudge is a constant
		// inset from the edge at any tilt) so the feet land just inside the edge. Upright, facing OUT over the drop
		// (+WallNormal) - the rotation warp snaps the loose approach angle to this before the down window turns the
		// character 180 to face the wall.
		const FRotator EdgeRot = UMovementUtils::ApplyGravityToOrientationIntent(WallNormal.ToOrientationRotator(), GetWorldToGravityTransform(), true);
		const FVector LipFeet = LipPoint - WallNormalHoriz * MantleDownEdgeBackward;
		MantleDownEdgeTransformCache = FTransform(EdgeRot.Quaternion(), LipFeet);
	}

#if ENABLE_DRAW_DEBUG
	if (bDebugDraw)
	{
		// Corridor is ALWAYS drawn once we have a face, so a green gate + green rays never leaves "nothing drawn":
		//   green  = armed (all gates pass, corridor clear) | red = corridor blocked | yellow = corridor clear but an
		//   upstream gate (facing / face-height) failed - the arrows below say which.
		const FColor CorridorColor = bMantleDownAvailable ? FColor::Green : (!bCorridorClear ? FColor::Red : FColor::Yellow);
		DrawDebugCapsule(GetWorld(), CorridorCenter, CorridorHalfHeight, CorridorRadius, HangQuat, CorridorColor, false, -1.0f, 0, 1.0f);

		// Facing arrow (your forward) magenta when facing is the blocker; face-normal arrow orange when the span is too
		// short. Drawn from the reconstructed lip, which is also where the warp targets anchor.
		DrawDebugDirectionalArrow(GetWorld(), LipPoint, LipPoint + Forward * 45.0f, 8.0f, bFacingOk ? FColor::Green : FColor::Magenta, false, -1.0f, 0, 2.0f);
		DrawDebugDirectionalArrow(GetWorld(), LipPoint, LipPoint + WallNormal * 40.0f, 8.0f, bHeightOk ? FColor::Green : FColor::Orange, false, -1.0f, 0, 2.0f);

		if (bMantleDownAvailable)
		{
			DrawDebugSphere(GetWorld(), MantleDownEdgeTransformCache.GetLocation(), 10.0f, 12, FColor::Yellow, false, -1.0f, 0, 2.0f);  // forward/lip target
			DrawDebugSphere(GetWorld(), MantleDownHangTransformCache.GetLocation(), 10.0f, 12, FColor::Purple, false, -1.0f, 0, 2.0f);  // down warp target (upright-feet drop)
			// The DESIRED final hang capsule (center on HangCenter, tilted to the wall): the live capsule should coincide
			// with this after the hand-off, flush against the face, on every tilt. Its side sits exactly CapR off the face.
			DrawDebugCapsule(GetWorld(), HangCenter, CapHH, CapR, HangQuat, FColor::Purple, false, -1.0f, 0, 1.0f);
		}
	}
#endif
}

bool URogueCharacterMoverComponent::BeginMantle()
{
	if (!MantleMontage || !MotionWarpingComp || !CacheOwnerCharacter)
	{
		return false;
	}

	USkeletalMeshComponent* Mesh = CacheOwnerCharacter->GetMesh();
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return false;
	}

	// The anim system snaps the montage's root bone to origin (the visual lock that keeps the mesh riding the capsule)
	// ONLY when root extraction is enabled - i.e. RootMotionMode != NoRootMotionExtraction (an anim-system step, not a
	// CMC one). Force it here so a locomotion AnimBP whose Root Motion Mode is "No Root Motion Extraction" doesn't leave
	// the mantle mesh drifting. (This can also be set once in the AnimBP's Class Settings -> Root Motion.)
	AnimInstance->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);

	// Each mantle starts HOLDING the climb's wall tilt; the upright blend waits for the montage's gameplay event. (When
	// MantleUprightBlendEventTag is unset, ShouldMantleBlendUpright() returns true immediately - the pre-notify behavior.)
	bMantleUprightBlendRequested = false;

	// Precisely localize the lip NOW (commit-time), decoupled from the landing - handles a lip that sits higher than the
	// walkable top, which the cheap plane-derived edge can't. Fall back to the per-frame cache if the scan finds nothing.
	FTransform EdgeTarget = MantleEdgeTransformCache;
	FTransform PreciseEdge;
	if (ComputePreciseMantleEdge(PreciseEdge))
	{
		EdgeTarget = PreciseEdge;
	}

	// Set the warp targets FIRST - the layered move reads them during ConvertLocalRootMotionToWorld, which first ticks
	// a frame later, so setting them now (while the move is only queued) guarantees the ordering. Two targets for the
	// two-phase path: "up" -> the lip/edge, "forward" -> the landing on top.
	MotionWarpingComp->AddOrUpdateWarpTargetFromTransform(MantleUpWarpTargetName, EdgeTarget);
	MotionWarpingComp->AddOrUpdateWarpTargetFromTransform(MantleForwardWarpTargetName, MantleLandingTransformCache);

	// Play the montage for the POSE on the mesh; the layered move independently re-samples its root motion for the sim.
	const float MontageLength = AnimInstance->Montage_Play(MantleMontage, MantleMontagePlayRate);
	if (MontageLength <= 0.0f)
	{
		return false;
	}

	FAnimMontageInstance* MontageInstance = AnimInstance->GetActiveInstanceForMontage(MantleMontage);
	if (!MontageInstance)
	{
		return false;
	}
	// Disable only the MOVEMENT-facing root motion so this layered move is the single capsule driver (no double). Per
	// the engine, PushDisableRootMotion does NOT disable the anim system's visual root SNAP (SlotEvaluatePose) - that
	// keeps the mesh root pinned to origin so the mesh rides the capsule, as long as extraction is enabled (the mode
	// set above / the AnimBP's Root Motion Mode must not be "No Root Motion Extraction").
	MontageInstance->PushDisableRootMotion();
	const float StartPosition = MontageInstance->GetPosition();

	TSharedPtr<FLayeredMove_AnimRootMotion> MantleMove = MakeShared<FLayeredMove_AnimRootMotion>();
	MantleMove->MontageState.Montage = MantleMontage;
	MantleMove->MontageState.PlayRate = MantleMontagePlayRate;
	MantleMove->MontageState.StartingMontagePosition = StartPosition;
	MantleMove->MontageState.CurrentPosition = StartPosition;
	MantleMove->DurationMs = ((MantleMontage->GetPlayLength() - StartPosition) / FMath::Abs(MantleMontagePlayRate)) * 1000.0f;
	// Zero the velocity handed to Walking when the move ends, so there's no launch/skate onto the ledge.
	MantleMove->FinishVelocitySettings.FinishVelocityMode = ELayeredMoveFinishVelocityMode::SetVelocity;
	MantleMove->FinishVelocitySettings.SetVelocity = FVector::ZeroVector;
	// MixMode is OverrideAll from the struct constructor.

	QueueLayeredMove(MantleMove);

	// (Capsule collision is relaxed for the traversal by URogueMantleMode::Activate/Deactivate.)
	return true;
}

void URogueCharacterMoverComponent::OnMantleUprightBlendEvent(FGameplayTag EventTag, FRogueGameplayEventData Payload)
{
	// Start the mantle's ease-to-vertical from this authored point in the montage. Only while actually mantling, and only
	// for our configured tag (the delegate is global - it fires for every gameplay event on the action system).
	if (IsMantling() && MantleUprightBlendEventTag.IsValid() && EventTag.MatchesTag(MantleUprightBlendEventTag))
	{
		bMantleUprightBlendRequested = true;
	}
}

bool URogueCharacterMoverComponent::BeginMantleDown()
{
	if (!MantleDownMontage || !MotionWarpingComp || !CacheOwnerCharacter)
	{
		return false;
	}

	USkeletalMeshComponent* Mesh = CacheOwnerCharacter->GetMesh();
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return false;
	}

	// Keep the mesh riding the capsule during warping: root extraction must be enabled (see BeginMantle for the full why).
	AnimInstance->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);

	// Each drop starts HOLDING upright; the tilt blend waits for the montage's gameplay event. (When the tag is unset,
	// ShouldMantleDownBlendTilt() returns true immediately - blend into the lean at entry.)
	bMantleDownTiltBlendRequested = false;

	// Warp targets from the per-tick probe (already validated -> always-succeed). Two-phase: "forward" -> the lip pivot
	// over the edge, "down" -> the hang on the face. Set BEFORE the layered move ticks (see BeginMantle for the ordering).
	MotionWarpingComp->AddOrUpdateWarpTargetFromTransform(MantleDownForwardWarpTargetName, MantleDownEdgeTransformCache);
	MotionWarpingComp->AddOrUpdateWarpTargetFromTransform(MantleDownDownWarpTargetName, MantleDownHangTransformCache);

	const float MontageLength = AnimInstance->Montage_Play(MantleDownMontage, MantleDownMontagePlayRate);
	if (MontageLength <= 0.0f)
	{
		return false;
	}

	FAnimMontageInstance* MontageInstance = AnimInstance->GetActiveInstanceForMontage(MantleDownMontage);
	if (!MontageInstance)
	{
		return false;
	}
	// Disable only the MOVEMENT-facing root motion so this layered move is the single capsule driver (the visual root
	// snap stays; see BeginMantle).
	MontageInstance->PushDisableRootMotion();
	const float StartPosition = MontageInstance->GetPosition();

	TSharedPtr<FLayeredMove_AnimRootMotion> MantleDownMove = MakeShared<FLayeredMove_AnimRootMotion>();
	MantleDownMove->MontageState.Montage = MantleDownMontage;
	MantleDownMove->MontageState.PlayRate = MantleDownMontagePlayRate;
	MantleDownMove->MontageState.StartingMontagePosition = StartPosition;
	MantleDownMove->MontageState.CurrentPosition = StartPosition;
	MantleDownMove->DurationMs = ((MantleDownMontage->GetPlayLength() - StartPosition) / FMath::Abs(MantleDownMontagePlayRate)) * 1000.0f;
	// Zero the velocity handed to Climbing when the move ends, so there's no launch off the face.
	MantleDownMove->FinishVelocitySettings.FinishVelocityMode = ELayeredMoveFinishVelocityMode::SetVelocity;
	MantleDownMove->FinishVelocitySettings.SetVelocity = FVector::ZeroVector;
	// MixMode is OverrideAll from the struct constructor.

	QueueLayeredMove(MantleDownMove);

	// (Capsule collision is relaxed for the traversal by URogueMantleDownMode::Activate/Deactivate.)
	return true;
}

void URogueCharacterMoverComponent::OnMantleDownTiltBlendEvent(FGameplayTag EventTag, FRogueGameplayEventData Payload)
{
	// Start the mantle-down's ease-into-wall-lean from this authored point in the montage. Only while actually mantling
	// down, and only for our configured tag (the delegate is global - it fires for every gameplay event).
	if (IsMantlingDown() && MantleDownTiltBlendEventTag.IsValid() && EventTag.MatchesTag(MantleDownTiltBlendEventTag))
	{
		bMantleDownTiltBlendRequested = true;
	}
}
