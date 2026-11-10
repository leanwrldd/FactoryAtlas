#include "AtlasMapLayer.h"

#include "AtlasMapWindow.h"
#include "AtlasSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "FactoryAtlas.h"
#include "Framework/Application/SlateApplication.h"

using namespace Atlas;

namespace AtlasLayerDetail
{
	constexpr double HoverIntervalSeconds = 0.08;
}

UAtlasMapLayer::UAtlasMapLayer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibilityInternal(ESlateVisibility::HitTestInvisible);
}

void UAtlasMapLayer::NativeConstruct()
{
	Super::NativeConstruct();
	UE_LOG(LogFactoryAtlas, Display, TEXT("[frame %llu] Map layer constructed"), GFrameCounter);
	if (UAtlasSubsystem* Atlas = UAtlasSubsystem::GetActive())
	{
		Atlas->OnMapOpened();
	}

	// The hook places this layer inside Widget_Map, which sits inside Widget_MapContainer. Both hold mMaxZoom.
	ZoomOwners.Reset();
	for (UObject* Outer = GetOuter(); Outer; Outer = Outer->GetOuter())
	{
		if (UUserWidget* Widget = Cast<UUserWidget>(Outer))
		{
			ZoomOwners.Add(Widget);
		}
	}
	AppliedZoomFactor = 0;

	// The game's map image, lent to the separate map window, which has no map of its own to draw on.
	// Its brush changes after construction, so it is read again while painting.
	MapImage.Reset();
	if (ZoomOwners.Num() > 0 && ZoomOwners[0].IsValid() && ZoomOwners[0]->WidgetTree)
	{
		UWidget* Found = ZoomOwners[0]->WidgetTree->FindWidget(TEXT("mMap"));
		MapImage = Cast<UImage>(Found);
		if (!MapImage.IsValid())
		{
			UE_LOG(LogFactoryAtlas, Display, TEXT("Map image not found for the map window (mMap is %s)"), Found ? *Found->GetClass()->GetName() : TEXT("missing"));
		}
	}
}

void UAtlasMapLayer::NativeDestruct()
{
	UE_LOG(LogFactoryAtlas, Display, TEXT("[frame %llu] Map layer destructed"), GFrameCounter);
	Painter.Reset();
	if (UAtlasSubsystem* Atlas = UAtlasSubsystem::GetActive())
	{
		Atlas->ClearHover(EView::Game);
		Atlas->SetPointerOverPanel(false, EView::Game);
	}
	Super::NativeDestruct();
}

void UAtlasMapLayer::ApplyZoomLimit(int32 Factor)
{
	if (Factor == AppliedZoomFactor)
	{
		return;
	}
	AppliedZoomFactor = Factor;

	for (const TWeakObjectPtr<UUserWidget>& Owner : ZoomOwners)
	{
		UUserWidget* Widget = Owner.Get();
		FNumericProperty* Property = Widget ? CastField<FNumericProperty>(Widget->GetClass()->FindPropertyByName(TEXT("mMaxZoom"))) : nullptr;
		if (!Property || !Property->IsFloatingPoint())
		{
			continue;
		}
		const double Base = Property->GetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(Widget->GetClass()->GetDefaultObject()));
		Property->SetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(Widget), Base * Factor);
		UE_LOG(LogFactoryAtlas, Display, TEXT("Zoom limit on %s: %.0f (game default %.0f)"), *Widget->GetClass()->GetName(), Base * Factor, Base);
	}
}

void UAtlasMapLayer::UpdateHover(UAtlasSubsystem& Atlas, const FGeometry& Geometry, float PixelsPerMetre)
{
	const FSettings& Settings = Atlas.GetSettings();
	// Wire mode finds the building to connect through the hover, so it needs it even with the label and highlight off.
	if (!Settings.bHoverLabel && !Settings.bHighlight && !Atlas.IsWiring(EView::Game))
	{
		Atlas.ClearHover(EView::Game);
		return;
	}

	const double Now = FPlatformTime::Seconds();
	const FVector2f Pointer(FSlateApplication::Get().GetCursorPos());
	const double Interval = Atlas.WantsHoverEveryFrame() ? 0.0 : AtlasLayerDetail::HoverIntervalSeconds;
	if (Now - LastHoverSeconds < Interval || Pointer.Equals(LastCursor, 0.5f))
	{
		return;
	}
	LastHoverSeconds = Now;
	LastCursor = Pointer;

	// The game's own menus sit on top of the map; hovering through them would label buildings beneath.
	// The pointer may also be over the separate map window, which keeps its own hover.
	if (Atlas.IsPointOnMap(Pointer, EView::Game))
	{
		const FVector2f Size(Geometry.GetLocalSize());
		const FVector2f Local(Geometry.AbsoluteToLocal(Pointer));
		Atlas.UpdateHover(Local / Size, PixelsPerMetre, EView::Game);
	}
	else
	{
		Atlas.ClearHover(EView::Game);
	}
}

int32 UAtlasMapLayer::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	UAtlasSubsystem* Atlas = UAtlasSubsystem::GetActive();
	UAtlasMapLayer* Self = const_cast<UAtlasMapLayer*>(this);
	if (!Atlas)
	{
		return Layer;
	}

	// The first paint is the first moment the map is really on screen, so a map that is never
	// opened costs nothing. Scanning is kept alive even while the layer is hidden, for the menu.
	Atlas->EnsureScanned();
	Atlas->Pump();
	Self->ApplyZoomLimit(Atlas->GetSettings().ZoomFactor);
	if (const UImage* Image = MapImage.Get())
	{
		MapWindow::SetTerrainBrush(Image->GetBrush()); // Returns at once when nothing changed
	}
	if (ZoomOwners.Num() > 0 && ZoomOwners[0].IsValid())
	{
		Atlas->SetMapWidget(ZoomOwners[0]->GetCachedWidget(), EView::Game);
	}

	const FVector2f Size(AllottedGeometry.GetLocalSize());
	const FSlateRenderTransform& Transform = AllottedGeometry.GetAccumulatedRenderTransform();
	const FVector2f Origin = Transform.TransformPoint(FVector2f::ZeroVector);
	const FVector2f AxisX = Transform.TransformPoint(FVector2f(Size.X, 0.f)) - Origin;
	const FVector2f AxisY = Transform.TransformPoint(FVector2f(0.f, Size.Y)) - Origin;
	const FVector2f Scale(AxisX.X, AxisY.Y);
	const float PixelsPerMetre = FMath::Abs(Scale.X) / MapWidthMetres;
	// For the HUD, which draws the measure tool over this map. Same space as the selection anchor below.
	Atlas->SetMapTransform(EView::Game, Origin, Scale);

	// The configure panel follows the selected building as the map pans and zooms.
	if (Atlas->HasSelection())
	{
		const FDecoded Record(Atlas->GetSelectionRecord());
		const FVector2f Uv(float(Record.X) / PositionMax, float(Record.Y) / PositionMax);
		const float ReachMetres = FVector2f(float(Record.HalfX), float(Record.HalfY)).Size() / 10.f;
		Atlas->SetSelectionAnchor(Origin + Scale * Uv, ReachMetres * PixelsPerMetre, EView::Game);
	}

	// For finding the port under the cursor. Paint space is the game window's, which starts at the desktop's corner.
	const FVector2f CursorLocal(AllottedGeometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos()));
	Atlas->SetMapCursor(EView::Game, CursorLocal / Size, PixelsPerMetre);

	if (!FAtlasMapPainter::HasAnythingToDraw(*Atlas))
	{
		return Layer;
	}
	Self->UpdateHover(*Atlas, AllottedGeometry, PixelsPerMetre);
	return Self->Painter.Paint(*Atlas, EView::Game, Origin, Scale, MyCullingRect, OutDrawElements, Layer, Atlas->GetHoverView() == EView::Game, TEXT("Paint"));
}
