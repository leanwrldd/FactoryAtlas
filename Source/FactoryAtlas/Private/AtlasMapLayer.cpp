#include "AtlasMapLayer.h"

#include "AtlasSubsystem.h"
#include "FactoryAtlas.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/ISlateRHIRendererModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Modules/ModuleManager.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "SlateMaterialBrush.h"

using namespace Atlas;

namespace
{
	constexpr int64 UploadBudgetBytes = 16 * 1024 * 1024;
	constexpr double HoverIntervalSeconds = 0.08;

	ISlateRHIRendererModule& Renderer()
	{
		return FModuleManager::Get().LoadModuleChecked<ISlateRHIRendererModule>(TEXT("SlateRHIRenderer"));
	}
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
}

void UAtlasMapLayer::NativeDestruct()
{
	UE_LOG(LogFactoryAtlas, Display, TEXT("[frame %llu] Map layer destructed"), GFrameCounter);
	Cells.Reset();
	HighlightBuffer.Reset();
	RadiationBuffer.Reset();
	RadiationVersion = 0;
	RadiationCount = 0;
	MarkerBuffer.Reset();
	MarkerVersion = 0;
	MarkerCount = 0;
	SelectionBuffer.Reset();
	bHighlightReady = false;
	if (UAtlasSubsystem* Atlas = UAtlasSubsystem::GetActive())
	{
		Atlas->ClearHover();
	}
	Super::NativeDestruct();
}

bool UAtlasMapLayer::EnsureMaterial(UAtlasSubsystem& Atlas)
{
	if (Material && MaterialHandle.IsValid())
	{
		return true;
	}
	if (bMaterialFailed)
	{
		return false;
	}

	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/FactoryAtlas/M_AtlasInstances.M_AtlasInstances"));
	if (!Parent)
	{
		bMaterialFailed = true;
		UE_LOG(LogFactoryAtlas, Error, TEXT("M_AtlasInstances did not load"));
		return false;
	}

	Material = UMaterialInstanceDynamic::Create(Parent, this);
	Material->SetTextureParameterValue(TEXT("Palette"), Atlas.GetPalette());
	Brush = MakeShared<FSlateMaterialBrush>(*Material, FVector2f(64.f, 64.f));
	MaterialHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*Brush);
	return MaterialHandle.IsValid();
}

void UAtlasMapLayer::UpdateParameters(UAtlasSubsystem& Atlas)
{
	const FVector2f Filter = Atlas.GetHeightFilter();
	if (Filter.X != AppliedLow || Filter.Y != AppliedHigh)
	{
		AppliedLow = Filter.X;
		AppliedHigh = Filter.Y;
		Material->SetScalarParameterValue(TEXT("ZMin"), float(MetresToZ(Filter.X)));
		Material->SetScalarParameterValue(TEXT("ZMax"), float(MetresToZ(Filter.Y)));
	}

	const int32 Paint = Atlas.IsPaintMode() ? 1 : 0;
	if (Paint != AppliedPaint)
	{
		AppliedPaint = Paint;
		Material->SetScalarParameterValue(TEXT("PaintMode"), float(Paint));
	}

	const FSettings& Settings = Atlas.GetSettings();
	const int32 Outlines = Settings.bOutlines ? 1 : 0;
	if (Outlines != AppliedOutlines)
	{
		AppliedOutlines = Outlines;
		Material->SetScalarParameterValue(TEXT("Outlines"), float(Outlines));
	}
	if (Settings.Opacity != AppliedOpacity)
	{
		AppliedOpacity = Settings.Opacity;
		Material->SetScalarParameterValue(TEXT("Opacity"), Settings.Opacity);
	}
}

void UAtlasMapLayer::Upload(UAtlasSubsystem& Atlas)
{
	const FGrid* Grid = Atlas.GetGrid();
	if (!Grid)
	{
		return;
	}

	Cells.SetNum(GridCells);
	int64 Budget = UploadBudgetBytes;
	for (int32 i = 0; i < GridCells && Budget > 0; i++)
	{
		FCellBuffer& Cell = Cells[i];
		if (Cell.Version == Grid->Versions[i])
		{
			continue;
		}

		const TArray<FVector4f>& Source = Grid->Cells[i];
		Cell.Version = Grid->Versions[i];
		Cell.NumInstances = Source.Num();
		if (Source.IsEmpty())
		{
			Cell.Buffer.Reset();
			continue;
		}

		FSlateInstanceBufferData Data = Source;
		if (!Cell.Buffer)
		{
			Cell.Buffer = Renderer().CreateInstanceBuffer(Data.Num());
		}
		Cell.Buffer->Update(Data);
		Budget -= int64(Source.Num()) * sizeof(FVector4f);
	}
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

void UAtlasMapLayer::UpdateRadiation(UAtlasSubsystem& Atlas)
{
	uint32 Version = 0;
	const TArray<FVector4f>& Circles = Atlas.GetRadiation(Version);
	if (Version == RadiationVersion)
	{
		return;
	}
	RadiationVersion = Version;
	RadiationCount = Circles.Num();
	if (Circles.IsEmpty())
	{
		return;
	}
	if (!RadiationBuffer)
	{
		RadiationBuffer = Renderer().CreateInstanceBuffer(Circles.Num());
	}
	FSlateInstanceBufferData Data = Circles;
	RadiationBuffer->Update(Data);
}

void UAtlasMapLayer::UpdateMarkers(UAtlasSubsystem& Atlas)
{
	uint32 Version = 0;
	const TArray<FVector4f>& Points = Atlas.GetMarkers(Version);
	if (Version == MarkerVersion)
	{
		return;
	}
	MarkerVersion = Version;
	MarkerCount = Points.Num();
	if (Points.IsEmpty())
	{
		return;
	}
	if (!MarkerBuffer)
	{
		MarkerBuffer = Renderer().CreateInstanceBuffer(Points.Num());
	}
	FSlateInstanceBufferData Data = Points;
	MarkerBuffer->Update(Data);
}

void UAtlasMapLayer::UpdateHover(UAtlasSubsystem& Atlas, const FGeometry& Geometry, float PixelsPerMetre)
{
	const FSettings& Settings = Atlas.GetSettings();
	if (!Settings.bHoverLabel && !Settings.bHighlight)
	{
		Atlas.ClearHover();
		return;
	}

	const double Now = FPlatformTime::Seconds();
	const FVector2f Pointer(FSlateApplication::Get().GetCursorPos());
	if (Now - LastHoverSeconds >= HoverIntervalSeconds && !Pointer.Equals(LastCursor, 0.5f))
	{
		LastHoverSeconds = Now;
		LastCursor = Pointer;
		if (ZoomOwners.Num() > 0 && ZoomOwners[0].IsValid())
		{
			Atlas.SetMapWidget(ZoomOwners[0]->GetCachedWidget());
		}

		// The game's own menus sit on top of the map; hovering through them would label buildings beneath.
		if (Atlas.IsPointOnMap(Pointer))
		{
			const FVector2f Size(Geometry.GetLocalSize());
			const FVector2f Local(Geometry.AbsoluteToLocal(Pointer));
			Atlas.UpdateHover(Local / Size, PixelsPerMetre);
		}
		else
		{
			Atlas.ClearHover();
		}
	}

	const FHover& Hover = Atlas.GetHover();
	if (Hover.bValid && (!bHighlightReady || Hover.Record != HighlightRecord))
	{
		HighlightRecord = Hover.Record;
		bHighlightReady = true;
		if (!HighlightBuffer)
		{
			HighlightBuffer = Renderer().CreateInstanceBuffer(1);
		}
		FSlateInstanceBufferData Data;
		Data.Add(HighlightRecord);
		HighlightBuffer->Update(Data);
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
	const bool bBuildings = Atlas->IsLayerEnabled();
	if ((!bBuildings && !Atlas->GetSettings().bRadiation && !Atlas->AnyMarkersShown()) || !Self->EnsureMaterial(*Atlas))
	{
		return Layer;
	}

	const double PaintStart = FPlatformTime::Seconds();
	Atlas->FlushPalette();
	Self->UpdateParameters(*Atlas);
	Self->Upload(*Atlas);

	const FVector2f Size(AllottedGeometry.GetLocalSize());
	const FSlateRenderTransform& Transform = AllottedGeometry.GetAccumulatedRenderTransform();
	const FVector2f Origin = Transform.TransformPoint(FVector2f::ZeroVector);
	const FVector2f AxisX = Transform.TransformPoint(FVector2f(Size.X, 0.f)) - Origin;
	const FVector2f AxisY = Transform.TransformPoint(FVector2f(0.f, Size.Y)) - Origin;
	const FVector2f Scale(AxisX.X, AxisY.Y);
	const float PixelsPerMetre = FMath::Abs(Scale.X) / MapWidthMetres;

	Self->UpdateHover(*Atlas, AllottedGeometry, PixelsPerMetre);

	// The map-to-window transform rides on the four corner vertices, so panning touches no GPU data.
	static const TArray<SlateIndex> Indices = { 0, 1, 2, 0, 2, 3 };
	TArray<FSlateVertex> Vertices;
	Vertices.SetNumUninitialized(4);
	const FVector2f Corners[4] = { { -1.f, -1.f }, { 1.f, -1.f }, { 1.f, 1.f }, { -1.f, 1.f } };
	for (int32 i = 0; i < 4; i++)
	{
		FSlateVertex& Vertex = Vertices[i];
		Vertex.TexCoords[0] = Scale.X;
		Vertex.TexCoords[1] = Scale.Y;
		Vertex.TexCoords[2] = Origin.X;
		Vertex.TexCoords[3] = Origin.Y;
		Vertex.MaterialTexCoords = Corners[i];
		Vertex.Position = Corners[i];
		Vertex.Color = FColor::White;
		Vertex.SecondaryColor = FColor::White;
		Vertex.PixelSize[0] = 0;
		Vertex.PixelSize[1] = 0;
	}

	const int32 DrawLayer = Layer + 1;
	const float CellUv = 1.f / GridSize;
	const float Margin = CellUv * 0.5f; // Long shapes overhang their cell
	int64 DrawnInstances = 0;
	for (int32 i = 0; bBuildings && i < Cells.Num(); i++)
	{
		const FCellBuffer& Cell = Cells[i];
		if (!Cell.Buffer || Cell.NumInstances == 0)
		{
			continue;
		}

		const FVector2f UvMin = FVector2f(float(i % GridSize), float(i / GridSize)) * CellUv - FVector2f(Margin);
		const FVector2f Min = Origin + Scale * UvMin;
		const FVector2f Max = Min + Scale * (CellUv + Margin * 2.f);
		if (Max.X < MyCullingRect.Left || Min.X > MyCullingRect.Right || Max.Y < MyCullingRect.Top || Min.Y > MyCullingRect.Bottom)
		{
			continue;
		}

		FSlateDrawElement::MakeCustomVerts(OutDrawElements, DrawLayer, MaterialHandle, Vertices, Indices,
			Cell.Buffer.Get(), 0, uint32(Cell.NumInstances));
		DrawnInstances += Cell.NumInstances;
	}

	int32 TopLayer = DrawLayer;
	if (Atlas->GetSettings().bRadiation)
	{
		Self->UpdateRadiation(*Atlas);
		if (RadiationBuffer && RadiationCount > 0)
		{
			TopLayer = DrawLayer + 1;
			FSlateDrawElement::MakeCustomVerts(OutDrawElements, TopLayer, MaterialHandle, Vertices, Indices, RadiationBuffer.Get(), 0, uint32(RadiationCount));
		}
	}

	if (Atlas->AnyMarkersShown())
	{
		Self->UpdateMarkers(*Atlas);
		if (MarkerBuffer && MarkerCount > 0)
		{
			TopLayer = TopLayer + 1;
			FSlateDrawElement::MakeCustomVerts(OutDrawElements, TopLayer, MaterialHandle, Vertices, Indices, MarkerBuffer.Get(), 0, uint32(MarkerCount));
		}
	}

	if (Atlas->HasSelection())
	{
		if (!SelectionBuffer || SelectionRecord != Atlas->GetSelectionRecord())
		{
			Self->SelectionRecord = Atlas->GetSelectionRecord();
			if (!SelectionBuffer)
			{
				Self->SelectionBuffer = Renderer().CreateInstanceBuffer(1);
			}
			FSlateInstanceBufferData Data;
			Data.Add(SelectionRecord);
			SelectionBuffer->Update(Data);
		}
		TopLayer = TopLayer + 1;
		FSlateDrawElement::MakeCustomVerts(OutDrawElements, TopLayer, MaterialHandle, Vertices, Indices, SelectionBuffer.Get(), 0, 1);
	}

	if (Atlas->GetHover().bValid && !Atlas->GetHover().bMarker && Atlas->GetSettings().bHighlight && HighlightBuffer)
	{
		TopLayer = TopLayer + 1;
		FSlateDrawElement::MakeCustomVerts(OutDrawElements, TopLayer, MaterialHandle, Vertices, Indices, HighlightBuffer.Get(), 0, 1);
	}

	Self->PaintSeconds += FPlatformTime::Seconds() - PaintStart;
	Self->PeakInstances = FMath::Max(PeakInstances, DrawnInstances);
	if (++Self->PaintFrames >= 300)
	{
		UE_LOG(LogFactoryAtlas, Display, TEXT("Paint: %.1f us per frame, peak %lld quads drawn, now %.2f px per metre"),
			Self->PaintSeconds / Self->PaintFrames * 1e6, Self->PeakInstances, PixelsPerMetre);
		Self->PaintFrames = 0;
		Self->PaintSeconds = 0.0;
		Self->PeakInstances = 0;
	}

	return TopLayer;
}
