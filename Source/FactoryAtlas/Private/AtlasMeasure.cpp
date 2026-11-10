#include "AtlasSubsystem.h"

#include "FactoryAtlas.h"

using namespace Atlas;

/**
 * The measure tool. Everything is in map metres: X east and Y south from the map's north-west corner, which is
 * all a length or an area needs. A box snaps to the grid of the foundation it was started on, 8 m a step and
 * turned with it, or to a 1 m world grid when there is no foundation there. Heights, floors and which copies are
 * blocked are worked out again only when the boxes, the buildings or what the map shows change.
 */
namespace AtlasMeasureDetail
{
	constexpr float FoundationPitch = 8.f;
	constexpr float BlockMarginMetres = 0.5f;
	constexpr float DragPixels = 4.f;
	constexpr int32 MaxCopies = 16;
	constexpr int32 MaxHits = 4096;

	FVector2f Rotate(const FVector2f& V, float Sin, float Cos)
	{
		return FVector2f(V.X * Cos - V.Y * Sin, V.X * Sin + V.Y * Cos);
	}

	FVector2f RecordMetres(const FDecoded& Record)
	{
		return FVector2f(float(Record.X) / PositionMax, float(Record.Y) / PositionMax) * MapWidthMetres;
	}

	float RecordAngle(const FDecoded& Record)
	{
		return Record.Angle / 1024.f * UE_TWO_PI;
	}

	int32 CellCoord(float Metres)
	{
		return FMath::Clamp(int32(Metres / MapWidthMetres * GridSize), 0, GridSize - 1);
	}

	/** Separating axis test for two rectangles, each a centre, half size and angle. */
	bool Overlaps(const FVector2f& CenterA, const FVector2f& HalfA, float AngleA, const FVector2f& CenterB, const FVector2f& HalfB, float AngleB)
	{
		float SinA, CosA, SinB, CosB;
		FMath::SinCos(&SinA, &CosA, AngleA);
		FMath::SinCos(&SinB, &CosB, AngleB);
		const FVector2f AxA(CosA, SinA), AyA(-SinA, CosA), AxB(CosB, SinB), AyB(-SinB, CosB);
		const FVector2f Delta = CenterB - CenterA;
		const FVector2f Axes[4] = { AxA, AyA, AxB, AyB };
		for (const FVector2f& Axis : Axes)
		{
			const float ReachA = HalfA.X * FMath::Abs(AxA | Axis) + HalfA.Y * FMath::Abs(AyA | Axis);
			const float ReachB = HalfB.X * FMath::Abs(AxB | Axis) + HalfB.Y * FMath::Abs(AyB | Axis);
			if (FMath::Abs(Delta | Axis) > ReachA + ReachB)
			{
				return false;
			}
		}
		return true;
	}
}
using namespace AtlasMeasureDetail;

FVector2f FMeasureBox::ToMap(const FVector2f& Local) const
{
	float Sin, Cos;
	FMath::SinCos(&Sin, &Cos, Angle);
	return Origin + Rotate(Local, Sin, Cos);
}

FVector2f FMeasureBox::ToLocal(const FVector2f& Map) const
{
	float Sin, Cos;
	FMath::SinCos(&Sin, &Cos, Angle);
	return Rotate(Map - Origin, -Sin, Cos);
}

FVector2f FMeasureBox::CopyOffset(int32 Index) const
{
	if (Index == 0)
	{
		return FVector2f::ZeroVector;
	}
	// Copies run along whichever of the box's own sides points most nearly the way chosen on screen.
	static const FVector2f Screen[] = { { 1.f, 0.f }, { 0.f, 1.f }, { -1.f, 0.f }, { 0.f, -1.f } };
	const FVector2f Wanted = Screen[FMath::Min(int32(Direction), 3)];
	float Sin, Cos;
	FMath::SinCos(&Sin, &Cos, Angle);
	const FVector2f AxisX(Cos, Sin);
	const FVector2f AxisY(-Sin, Cos);
	const float AlongX = AxisX | Wanted;
	const float AlongY = AxisY | Wanted;
	const FVector2f Span = Size();
	if (FMath::Abs(AlongX) >= FMath::Abs(AlongY))
	{
		return FVector2f(FMath::Sign(AlongX) * Span.X * Index, 0.f);
	}
	return FVector2f(0.f, FMath::Sign(AlongY) * Span.Y * Index);
}

void FMeasureBox::Corners(int32 Index, FVector2f Out[4]) const
{
	const FVector2f Shift = CopyOffset(Index);
	Out[0] = ToMap(Min + Shift);
	Out[1] = ToMap(FVector2f(Max.X, Min.Y) + Shift);
	Out[2] = ToMap(Max + Shift);
	Out[3] = ToMap(FVector2f(Min.X, Max.Y) + Shift);
}

bool UAtlasSubsystem::IsRecordShown(const FDecoded& Record) const
{
	if (!Types.IsValidIndex(Record.Type) || Types[Record.Type].bHidden || IsCategoryHidden(Types[Record.Type].Category))
	{
		return false;
	}
	return Record.Z >= MetresToZ(FilterLow) && Record.Z <= MetresToZ(FilterHigh);
}

bool UAtlasSubsystem::FindFoundationAt(const FVector2f& Metres, FVector4f& Out) const
{
	const FGrid* Current = GetGrid();
	if (!Current)
	{
		return false;
	}
	const int32 CenterX = CellCoord(Metres.X);
	const int32 CenterY = CellCoord(Metres.Y);
	int32 BestZ = -1;
	for (int32 CellY = FMath::Max(0, CenterY - 1); CellY <= FMath::Min(GridSize - 1, CenterY + 1); CellY++)
	{
		for (int32 CellX = FMath::Max(0, CenterX - 1); CellX <= FMath::Min(GridSize - 1, CenterX + 1); CellX++)
		{
			const TArray<FVector4f>& Cell = Current->Cells[CellY * GridSize + CellX];
			for (int32 i = Cell.Num() - 1; i >= 0; i--)
			{
				const int32 Z = DecodeZ(Cell[i]);
				if (Z <= BestZ)
				{
					break; // Cells are sorted by height
				}
				const FDecoded Record(Cell[i]);
				if (!Types.IsValidIndex(Record.Type) || Types[Record.Type].Category != ECategory::Foundations || !IsRecordShown(Record))
				{
					continue;
				}
				float Sin, Cos;
				FMath::SinCos(&Sin, &Cos, RecordAngle(Record));
				const FVector2f Local = Rotate(Metres - RecordMetres(Record), -Sin, Cos);
				if (FMath::Abs(Local.X) <= Record.HalfX * 0.1f && FMath::Abs(Local.Y) <= Record.HalfY * 0.1f)
				{
					BestZ = Z;
					Out = Cell[i];
					break;
				}
			}
		}
	}
	return BestZ >= 0;
}

void UAtlasSubsystem::SnapDraft(FMeasureView& State, const FVector2f& Metres) const
{
	FMeasureBox& Box = State.Draft;
	const auto Snap = [&Box](const FVector2f& Local)
	{
		return FVector2f(FMath::RoundToFloat(Local.X / Box.Pitch) * Box.Pitch, FMath::RoundToFloat(Local.Y / Box.Pitch) * Box.Pitch);
	};
	const FVector2f Start = Snap(Box.ToLocal(State.PressMetres));
	const FVector2f End = Snap(Box.ToLocal(Metres));
	Box.Min = FVector2f(FMath::Min(Start.X, End.X), FMath::Min(Start.Y, End.Y));
	Box.Max = FVector2f(FMath::Max(Start.X, End.X), FMath::Max(Start.Y, End.Y));
}

void UAtlasSubsystem::SetMeasuring(EView View, bool bOn)
{
	FMeasureView& State = Measure[int32(View)];
	State.bMode = bOn;
	if (bOn)
	{
		SetWiring(View, false); // One mode at a time
	}
	if (!bOn)
	{
		State.bPressed = false;
	}
}

void UAtlasSubsystem::PressMeasure(EView View, bool bLine)
{
	FMeasureView& State = Measure[int32(View)];
	if (!State.bMode)
	{
		return;
	}
	const FVector2f Point = CursorMetres(View);
	State.bPressed = true;
	State.bLineDrag = bLine;
	State.bMoved = false;
	State.PressMetres = Point;
	State.DraftLine = { Point, Point };

	FMeasureBox Box;
	FVector4f Foundation;
	if (!bLine && FindFoundationAt(Point, Foundation))
	{
		// The foundation's own corner is a grid point, so the box lands on whole foundations around it.
		const FDecoded Record(Foundation);
		Box.Angle = RecordAngle(Record);
		Box.Pitch = FoundationPitch;
		Box.bFoundationGrid = true;
		float Sin, Cos;
		FMath::SinCos(&Sin, &Cos, Box.Angle);
		Box.Origin = RecordMetres(Record) - Rotate(FVector2f(Record.HalfX * 0.1f, Record.HalfY * 0.1f), Sin, Cos);
	}
	State.Draft = Box;
	SnapDraft(State, Point);
}

void UAtlasSubsystem::MoveMeasure(EView View)
{
	FMeasureView& State = Measure[int32(View)];
	if (!State.bPressed)
	{
		return;
	}
	const FVector2f Point = CursorMetres(View);
	if (!State.bMoved && FVector2f::Distance(Point, State.PressMetres) * MapCursorPixelsPerMetre[int32(View)] > DragPixels)
	{
		State.bMoved = true;
	}
	if (State.bLineDrag)
	{
		State.DraftLine.B = Point;
	}
	else
	{
		SnapDraft(State, Point);
	}
}

void UAtlasSubsystem::ReleaseMeasure(EView View)
{
	FMeasureView& State = Measure[int32(View)];
	if (!State.bPressed)
	{
		return;
	}
	MoveMeasure(View);
	State.bPressed = false;

	if (!State.bMoved)
	{
		// A click: the box under the cursor becomes the one the controls work on.
		const FVector2f Point = CursorMetres(View);
		for (int32 i = State.Boxes.Num() - 1; i >= 0; i--)
		{
			const FMeasureBox& Box = State.Boxes[i];
			const FVector2f Local = Box.ToLocal(Point);
			if (Local.X >= Box.Min.X && Local.X <= Box.Max.X && Local.Y >= Box.Min.Y && Local.Y <= Box.Max.Y)
			{
				State.Active = i;
				return;
			}
		}
		return;
	}

	if (State.bLineDrag)
	{
		if (FVector2f::Distance(State.DraftLine.A, State.DraftLine.B) >= 0.5f)
		{
			State.Lines.Add(State.DraftLine);
		}
		return;
	}
	const FVector2f Span = State.Draft.Size();
	if (Span.X > 0.f && Span.Y > 0.f)
	{
		State.Boxes.Add(State.Draft);
		State.Active = State.Boxes.Num() - 1;
		State.StatsKey = 0;
	}
}

void UAtlasSubsystem::CancelMeasureDrag(EView View)
{
	Measure[int32(View)].bPressed = false;
}

void UAtlasSubsystem::SetMeasureCopies(EView View, int32 Copies)
{
	FMeasureView& State = Measure[int32(View)];
	if (State.Boxes.IsValidIndex(State.Active))
	{
		State.Boxes[State.Active].Copies = FMath::Clamp(Copies, 0, MaxCopies);
		State.StatsKey = 0;
	}
}

void UAtlasSubsystem::SetMeasureDirection(EView View, EMeasureDirection Direction)
{
	FMeasureView& State = Measure[int32(View)];
	if (State.Boxes.IsValidIndex(State.Active))
	{
		State.Boxes[State.Active].Direction = Direction;
		State.StatsKey = 0;
	}
}

void UAtlasSubsystem::RemoveActiveMeasure(EView View)
{
	FMeasureView& State = Measure[int32(View)];
	if (State.Boxes.IsValidIndex(State.Active))
	{
		State.Boxes.RemoveAt(State.Active);
		State.Active = State.Boxes.Num() - 1;
		State.StatsKey = 0;
	}
}

void UAtlasSubsystem::ClearMeasures(EView View)
{
	FMeasureView& State = Measure[int32(View)];
	State.Boxes.Reset();
	State.Lines.Reset();
	State.Active = INDEX_NONE;
	State.bPressed = false;
	State.StatsKey = 0;
}

void UAtlasSubsystem::UpdateMeasureStats(EView View)
{
	FMeasureView& State = Measure[int32(View)];
	const FGrid* Current = GetGrid();
	if (!Current)
	{
		return; // A worker owns the grid; try again next frame
	}

	uint32 Hash = HashCombine(GetTypeHash(GridChanges), GetTypeHash(PaletteBuilds));
	Hash = HashCombine(Hash, HashCombine(GetTypeHash(FilterLow), GetTypeHash(FilterHigh)));
	for (const FMeasureBox& Box : State.Boxes)
	{
		Hash = HashCombine(Hash, HashCombine(GetTypeHash(Box.Origin), GetTypeHash(Box.Angle)));
		Hash = HashCombine(Hash, HashCombine(GetTypeHash(Box.Min), GetTypeHash(Box.Max)));
		Hash = HashCombine(Hash, HashCombine(GetTypeHash(Box.Copies), GetTypeHash(int32(Box.Direction))));
	}
	const uint64 Key = (uint64(Hash) << 8) | uint64(State.Boxes.Num() & 0xff) | (1ull << 63);
	if (Key == State.StatsKey)
	{
		return;
	}
	State.StatsKey = Key;
	const double Start = FPlatformTime::Seconds();

	State.Hits.Reset();
	TSet<FVector4f> HitSet;
	for (FMeasureBox& Box : State.Boxes)
	{
		// Heights and floors of what stands in the box itself.
		FVector2f Corners[4];
		Box.Corners(0, Corners);
		FVector2f Low = Corners[0], High = Corners[0];
		for (const FVector2f& Corner : Corners)
		{
			Low = FVector2f::Min(Low, Corner);
			High = FVector2f::Max(High, Corner);
		}
		int32 ZLow = MAX_int32, ZHigh = MIN_int32;
		TSet<int32> FloorHeights;
		for (int32 CellY = CellCoord(Low.Y); CellY <= CellCoord(High.Y); CellY++)
		{
			for (int32 CellX = CellCoord(Low.X); CellX <= CellCoord(High.X); CellX++)
			{
				for (const FVector4f& Packed : Current->Cells[CellY * GridSize + CellX])
				{
					const FDecoded Record(Packed);
					const FVector2f Local = Box.ToLocal(RecordMetres(Record));
					if (Local.X < Box.Min.X || Local.X > Box.Max.X || Local.Y < Box.Min.Y || Local.Y > Box.Max.Y || !IsRecordShown(Record))
					{
						continue;
					}
					ZLow = FMath::Min(ZLow, Record.Z);
					ZHigh = FMath::Max(ZHigh, Record.Z);
					if (Types[Record.Type].Category == ECategory::Foundations)
					{
						FloorHeights.Add(FMath::RoundToInt32(ZToMetres(Record.Z)));
					}
				}
			}
		}
		Box.bHasHeight = ZLow <= ZHigh;
		Box.LowMetres = Box.bHasHeight ? ZToMetres(ZLow) : 0.f;
		Box.HighMetres = Box.bHasHeight ? ZToMetres(ZHigh) : 0.f;
		Box.Floors = FloorHeights.Num();

		// Each copy: blocked when a building on the map overlaps it by more than the margin.
		Box.Blocked.SetNum(Box.Copies);
		const FVector2f Half = FVector2f::Max(Box.Size() * 0.5f - FVector2f(BlockMarginMetres), FVector2f(0.05f));
		for (int32 Copy = 1; Copy <= Box.Copies; Copy++)
		{
			bool& bBlocked = Box.Blocked[Copy - 1];
			bBlocked = false;
			const FVector2f Center = Box.ToMap((Box.Min + Box.Max) * 0.5f + Box.CopyOffset(Copy));
			Box.Corners(Copy, Corners);
			Low = Corners[0];
			High = Corners[0];
			for (const FVector2f& Corner : Corners)
			{
				Low = FVector2f::Min(Low, Corner);
				High = FVector2f::Max(High, Corner);
			}
			// One cell more each way: a long building is filed under its centre and can reach into this one.
			for (int32 CellY = FMath::Max(0, CellCoord(Low.Y) - 1); CellY <= FMath::Min(GridSize - 1, CellCoord(High.Y) + 1); CellY++)
			{
				for (int32 CellX = FMath::Max(0, CellCoord(Low.X) - 1); CellX <= FMath::Min(GridSize - 1, CellCoord(High.X) + 1); CellX++)
				{
					for (const FVector4f& Packed : Current->Cells[CellY * GridSize + CellX])
					{
						const FDecoded Record(Packed);
						const FVector2f RecordHalf(Record.HalfX * 0.1f, Record.HalfY * 0.1f);
						const FVector2f RecordCenter = RecordMetres(Record);
						// Cheap reject before the exact test.
						const float Reach = RecordHalf.X + RecordHalf.Y;
						if (RecordCenter.X + Reach < Low.X || RecordCenter.X - Reach > High.X || RecordCenter.Y + Reach < Low.Y || RecordCenter.Y - Reach > High.Y)
						{
							continue;
						}
						if (!IsRecordShown(Record) || !Overlaps(Center, Half, Box.Angle, RecordCenter, RecordHalf, RecordAngle(Record)))
						{
							continue;
						}
						bBlocked = true;
						if (HitSet.Num() < MaxHits)
						{
							const FVector4f Hit = Encode(Record.X, Record.Y, Record.Z, Record.HalfX, Record.HalfY, Record.Angle, BlockedType, 0);
							bool bAlready = false;
							HitSet.Add(Hit, &bAlready);
							if (!bAlready)
							{
								State.Hits.Add(Hit);
							}
						}
					}
				}
			}
		}
	}
	State.HitsVersion++;

	const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0;
	if (Ms > 2.0)
	{
		UE_LOG(LogFactoryAtlas, Display, TEXT("Measure: %d boxes, %d buildings in the way, %.2f ms"), State.Boxes.Num(), State.Hits.Num(), Ms);
	}
}
