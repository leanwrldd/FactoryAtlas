#pragma once

#include "CoreMinimal.h"
#include "AtlasMapPainter.h"
#include "AtlasTypes.h"
#include "Blueprint/UserWidget.h"
#include "AtlasMapLayer.generated.h"

class UAtlasSubsystem;

/**
 * The Atlas map inside the game's map screen, in the map's own zoom and scroll containers. Drawing is done by
 * FAtlasMapPainter; this widget works out where the map is, the hover, and the game's zoom limit.
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
	void UpdateHover(UAtlasSubsystem& Atlas, const FGeometry& Geometry, float PixelsPerMetre);
	void ApplyZoomLimit(int32 Factor);

	FAtlasMapPainter Painter;

	/** Widget_Map and Widget_MapContainer, which own the zoom limit. */
	TArray<TWeakObjectPtr<UUserWidget>> ZoomOwners;
	int32 AppliedZoomFactor = 0;

	/** Widget_Map's mMap image, whose brush the separate map window borrows. */
	TWeakObjectPtr<class UImage> MapImage;

	FVector2f LastCursor = FVector2f(-1.f, -1.f);
	double LastHoverSeconds = 0.0;
};
