// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueCharacterMoverComponent.h"

#include "RogueClimbMode.h"
#include "GameFramework/Character.h"
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
	const FVector Start = Updated->GetComponentLocation() + Forward * ClimbDetectionForwardOffset;
	const FVector End = Start + Forward; // Sweep slightly ahead of the character

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
		ClimbDominantSurfaceNormalCache = NormalSum.GetSafeNormal();
		ClimbDominantSurfaceLocationCache = PointSum / ValidCount;
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
