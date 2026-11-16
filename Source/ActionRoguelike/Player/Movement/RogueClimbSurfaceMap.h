// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "RogueClimbSurfaceMap.generated.h"

/*
 * The CLIMB SURFACE MAP - a small grid of the wall around the climber, MARCHED along the surface (Docs/climb/surface-warp.md).
 *
 * The climb clips are authored against a FLAT wall; the climb places the body against one averaged plane (the dominant
 * plane). The map records where the REAL surface is over the whole reach area, every tick - so the pose can be warped onto
 * it (a swinging hand follows a cylinder instead of floating off it) and the limb IK can ask about the surface anywhere, not
 * only where a limb is about to plant.
 *
 * Flat frame (world space): Origin = the capsule centre projected onto the plane; Right / Up span the plane (Up = gravity
 * flattened onto it, Right = the climber's right); Normal points out of the wall. A point of the clip's flat-wall pose is
 * (u, v) on the plane plus a height along Normal.
 *
 * Grid: (s, t) = DISTANCE ALONG THE SURFACE (cm). One start cell is traced straight into the wall; every other cell is
 * reached by stepping one cell spacing ALONG the surface from a neighbour (along the neighbour's tangent, then down onto the
 * surface along its normal) - so the grid wraps round a curve instead of missing it, and neighbouring cells stay one spacing
 * apart on the surface. Each cell stores its world surface point and normal.
 *
 * The warp reads the clip's flat (u, v) as (s, t): a hand 60 cm right of the body on the flat wall lands 60 cm round the
 * curve, at the clip's own clearance above the surface there. Directions carry over by the minimal rotation from the plane
 * normal to the local normal (TangentsAt) - Right becomes the surface's "right" there, Up its "up".
 */

/** What the map's trace callback reports (Build). */
struct FClimbSurfaceMapHit
{
	FVector Point = FVector::ZeroVector;
	FVector Normal = FVector::ZeroVector;
	// The owner's wall rule (near-vertical, not a walkable top). A hit that isn't climbable stops a march there.
	bool bClimbable = false;
};

/** The owner's line trace: true = a blocking hit that didn't start inside geometry. */
using FClimbSurfaceMapTraceFn = TFunctionRef<bool(const FVector& Start, const FVector& End, FClimbSurfaceMapHit& OutHit)>;

/** How to build the map (FClimbSurfaceMap::Build). */
struct FClimbSurfaceMapBuildSettings
{
	int32 Columns = 7;
	int32 Rows = 9;
	// Grid extent in (s, t): cm along the surface, measured on the flat plane's axes from the capsule centre.
	FVector2D Min = FVector2D(-70.0, -110.0);
	FVector2D Max = FVector2D(70.0, 90.0);
	// The start trace runs along -Normal, from FrontReach in front of the plane to BackReach behind it.
	double FrontReach = 40.0;
	double BackReach = 80.0;
	// A step whose surface normal turns more than this from the cell it left is an edge or a corner: the march stops there.
	double MaxTurnPerCellDegrees = 50.0;
	// Every cell must face out of the wall at least this much (dot with the plane normal; 0 = up to side-on). The start cell
	// must face the climber (StartFacingDot), like every climb probe.
	double MinFacingDot = 0.0;
	double StartFacingDot = 0.3;
	// Up to this many cells nearest the body are tried as the start (the body's own cell can sit over a hole - a dead hang
	// under a lip, a gap between holds).
	int32 MaxStartAttempts = 8;
};

/**
 * One limb's REACH, as the mover component defines it - the single definition the reach leash, the pin placement and the
 * Climb Surface Warp rig node all use. Published per limb on FClimbLimbPin::Reach every tick.
 */
USTRUCT(BlueprintType)
struct FClimbLimbReach
{
	GENERATED_BODY()

	// How far the contact point may be from the limb's root (shoulder / hip) right now, in cm: LimbReachLeashRatio x the
	// limb's straight reach - or, for a hold the clip itself placed farther, that hold's own distance (up to the full reach).
	// 0 = unknown (no mesh yet): consumers skip their reach rule.
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|LimbIK")
	float Limit = 0.0f;

	// The point that touches the surface (the palm socket / the ball), as an offset in ContactBone's local space. ContactBone =
	// the bone it hangs off (the hand / foot in a standard setup). Measure the limit to THIS point, not to the hand / foot bone.
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|LimbIK")
	FName ContactBone = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|LimbIK")
	FVector ContactOffset = FVector::ZeroVector;
};

/** One sample of the map: the real surface at one (s, t) of the grid. */
USTRUCT(BlueprintType)
struct FClimbSurfaceMapCell
{
	GENERATED_BODY()

	// The march reached a climbable surface here. False = a hole, past an edge or a corner, or something that isn't a wall (a
	// floor, a ceiling) - consumers fade their correction out there.
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|SurfaceMap")
	bool bValid = false;

	// The surface point (world).
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|SurfaceMap")
	FVector Point = FVector::ZeroVector;

	// The surface normal there (world).
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|SurfaceMap")
	FVector Normal = FVector::ZeroVector;
};

/** The map itself: a Columns x Rows grid over [Min, Max] in surface coordinates (s, t) (cm along the surface), world space. */
USTRUCT(BlueprintType)
struct FClimbSurfaceMap
{
	GENERATED_BODY()

	// Built this tick (climbing on a plane). False = empty: off the wall, mantling, turning a corner, or disabled.
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|SurfaceMap")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|SurfaceMap")
	FVector Origin = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|SurfaceMap")
	FVector Right = FVector::RightVector;

	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|SurfaceMap")
	FVector Up = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|SurfaceMap")
	FVector Normal = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|SurfaceMap")
	int32 Columns = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|SurfaceMap")
	int32 Rows = 0;

	// Grid extent in (s, t) (cm along the surface): column 0 sits at Min.X, the last column at Max.X; row 0 at Min.Y (lowest),
	// the last at Max.Y.
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|SurfaceMap")
	FVector2D Min = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|SurfaceMap")
	FVector2D Max = FVector2D::ZeroVector;

	// Row-major: Cells[Row * Columns + Column].
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Climbing|SurfaceMap")
	TArray<FClimbSurfaceMapCell> Cells;

	/** Set up the frame and an empty (all-invalid) grid. Right = Normal x Up (the climber's right). */
	void Reset(const FVector& InOrigin, const FVector& InNormal, const FVector& InUp, int32 InColumns, int32 InRows,
		const FVector2D& InMin, const FVector2D& InMax);

	/**
	 * Reset, then MARCH the grid over the surface with the owner's line trace. The start cell (the one nearest the body that
	 * hits a wall facing the climber) is traced straight into the wall and keeps its plane (u, v) as its (s, t). Its row is
	 * marched left and right, every column up and down from that row, then cells still empty are tried sideways from a filled
	 * neighbour. A march steps over one empty cell (a small hole) and stops at the second, at an edge, or at a turn sharper
	 * than MaxTurnPerCellDegrees. Cost: ~2 line traces per cell.
	 */
	void Build(const FVector& InOrigin, const FVector& InNormal, const FVector& InUp, const FClimbSurfaceMapBuildSettings& Settings,
		FClimbSurfaceMapTraceFn Trace);

	/** Surface coordinates (s, t) of a cell. */
	FVector2D CellUV(int32 Column, int32 Row) const;

	/** FLAT-plane coordinates (u, v) of a world point - for the clip's flat-wall pose, whose (u, v) the warp reads as (s, t). */
	FVector2D ToUV(const FVector& World) const;

	/**
	 * Surface coordinates (s, t) of the surface point nearest World (a point on or near the real surface - a pin): the
	 * nearest valid cell, refined on the local tangent plane. False when the map has no valid cell.
	 */
	bool Locate(const FVector& World, FVector2D& OutUV) const;

	/** Signed height of a world point above the flat plane (cm, along Normal). */
	double HeightAbovePlane(const FVector& World) const { return FVector::DotProduct(World - Origin, Normal); }

	/** The world point at flat-plane coordinates (u, v), Height along Normal (negative = behind the plane). */
	FVector PlanePoint(const FVector2D& UV, double Height = 0.0) const { return Origin + Right * UV.X + Up * UV.Y + Normal * Height; }

	/** The surface point recorded for a cell (meaningless for an invalid cell). */
	FVector CellSurfacePoint(int32 Column, int32 Row) const { return Cell(Column, Row).Point; }

	const FClimbSurfaceMapCell& Cell(int32 Column, int32 Row) const { return Cells[Row * Columns + Column]; }
	FClimbSurfaceMapCell& Cell(int32 Column, int32 Row) { return Cells[Row * Columns + Column]; }

	/** The surface's "right" and "up" where its normal is SurfaceNormal: Right / Up turned by the minimal rotation Normal -> it. */
	void TangentsAt(const FVector& SurfaceNormal, FVector& OutRight, FVector& OutUp) const;

	/**
	 * The surface at (s, t): bilinear over the four surrounding cells. A missing corner (a hole) is extended from the valid
	 * ones along their tangents, so a point next to a hole stays where it belongs. OutCoverage = the share of the bilinear
	 * weight that was valid (1 = all four, 0 = none) - fade a correction by it near holes. Outside the grid it carries on
	 * along the edge's tangent (flat past the edge). False only with no valid corner.
	 */
	bool Sample(const FVector2D& UV, FVector& OutPoint, FVector& OutNormal, double* OutCoverage = nullptr) const;

	/**
	 * THE WARP: a point of the flat-wall pose, moved onto the real surface. Its flat (u, v) becomes (s, t) - the same
	 * distances, measured along the surface - and its height above the plane (the clip's clearance) is kept, measured from
	 * the surface there along its normal. OutRotation turns the plane normal into the local normal: apply it to the point's
	 * orientation (a hand lies flat on the curve). False where the map has no surface around.
	 */
	bool WarpPoint(const FVector& Flat, FVector& OutWarped, FQuat& OutRotation) const;
	bool WarpPointAt(const FVector2D& UV, double Height, FVector& OutWarped, FQuat& OutRotation) const;

	/**
	 * WarpPoint, kept within reach of Root (a shoulder or hip): if the warped point is farther than MaxReach from Root, or
	 * there's no surface under it, it moves to the NEAREST spot on the map (searched in a ring of directions in (s, t)) that
	 * has surface and is in reach - so a wide reach on a convex wall comes in sideways, keeping its height. OutReachLimited =
	 * it had to move. If nothing within reach qualifies, falls back to the plain warp (the limb then straightens toward it);
	 * false only when there's no surface at the point either.
	 */
	bool WarpPointWithinReach(const FVector& Flat, const FVector& Root, double MaxReach, FVector& OutWarped, FQuat& OutRotation,
		bool* OutReachLimited = nullptr) const;

	/**
	 * REACH SEARCHES. A spot = WarpPointAt(UV, Height): the surface at (s, t), then Height out along ITS normal. It qualifies
	 * when the map has surface there and it's within MaxReach of Root. Both return the qualifying (s, t) nearest StartUV
	 * (StartUV itself if it qualifies), searched out to MaxDistance along the surface; false if none.
	 *  - FindNearestInReach: any direction (a ring of 16 starting toward the root, then the best one refined).
	 *  - FindAlongLineInReach: only along the line through StartUV in Direction, either way (the shorter one wins).
	 */
	bool FindNearestInReach(const FVector2D& StartUV, double Height, const FVector& Root, double MaxReach, double MaxDistance,
		FVector2D& OutUV) const;
	bool FindAlongLineInReach(const FVector2D& StartUV, const FVector2D& Direction, double Height, const FVector& Root,
		double MaxReach, double MaxDistance, FVector2D& OutUV) const;

	/** A copy with the frame, points and normals moved by Transform (e.g. world -> a rig's component space). Uniform scale assumed. */
	FClimbSurfaceMap Transformed(const FTransform& Transform) const;

private:
	/**
	 * One march step: from a cell, Length along the surface in Direction (turned onto its tangent plane). Steps along the
	 * tangent at a small lift; if the surface rises into the way (a concave curve, a bump) it climbs onto it and carries on
	 * with what's left; then traces down onto the surface, and pulls a convex step's chord back to Length. Fails at a hole or
	 * an edge, on a non-wall, or past the turn limit.
	 */
	bool MarchStep(const FClimbSurfaceMapCell& From, const FVector& Direction, double Length,
		const FClimbSurfaceMapBuildSettings& Settings, FClimbSurfaceMapTraceFn Trace, FClimbSurfaceMapCell& OutCell) const;

	/** One step from cell (Column, Row) toward (Column + DC * Span, Row + DR * Span), DC / DR = -1, 0 or 1. */
	bool StepFromCell(int32 Column, int32 Row, int32 DC, int32 DR, int32 Span, const FClimbSurfaceMapBuildSettings& Settings,
		FClimbSurfaceMapTraceFn Trace, FClimbSurfaceMapCell& OutCell) const;

	/** March from (Column, Row) in (DC, DR) to the grid's edge: steps over one empty cell, stops at the second. */
	void MarchLine(int32 Column, int32 Row, int32 DC, int32 DR, const FClimbSurfaceMapBuildSettings& Settings,
		FClimbSurfaceMapTraceFn Trace);

	/** Cell spacing in (s, t) (cm). */
	FVector2D Spacing() const;
};
