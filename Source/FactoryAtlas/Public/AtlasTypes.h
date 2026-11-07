#pragma once

#include "CoreMinimal.h"

/**
 * One shape on the map is four floats. Every field below is an integer that fits in 24 bits, so a float
 * carries it exactly and the material can read it back with power-of-two floor and fmod:
 *
 *   X    x:17 zHigh:7       map position, about 5.7 cm a step, and the top of the height
 *   Y    y:17 zLow:7        height: 14 bits at 25 cm a step, starting at -200 m
 *   Z    halfX:11 halfY:9 paintHigh:4    half size in decimetres, the long side is always X
 *   W    angle:10 type:10 paintLow:4     paint is an index into the paint palette, 0 means unpainted
 *
 * A record depends only on the building it came from, which is why removal can rebuild the record and look
 * for it instead of keeping per-building handles. Paint bits are ignored in that comparison.
 */
namespace Atlas
{
	// Same bounds Cartograph worked out from SCIM.
	constexpr double MapWestCm = -324698.832031;
	constexpr double MapNorthCm = -375000.0;
	constexpr double MapWidthCm = 750000.664062;
	constexpr double MapHeightCm = 750000.0;
	constexpr float MapWidthMetres = 7500.f;

	constexpr int32 GridSize = 32;
	constexpr int32 GridCells = GridSize * GridSize;

	constexpr int32 PositionMax = 131071;
	constexpr double ZBaseCm = 20000.0;
	constexpr double ZStepCm = 25.0;
	constexpr int32 ZMaxValue = 16383;
	constexpr int32 HalfXMax = 2047;
	constexpr int32 HalfYMax = 511;

	constexpr int32 MaxTypes = 1024;
	constexpr int32 HighlightType = 1023; // Always drawn, in the accent colour
	constexpr int32 RadiationType = 1021; // A soft circle, sizes in 40 cm steps instead of 10 cm
	// Markers are dots whose size is in pixels. They ignore the height filter and paint mode, and their
	// paint bits carry flags instead: bit 0 draws the dot hollow (an occupied node).
	constexpr int32 AnyMarkerFirst = 981;
	constexpr int32 ResourceFirst = 990;  // One per resource and node kind, handed out as they are found
	constexpr int32 ResourceLast = 1009;
	constexpr int32 SporeFlowerType = 1010;
	constexpr int32 GasPillarType = 1011;
	constexpr int32 MarkerFirst = 1012;   // Collectibles, 1012 to 1017
	constexpr int32 MarkerLast = 1017;
	constexpr int32 OverflowType = 980;   // Shared once a game has more building types than this

	enum class EFeature : uint8 { ResourceNodes, ResourceWells, SporeFlowers, GasPillars, Count };

	inline const TCHAR* FeatureName(EFeature Feature)
	{
		static const TCHAR* Names[] = { TEXT("Resource nodes"), TEXT("Resource wells"), TEXT("Spore flowers"), TEXT("Gas pillars") };
		return Names[int32(Feature)];
	}

	/** Colours for resources by descriptor name; anything else gets a colour from its name. */
	inline FColor ResourceColor(const FString& DescriptorName)
	{
		static const TPair<const TCHAR*, FColor> Known[] = {
			{ TEXT("OreIron"), FColor(180, 120, 110) }, { TEXT("OreCopper"), FColor(230, 120, 60) }, { TEXT("Stone"), FColor(220, 210, 180) },
			{ TEXT("Coal"), FColor(70, 70, 80) }, { TEXT("OreGold"), FColor(240, 200, 60) }, { TEXT("RawQuartz"), FColor(240, 140, 200) },
			{ TEXT("Sulfur"), FColor(230, 230, 60) }, { TEXT("OreBauxite"), FColor(200, 90, 70) }, { TEXT("OreUranium"), FColor(90, 230, 60) },
			{ TEXT("SAM"), FColor(160, 70, 230) }, { TEXT("LiquidOil"), FColor(40, 30, 50) }, { TEXT("NitrogenGas"), FColor(170, 200, 230) },
			{ TEXT("Water"), FColor(60, 140, 240) }, { TEXT("Geyser"), FColor(240, 90, 40) },
		};
		for (const TPair<const TCHAR*, FColor>& Entry : Known)
		{
			if (DescriptorName.Contains(Entry.Key))
			{
				return Entry.Value;
			}
		}
		const uint32 Hash = GetTypeHash(DescriptorName);
		return FColor(uint8(80 + Hash % 160), uint8(80 + (Hash >> 8) % 160), uint8(80 + (Hash >> 16) % 160));
	}
	constexpr float RadiationSizeScale = 4.f;
	constexpr int32 MarkerRadiusPixels = 6;

	enum class ECollectible : uint8 { Somersloop, MercerSphere, HardDrive, PowerSlug, Count };

	/** Marker types: one per collectible, plus two extra slug tiers. */
	enum EMarker : int32
	{
		MarkerSomersloop = MarkerFirst, MarkerMercerSphere, MarkerHardDrive, MarkerSlugBlue, MarkerSlugYellow, MarkerSlugPurple
	};

	inline const TCHAR* CollectibleName(ECollectible Kind)
	{
		static const TCHAR* Names[] = { TEXT("Somersloops"), TEXT("Mercer Spheres"), TEXT("Hard Drives"), TEXT("Power Slugs") };
		return Names[int32(Kind)];
	}

	inline ECollectible CollectibleOfMarker(int32 Marker)
	{
		return Marker >= MarkerSlugBlue ? ECollectible::PowerSlug : ECollectible(Marker - MarkerFirst);
	}

	inline const TCHAR* MarkerName(int32 Marker)
	{
		static const TCHAR* Names[] = { TEXT("Somersloop"), TEXT("Mercer Sphere"), TEXT("Hard Drive"), TEXT("Blue Power Slug"), TEXT("Yellow Power Slug"), TEXT("Purple Power Slug") };
		return Names[Marker - MarkerFirst];
	}

	inline FColor MarkerColor(int32 Marker)
	{
		static const FColor Colors[] = { FColor(235, 70, 70), FColor(170, 90, 255), FColor(255, 214, 60), FColor(70, 170, 255), FColor(255, 220, 70), FColor(200, 90, 255) };
		return Colors[Marker - MarkerFirst];
	}
	constexpr int32 MaxPaints = 256;

	enum class ECategory : uint8
	{
		Foundations, Walls, Roofs, Structures, Belts, Lifts, Pipes, Hypertubes, Rails, Paths,
		Production, Extraction, Generators, PowerLines, Logistics, Storage, Stations, Other,
		Count
	};

	inline const TCHAR* CategoryName(ECategory Category)
	{
		static const TCHAR* Names[] = {
			TEXT("Foundations"), TEXT("Walls"), TEXT("Roofs"), TEXT("Structures"), TEXT("Conveyor Belts"), TEXT("Conveyor Lifts"),
			TEXT("Pipelines"), TEXT("Hypertubes"), TEXT("Railways"), TEXT("Roads and Paths"), TEXT("Production"), TEXT("Extraction"),
			TEXT("Power Generation"), TEXT("Power Lines"), TEXT("Logistics"), TEXT("Storage"), TEXT("Stations"), TEXT("Other"),
		};
		return Names[int32(Category)];
	}

	inline FColor CategoryColor(ECategory Category)
	{
		static const FColor Colors[] = {
			FColor(92, 98, 108), FColor(122, 128, 138), FColor(108, 112, 124), FColor(140, 138, 130), FColor(255, 158, 41), FColor(255, 204, 76),
			FColor(51, 184, 242), FColor(184, 107, 255), FColor(184, 173, 153), FColor(150, 150, 165), FColor(71, 217, 122), FColor(242, 89, 89),
			FColor(255, 219, 51), FColor(255, 242, 140), FColor(242, 140, 140), FColor(214, 150, 95), FColor(64, 230, 217), FColor(204, 204, 204),
		};
		return Colors[int32(Category)];
	}

	inline FVector4f Encode(int32 X, int32 Y, int32 Z, int32 HalfX, int32 HalfY, int32 Angle, int32 Type, int32 Paint)
	{
		return FVector4f(
			float((X << 7) | (Z >> 7)),
			float((Y << 7) | (Z & 127)),
			float((HalfX << 13) | (HalfY << 4) | (Paint >> 4)),
			float((Angle << 14) | (Type << 4) | (Paint & 15)));
	}

	/** Returns false when the point is off the map. */
	inline bool EncodeWorld(const FVector& Center, float HalfXCm, float HalfYCm, float Radians, int32 Type, int32 Paint, FVector4f& Out)
	{
		const double U = (Center.X - MapWestCm) / MapWidthCm;
		const double V = (Center.Y - MapNorthCm) / MapHeightCm;
		if (U < 0.0 || U > 1.0 || V < 0.0 || V > 1.0)
		{
			return false;
		}

		if (HalfYCm > HalfXCm)
		{
			Swap(HalfXCm, HalfYCm);
			Radians += UE_HALF_PI;
		}

		float Turns = FMath::Fmod(Radians / UE_TWO_PI, 1.f);
		if (Turns < 0.f)
		{
			Turns += 1.f;
		}

		Out = Encode(
			FMath::RoundToInt32(U * PositionMax),
			FMath::RoundToInt32(V * PositionMax),
			FMath::Clamp(FMath::RoundToInt32((Center.Z + ZBaseCm) / ZStepCm), 0, ZMaxValue),
			FMath::Clamp(FMath::RoundToInt32(HalfXCm / 10.f), 1, HalfXMax),
			FMath::Clamp(FMath::RoundToInt32(HalfYCm / 10.f), 1, HalfYMax),
			FMath::Min(FMath::FloorToInt32(Turns * 1024.f), 1023),
			Type,
			Paint);
		return true;
	}

	struct FDecoded
	{
		int32 X, Y, Z, HalfX, HalfY, Angle, Type, Paint;

		explicit FDecoded(const FVector4f& R)
		{
			const int32 A = int32(R.X), B = int32(R.Y), C = int32(R.Z), D = int32(R.W);
			X = A >> 7;
			Y = B >> 7;
			Z = ((A & 127) << 7) | (B & 127);
			HalfX = C >> 13;
			HalfY = (C >> 4) & 511;
			Angle = D >> 14;
			Type = (D >> 4) & 1023;
			Paint = ((C & 15) << 4) | (D & 15);
		}
	};

	inline int32 DecodeZ(const FVector4f& R)
	{
		return ((int32(R.X) & 127) << 7) | (int32(R.Y) & 127);
	}

	inline int32 CellOf(const FVector4f& R)
	{
		const int32 CellX = FMath::Min((int32(R.X) >> 7) * GridSize / (PositionMax + 1), GridSize - 1);
		const int32 CellY = FMath::Min((int32(R.Y) >> 7) * GridSize / (PositionMax + 1), GridSize - 1);
		return CellY * GridSize + CellX;
	}

	/** True when two records are the same building, whatever its paint. */
	inline bool SameBuilding(const FVector4f& A, const FVector4f& B)
	{
		return A.X == B.X && A.Y == B.Y && (int32(A.Z) & ~15) == (int32(B.Z) & ~15) && (int32(A.W) & ~15) == (int32(B.W) & ~15);
	}

	inline float ZToMetres(int32 Z)
	{
		return float((Z * ZStepCm - ZBaseCm) / 100.0);
	}

	inline int32 MetresToZ(float Metres)
	{
		return FMath::Clamp(FMath::RoundToInt32((Metres * 100.0 + ZBaseCm) / ZStepCm), 0, ZMaxValue);
	}
}
