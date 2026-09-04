// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueCharacterMoverComponent.h"

#include "RogueClimbMode.h"
#include "RogueClimbTransition.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "CollisionShape.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

TAutoConsoleVariable<bool> CVarClimbingDebugDrawing(TEXT("game.climbing.DebugDraw"), false,
	TEXT("Enable climbing mover component debug rendering. (0 = off, 1 = enabled)"),
	ECVF_Cheat);


URogueCharacterMoverComponent::URogueCharacterMoverComponent()
{
	// Register the climbing mode
	MovementModes.Add(URogueClimbMode::ModeName, CreateDefaultSubobject<URogueClimbMode>(TEXT("ClimbMode")));

	// Register the global transition that enters/leaves climbing.
	Transitions.Add(CreateDefaultSubobject<URogueClimbTransition>(TEXT("ClimbTransition")));
}

void URogueCharacterMoverComponent::BeginPlay()
{
	Super::BeginPlay();

	CacheOwnerCharacter = Cast<ACharacter>(GetOwner());

	ClimbQueryParams.AddIgnoredActor(CacheOwnerCharacter);

	OnPreSimulationTick.AddDynamic(this, &URogueCharacterMoverComponent::HandlePreSimulationTick);
}

bool URogueCharacterMoverComponent::IsClimbing() const
{
	return GetMovementModeName() == URogueClimbMode::ModeName;
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
