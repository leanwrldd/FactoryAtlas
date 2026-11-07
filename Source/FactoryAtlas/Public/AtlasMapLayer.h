#pragma once

#include "CoreMinimal.h"
#include "AtlasTypes.h"
#include "Blueprint/UserWidget.h"
#include "Rendering/RenderingCommon.h"
#include "Rendering/SlateResourceHandle.h"
#include "AtlasMapLayer.generated.h"

class UAtlasSubsystem;
class UMaterialInstanceDynamic;
struct FSlateBrush;

/**
 * Draws every building as one instanced quad, inside the map's own zoom and scroll containers.
 * GPU buffers exist only while the map is on screen, one per map cell.
 */
UCLASS(NotBlueprintable)
class FACTORYATLAS_API UAtlasMapLayer : public UUserWidget
{
	GENERATED_BODY()
public:
	UAtlasMapLayer(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	struct FCellBuffer
	{
		TSharedPtr<ISlateUpdatableInstanceBuffer> Buffer;
		uint32 Version = 0;
		int32 NumInstances = 0;
	};

	bool EnsureMaterial(UAtlasSubsystem& Atlas);
	void Upload(UAtlasSubsystem& Atlas);
	void UpdateParameters(UAtlasSubsystem& Atlas);
	void UpdateHover(UAtlasSubsystem& Atlas, const FGeometry& Geometry, float PixelsPerMetre);
	void UpdateRadiation(UAtlasSubsystem& Atlas);
	void ApplyZoomLimit(int32 Factor);

	/** Widget_Map and Widget_MapContainer, which own the zoom limit. */
	TArray<TWeakObjectPtr<UUserWidget>> ZoomOwners;
	int32 AppliedZoomFactor = 0;

	TSharedPtr<ISlateUpdatableInstanceBuffer> RadiationBuffer;
	uint32 RadiationVersion = 0;
	int32 RadiationCount = 0;

	TSharedPtr<ISlateUpdatableInstanceBuffer> SelectionBuffer;
	FVector4f SelectionRecord = FVector4f::Zero();

	void UpdateMarkers(UAtlasSubsystem& Atlas);
	TSharedPtr<ISlateUpdatableInstanceBuffer> MarkerBuffer;
	uint32 MarkerVersion = 0;
	int32 MarkerCount = 0;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	TSharedPtr<FSlateBrush> Brush;
	FSlateResourceHandle MaterialHandle;

	TArray<FCellBuffer> Cells;
	TSharedPtr<ISlateUpdatableInstanceBuffer> HighlightBuffer;
	FVector4f HighlightRecord = FVector4f::Zero();
	bool bHighlightReady = false;
	bool bMaterialFailed = false;

	float AppliedLow = -1e30f;
	float AppliedHigh = -1e30f;
	int32 AppliedPaint = -1;
	int32 AppliedOutlines = -1;
	float AppliedOpacity = -1.f;

	FVector2f LastCursor = FVector2f(-1.f, -1.f);
	double LastHoverSeconds = 0.0;

	double PaintSeconds = 0.0;
	int32 PaintFrames = 0;
	int64 PeakInstances = 0;
};
