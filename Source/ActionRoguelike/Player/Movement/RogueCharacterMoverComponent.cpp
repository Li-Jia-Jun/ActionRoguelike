// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueCharacterMoverComponent.h"

#include "RogueClimbMode.h"
#include "RogueClimbTransition.h"
#include "RogueMantleMode.h"
#include "RogueMantleTransition.h"
#include "ActionSystem/RogueActionSystemComponent.h"
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

		// Listen for the mantle upright-blend gameplay event the montage fires via its "Send Gameplay Event" notify.
		if (URogueActionSystemComponent* ActionSystem = Owner->FindComponentByClass<URogueActionSystemComponent>())
		{
			ActionSystem->GameplayEventReceivedDelegate.AddDynamic(this, &URogueCharacterMoverComponent::OnMantleUprightBlendEvent);
		}
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
	
	// While climbing, bias the whole grid up the wall (hands reach high, legs curl). Follow the WALL's up-slope
	// (via PrevNormal — this frame's cache was reset above) so the samples stay on a tilted/receding surface. As a
	// world-space vector added to each Origin below, it shifts the grid without changing its spacing.
	const FVector ClimbUpBias = IsClimbing() ? GetClimbSampleUpBias(PrevNormal) : FVector::ZeroVector;

	// Even spacing over the grid. Divide the span by (count - 1) so the first/last samples land exactly on
	// the bottom/top (and left/right) bounds; step is 0 for a single row/column.
	const float RowStep = (Rows > 1) ? (ClimbGridTopOffset - ClimbGridBottomOffset) / (Rows - 1) : 0.0f;
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
		const float VOffset = ClimbGridBottomOffset + Row * RowStep; // row 0 = bottom, last row = top
		const bool bLowerRow = (Row < ClimbLowerBodyRowCount);

		for (int32 Col = 0; Col < Cols; ++Col)
		{
			const float HOffset = -ClimbGridHalfWidth + Col * ColStep; // col 0 = left, last col = right

			const FVector Origin = Base + Up * VOffset + Right * HOffset + ClimbUpBias;
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

FVector URogueCharacterMoverComponent::GetClimbSampleUpBias(const FVector& PlaneNormal) const
{
	// Project world-up onto the wall plane so the climb sampling shift follows the wall's up-slope. On a vertical
	// wall this equals world up; on a back-tilted wall it gains the forward component that keeps the sweep/grid on
	// the receding surface (a pure world-up shift would miss it and drop the climb).
	const FVector Up = GetUpDirection();
	const FVector WallUp = (Up - FVector::DotProduct(Up, PlaneNormal) * PlaneNormal).GetSafeNormal();
	return (WallUp.IsNearlyZero() ? Up : WallUp) * OnClimbAdditionalOffset;
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
	FVector WallUp = (Up - Up.ProjectOnToNormal(WallNormal)).GetSafeNormal();
	const FVector UpBias = GetClimbSampleUpBias(ClimbDominantSurfaceNormalCache);
	const FVector Feet = Updated->GetComponentLocation() - Up * CapHH + UpBias;
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
		const float EdgeUp = FVector::DotProduct(DownHit.ImpactPoint - Feet, WallUp);
		MantleLandingTransformCache = FTransform(LandingQuat, RestFeet);
		MantleEdgeTransformCache = FTransform(LandingQuat, Feet + WallUp * EdgeUp + WallUp * MantleEdgeUp - IntoWall * MantleEdgeBackward);

#if ENABLE_DRAW_DEBUG
		if (CVarClimbingDebugDrawing.GetValueOnGameThread())
		{
			// Landing (MantleForward) target. The MantleUp/edge target is drawn by the precise scan (cyan) at commit;
			// MantleEdgeTransformCache (set above) is now only a silent fallback, so it isn't drawn.
			DrawDebugSphere(GetWorld(), MantleLandingTransformCache.GetLocation(), 12.0f, 10, FColor::Purple, false, 6.0f, 0, 2.0f);
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
	const FVector Feet = Updated->GetComponentLocation() - Up * CapHH + GetClimbSampleUpBias(WallNormal);
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

		float Lo = MantleMinLedgeHeightFromFeet; // along-wall: known to still be on the face (steep hit)
		float Hi = MantleMaxLedgeHeightFromFeet; // along-wall: known to be above the lip (empty, or a walkable top)
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
		DrawDebugSphere(GetWorld(), OutEdge.GetLocation(), 10.0f, 12, FColor::Cyan, false, 6.0f, 0, 2.0f);  // precise lip (feet target)
		DrawDebugSphere(GetWorld(), LipPoint, 5.0f, 8, FColor::White, false, 3.0f, 0, 1.5f);                // raw averaged lip hit on the face
	}
#endif

	return true;
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
