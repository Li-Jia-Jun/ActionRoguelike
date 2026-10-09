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
#include "MotionWarpingComponent.h"
#include "MoveLibrary/MovementUtils.h"
#include "DefaultMovementSet/LayeredMoves/AnimRootMotionLayeredMove.h"
#include "CollisionShape.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

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

float URogueCharacterMoverComponent::GetClimbCadenceScale() const
{
	if (const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		if (const USkeletalMeshComponent* Mesh = OwnerCharacter->GetMesh())
		{
			if (const UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
			{
				// Returns true only if the curve is present in the current pose; default to 1 (no surge) otherwise
				// so climbing behaves normally until the cadence curve is present on the clips.
				float CadenceValue = 1.0f;
				if (AnimInstance->GetCurveValue(ClimbCadenceCurveName, CadenceValue))
				{
					// Normalize an absolute-speed curve (cm/s) into a ~mean-1 multiplier; 0 means it's already a multiplier.
					const float Normalized = (ClimbCadenceReferenceSpeed > 0.0f) ? (CadenceValue / ClimbCadenceReferenceSpeed) : CadenceValue;
					// Blend toward a flat 1.0 to tame surge amplitude without changing the average speed.
					return FMath::Lerp(1.0f, Normalized, ClimbSurgeStrength);
				}
			}
		}
	}

	return 1.0f;
}

void URogueCharacterMoverComponent::HandlePreSimulationTick(const FMoverTimeStep& TimeStep, const FMoverInputCmdContext& InputCmd)
{
	SweepAndStoreWallHits();
	RefreshClimbSurfaceInfo();
	RefreshFootProbes();   // per-foot foothold probes + the both-feet gross-hang flag (self-gates on IsClimbing)
	RefreshHandGrips();    // per-hand wall grip points for the Control Rig hand pins (self-gates on IsClimbing)
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

	// Carry each foot's previous hanging state across the reset - it feeds the release hysteresis in ProbeFoot.
	const bool bLeftWasHanging = LeftFootProbe.bHanging;
	const bool bRightWasHanging = RightFootProbe.bHanging;

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

void URogueCharacterMoverComponent::RefreshHandGrips()
{
	const USceneComponent* Updated = GetUpdatedComponent();
	if (!IsClimbing() || ClimbDominantSurfaceNormalCache.IsNearlyZero() || !Updated)
	{
		// No grips outside Climbing (corner turn, mantles, falling): drop bFound but KEEP the last Location/Normal. A hand pin
		// still blending out then eases from where the hands actually were - resetting to a default struct published world
		// ORIGIN, which the FBIK pin (pelvis translation on) dragged the whole body toward at the start of a corner turn.
		LeftHandGrip.bFound = false;
		RightHandGrip.bFound = false;
		return;
	}

	// Wall basis from the dominant plane. Anchor the grips to the CAPSULE (pure sim - stable, pose-independent) at a
	// fixed hand offset, so they're steady pins even as the arms animate. Tune the offsets to sit where the braced idle
	// hands already grab, so the Control Rig pin is seamless when the dead-hang blend engages.
	const FVector Center = Updated->GetComponentLocation();
	const FVector WallNormal = ClimbDominantSurfaceNormalCache;
	const FVector Up = GetUpDirection();
	FVector WallUp = (Up - Up.ProjectOnToNormal(WallNormal)).GetSafeNormal();
	if (WallUp.IsNearlyZero()) { WallUp = Up; }
	const FVector WallRight = FVector::CrossProduct(WallNormal, WallUp).GetSafeNormal();

	const FVector AnchorBase = Center + WallUp * HandGripAnchorHeight;
	LeftHandGrip  = ProbeHandGrip(AnchorBase - WallRight * HandGripStanceHalfWidth, WallNormal);
	RightHandGrip = ProbeHandGrip(AnchorBase + WallRight * HandGripStanceHalfWidth, WallNormal);
}

FClimbHandGrip URogueCharacterMoverComponent::ProbeHandGrip(const FVector& Anchor, const FVector& WallNormal) const
{
	// Default (no grip found) to the anchor projected onto the climb plane rather than a zero vector, so Location is always
	// a sensible nearby point - a consumer that blends on bFound never sees a world-origin target mid-blend.
	FClimbHandGrip Grip;
	const FVector PlanePoint = ClimbDominantSurfaceLocationCache;
	Grip.Location = Anchor - WallNormal * FVector::DotProduct(Anchor - PlanePoint, WallNormal) + WallNormal * HandGripSurfaceGap;
	Grip.Normal = WallNormal;
	if (!GetWorld())
	{
		return Grip;
	}

	// Start OFF the wall and sweep toward it (same pattern as the foot probe), so it begins in free space.
	const FVector Origin = Anchor + WallNormal * HandProbeBackoff;
	const FVector End = Origin - WallNormal * (HandProbeBackoff + HandProbeReach);

	const FCollisionShape Sphere = FCollisionShape::MakeSphere(HandProbeRadius);
	FHitResult Hit;
	const bool bHit = GetWorld()->SweepSingleByChannel(Hit, Origin, End, FQuat::Identity, ECC_WorldStatic, Sphere, ClimbQueryParams);

	if (bHit && !Hit.bStartPenetrating)
	{
		const float SteepnessDot = FVector::DotProduct(Hit.ImpactNormal, GetUpDirection());
		const bool bSameFace = FVector::DotProduct(Hit.ImpactNormal, WallNormal) > 0.3f;
		if (IsSurfaceClimbable(SteepnessDot) && bSameFace)
		{
			Grip.bFound = true;
			// Hold the grip a little OFF the surface (along the hit normal) so the hand isn't flush/embedded in the wall.
			Grip.Location = Hit.ImpactPoint + Hit.ImpactNormal * HandGripSurfaceGap;
			Grip.Normal = Hit.ImpactNormal;
		}
	}

#if ENABLE_DRAW_DEBUG
	if (CVarClimbingDebugDrawing.GetValueOnGameThread())
	{
		const FColor C = Grip.bFound ? FColor::Magenta : FColor::Silver;
		const FVector Marker = Grip.bFound ? Grip.Location : End;
		DrawDebugLine(GetWorld(), Origin, Marker, C, false, -1.0f, 0, 0.5f);
		DrawDebugSphere(GetWorld(), Marker, HandProbeRadius, 12, C, false, -1.0f, 0, 0.5f);
	}
#endif

	return Grip;
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
