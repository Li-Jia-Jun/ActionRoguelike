// Fill out your copyright notice in the Description page of Project Settings.

#include "RogueClimbSurfaceMap.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RogueClimbSurfaceMap)

void FClimbSurfaceMap::Reset(const FVector& InOrigin, const FVector& InNormal, const FVector& InUp, int32 InColumns, int32 InRows,
	const FVector2D& InMin, const FVector2D& InMax)
{
	Origin = InOrigin;
	Normal = InNormal.GetSafeNormal();
	Up = FVector::VectorPlaneProject(InUp, Normal).GetSafeNormal();
	Right = FVector::CrossProduct(Normal, Up).GetSafeNormal();
	Columns = FMath::Max(InColumns, 2);
	Rows = FMath::Max(InRows, 2);
	Min = InMin;
	Max = InMax;
	Cells.Reset(Columns * Rows);
	Cells.AddDefaulted(Columns * Rows);
	StartCell = INDEX_NONE;
	bValid = !Up.IsNearlyZero() && !Right.IsNearlyZero() && Max.X - Min.X > UE_KINDA_SMALL_NUMBER && Max.Y - Min.Y > UE_KINDA_SMALL_NUMBER;
}

FVector2D FClimbSurfaceMap::Spacing() const
{
	return FVector2D((Columns > 1) ? (Max.X - Min.X) / (Columns - 1) : 0.0, (Rows > 1) ? (Max.Y - Min.Y) / (Rows - 1) : 0.0);
}

FVector2D FClimbSurfaceMap::CellUV(int32 Column, int32 Row) const
{
	const FVector2D Step = Spacing();
	return FVector2D(Min.X + Step.X * Column, Min.Y + Step.Y * Row);
}

FVector2D FClimbSurfaceMap::ToUV(const FVector& World) const
{
	const FVector Delta = World - Origin;
	return FVector2D(FVector::DotProduct(Delta, Right), FVector::DotProduct(Delta, Up));
}

void FClimbSurfaceMap::TangentsAt(const FVector& SurfaceNormal, FVector& OutRight, FVector& OutUp) const
{
	const FVector N = SurfaceNormal.GetSafeNormal();
	const FQuat Turn = N.IsZero() ? FQuat::Identity : FQuat::FindBetweenNormals(Normal, N);
	OutRight = Turn.RotateVector(Right);
	OutUp = Turn.RotateVector(Up);
}

// ------------------------------------------------------------------------------------------------------------------------
// Build: the march

void FClimbSurfaceMap::Build(const FVector& InOrigin, const FVector& InNormal, const FVector& InUp,
	const FClimbSurfaceMapBuildSettings& Settings, FClimbSurfaceMapTraceFn Trace)
{
	Reset(InOrigin, InNormal, InUp, Settings.Columns, Settings.Rows, Settings.Min, Settings.Max);
	SnapRadius = static_cast<float>(FMath::Max(Settings.SnapRadius, 0.0));
	if (!bValid)
	{
		return;
	}
	const FVector2D Step = Spacing();

	// A start trace: straight into the wall at a cell's plane spot; it counts when it hits a wall facing the climber.
	auto TryStart = [&](int32 Index)
	{
		const FVector OnPlane = PlanePoint(CellUV(Index % Columns, Index / Columns));
		FClimbSurfaceMapHit Hit;
		if (Trace(OnPlane + Normal * Settings.FrontReach, OnPlane - Normal * Settings.BackReach, Hit) && Hit.bClimbable
			&& FVector::DotProduct(Hit.Normal.GetSafeNormal(), Normal) > Settings.StartFacingDot)
		{
			FClimbSurfaceMapCell& Start = Cells[Index];
			Start.bValid = true;
			Start.Point = Hit.Point;
			Start.Normal = Hit.Normal.GetSafeNormal();
			return true;
		}
		return false;
	};
	int32 StartIndex = INDEX_NONE;

	// --- 1) ANCHORED START: shift the grid (by at most half a cell) so a cell sits exactly on the hint - a point the climb
	// already found to be wall this tick. Its trace runs through that very point, so the start finds the wall wherever the
	// climb found it: up at the hands in a full hang (nothing near the body), on a lip face thinner than a row.
	int32 AnchorIndex = INDEX_NONE;
	if (Settings.bHasStartHint)
	{
		const FVector2D HintUV = ToUV(Settings.StartHint);
		const int32 HintColumn = FMath::RoundToInt32((HintUV.X - Min.X) / Step.X);
		const int32 HintRow = FMath::RoundToInt32((HintUV.Y - Min.Y) / Step.Y);
		if (HintColumn >= 0 && HintColumn < Columns && HintRow >= 0 && HintRow < Rows)
		{
			const FVector2D Shift = HintUV - (Min + FVector2D(Step.X * HintColumn, Step.Y * HintRow));
			Min += Shift;
			Max += Shift;
			AnchorIndex = HintRow * Columns + HintColumn;
			if (TryStart(AnchorIndex))
			{
				StartIndex = AnchorIndex;
			}
		}
	}

	// --- 2) FALLBACK: the cell nearest the body (plane (0, 0)) that hits. Cells BELOW the body count as farther: in a dead
	// hang the wall that matters is up at the hands.
	if (StartIndex == INDEX_NONE)
	{
		const int32 BodyColumn = FMath::Clamp(FMath::RoundToInt32(-Min.X / Step.X), 0, Columns - 1);
		const int32 BodyRow = FMath::Clamp(FMath::RoundToInt32(-Min.Y / Step.Y), 0, Rows - 1);
		TArray<TPair<double, int32>, TInlineAllocator<128>> Candidates;
		Candidates.Reserve(Cells.Num());
		for (int32 Index = 0; Index < Cells.Num(); ++Index)
		{
			if (Index == AnchorIndex)
			{
				continue; // already tried
			}
			const double DU = (Index % Columns - BodyColumn) * Step.X;
			const double DV = (Index / Columns - BodyRow) * Step.Y * ((Index / Columns < BodyRow) ? 1.5 : 1.0);
			Candidates.Emplace(DU * DU + DV * DV, Index);
		}
		Candidates.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });

		const int32 NumAttempts = FMath::Min(FMath::Max(Settings.MaxStartAttempts, 1), Candidates.Num());
		for (int32 Attempt = 0; Attempt < NumAttempts && StartIndex == INDEX_NONE; ++Attempt)
		{
			if (TryStart(Candidates[Attempt].Value))
			{
				StartIndex = Candidates[Attempt].Value;
			}
		}
	}
	if (StartIndex == INDEX_NONE)
	{
		return; // no wall to start from: a map with no valid cell (StartCell stays INDEX_NONE)
	}
	StartCell = StartIndex;
	const int32 StartColumn = StartIndex % Columns;
	const int32 StartRow = StartIndex / Columns;

	// --- The start row out to both sides, then every column up and down from it.
	MarchLine(StartColumn, StartRow, 1, 0, Settings, Trace);
	MarchLine(StartColumn, StartRow, -1, 0, Settings, Trace);
	for (int32 Column = 0; Column < Columns; ++Column)
	{
		if (Cell(Column, StartRow).bValid)
		{
			MarchLine(Column, StartRow, 0, 1, Settings, Trace);
			MarchLine(Column, StartRow, 0, -1, Settings, Trace);
		}
	}

	// --- Fill: a column whose start-row cell is a hole, or a column cut short, has empty cells next to filled ones - step
	// into them sideways (a sweep each way, so a filled cell feeds the next).
	for (const int32 DC : { 1, -1 })
	{
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			for (int32 Offset = 1; Offset < Columns; ++Offset)
			{
				const int32 Column = (DC > 0) ? Offset : Columns - 1 - Offset;
				const int32 FromColumn = Column - DC;
				if (Cell(Column, Row).bValid || !Cell(FromColumn, Row).bValid)
				{
					continue;
				}
				FClimbSurfaceMapCell Filled;
				if (StepFromCell(FromColumn, Row, DC, 0, 1, Settings, Trace, Filled))
				{
					Cell(Column, Row) = Filled;
				}
			}
		}
	}
}

void FClimbSurfaceMap::MarchLine(int32 Column, int32 Row, int32 DC, int32 DR, const FClimbSurfaceMapBuildSettings& Settings,
	FClimbSurfaceMapTraceFn Trace)
{
	auto InGrid = [this](int32 C, int32 R) { return C >= 0 && C < Columns && R >= 0 && R < Rows; };
	while (true)
	{
		for (int32 Span = 1; Span <= 2; ++Span)
		{
			const int32 NextColumn = Column + DC * Span;
			const int32 NextRow = Row + DR * Span;
			if (!InGrid(NextColumn, NextRow))
			{
				return;
			}
			FClimbSurfaceMapCell Next;
			if (StepFromCell(Column, Row, DC, DR, Span, Settings, Trace, Next))
			{
				// Span 2 = stepped over one empty cell (a small hole); it stays empty.
				Cell(NextColumn, NextRow) = Next;
				Column = NextColumn;
				Row = NextRow;
				break;
			}
			if (Span == 2)
			{
				return; // two empty cells in a row, an edge, or a corner: the march stops
			}
		}
	}
}

bool FClimbSurfaceMap::StepFromCell(int32 Column, int32 Row, int32 DC, int32 DR, int32 Span,
	const FClimbSurfaceMapBuildSettings& Settings, FClimbSurfaceMapTraceFn Trace, FClimbSurfaceMapCell& OutCell) const
{
	// The grid's axes carried onto the surface here: Right / Up turned by the minimal rotation from the plane normal to this
	// cell's normal (round a vertical cylinder, Right turns round it and Up stays up).
	const FClimbSurfaceMapCell& From = Cell(Column, Row);
	FVector SurfaceRight;
	FVector SurfaceUp;
	TangentsAt(From.Normal, SurfaceRight, SurfaceUp);
	const FVector2D Step = Spacing();
	const FVector Direction = SurfaceRight * DC + SurfaceUp * DR;
	const double Length = ((DC != 0) ? Step.X : Step.Y) * Span;
	return MarchStep(From, Direction, Length, Settings, Trace, OutCell);
}

bool FClimbSurfaceMap::MarchStep(const FClimbSurfaceMapCell& From, const FVector& Direction, double Length,
	const FClimbSurfaceMapBuildSettings& Settings, FClimbSurfaceMapTraceFn Trace, FClimbSurfaceMapCell& OutCell) const
{
	// A landing counts when it's a wall, faces out of the plane enough, and doesn't turn too sharply from the cell it left.
	const double MinTurnDot = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Settings.MaxTurnPerCellDegrees, 0.0, 179.0)));
	auto Accept = [&](const FClimbSurfaceMapHit& Hit)
	{
		const FVector N = Hit.Normal.GetSafeNormal();
		return Hit.bClimbable && FVector::DotProduct(N, Normal) > Settings.MinFacingDot && FVector::DotProduct(N, From.Normal) >= MinTurnDot;
	};
	auto Land = [&OutCell](const FVector& Point, const FVector& N)
	{
		OutCell.bValid = true;
		OutCell.Point = Point;
		OutCell.Normal = N;
		return true;
	};

	FVector Point = From.Point;
	FVector PointNormal = From.Normal;
	double Remaining = Length;
	const double Lift = FMath::Max(0.25 * Length, 2.0);
	for (int32 SubStep = 0; SubStep < 3; ++SubStep)
	{
		const FVector Tangent = FVector::VectorPlaneProject(Direction, PointNormal).GetSafeNormal();
		if (Tangent.IsZero())
		{
			return false;
		}

		// Along the tangent, a little off the surface. Blocked = the surface rises into the step faster than the lift (a tight
		// concave curve, a bump): climb onto it and carry on with what's left of the step from there.
		const FVector A = Point + PointNormal * Lift;
		const FVector B = A + Tangent * Remaining;
		FClimbSurfaceMapHit Hit;
		if (Trace(A, B, Hit))
		{
			if (!Accept(Hit))
			{
				return false;
			}
			Remaining -= FVector::Dist(Point, Hit.Point);
			Point = Hit.Point;
			PointNormal = Hit.Normal.GetSafeNormal();
			if (Remaining <= 0.5)
			{
				return Land(Point, PointNormal);
			}
			continue;
		}

		// Down onto the surface from the end of the step, deep enough for a convex turn up to ~90 degrees. A sharp edge's far
		// side runs parallel to this trace and is missed - the march stops at the edge.
		if (!Trace(B, B - PointNormal * (Lift + Remaining), Hit) || !Accept(Hit))
		{
			return false;
		}
		FVector Landing = Hit.Point;
		FVector LandingNormal = Hit.Normal.GetSafeNormal();

		// Keep the step's length ON THE SURFACE: past a convex turn the landing is farther than the step (the drop adds to the
		// chord) - pull it back along the chord and drop onto the surface again.
		const FVector Chord = Landing - Point;
		const double ChordLength = Chord.Size();
		if (ChordLength > Remaining * 1.05)
		{
			const FVector Mid = (PointNormal + LandingNormal).GetSafeNormal();
			const FVector Pulled = Point + Chord * (Remaining / ChordLength);
			FClimbSurfaceMapHit Fix;
			if (!Mid.IsZero() && Trace(Pulled + Mid * Lift, Pulled - Mid * (Lift + 0.5 * Remaining), Fix) && Accept(Fix))
			{
				Landing = Fix.Point;
				LandingNormal = Fix.Normal.GetSafeNormal();
			}
		}
		return Land(Landing, LandingNormal);
	}
	return false; // blocked three times: a step or a corner, not a curve
}

// ------------------------------------------------------------------------------------------------------------------------
// Queries

bool FClimbSurfaceMap::Sample(const FVector2D& UV, FVector& OutPoint, FVector& OutNormal, double* OutCoverage) const
{
	OutPoint = PlanePoint(UV);
	OutNormal = Normal;
	if (OutCoverage)
	{
		*OutCoverage = 0.0;
	}
	if (!bValid || Columns < 2 || Rows < 2 || Cells.Num() != Columns * Rows)
	{
		return false;
	}
	const FVector2D Step = Spacing();
	if (Step.X <= UE_KINDA_SMALL_NUMBER || Step.Y <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// Continuous cell coordinates. Past the grid, clamp to the edge and keep the overshoot: the point carries on along the
	// edge's tangent, so a hand crossing the edge goes on smoothly instead of losing its surface (which would make the reach
	// rule slide it back inside - a high hold pulled down to the top row).
	const double X = (UV.X - Min.X) / Step.X;
	const double Y = (UV.Y - Min.Y) / Step.Y;
	const double XC = FMath::Clamp(X, 0.0, static_cast<double>(Columns - 1));
	const double YC = FMath::Clamp(Y, 0.0, static_cast<double>(Rows - 1));
	const FVector2D Overshoot((X - XC) * Step.X, (Y - YC) * Step.Y);

	const int32 X0 = FMath::Clamp(FMath::FloorToInt32(XC), 0, Columns - 2);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt32(YC), 0, Rows - 2);
	const double TX = XC - X0;
	const double TY = YC - Y0;

	const int32 CornerX[4] = { X0, X0 + 1, X0, X0 + 1 };
	const int32 CornerY[4] = { Y0, Y0, Y0 + 1, Y0 + 1 };
	const double CornerW[4] = { (1.0 - TX) * (1.0 - TY), TX * (1.0 - TY), (1.0 - TX) * TY, TX * TY };
	FVector CornerPoint[4];
	FVector CornerNormal[4];
	bool CornerValid[4];
	int32 NumValid = 0;
	double SupportWeight = 0.0;  // coverage: the bilinear weight of corners that count as surface (valid or a one-cell hole)
	FVector ValidNormalSum = FVector::ZeroVector;
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		const FClimbSurfaceMapCell& C = Cell(CornerX[Corner], CornerY[Corner]);
		CornerValid[Corner] = C.bValid;
		if (C.bValid)
		{
			CornerPoint[Corner] = C.Point;
			CornerNormal[Corner] = C.Normal;
			ValidNormalSum += C.Normal;
			++NumValid;
		}
		if (C.bValid || IsCellSupported(CornerX[Corner], CornerY[Corner]))
		{
			SupportWeight += CornerW[Corner];
		}
	}
	if (NumValid == 0)
	{
		return false;
	}

	// A missing corner (a hole): extended from each valid corner along that corner's own tangents, averaged - so the
	// interpolated point stays at its (s, t) instead of being pulled toward the valid corners.
	if (NumValid < 4)
	{
		const FVector FillNormal = ValidNormalSum.GetSafeNormal();
		for (int32 Missing = 0; Missing < 4; ++Missing)
		{
			if (CornerValid[Missing])
			{
				continue;
			}
			FVector Estimate = FVector::ZeroVector;
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				if (!CornerValid[Corner])
				{
					continue;
				}
				FVector SurfaceRight;
				FVector SurfaceUp;
				TangentsAt(CornerNormal[Corner], SurfaceRight, SurfaceUp);
				Estimate += CornerPoint[Corner] + SurfaceRight * ((CornerX[Missing] - CornerX[Corner]) * Step.X)
					+ SurfaceUp * ((CornerY[Missing] - CornerY[Corner]) * Step.Y);
			}
			CornerPoint[Missing] = Estimate / NumValid;
			CornerNormal[Missing] = FillNormal.IsZero() ? Normal : FillNormal;
		}
	}

	FVector Point = FVector::ZeroVector;
	FVector NormalSum = FVector::ZeroVector;
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		Point += CornerPoint[Corner] * CornerW[Corner];
		NormalSum += CornerNormal[Corner] * CornerW[Corner];
	}
	OutNormal = NormalSum.GetSafeNormal();
	if (OutNormal.IsZero())
	{
		OutNormal = Normal;
	}
	if (!Overshoot.IsNearlyZero())
	{
		FVector SurfaceRight;
		FVector SurfaceUp;
		TangentsAt(OutNormal, SurfaceRight, SurfaceUp);
		Point += SurfaceRight * Overshoot.X + SurfaceUp * Overshoot.Y;
	}
	OutPoint = Point;
	if (OutCoverage)
	{
		*OutCoverage = FMath::Clamp(SupportWeight, 0.0, 1.0);
	}
	return true;
}

bool FClimbSurfaceMap::IsCellSupported(int32 Column, int32 Row) const
{
	if (Column < 0 || Column >= Columns || Row < 0 || Row >= Rows)
	{
		return false;
	}
	if (Cell(Column, Row).bValid)
	{
		return true;
	}
	auto IsValid = [this](int32 C, int32 R) { return C >= 0 && C < Columns && R >= 0 && R < Rows && Cell(C, R).bValid; };
	return (IsValid(Column - 1, Row) && IsValid(Column + 1, Row)) || (IsValid(Column, Row - 1) && IsValid(Column, Row + 1));
}

namespace
{
	// ON the surface = at least half the bilinear weight around the point is surface: the edge sits halfway between the last
	// valid cell and the first empty one.
	constexpr double OnSurfaceCoverage = 0.5;
}

bool FClimbSurfaceMap::IsOnSurface(const FVector2D& UV) const
{
	FVector Point;
	FVector SurfaceNormal;
	double Coverage = 0.0;
	return Sample(UV, Point, SurfaceNormal, &Coverage) && Coverage >= OnSurfaceCoverage;
}

double FClimbSurfaceMap::SnapWeight(double Distance) const
{
	if (Distance <= 0.0)
	{
		return 1.0;
	}
	if (SnapRadius <= UE_KINDA_SMALL_NUMBER)
	{
		return 0.0;
	}
	return 1.0 - FMath::SmoothStep(0.5 * SnapRadius, static_cast<double>(SnapRadius), Distance);
}

bool FClimbSurfaceMap::Locate(const FVector& World, FVector2D& OutUV) const
{
	if (!bValid || Cells.Num() != Columns * Rows)
	{
		return false;
	}
	int32 Nearest = INDEX_NONE;
	double NearestDistSq = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		if (Cells[Index].bValid)
		{
			const double DistSq = FVector::DistSquared(Cells[Index].Point, World);
			if (DistSq < NearestDistSq)
			{
				NearestDistSq = DistSq;
				Nearest = Index;
			}
		}
	}
	if (Nearest == INDEX_NONE)
	{
		return false;
	}

	// From the nearest cell, slide (s, t) by the offset seen on the local tangent plane until the sampled surface point sits
	// under World (the grid is spaced by distance along the surface, so one cm on the tangent plane is ~one cm of (s, t)).
	const FVector2D Step = Spacing();
	const FVector2D MaxMove = Step * 2.0;
	FVector2D UV = CellUV(Nearest % Columns, Nearest / Columns);
	for (int32 Iteration = 0; Iteration < 4; ++Iteration)
	{
		FVector Point;
		FVector SurfaceNormal;
		if (!Sample(UV, Point, SurfaceNormal))
		{
			break;
		}
		FVector SurfaceRight;
		FVector SurfaceUp;
		TangentsAt(SurfaceNormal, SurfaceRight, SurfaceUp);
		const FVector Delta = World - Point;
		const FVector2D Move(FMath::Clamp(FVector::DotProduct(Delta, SurfaceRight), -MaxMove.X, MaxMove.X),
			FMath::Clamp(FVector::DotProduct(Delta, SurfaceUp), -MaxMove.Y, MaxMove.Y));
		UV += Move;
		if (Move.SizeSquared() < 0.01)
		{
			break;
		}
	}
	OutUV = UV;
	return true;
}

bool FClimbSurfaceMap::WarpPointAt(const FVector2D& UV, double Height, FVector& OutWarped, FQuat& OutRotation, double* OutCoverage) const
{
	FVector SurfacePoint;
	FVector SurfaceNormal;
	if (!Sample(UV, SurfacePoint, SurfaceNormal, OutCoverage))
	{
		return false;
	}
	// The surface point at (s, t), then out along the LOCAL normal by the same height the flat pose has above the plane.
	OutWarped = SurfacePoint + SurfaceNormal * Height;
	OutRotation = FQuat::FindBetweenNormals(Normal, SurfaceNormal);
	return true;
}

namespace
{
	// Along one direction from Start: march out to the first spot that qualifies (a hole may sit between), then bisect back to
	// the edge. Returns that distance, or -1 when nothing within Limit qualifies.
	template <typename FQualifies>
	double DistanceToFirstQualifying(const FVector2D& Start, const FVector2D& Direction, double Limit, int32 MarchSteps,
		int32 RefineSteps, const FQualifies& Qualifies)
	{
		double Bad = 0.0;
		double Good = -1.0;
		for (int32 Step = 1; Step <= MarchSteps; ++Step)
		{
			const double Distance = Limit * Step / MarchSteps;
			if (Qualifies(Start + Direction * Distance))
			{
				Good = Distance;
				break;
			}
			Bad = Distance;
		}
		if (Good < 0.0)
		{
			return -1.0;
		}
		for (int32 Step = 0; Step < RefineSteps; ++Step)
		{
			const double Middle = 0.5 * (Bad + Good);
			if (Qualifies(Start + Direction * Middle))
			{
				Good = Middle;
			}
			else
			{
				Bad = Middle;
			}
		}
		return Good;
	}

	// The qualifying spot nearest Start in any direction, out to MaxDistance: a ring of directions (starting at StartAngle,
	// which wins ties), each searched only as far as the best so far; then the best direction's angle is refined so the result
	// moves smoothly as Start moves. Start itself is assumed NOT to qualify. Returns the distance, or -1 when nothing does.
	template <typename FQualifies>
	double NearestQualifying(const FVector2D& Start, double StartAngle, double MaxDistance, const FQualifies& Qualifies,
		FVector2D& OutUV)
	{
		constexpr int32 NumDirections = 16;
		constexpr int32 MarchSteps = 8;
		constexpr int32 RefineSteps = 6;
		constexpr int32 AngleRefineSteps = 3;
		auto DistanceAlong = [&](double Angle, double Limit)
		{
			return DistanceToFirstQualifying(Start, FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)), Limit, MarchSteps, RefineSteps, Qualifies);
		};
		double BestDistance = -1.0;
		double BestAngle = StartAngle;
		for (int32 Index = 0; Index < NumDirections; ++Index)
		{
			const double Angle = StartAngle + UE_TWO_PI * Index / NumDirections;
			const double Distance = DistanceAlong(Angle, BestDistance > 0.0 ? BestDistance : MaxDistance);
			if (Distance >= 0.0 && (BestDistance < 0.0 || Distance < BestDistance))
			{
				BestDistance = Distance;
				BestAngle = Angle;
			}
		}
		if (BestDistance < 0.0)
		{
			return -1.0;
		}
		double AngleStep = UE_PI / NumDirections;
		for (int32 Step = 0; Step < AngleRefineSteps; ++Step, AngleStep *= 0.5)
		{
			for (const double Sign : { -1.0, 1.0 })
			{
				const double Angle = BestAngle + Sign * AngleStep;
				const double Distance = DistanceAlong(Angle, BestDistance);
				if (Distance >= 0.0 && Distance < BestDistance)
				{
					BestDistance = Distance;
					BestAngle = Angle;
				}
			}
		}
		OutUV = Start + FVector2D(FMath::Cos(BestAngle), FMath::Sin(BestAngle)) * BestDistance;
		return BestDistance;
	}
}

bool FClimbSurfaceMap::FindSurface(const FVector2D& UV, FVector2D& OutUV, double& OutDistance) const
{
	if (IsOnSurface(UV))
	{
		OutUV = UV;
		OutDistance = 0.0;
		return true;
	}
	if (SnapRadius <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}
	// Past the wall's edge (or over a hole): the nearest surface within the snap radius. The ring starts toward the body
	// ((0, 0)), where the wall usually is from a dangling foot.
	const double StartAngle = UV.IsNearlyZero() ? 0.0 : FMath::Atan2(-UV.Y, -UV.X);
	const double Distance = NearestQualifying(UV, StartAngle, SnapRadius, [this](const FVector2D& Candidate) { return IsOnSurface(Candidate); }, OutUV);
	if (Distance < 0.0)
	{
		return false;
	}
	OutDistance = Distance;
	return true;
}

bool FClimbSurfaceMap::WarpSnapped(const FVector& Flat, const FVector* Root, double MaxReach, FVector& OutWarped, FQuat& OutRotation,
	bool* OutReachLimited, double* OutSurfaceWeight) const
{
	if (OutReachLimited)
	{
		*OutReachLimited = false;
	}
	if (OutSurfaceWeight)
	{
		*OutSurfaceWeight = 0.0;
	}
	const FVector2D TargetUV = ToUV(Flat);
	const double Height = HeightAbovePlane(Flat);

	// 1) SNAP: the surface for this point - under it, or the nearest within SnapRadius. None = over a void: no warp.
	FVector2D SurfaceUV;
	double SnapDistance = 0.0;
	if (!FindSurface(TargetUV, SurfaceUV, SnapDistance))
	{
		return false;
	}
	const double Weight = SnapWeight(SnapDistance);
	if (Weight <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// 2) REACH (optional): the nearest on-surface spot in reach of the root - not "straight toward the root", which also
	// pulls a hold above the shoulder DOWN even when the extra distance came from a sideways curve. Searched from the snapped
	// spot, only across surface: it undoes distance the warp added, it never hunts for surface.
	FVector2D UV = SurfaceUV;
	if (Root)
	{
		FVector2D InReachUV;
		if (FindNearestInReach(SurfaceUV, Height, *Root, MaxReach, FMath::Max(MaxReach, 1.0), InReachUV))
		{
			UV = InReachUV;
		}
		if (OutReachLimited)
		{
			*OutReachLimited = UV != SurfaceUV;
		}
	}

	FVector Warped;
	FQuat Rotation;
	if (!WarpPointAt(UV, Height, Warped, Rotation))
	{
		return false;
	}

	// 3) FADE: blend back toward the flat pose as the surface gets farther (full up to half the snap radius, none at it). The
	// flat point is in reach (callers' MaxReach covers it) and so is the warped one, so the blend is too.
	OutWarped = FMath::Lerp(Flat, Warped, Weight);
	OutRotation = FQuat::Slerp(FQuat::Identity, Rotation, Weight).GetNormalized();
	if (OutSurfaceWeight)
	{
		*OutSurfaceWeight = Weight;
	}
	return true;
}

bool FClimbSurfaceMap::WarpPoint(const FVector& Flat, FVector& OutWarped, FQuat& OutRotation, double* OutSurfaceWeight) const
{
	return WarpSnapped(Flat, nullptr, 0.0, OutWarped, OutRotation, nullptr, OutSurfaceWeight);
}

bool FClimbSurfaceMap::WarpPointWithinReach(const FVector& Flat, const FVector& Root, double MaxReach, FVector& OutWarped,
	FQuat& OutRotation, bool* OutReachLimited, double* OutSurfaceWeight) const
{
	return WarpSnapped(Flat, &Root, MaxReach, OutWarped, OutRotation, OutReachLimited, OutSurfaceWeight);
}

bool FClimbSurfaceMap::FindNearestInReach(const FVector2D& StartUV, double Height, const FVector& Root, double MaxReach,
	double MaxDistance, FVector2D& OutUV) const
{
	auto Qualifies = [&](const FVector2D& UV)
	{
		FVector Point;
		FQuat Rotation;
		double Coverage = 0.0;
		return WarpPointAt(UV, Height, Point, Rotation, &Coverage) && Coverage >= OnSurfaceCoverage
			&& FVector::DistSquared(Point, Root) <= FMath::Square(MaxReach);
	};
	if (Qualifies(StartUV))
	{
		OutUV = StartUV;
		return true;
	}
	if (MaxDistance <= 0.0)
	{
		return false;
	}
	// The ring starts toward the root, which wins ties.
	const FVector2D ToRoot = ToUV(Root) - StartUV;
	const double StartAngle = ToRoot.IsNearlyZero() ? 0.0 : FMath::Atan2(ToRoot.Y, ToRoot.X);
	return NearestQualifying(StartUV, StartAngle, MaxDistance, Qualifies, OutUV) >= 0.0;
}

bool FClimbSurfaceMap::FindAlongLineInReach(const FVector2D& StartUV, const FVector2D& Direction, double Height,
	const FVector& Root, double MaxReach, double MaxDistance, FVector2D& OutUV) const
{
	auto Qualifies = [&](const FVector2D& UV)
	{
		FVector Point;
		FQuat Rotation;
		double Coverage = 0.0;
		return WarpPointAt(UV, Height, Point, Rotation, &Coverage) && Coverage >= OnSurfaceCoverage
			&& FVector::DistSquared(Point, Root) <= FMath::Square(MaxReach);
	};
	if (Qualifies(StartUV))
	{
		OutUV = StartUV;
		return true;
	}
	const FVector2D Unit = Direction.GetSafeNormal();
	if (Unit.IsNearlyZero() || MaxDistance <= 0.0)
	{
		return false;
	}

	// Both ways along the line, the second searched only as far as the first's answer. Finer than the ring (a single line, and
	// the leash moves by fractions of a cm per tick): 16 march steps, then 8 bisections.
	constexpr int32 MarchSteps = 16;
	constexpr int32 RefineSteps = 8;
	double BestDistance = -1.0;
	FVector2D BestDirection = Unit;
	for (const double Sign : { 1.0, -1.0 })
	{
		const double Distance = DistanceToFirstQualifying(StartUV, Unit * Sign, BestDistance > 0.0 ? BestDistance : MaxDistance,
			MarchSteps, RefineSteps, Qualifies);
		if (Distance >= 0.0 && (BestDistance < 0.0 || Distance < BestDistance))
		{
			BestDistance = Distance;
			BestDirection = Unit * Sign;
		}
	}
	if (BestDistance < 0.0)
	{
		return false;
	}
	OutUV = StartUV + BestDirection * BestDistance;
	return true;
}

FClimbSurfaceMap FClimbSurfaceMap::Transformed(const FTransform& Transform) const
{
	FClimbSurfaceMap Out = *this;
	const double Scale = Transform.GetMaximumAxisScale();
	Out.Origin = Transform.TransformPosition(Origin);
	Out.Right = Transform.TransformVectorNoScale(Right);
	Out.Up = Transform.TransformVectorNoScale(Up);
	Out.Normal = Transform.TransformVectorNoScale(Normal);
	Out.Min = Min * Scale;
	Out.Max = Max * Scale;
	Out.SnapRadius = static_cast<float>(SnapRadius * Scale);
	for (FClimbSurfaceMapCell& Cell : Out.Cells)
	{
		Cell.Point = Transform.TransformPosition(Cell.Point);
		Cell.Normal = Transform.TransformVectorNoScale(Cell.Normal);
	}
	return Out;
}
