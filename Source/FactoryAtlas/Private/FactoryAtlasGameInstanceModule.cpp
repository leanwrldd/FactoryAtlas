#include "FactoryAtlasGameInstanceModule.h"

#include "AtlasMapHud.h"
#include "AtlasMapLayer.h"
#include "Engine/Engine.h"
#include "FactoryAtlas.h"
#include "Patching/WidgetBlueprintHookManager.h"

UFactoryAtlasGameInstanceModule::UFactoryAtlasGameInstanceModule()
{
	// Without this SML never instantiates a native module, and silently so.
	bRootModule = true;
}

void UFactoryAtlasGameInstanceModule::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
	Super::DispatchLifecycleEvent(Phase);

	if (Phase != ELifecyclePhase::INITIALIZATION || IsRunningDedicatedServer() || MapHook)
	{
		return;
	}

	UWidgetBlueprintHookManager* Hooks = GEngine->GetEngineSubsystem<UWidgetBlueprintHookManager>();

	// Widget_Map: ...ZoomContainer > ScrollContainer > Overlay_0 > [mMap, painter, mMapObjectPanel].
	// Slot 1 sits just above the map image and below the icons, and inherits pan and zoom.
	MapHook = NewObject<UWidgetBlueprintHookData>(this);
	MapHook->WidgetClass = TSoftClassPtr<UUserWidget>(FSoftObjectPath(TEXT("/Game/FactoryGame/Interface/UI/Minimap/Widget_Map.Widget_Map_C")));
	MapHook->NewWidgetClass = UAtlasMapLayer::StaticClass();
	MapHook->NewWidgetName = TEXT("FactoryAtlasLayer");
	MapHook->ParentWidgetType = EWidgetBlueprintHookParentType::Indirect_Child;
	MapHook->ParentWidgetName = TEXT("mMap");
	MapHook->ParentSlotIndex = 1;
	MapHook->SlotConfiguration = NewObject<UWidgetBlueprintHookSlot_Generic>(MapHook);
	Hooks->RegisterWidgetBlueprintHook(MapHook);

	// Widget_MapContainer's root canvas holds the map and the zoom slider, and does not pan or zoom.
	HudHook = NewObject<UWidgetBlueprintHookData>(this);
	HudHook->WidgetClass = TSoftClassPtr<UUserWidget>(FSoftObjectPath(TEXT("/Game/FactoryGame/Interface/UI/Minimap/Widget_MapContainer.Widget_MapContainer_C")));
	HudHook->NewWidgetClass = UAtlasMapHud::StaticClass();
	HudHook->NewWidgetName = TEXT("FactoryAtlasHud");
	HudHook->ParentWidgetType = EWidgetBlueprintHookParentType::Indirect_Child;
	HudHook->ParentWidgetName = TEXT("mZoomSlider");

	UWidgetBlueprintHookSlot_Canvas* CanvasSlot = NewObject<UWidgetBlueprintHookSlot_Canvas>(HudHook);
	CanvasSlot->LayoutData.Anchors = FAnchors(0.f, 0.f, 1.f, 1.f);
	CanvasSlot->LayoutData.Offsets = FMargin(0.f);
	CanvasSlot->ZOrder = 3;
	HudHook->SlotConfiguration = CanvasSlot;
	Hooks->RegisterWidgetBlueprintHook(HudHook);

	UE_LOG(LogFactoryAtlas, Display, TEXT("Map hooks registered"));
}
