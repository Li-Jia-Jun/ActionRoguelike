// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueCharacterMoverComponent.h"

#include "RogueClimbMode.h"
#include "RogueClimbTransition.h"
#include "RogueMantleMode.h"
#include "RogueMantleTransition.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
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

	// Global transitions are evaluated in array order (first match wins), so the mantle must come before the climb
	// transition (it wins over climb's exit-to-Falling at the lip). Drop any stale copies a BP may have snapshotted,
	// then insert ours at the front in priority order.
	Transitions.RemoveAll([](const TObjectPtr<UBaseMovementModeTransition>& Transition)
	{
		return Transition && (Transition->IsA<URogueMantleTransition>() || Transition->IsA<URogueClimbTransition>());
	});
	Transitions.Insert(NewObject<URogueMantleTransition>(this), 0);
	Transitions.Insert(NewObject<URogueClimbTransition>(this), 1);

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
	}

	OnPreSimulationTick.AddDynamic(this, &URogueCharacterMoverComponent::HandlePreSimulationTick);
}

bool URogueCharacterMoverComponent::IsClimbing() const
{
	return GetMovementModeName() == URogueClimbMode::ModeName;
}

bool URogueCharacterMoverComponent::IsMantling() const
{
	return GetMovementModeName() == URogueMantleMode::ModeName;
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

	// The mantle probe only matters while climbing (it feeds the climb->mantle top-out transition).
	if (IsClimbing())
	{
		RefreshMantleProbe();
	}
	else
	{
		bMantleLedgeAvailable = false;
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
	const FVector Up = GetUpDirection();
	FVector Start = Updated->GetComponentLocation() + Forward * ClimbDetectionForwardOffset;
	FVector End = Start + Forward; // Sweep slightly ahead of the character
	
	if (IsClimbing())
	{
		Start += Up * OnClimbAdditionalOffset;
		End += Up * OnClimbAdditionalOffset;
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

	// Reset cached outputs each refresh.
	bFacingClimbableSurface = false;
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
	
	// Climbing will add a small offset for grid because hands are reaching for higher and legs are curling
	float NewClimbGridBottomOffset = ClimbGridBottomOffset;
	float NewClimbGridTopOffset = ClimbGridTopOffset;
	if (IsClimbing())
	{
		NewClimbGridBottomOffset += OnClimbAdditionalOffset;
		NewClimbGridTopOffset += OnClimbAdditionalOffset;
	}
	

	// Even spacing over the grid. Divide the span by (count - 1) so the first/last samples land exactly on
	// the bottom/top (and left/right) bounds; step is 0 for a single row/column.
	const float RowStep = (Rows > 1) ? (NewClimbGridTopOffset - NewClimbGridBottomOffset) / (Rows - 1) : 0.0f;
	const float ColStep = (Cols > 1) ? (ClimbGridHalfWidth * 2.0f) / (Cols - 1) : 0.0f;

	const FVector Base = Updated->GetComponentLocation();
	const FVector Forward = Updated->GetForwardVector();
	const FVector Up = GetUpDirection();
	const FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();

	ClimbSurfaceSamplesCache.Reserve(Rows * Cols);

	int32 ValidCount = 0;
	FVector NormalSum = FVector::ZeroVector;
	FVector PointSum = FVector::ZeroVector;

	// Regional hit info (valid means climbable)
	int32 GripValid = 0, GripTotal = 0;		// Upper body
	int32 LowerValid = 0, LowerTotal = 0;
	int32 LeftValid = 0, LeftTotal = 0;		
	int32 RightValid = 0, RightTotal = 0;
	
	// Grid-based line-tracing
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		const float VOffset = NewClimbGridBottomOffset + Row * RowStep; // row 0 = bottom, last row = top
		const bool bLowerRow = (Row < ClimbLowerBodyRowCount);

		for (int32 Col = 0; Col < Cols; ++Col)
		{
			const float HOffset = -ClimbGridHalfWidth + Col * ColStep; // col 0 = left, last col = right

			const FVector Origin = Base + Up * VOffset + Right * HOffset;
			const FVector End = Origin + Forward * ClimbSampleReach;

			FClimbSurfaceSample Sample;
			FHitResult Hit;
			const bool bTraceHit = GetWorld()->LineTraceSingleByChannel(Hit, Origin, End, ECC_WorldStatic, ClimbQueryParams);

			if (bTraceHit)
			{
				const float SteepnessDot = FVector::DotProduct(Hit.ImpactNormal, Up);
				const FVector HorizontalNormal = Hit.ImpactNormal.GetSafeNormal2D();
				// Clamp before Acos: dot of two unit vectors can drift slightly outside [-1,1] and yield NaN.
				const float FacingDot = FMath::Clamp(FVector::DotProduct(Forward, -HorizontalNormal), -1.0f, 1.0f);
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

	// Entry gate 1 - coverage: enough of the GRIP (upper) band is backed by surface. (Lower rows may hang).
	const bool bCoveragePass = (GripTotal > 0) && (GripValid >= FMath::CeilToInt(GripTotal * MinClimbCoverageRatio));

	// Entry gate 2 - consistency: every valid sample normal agrees with the average (rejects corners / fragments).
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

	bFacingClimbableSurface = bCoveragePass && bConsistencyPass;

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
	// A surface is climbable when its normal is close enough to horizontal (i.e. the surface is near-vertical).
	return FMath::Abs(SteepnessDotProduct) <= MaxClimbableSteepnessDot;
}

bool URogueCharacterMoverComponent::EyeHeightTrace(const float TraceDistance) const
{
	// Trace a line from eye level and see if it hits
	
	FHitResult HitResult;

	const FVector Start = UpdatedComponent->GetComponentLocation() +
			(UpdatedComponent->GetUpVector() * CacheOwnerCharacter->BaseEyeHeight);
	const FVector End = Start + (UpdatedComponent->GetForwardVector() * TraceDistance);

	return GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, ECC_WorldStatic, ClimbQueryParams);
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
	const FVector Feet = Updated->GetComponentLocation() - Up * CapHH;
	const float ForwardOffset = CapR + MantleForwardReach;

	// (a) Overhead test: forward line trace at the top of the band, expecting empty space (the wall has ended into a lip).
	const FVector OverheadStart = Feet + Up * MantleMaxLedgeHeightFromFeet;
	const FVector OverheadEnd = OverheadStart + IntoWall * ForwardOffset;
	FHitResult OverheadHit;
	const bool bOverheadBlocked = GetWorld()->LineTraceSingleByChannel(OverheadHit, OverheadStart, OverheadEnd, ECC_WorldStatic, ClimbQueryParams);

	// (b) Down test: from beyond the lip, trace down across the whole band to find the walkable top (expect a hit and surface not too steep).
	const float DownSpan = MantleMaxLedgeHeightFromFeet - MantleMinLedgeHeightFromFeet;
	const FVector DownStart = OverheadStart + IntoWall * ForwardOffset;
	const FVector DownEnd = DownStart - Up * DownSpan;
	FHitResult DownHit;
	const bool bLandingHit = GetWorld()->LineTraceSingleByChannel(DownHit, DownStart, DownEnd, ECC_WorldStatic, ClimbQueryParams);
	const bool bWalkable = bLandingHit && (FVector::DotProduct(DownHit.ImpactNormal, Up) >= MantleMinWalkableDot);

	bool bHeightOk = false;
	bool bFits = false;
	FVector LandingCenter = FVector::ZeroVector;
	if (bWalkable)
	{
		LandingCenter = DownHit.ImpactPoint + Up * (CapHH + MantleLandingSkin);

		// Check if ledge height from the feet along the wall is enough for capsule
		const float LedgeRise = FVector::DotProduct(DownHit.ImpactPoint - Feet, Up);
		bHeightOk = (LedgeRise >= MantleMinLedgeHeightFromFeet) && (LedgeRise <= MantleMaxLedgeHeightFromFeet);

		// (c) Capsule fit at the landing so the character actually stands there (expect no hit)
		const FCollisionShape FitCapsule = FCollisionShape::MakeCapsule(CapR, CapHH);
		bFits = !GetWorld()->OverlapBlockingTestByChannel(LandingCenter, FQuat::Identity, ECC_WorldStatic, FitCapsule, ClimbQueryParams);
	}

	bMantleLedgeAvailable = !bOverheadBlocked && bWalkable && bHeightOk && bFits;

	if (bMantleLedgeAvailable)
	{
		// Face away from the wall (onto the ledge), kept vertical - same convention as the climb mode's orientation.
		const FRotator LandingRot = UMovementUtils::ApplyGravityToOrientationIntent((-WallNormal).ToOrientationRotator(), GetWorldToGravityTransform(), true);
		const FQuat LandingQuat = LandingRot.Quaternion();

		// Two warp targets for the two-phase (up-then-over) path (both FEET targets via WarpToFeetLocation):
		//  - Landing ("forward"): the down-trace hit on top, forward of the lip - where the character ends up standing.
		//  - Edge ("up"): at ledge height, only MantleEdgeForward in front of the FEET, so phase 1 goes mostly straight
		//    UP to just inside the lip rather than diagonally out over it (which would clip the wall corner).
		const float EdgeUp = FVector::DotProduct(DownHit.ImpactPoint - Feet, Up);
		MantleLandingTransformCache = FTransform(LandingQuat, DownHit.ImpactPoint);
		MantleEdgeTransformCache = FTransform(LandingQuat, Feet + Up * EdgeUp + IntoWall * MantleEdgeForward);

#if ENABLE_DRAW_DEBUG
		if (CVarClimbingDebugDrawing.GetValueOnGameThread())
		{
			const FVector EdgeLoc = MantleEdgeTransformCache.GetLocation();
			const FVector LandLoc = MantleLandingTransformCache.GetLocation();
			DrawDebugSphere(GetWorld(), EdgeLoc, 12.0f, 10, FColor::Blue, false, -1.0f, 0, 2.0f);    // MantleUp target
			DrawDebugSphere(GetWorld(), LandLoc, 12.0f, 10, FColor::Purple, false, -1.0f, 0, 2.0f);  // MantleForward target
			DrawDebugLine(GetWorld(), Feet, EdgeLoc, FColor::Blue, false, -1.0f, 0, 2.0f);           // phase 1: up
			DrawDebugLine(GetWorld(), EdgeLoc, LandLoc, FColor::Purple, false, -1.0f, 0, 2.0f);      // phase 2: over
		}
#endif
	}

#if ENABLE_DRAW_DEBUG
	if (CVarClimbingDebugDrawing.GetValueOnGameThread())
	{
		DrawDebugLine(GetWorld(), OverheadStart, bOverheadBlocked ? OverheadHit.ImpactPoint : OverheadEnd, bOverheadBlocked ? FColor::Red : FColor::Green, false, -1.0f, 0, 1.0f);
		DrawDebugLine(GetWorld(), DownStart, bLandingHit ? DownHit.ImpactPoint : DownEnd, bWalkable ? FColor::Green : FColor::Orange, false, -1.0f, 0, 1.0f);
		if (bWalkable)
		{
			DrawDebugPoint(GetWorld(), DownHit.ImpactPoint, 20.0f, FColor::Cyan, false, -1.0f);
			const FColor FitColor = (bFits && bHeightOk) ? FColor::Green : FColor::Red;
			DrawDebugCapsule(GetWorld(), LandingCenter, CapHH, CapR, FQuat::Identity, FitColor, false, -1.0f, 0, 1.0f);
			if (bMantleLedgeAvailable)
			{
				DrawDebugDirectionalArrow(GetWorld(), LandingCenter, LandingCenter + IntoWall * 40.0f, 8.0f, FColor::Yellow, false, -1.0f, 0, 2.0f);
			}
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

	// Set the warp targets FIRST - the layered move reads them during ConvertLocalRootMotionToWorld, which first ticks
	// a frame later, so setting them now (while the move is only queued) guarantees the ordering. Two targets for the
	// two-phase path: "up" -> the lip/edge, "forward" -> the landing on top.
	MotionWarpingComp->AddOrUpdateWarpTargetFromTransform(MantleUpWarpTargetName, MantleEdgeTransformCache);
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
