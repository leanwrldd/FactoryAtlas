#pragma once

#include "CoreMinimal.h"
#include "Rendering/RenderingCommon.h"
#include "Rendering/SlateResourceHandle.h"
#include "UObject/StrongObjectPtr.h"
#include "AtlasTypes.h"

class UAtlasSubsystem;
namespace Atlas { struct FGrid; }
class UMaterialInstanceDynamic;
class UTexture2D;
class FSlateWindowElementList;
struct FSlateBrush;

/**
 * Draws the Atlas map: every building as one instanced quad per grid cell, radiation, recipe icons, markers, the
 * selection and the hover highlight. Used by the game's map screen and by the separate map window, each with its
 * own copy, since GPU buffers belong to one painter. Buffers exist only while something is painting.
 */
class FACTORYATLAS_API FAtlasMapPainter
{
public:
	/**
	 * Draws onto OutDrawElements. Origin is where map UV (0,0) lands in window pixels and Scale how many window
	 * pixels the whole map spans, so panning and zooming touch no GPU data. Returns the top layer used.
	 */
	int32 Paint(UAtlasSubsystem& Atlas, Atlas::EView View, FVector2f Origin, FVector2f Scale, const FSlateRect& CullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, bool bHighlightHover, const TCHAR* LogName);

	/** True when the map has anything at all to draw, so hover and painting can be skipped otherwise. */
	static bool HasAnythingToDraw(const UAtlasSubsystem& Atlas);

	/** Drops every GPU buffer and brush; the next Paint uploads from scratch. */
	void Reset();

private:
	struct FCellBuffer
	{
		TSharedPtr<ISlateUpdatableInstanceBuffer> Buffer;
		uint32 Version = 0;
		int32 NumInstances = 0;
	};

	bool EnsureMaterial(UAtlasSubsystem& Atlas);
	void UpdateParameters(UAtlasSubsystem& Atlas);
	void SetScalar(const TCHAR* Name, float Value);
	void Upload(UAtlasSubsystem& Atlas);
	/** Brings Out up to date with Source's cells, uploading only those whose version changed, within Budget. */
	static void UploadGrid(const Atlas::FGrid& Source, TArray<FCellBuffer>& Out, int64& Budget);
	/** Draws each cell of Buffers that is on screen with one call. Returns the number of instances drawn. */
	int64 DrawCells(const TArray<FCellBuffer>& Buffers, const FSlateResourceHandle& Handle, FSlateWindowElementList& OutDrawElements, int32 Layer,
		const TArray<FSlateVertex>& Vertices, FVector2f Origin, FVector2f Scale, const FSlateRect& CullingRect) const;
	static void UpdateBuffer(const TArray<FVector4f>& Source, uint32 Version, uint32& AppliedVersion, int32& Count,
		TSharedPtr<ISlateUpdatableInstanceBuffer>& Buffer);
	static void UpdateSingle(const FVector4f& Record, FVector4f& Applied, bool& bReady, TSharedPtr<ISlateUpdatableInstanceBuffer>& Buffer);
	int32 PaintRecipeIcons(UAtlasSubsystem& Atlas, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		FVector2f Origin, FVector2f Scale, float PixelsPerMetre, const FSlateRect& CullingRect);
	const FSlateBrush* IconBrush(UTexture2D* Icon);
	/** Free conveyor ports as nodes, and the belt being dragged between two of them. Returns the top layer used. */
	int32 PaintPorts(UAtlasSubsystem& Atlas, Atlas::EView View, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		FVector2f Origin, FVector2f Scale, const FSlateRect& CullingRect);
	/** In wire mode: the wire being run, from its start to the building or ground under the cursor. */
	int32 PaintWire(const UAtlasSubsystem& Atlas, Atlas::EView View, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		FVector2f Origin, FVector2f Scale) const;

	TArray<FCellBuffer> Cells;
	TArray<FCellBuffer> StatusCells;
	TSharedPtr<ISlateUpdatableInstanceBuffer> HitsBuffer;
	uint32 HitsVersion = 0;
	int32 HitsCount = 0;
	TSharedPtr<ISlateUpdatableInstanceBuffer> RadiationBuffer;
	uint32 RadiationVersion = 0;
	int32 RadiationCount = 0;
	TSharedPtr<ISlateUpdatableInstanceBuffer> MarkerBuffer;
	uint32 MarkerVersion = 0;
	int32 MarkerCount = 0;
	TSharedPtr<ISlateUpdatableInstanceBuffer> SelectionBuffer;
	FVector4f SelectionRecord = FVector4f::Zero();
	bool bSelectionReady = false;
	TSharedPtr<ISlateUpdatableInstanceBuffer> HighlightBuffer;
	FVector4f HighlightRecord = FVector4f::Zero();
	bool bHighlightReady = false;

	TMap<UTexture2D*, TSharedPtr<FSlateBrush>> IconBrushes;
	TArray<TStrongObjectPtr<UTexture2D>> IconTextures;

	TStrongObjectPtr<UMaterialInstanceDynamic> Material;
	TSharedPtr<FSlateBrush> Brush;
	FSlateResourceHandle MaterialHandle;
	/** The same material with StatusMode on, which draws status records as corner dots. */
	TStrongObjectPtr<UMaterialInstanceDynamic> StatusMaterial;
	TSharedPtr<FSlateBrush> StatusBrush;
	FSlateResourceHandle StatusHandle;
	bool bMaterialFailed = false;
	float AppliedLow = -1e30f;
	float AppliedHigh = -1e30f;
	int32 AppliedPaint = -1;
	int32 AppliedOutlines = -1;
	float AppliedOpacity = -1.f;

	double PaintSeconds = 0.0;
	int32 PaintFrames = 0;
	int64 PeakInstances = 0;
};
