// A second OS window with the whole Atlas map in it, for players with more than one monitor.
//
// The map is drawn by the same FAtlasMapPainter as the game's map screen, with the game's own terrain material
// under it, and the same HUD over it: layer menu, configure panel, hover label, toasts and shortcuts. Only pan
// and zoom are this window's own, since the game's map widget does those on the map screen.

#include "AtlasMapWindow.h"

#include "AtlasMapHud.h"
#include "AtlasMapPainter.h"
#include "AtlasSubsystem.h"
#include "AtlasTypes.h"
#include "FactoryAtlas.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWindow.h"

using namespace Atlas;

// Named, not anonymous: unity builds put this file next to others with helpers of the same names.
namespace AtlasWindowDetail
{
	constexpr float StartPixelsPerMetre = 0.4f;
	constexpr float MaxPixelsPerMetre = 16.f;
	constexpr float WheelStep = 1.25f;
	constexpr double HoverIntervalSeconds = 0.08;
	constexpr float ClickSlop = 5.f; // A press that moves less than this is a click, not a pan

	FSlateBrush TerrainBrush;
	TWeakObjectPtr<UObject> TerrainResource;
	TWeakObjectPtr<UObject> TerrainSource; // What the game's map image was, so it is only looked into once

	TSharedPtr<SWindow> Window;

	FVector2f ToUv(const FVector& Location)
	{
		return FVector2f(float((Location.X - MapWestCm) / MapWidthCm), float((Location.Y - MapNorthCm) / MapHeightCm));
	}

	APawn* PlayerPawn(UAtlasSubsystem& Atlas)
	{
		UWorld* World = Atlas.GetWorld();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		return Controller ? Controller->GetPawn() : nullptr;
	}
}
using namespace AtlasWindowDetail;

/** The map itself: paints terrain, the Atlas layer and the player, works out the hover, and pans and zooms. */
class SAtlasWindowMap : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAtlasWindowMap) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&) {}

	virtual ~SAtlasWindowMap() override
	{
		if (UAtlasSubsystem* Atlas = UAtlasSubsystem::GetActive())
		{
			Atlas->ClearHover(EView::Window);
			Atlas->SetPointerOverPanel(false, EView::Window);
		}
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(900.0, 900.0); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		return const_cast<SAtlasWindowMap*>(this)->Paint(AllottedGeometry, MyCullingRect, OutDrawElements, LayerId);
	}

	// Clicks that select, copy or paste are seen first by the HUD's click processor, as on the game's map; a
	// Ctrl-click that acted never reaches here. What does reach here pans.
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		LogClick(MouseEvent);
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton || MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			bDragging = true;
			bPanning = false;
			DragStart = FVector2f(MouseEvent.GetScreenSpacePosition());
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (bDragging)
		{
			bDragging = false;
			bPanning = false;
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Unhandled();
	}

	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override
	{
		bDragging = false;
		bPanning = false;
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!bDragging || !HasMouseCapture())
		{
			return FReply::Unhandled();
		}
		// A press only becomes a pan once it has moved, so a click that wobbles a pixel still selects.
		if (!bPanning && FVector2f::Distance(FVector2f(MouseEvent.GetScreenSpacePosition()), DragStart) < ClickSlop)
		{
			return FReply::Handled();
		}
		bPanning = true;
		const FVector2f Delta = FVector2f(MouseEvent.GetCursorDelta()) / FMath::Max(MyGeometry.Scale, 0.01f);
		Center = ClampCenter(Center - Delta / Zoom);
		return FReply::Handled();
	}

	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		// Zoom about the cursor, so the point under it stays put.
		const FVector2f Size(MyGeometry.GetLocalSize());
		const FVector2f Local(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
		const FVector2f Under = Center + (Local - Size * 0.5f) / Zoom;
		Zoom = ClampZoom(Zoom * FMath::Pow(WheelStep, MouseEvent.GetWheelDelta()), Size);
		Center = ClampCenter(Under - (Local - Size * 0.5f) / Zoom);
		return FReply::Handled();
	}

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		bCentred = false; // Jumps back to the player on the next paint
		return FReply::Handled();
	}

private:
	// View: the map UV at the middle of the widget, and how many local units one whole map width spans.
	FVector2f Center = FVector2f(0.5f);
	float Zoom = StartPixelsPerMetre * MapWidthMetres;
	bool bCentred = false;
	bool bDragging = false;
	bool bPanning = false;
	FVector2f DragStart = FVector2f::ZeroVector;

	FAtlasMapPainter Painter;

	/** Diagnostics: what a click on this map found, since the HUD acts on clicks only when all of it is right. */
	void LogClick(const FPointerEvent& Event) const
	{
		UAtlasSubsystem* Atlas = UAtlasSubsystem::GetActive();
		if (!Atlas)
		{
			return;
		}
		FSlateApplication& Slate = FSlateApplication::Get();
		const FVector2f Position(Event.GetScreenSpacePosition());
		const FWidgetPath Path = Slate.LocateWindowUnderMouse(Event.GetScreenSpacePosition(), Slate.GetInteractiveTopLevelWindows());
		const FHover& Hover = Atlas->GetHover();
		UE_LOG(LogFactoryAtlas, Verbose, TEXT("Window click %s%s at %.0f,%.0f: on map %d, over panel %d, hover %d (view %d, marker %d), path ends in %s, %d windows"),
			Event.IsControlDown() ? TEXT("Ctrl+") : TEXT(""), *Event.GetEffectingButton().ToString(), Position.X, Position.Y,
			Atlas->IsPointOnMap(Position, EView::Window) ? 1 : 0, Atlas->IsPointerOverPanel(EView::Window) ? 1 : 0,
			Hover.bValid ? 1 : 0, int32(Atlas->GetHoverView()), Hover.bMarker ? 1 : 0,
			Path.IsValid() ? *Path.GetLastWidget()->GetTypeAsString() : TEXT("nothing"), Slate.GetInteractiveTopLevelWindows().Num());
	}
	FVector2f LastCursor = FVector2f(-1.f, -1.f);
	double LastHoverSeconds = 0.0;

	static FVector2f ClampCenter(FVector2f Value)
	{
		return FVector2f(FMath::Clamp(Value.X, 0.f, 1.f), FMath::Clamp(Value.Y, 0.f, 1.f));
	}

	static float ClampZoom(float Value, FVector2f Size)
	{
		// Out far enough to see the whole map, in as far as the in-game map at its highest zoom limit.
		const float Min = FMath::Max(1.f, FMath::Min(Size.X, Size.Y) * 0.8f);
		return FMath::Clamp(Value, Min, MaxPixelsPerMetre * MapWidthMetres);
	}

	/** As on the game's map: a few times a second, or every frame during a drag-paste, and only over this map. */
	void UpdateHover(UAtlasSubsystem& Atlas, FVector2f LocalOrigin, float PixelsPerMetre)
	{
		const FSettings& Settings = Atlas.GetSettings();
		if (!Settings.bHoverLabel && !Settings.bHighlight && !Atlas.IsWiring(EView::Window))
		{
			Atlas.ClearHover(EView::Window);
			return;
		}
		const double Now = FPlatformTime::Seconds();
		const FVector2f Pointer(FSlateApplication::Get().GetCursorPos());
		const double Interval = Atlas.WantsHoverEveryFrame() ? 0.0 : HoverIntervalSeconds;
		if (Now - LastHoverSeconds < Interval || Pointer.Equals(LastCursor, 0.5f))
		{
			return;
		}
		LastHoverSeconds = Now;
		LastCursor = Pointer;

		// No hover while panning: the label would chase the cursor across the map.
		if (!bPanning && Atlas.IsPointOnMap(Pointer, EView::Window))
		{
			// The cursor is in desktop pixels, but paint geometry is relative to this window's corner, so it is
			// placed with the desktop-space geometry instead. They only agree for a window at the desktop's origin.
			const FVector2f Local(GetTickSpaceGeometry().AbsoluteToLocal(Pointer));
			Atlas.UpdateHover((Local - LocalOrigin) / Zoom, PixelsPerMetre, EView::Window);
		}
		else
		{
			Atlas.ClearHover(EView::Window);
		}
	}

	void PaintText(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, FVector2f Where, const FString& Text) const
	{
		static const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 10);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId, Geometry.ToPaintGeometry(FVector2f(800.f, 20.f), FSlateLayoutTransform(Where)),
			Text, Font, ESlateDrawEffect::None, FLinearColor(0.75f, 0.78f, 0.8f, 0.9f));
	}

	int32 Paint(const FGeometry& Geometry, const FSlateRect& CullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId)
	{
		static const FSlateColorBrush Solid(FLinearColor::White);
		static const FSlateRoundedBoxBrush Dot(FLinearColor::White, 7.f, FLinearColor(0.02f, 0.02f, 0.02f, 1.f), 2.f);

		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), &Solid, ESlateDrawEffect::None, FLinearColor(0.012f, 0.014f, 0.017f, 1.f));

		UAtlasSubsystem* Atlas = UAtlasSubsystem::GetActive();
		if (!Atlas)
		{
			PaintText(Geometry, OutDrawElements, LayerId + 1, FVector2f(12.f, 10.f), TEXT("Load a save to see the map."));
			return LayerId + 1;
		}

		Atlas->EnsureScanned();
		Atlas->Pump();
		Atlas->SetMapWidget(AsShared(), EView::Window);

		const FVector2f Size(Geometry.GetLocalSize());
		APawn* Pawn = PlayerPawn(*Atlas);
		if (!bCentred && Pawn)
		{
			bCentred = true;
			Center = ClampCenter(ToUv(Pawn->GetActorLocation()));
		}
		Zoom = ClampZoom(Zoom, Size);

		// Map UV 0..1 lands here, in window pixels, which is what the painter and FPaintGeometry both want.
		const FVector2f LocalOrigin = Size * 0.5f - Center * Zoom;
		const FVector2f Origin(Geometry.LocalToAbsolute(FVector2D(LocalOrigin)));
		const float ScaleX = Zoom * Geometry.Scale;
		const FVector2f Scale(ScaleX, ScaleX);
		const float PixelsPerMetre = ScaleX / MapWidthMetres;

		int32 Layer = LayerId + 1;
		const bool bTerrain = TerrainResource.IsValid();
		if (bTerrain)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, Layer, FPaintGeometry(Origin, Scale, 1.f), &TerrainBrush);
		}

		// The configure panel follows the selected building as this map pans and zooms.
		if (Atlas->HasSelection())
		{
			const FDecoded Record(Atlas->GetSelectionRecord());
			const FVector2f Uv(float(Record.X) / PositionMax, float(Record.Y) / PositionMax);
			const float ReachMetres = FVector2f(float(Record.HalfX), float(Record.HalfY)).Size() / 10.f;
			// In desktop pixels, like the geometry the HUD places its panel with; see UpdateHover.
			const FVector2f Anchor(GetTickSpaceGeometry().LocalToAbsolute(FVector2D(LocalOrigin + Uv * Zoom)));
			Atlas->SetSelectionAnchor(Anchor, ReachMetres * PixelsPerMetre, EView::Window);
		}

		// For the HUD's measure tool, in desktop pixels like the selection anchor.
		{
			const FGeometry& Tick = GetTickSpaceGeometry();
			const FVector2f DesktopOrigin(Tick.LocalToAbsolute(FVector2D(LocalOrigin)));
			const FVector2f DesktopCorner(Tick.LocalToAbsolute(FVector2D(LocalOrigin + FVector2f(Zoom))));
			Atlas->SetMapTransform(EView::Window, DesktopOrigin, DesktopCorner - DesktopOrigin);
		}

		// For finding the port under the cursor; desktop-space geometry, as for the hover.
		const FVector2f CursorLocal(GetTickSpaceGeometry().AbsoluteToLocal(FSlateApplication::Get().GetCursorPos()));
		Atlas->SetMapCursor(EView::Window, (CursorLocal - LocalOrigin) / Zoom, PixelsPerMetre);

		if (FAtlasMapPainter::HasAnythingToDraw(*Atlas))
		{
			UpdateHover(*Atlas, LocalOrigin, PixelsPerMetre);
			Layer = Painter.Paint(*Atlas, EView::Window, Origin, Scale, CullingRect, OutDrawElements, Layer, Atlas->GetHoverView() == EView::Window, TEXT("Window paint"));
		}

		// The player: a dot with a short line for where the camera faces.
		if (Pawn)
		{
			const FVector2f Player = LocalOrigin + ToUv(Pawn->GetActorLocation()) * Zoom;
			const AController* Controller = Pawn->GetController();
			const float Yaw = FMath::DegreesToRadians(Controller ? Controller->GetControlRotation().Yaw : Pawn->GetActorRotation().Yaw);
			const FVector2f Facing(FMath::Cos(Yaw), FMath::Sin(Yaw));
			const FLinearColor Orange(1.f, 0.45f, 0.08f, 1.f);
			TArray<FVector2f> Line = { Player, Player + Facing * 22.f };
			FSlateDrawElement::MakeLines(OutDrawElements, ++Layer, Geometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, Orange, true, 3.f);
			FSlateDrawElement::MakeBox(OutDrawElements, ++Layer, Geometry.ToPaintGeometry(FVector2f(14.f), FSlateLayoutTransform(Player - FVector2f(7.f))),
				&Dot, ESlateDrawEffect::None, Orange);
		}

		if (!bTerrain)
		{
			PaintText(Geometry, OutDrawElements, ++Layer, FVector2f(12.f, 10.f), TEXT("Open the in-game map once to show the terrain here."));
		}
		return Layer;
	}
};

namespace Atlas::MapWindow
{
	bool IsOpen()
	{
		return Window.IsValid();
	}

	void Open()
	{
		if (Window.IsValid() || !FSlateApplication::IsInitialized() || IsRunningDedicatedServer())
		{
			return;
		}

		// A plain OS window, not a child of the game's, so it can live on another monitor and is not minimised with it.
		Window = SNew(SWindow)
			.Title(FText::FromString(TEXT("Atlas")))
			.ClientSize(FVector2D(1100.0, 900.0))
			.SizingRule(ESizingRule::UserSized)
			.UseOSWindowBorder(true)
			.SupportsMaximize(true)
			.SupportsMinimize(true)
			.FocusWhenFirstShown(false)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SAtlasWindowMap)
				]
				+ SOverlay::Slot()
				[
					UAtlasMapHud::MakeWindowHud()
				]
			];
		Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([](const TSharedRef<SWindow>&) { Window.Reset(); }));
		FSlateApplication::Get().AddWindow(Window.ToSharedRef(), true);
		UE_LOG(LogFactoryAtlas, Display, TEXT("Map window opened"));
	}

	void Close()
	{
		if (TSharedPtr<SWindow> Closing = Window)
		{
			Window.Reset();
			Closing->RequestDestroyWindow();
			UE_LOG(LogFactoryAtlas, Display, TEXT("Map window closed"));
		}
	}

	void Toggle()
	{
		IsOpen() ? Close() : Open();
	}

	void SetTerrainBrush(const FSlateBrush& Brush)
	{
		UObject* Resource = Brush.GetResourceObject();
		if (!Resource || Resource == TerrainSource.Get())
		{
			return;
		}
		TerrainSource = Resource;

		// Widget_Map is built holding the bare fog-of-war material and only swaps in its own instance, with the
		// map's placement and fog set on it, after that. The bare material draws the explored patches in the
		// wrong place, so wait for the instance.
		if (Resource->GetClass() == UMaterial::StaticClass())
		{
			UE_LOG(LogFactoryAtlas, Display, TEXT("Map window: waiting for the map's own material instance (have %s)"), *Resource->GetName());
			return;
		}
		TerrainBrush = Brush;
		TerrainResource = Resource;
		UE_LOG(LogFactoryAtlas, Display, TEXT("Map window terrain: %s (%s)"), *Resource->GetName(), *Resource->GetClass()->GetName());
	}
}

namespace AtlasWindowDetail
{
	FAutoConsoleCommand WindowCommand(TEXT("Atlas.Window"), TEXT("Opens or closes the Atlas map in a separate window."),
		FConsoleCommandDelegate::CreateStatic(&Atlas::MapWindow::Toggle));
}
