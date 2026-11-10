#pragma once

#include "CoreMinimal.h"
#include "AtlasTypes.h"
#include "FGFactoryColoringTypes.h"
#include "FGLightweightBuildableSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "Containers/Ticker.h"
#include "Tasks/Task.h"
#include "AtlasSubsystem.generated.h"

class AFGBuildable;
class UFGFactoryConnectionComponent;
class UFGConnectionComponent;
class UFGCircuitConnectionComponent;
class AFGBuildableManufacturer;
class AFGGameState;
class UTexture2D;
class USoundBase;
struct FFactoryCustomizationData;
struct FRuntimeBuildableInstanceData;

namespace Atlas
{
	/** The sounds Ctrl-clicks on the map make. */
	enum class EClipboardSound : uint8 { Copy, Paste, Error, Count };

	/** SplineComponent is for paths built from a plain spline component, such as truck paths. */
	enum class EKind : uint8 { Box, Spline, SplineComponent, Wire, Beam, Skip };

	struct FType
	{
		UClass* Class = nullptr;
		FText Name;
		ECategory Category = ECategory::Other;
		EKind Kind = EKind::Box;
		float HalfWidthCm = 0.f; // Splines, wires and beams
		FVector Center = FVector::ZeroVector;
		FVector2f Half = FVector2f(100.f, 100.f);
		FColor Color = FColor::White;
		int32 Count = 0;
		bool bHidden = false;
	};

	struct FGrid
	{
		TArray<FVector4f> Cells[GridCells];
		uint32 Versions[GridCells] = {};
		int32 ZLow = 0;
		int32 ZHigh = 0;
		int64 Total = 0;
	};

	struct FOp
	{
		FVector4f Record;
		bool bAdd;
	};

	struct FHover
	{
		bool bValid = false;
		int32 Type = 0;
		float HeightMetres = 0.f;
		FVector2f WorldMetres = FVector2f::ZeroVector;
		FVector4f Record = FVector4f::Zero();
		FVector4f Source = FVector4f::Zero(); // The building's own record, before it was recoloured for the highlight
		bool bMarker = false; // A collectible rather than a building, so nothing gets highlighted
		int32 Flags = 0;      // Marker flags, see AtlasTypes.h
	};

	/** What runs between two ports. Each kind only connects to its own. */
	enum class EPortKind : uint8
	{
		Belt,
		Pipe,
		Hypertube,
		Rail,
		Count
	};

	/** A free port of a building on the map: where a belt, pipe, hypertube or track can start or end. */
	struct FPort
	{
		TWeakObjectPtr<UFGConnectionComponent> Connection;
		TWeakObjectPtr<AFGBuildable> Owner;
		FVector Location = FVector::ZeroVector;
		FVector Normal = FVector::ForwardVector;
		FVector2f Uv = FVector2f::ZeroVector;
		EPortKind Kind = EPortKind::Belt;
		bool bOutput = false;
		bool bAnyFlow = false; // Connects to anything of its kind: pipe ports without a set direction, hypertubes, tracks
	};

	/** A belt being dragged from one port to another on the map, and what the game's hologram says about it. */
	struct FBeltDrag
	{
		bool bPressed = false;  // The button went down on a port; not yet a drag until it moves
		bool bActive = false;   // Moving: the hologram is out and the preview shows
		EView View = EView::Game;
		FPort Start;
		FVector2f PressScreen = FVector2f::ZeroVector;
		bool bHasTarget = false;
		FPort Target;
		bool bReady = false;    // The hologram has settled on the current target, so Valid and Reason can be trusted
		bool bValid = false;
		FText Reason;
		TArray<FVector> Points; // The hologram's spline, in world space, for the preview
		FText BeltName;
		FText ModeName;
		bool bHasGround = false; // No port under the cursor, but ground the line can end on
		bool bClipping = false; // Valid only because clipping is allowed: clearance overlaps are dropped before building
	};

	/** What the configure panel shows for the selected building. Read through the game's own UFUNCTIONs. */
	struct FFactoryState
	{
		bool bValid = false;
		bool bAuthority = false;
		bool bFactory = false; // A machine, so the clock, pause and recipe lines apply
		bool bCanChangeClock = false;
		bool bCanBoost = false;
		bool bPaused = false;
		float Current = 1.f;
		float Pending = 1.f;
		float Min = 0.01f;
		float Max = 1.f;
		float Boost = 1.f;
		float MaxBoost = 1.f;
		float Productivity = 0.f;
		int32 Shards = 0;
		FText Name;

		bool bHasRecipes = false; // A manufacturer, so it runs a recipe the player can pick
		UClass* Recipe = nullptr;
		FText RecipeName;
		FString RecipeSummary;
	};

	struct FRecipeOption
	{
		UClass* Recipe = nullptr;
		FText Name;
		FString Summary;
	};

	/** One color, pattern or material the selected building can take. */
	struct FCustomizeOption
	{
		UClass* Desc = nullptr; // nullptr for "no pattern"
		FText Name;
		FLinearColor Primary = FLinearColor::White;   // Swatches only
		FLinearColor Secondary = FLinearColor::White;
		UTexture2D* Icon = nullptr;                   // Patterns and materials
	};

	/**
	 * What the selected building accepts, worked out once per selection. An empty list means the building does
	 * not take that kind of customization, and the panel leaves that section out.
	 */
	struct FCustomizeOptions
	{
		TArray<FCustomizeOption> Swatches;
		TArray<FCustomizeOption> Patterns;
		TArray<FCustomizeOption> Materials;
		/** The game's custom color swatch, when it is unlocked and the building takes it. Finishes go with it. */
		UClass* CustomSwatch = nullptr;
		TArray<FCustomizeOption> Finishes;
		/** Index into Materials of the one the building is made of now. */
		int32 CurrentMaterial = INDEX_NONE;
		/** Why materials are offered but cannot be changed right now, for example with other players connected. */
		FText MaterialNote;
	};

	/**
	 * The selected building's own settings beyond production: names, load modes, switches, flow limits, lights.
	 * Each b-flag says the building has that setting; the panel shows only those. Read every frame.
	 */
	struct FControls
	{
		bool bHasName = false;   // Train, truck and drone stations, and anything with a building tag
		FText Name;
		bool bHasLoadMode = false; // Freight and fluid platforms, truck stations
		bool bLoadMode = true;
		bool bHasSwitch = false;   // Power switches
		bool bSwitchOn = false;
		bool bHasFlowLimit = false; // Pumps and valves, in m3/s
		float FlowLimit = -1.f;     // -1 means no limit
		float MaxFlow = 0.f;
		bool bHasLight = false;
		bool bLightOn = true;
		bool bNightOnly = false;
		float Intensity = 1.f;
		int32 LightColorSlot = 0;
		TArray<FLinearColor> LightColors; // The game's light color slots, shared by every light

		bool Any() const { return bHasName || bHasLoadMode || bHasSwitch || bHasFlowLimit || bHasLight; }
	};

	/** The selected building's customization as it is now. Read every frame. */
	struct FCustomizeState
	{
		UClass* Swatch = nullptr;
		UClass* Pattern = nullptr;
		UClass* Material = nullptr;
		uint8 PatternRotation = 0;
		bool bCustom = false; // Painted with the custom color swatch; the colors below apply
		FLinearColor CustomPrimary = FLinearColor::White;   // Linear, as the game stores them
		FLinearColor CustomSecondary = FLinearColor::White;
		UClass* Finish = nullptr;
	};

	/** Everything on the Settings tab. Saved between sessions. */
	struct FSettings
	{
		bool bHoverLabel = true;
		bool bHoverHeight = true;
		bool bHoverCoordinates = true;
		bool bHighlight = true;
		bool bOutlines = true;
		bool bRadiation = false;
		bool bRecipeIcons = true;
		bool bStatusLights = false; // A light in each machine's corner showing what it is doing
		bool bHideSpoilers = false; // Spoiler-free: no collectibles, resource nodes, wells, spore flowers, gas pillars or radioactivity
		float Opacity = 1.f;
		int32 ExportSize = 8192;
		int32 ZoomFactor = 4; // Times the game's own zoom limit
		int32 BeltTierFromTop = 0; // Belts built from the map: 0 is the highest unlocked Mk, 1 the one below, and so on
		int32 PipeTierFromTop = 0; // The same for pipes
		bool bAllowClipping = true; // Build from the map through other buildings, ignoring only clearance overlaps
	};

	/** A production building and the icon of what it makes, drawn on top of it when the map is zoomed in. */
	struct FRecipeIcon
	{
		FVector Location = FVector::ZeroVector;
		float HalfCm = 0.f; // Half the building's shorter side
		int32 Type = 0;
		UTexture2D* Icon = nullptr;
	};

	struct FExportState
	{
		std::atomic<int32> Stage{ 0 }; // 0 idle, 1 working, 2 done, 3 failed
		FString Path;
	};

	/** Which way a measure box's copies run, as seen on the map. */
	enum class EMeasureDirection : uint8 { Right, Down, Left, Up, Count };

	/**
	 * A rectangle measured on the map. Map metres run east (X) and south (Y) from the map's north-west corner.
	 * The box lives in its own grid: Origin and Angle place it, Pitch is the snapping step, and Min and Max are
	 * its corners in that grid's metres.
	 */
	struct FMeasureBox
	{
		FVector2f Origin = FVector2f::ZeroVector;
		float Angle = 0.f;
		float Pitch = 1.f;
		bool bFoundationGrid = false;
		FVector2f Min = FVector2f::ZeroVector;
		FVector2f Max = FVector2f::ZeroVector;
		int32 Copies = 0;
		EMeasureDirection Direction = EMeasureDirection::Right;

		// Worked out when the box, the buildings or what the map shows change
		bool bHasHeight = false;
		float LowMetres = 0.f;
		float HighMetres = 0.f;
		int32 Floors = 0;
		TArray<bool> Blocked; // One per copy

		FVector2f Size() const { return Max - Min; }
		/** A point in this box's grid, in map metres. */
		FVector2f ToMap(const FVector2f& Local) const;
		FVector2f ToLocal(const FVector2f& Map) const;
		/** Where copy Index (0 is the box itself) is shifted to, in the box's grid. */
		FVector2f CopyOffset(int32 Index) const;
		/** The four corners of copy Index, in map metres, going round. */
		void Corners(int32 Index, FVector2f Out[4]) const;
	};

	struct FMeasureLine
	{
		FVector2f A = FVector2f::ZeroVector;
		FVector2f B = FVector2f::ZeroVector;
	};

	/** Measure mode and everything measured, for one view. */
	struct FMeasureView
	{
		bool bMode = false;
		TArray<FMeasureBox> Boxes;
		TArray<FMeasureLine> Lines;
		int32 Active = INDEX_NONE;

		bool bPressed = false;
		bool bLineDrag = false;
		bool bMoved = false;
		FVector2f PressMetres = FVector2f::ZeroVector;
		FMeasureBox Draft;
		FMeasureLine DraftLine;

		/** Buildings in the way of a copy, as BlockedType records. */
		TArray<FVector4f> Hits;
		uint32 HitsVersion = 0;
		uint64 StatsKey = 0;
	};

	/**
	 * Power wire mode (AtlasWires.cpp): click a building, then another, and a power line runs between them through
	 * the game's own wire hologram. Clicking the ground puts a power pole there, as the build gun does. Each wire
	 * starts the next from where it ended, so a run of clicks chains buildings together.
	 */
	struct FWireTool
	{
		bool bMode = false;
		EView View = EView::Game;
		bool bHasStart = false;
		TWeakObjectPtr<UFGCircuitConnectionComponent> Start;
		TWeakObjectPtr<AFGBuildable> StartOwner;
		FVector StartLocation = FVector::ZeroVector;
		bool bHasTarget = false; // A building with a power connection under the cursor
		TWeakObjectPtr<AFGBuildable> Target;
		TWeakObjectPtr<UFGCircuitConnectionComponent> TargetConnection;
		bool bHasGround = false; // No building, but ground a pole can go on
		FVector EndLocation = FVector::ZeroVector;
		bool bReady = false;     // The hologram has settled on what is under the cursor, so Valid and Reason can be trusted
		bool bValid = false;
		bool bClipping = false;
		FText Reason;
		FText WireName;
		FString CostText;
		float LengthMetres = 0.f;
	};
}

/**
 * Owns every map record for the loaded world.
 *
 * Nothing here ticks. The first time the map opens, the world is scanned once; from then on building changes
 * are appended to a queue in constant time. The queue is applied on a worker thread, which also keeps each
 * cell sorted by height, so the game thread never pays for it. A worker owns the grid while a job runs, so the
 * game thread only reads it when IsBusy() is false.
 */
UCLASS()
class FACTORYATLAS_API UAtlasSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	static UAtlasSubsystem* GetActive() { return Active; }

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Data
	void OnMapOpened();
	void EnsureScanned();
	void Pump();
	bool IsScanned() const { return bScanned; }
	bool IsBusy() const { return bJobActive; }
	const Atlas::FGrid* GetGrid() const { return bJobActive ? nullptr : Grid.Get(); }

	void HandleLightweight(UClass* Class, const FRuntimeBuildableInstanceData& Data, bool bAdded);

	// View state
	const TArray<Atlas::FType>& GetTypes() const { return Types; }
	bool IsCategoryHidden(Atlas::ECategory Category) const { return HiddenCategories[int32(Category)]; }
	void SetCategoryHidden(Atlas::ECategory Category, bool bHidden);
	void SetTypeHidden(int32 Type, bool bHidden);
	bool IsPaintMode() const { return bPaintMode; }
	void SetPaintMode(bool bEnabled);
	bool IsLayerEnabled() const { return bLayerEnabled; }
	void SetLayerEnabled(bool bEnabled);
	bool IsPanelOpen() const { return bPanelOpen; }
	void SetPanelOpen(bool bOpen);
	/** The map fills the whole map screen, without the game's window frame and filter list. */
	bool IsFullscreen() const { return bFullscreen; }
	void SetFullscreen(bool bInFullscreen);
	/** The building panel's color, pattern and material section is open rather than folded to one row. */
	bool IsAppearanceOpen() const { return bAppearanceOpen; }
	void SetAppearanceOpen(bool bOpen);
	const Atlas::FSettings& GetSettings() const { return Settings; }
	Atlas::FSettings& EditSettings() { return Settings; }
	void ResetSettings();
	void SaveSettings() const;
	/** Pulls the toggles that live in the mod settings menu (Mods > Atlas) into Settings. */
	void SyncModConfig();

	// Colours chosen by the player, per category or per building type
	FColor GetCategoryDisplayColor(Atlas::ECategory Category) const;
	bool HasCategoryColor(Atlas::ECategory Category) const { return CategoryColorOverrides[int32(Category)].IsSet(); }
	bool HasTypeColor(int32 Type) const;
	void SetCategoryColor(Atlas::ECategory Category, TOptional<FColor> Color);
	void SetTypeColor(int32 Type, TOptional<FColor> Color);
	void ResetColors();

	/** Circles around radioactive items, refreshed at most once a second. */
	const TArray<FVector4f>& GetRadiation(uint32& OutVersion);

	/** Uncollected Somersloops, Mercer Spheres, hard drives and power slugs, refreshed every few seconds. */
	const TArray<FVector4f>& GetMarkers(uint32& OutVersion);
	bool IsCollectibleShown(Atlas::ECollectible Kind) const { return !Settings.bHideSpoilers && ((CollectibleMask >> int32(Kind)) & 1); }
	void SetCollectibleShown(Atlas::ECollectible Kind, bool bShown);
	bool AnyMarkersShown() const { return !Settings.bHideSpoilers && (CollectibleMask != 0 || FeatureMask != 0); }
	bool IsRadiationShown() const { return Settings.bRadiation && !Settings.bHideSpoilers; }
	bool AreSpoilersHidden() const { return Settings.bHideSpoilers; }
	void SetSpoilersHidden(bool bHidden);
	int32 GetCollectibleCount(Atlas::ECollectible Kind) const { return CollectibleCounts[int32(Kind)]; }

	/** Name for the hover label, building or collectible. */
	FText GetHoverName() const;

	// Resource nodes, wells, spore flowers and gas pillars
	struct FResourceKind
	{
		UClass* Descriptor = nullptr;
		FText Name;
		FColor Color;
		bool bWell = false;
		int32 Count = 0;
	};
	bool IsFeatureShown(Atlas::EFeature Feature) const { return !Settings.bHideSpoilers && ((FeatureMask >> int32(Feature)) & 1); }
	void SetFeatureShown(Atlas::EFeature Feature, bool bShown);
	int32 GetFeatureCount(Atlas::EFeature Feature) const { return FeatureCounts[int32(Feature)]; }
	/** Resource kinds found so far, in marker order (ResourceFirst + index). */
	const TArray<FResourceKind>& GetResourceKinds() const { return ResourceKinds; }
	bool IsResourceHidden(UClass* Descriptor) const;
	void SetResourceHidden(UClass* Descriptor, bool bHidden);
	bool IsMarkerShown(int32 Type) const;

	// Selection and configuration
	/** Selects the building under the cursor. Returns false when it has nothing to configure or customize. */
	bool SelectHovered();
	void ClearSelection();
	bool HasSelection() const;
	const FVector4f& GetSelectionRecord() const { return SelectionRecord; }
	/** Where the map last drew the selected building, in window pixels, and how far it reaches from there. */
	void SetSelectionAnchor(const FVector2f& Absolute, float RadiusPixels, Atlas::EView View = Atlas::EView::Game)
	{
		SelectionAnchor[int32(View)] = Absolute;
		SelectionAnchorRadius[int32(View)] = RadiusPixels;
	}
	const FVector2f& GetSelectionAnchor(Atlas::EView View = Atlas::EView::Game) const { return SelectionAnchor[int32(View)]; }
	float GetSelectionAnchorRadius(Atlas::EView View = Atlas::EView::Game) const { return SelectionAnchorRadius[int32(View)]; }
	Atlas::FFactoryState GetSelectionState() const;
	void SetSelectedClock(float Potential);
	void SetSelectedPaused(bool bPaused);

	/** Recipes the selected manufacturer can run, as the game offers them at the machine. */
	TArray<Atlas::FRecipeOption> GetSelectedRecipeOptions() const;
	/** Changes the recipe the same way the machine's own screen does. Returns false if the game refused. */
	bool SetSelectedRecipe(UClass* Recipe);

	// Customization of the selection: colors, patterns and materials
	/** Goes up whenever the selection or the options it offers change, so the panel knows to rebuild its lists. */
	uint32 GetSelectionVersion() const { return SelectionVersion; }
	const Atlas::FCustomizeOptions& GetCustomizeOptions() const { return CustomizeOptions; }
	Atlas::FCustomizeState GetCustomizeState() const;
	void SetSelectedSwatch(UClass* Swatch);
	/** Paints the selection with the custom swatch. Colors are linear; Finish is nullptr or one of the offered finishes. */
	void SetSelectedCustomColor(const FLinearColor& Primary, const FLinearColor& Secondary, UClass* Finish);
	/** nullptr removes the pattern. */
	void SetSelectedPattern(UClass* Pattern);
	void RotateSelectedPattern();
	void SetSelectedMaterial(UClass* Material);
	/** The last customization the game refused, with the reason. Empty after one succeeds. */
	const FText& GetCustomizeError() const { return CustomizeError; }

	// Building settings: names, load modes, switches, flow limits, lights. All need the host.
	Atlas::FControls GetControls() const;
	void SetSelectedName(const FText& Name);
	void SetSelectedLoadMode(bool bLoad);
	void SetSelectedSwitch(bool bOn);
	/** m3/s; negative removes the limit. */
	void SetSelectedFlowLimit(float Limit);
	void SetSelectedLight(bool bOn, bool bNightOnly, float Intensity, int32 ColorSlot);

	// Copy and paste. Machine settings go through the game's own clipboard, the one Ctrl+C and Ctrl+V on a
	// building use, so the two share it. Color, pattern and material are kept here, since the game has no clipboard for them.
	bool CanCopySelected() const;
	void CopySelected();
	/** True when the clipboard holds anything the selection can take. */
	bool CanPasteToSelected() const;
	void PasteToSelected();
	/** What the last copy or paste did, for the panel. Cleared when the selection changes. */
	const FText& GetClipboardStatus() const { return ClipboardStatus; }
	void SetClipboardStatus(const FText& Status) { ClipboardStatus = Status; }

	/**
	 * Ctrl-clicks on the map: copy from, or paste onto, the building under the cursor without selecting it, so
	 * the panel stays as it was. Done collects what a drag-paste has reached, so each building is pasted once.
	 * False when there is no usable building there. What happened is shown as a toast.
	 */
	bool CopyHovered();
	bool PasteHovered(TSet<const void*>* Done = nullptr);
	void ShowToast(const FText& Text, bool bError = false);
	/** Plays one of the Ctrl-click sounds. A sound asked for again within a moment of the last one is skipped. */
	void PlayClipboardSound(Atlas::EClipboardSound Sound);
	/** A short message by the cursor, while it lasts. */
	bool GetToast(FText& OutText, bool& bOutError, float& OutAlpha) const;
	/** Tells one selected building from another: its actor, or its instance's data for a lightweight. */
	const void* GetSelectionIdentity() const;

	/** The map widget's Slate widget, used to tell clicks on the map from clicks on menus drawn over it. */
	void SetMapWidget(const TSharedPtr<SWidget>& Widget, Atlas::EView View = Atlas::EView::Game) { MapWidget[int32(View)] = Widget; }
	bool IsPointOnMap(const FVector2f& ScreenPosition, Atlas::EView View = Atlas::EView::Game) const;

	/** Lowest and highest building on the map, in metres. */
	FVector2f GetHeightBounds() const;
	FVector2f GetHeightFilter() const { return FVector2f(FilterLow, FilterHigh); }
	void SetHeightFilter(float Low, float High);

	UTexture2D* GetPalette();
	void FlushPalette();

	/** Changes whenever the list of building types or their counts change; each menu keeps the one it built. */
	uint32 GetMenuVersion();

	// Hover
	void UpdateHover(FVector2f Uv, float PixelsPerMetre, Atlas::EView View = Atlas::EView::Game);
	void ClearHover() { Hover = Atlas::FHover(); }
	/** Clears the hover only if this view set it, so the other view's stays. */
	void ClearHover(Atlas::EView View) { if (HoverView == View) { ClearHover(); } }
	const Atlas::FHover& GetHover() const { return Hover; }
	/** The view the hover was found in; only that view draws its label and highlight. */
	Atlas::EView GetHoverView() const { return HoverView; }
	void SetPointerOverPanel(bool bOver, Atlas::EView View = Atlas::EView::Game);
	bool IsPointerOverPanel(Atlas::EView View = Atlas::EView::Game) const { return bPointerOverPanel[int32(View)]; }
	/** During a drag-paste the hover is worked out every frame rather than a few times a second, so no building is skipped. */
	void SetHoverEveryFrame(bool bEveryFrame) { bHoverEveryFrame = bEveryFrame; }
	bool WantsHoverEveryFrame() const { return bHoverEveryFrame; }

	// Export
	void StartExport();
	FText GetExportStatus() const;

	// Recipe icons
	/** Production buildings with a recipe set. Rebuilt at most twice a second, so recipe changes show up quickly. */
	const TArray<Atlas::FRecipeIcon>& GetRecipeIcons();

	// Building belts from the map (AtlasBelts.cpp)
	/** Port nodes show from this zoom on, in pixels per metre. */
	static constexpr float PortMinPixelsPerMetre = 1.5f;
	/** Where the cursor is on a view's map, set by that view every paint, so ports under it can be found. */
	void SetMapCursor(Atlas::EView View, FVector2f Uv, float PixelsPerMetre);
	/** Free conveyor ports of buildings the map shows, or none when belts can't be built here (no authority). */
	const TArray<Atlas::FPort>& GetPorts();
	/** The port under the cursor in this view, or INDEX_NONE. During a drag, only ports the belt could end on. */
	int32 GetPortUnderCursor(Atlas::EView View);
	bool CanBuildBelts() const;
	/** A press on the map: true when it landed on a port, which then owns the press until release. */
	bool PressBeltPort(Atlas::EView View, const FVector2f& ScreenPosition);
	/** The pointer moved with the button held; past a few pixels the press becomes a drag. */
	void MoveBeltPress(const FVector2f& ScreenPosition);
	/** The button came up. True when the press never moved, so it counts as an ordinary click on the building. */
	bool ReleaseBeltPress();
	void CancelBeltDrag();
	void CycleBeltBuildMode();
	void CycleBeltTier();
	bool IsBeltPressed() const { return BeltDrag.bPressed; }
	bool IsDraggingBelt() const { return BeltDrag.bActive; }
	const Atlas::FBeltDrag& GetBeltDrag() const { return BeltDrag; }
	/** While dragging: whether the drag could end on this port. */
	bool IsTargetFor(const Atlas::FPort& Candidate) const;
	FVector2f GetMapCursorUv(Atlas::EView View) const { return MapCursorUv[int32(View)]; }
	/** Where map UV (0,0) is drawn and how many pixels the whole map spans, in paint space, set by each view every paint. */
	void SetMapTransform(Atlas::EView View, FVector2f Origin, FVector2f Scale)
	{
		MapOrigin[int32(View)] = Origin;
		MapScale[int32(View)] = Scale;
	}
	/** A point in map metres, in the view's paint space. */
	FVector2f MetresToPaint(Atlas::EView View, const FVector2f& Metres) const
	{
		return MapOrigin[int32(View)] + MapScale[int32(View)] * (Metres / Atlas::MapWidthMetres);
	}

	// Power wires (AtlasWires.cpp)
	bool IsWiring(Atlas::EView View) const { return Wire.bMode && Wire.View == View; }
	/** Wire mode on or off for a view. On turns measure mode off there; only one view wires at a time. */
	void SetWiring(Atlas::EView View, bool bOn);
	/** A click on the map in wire mode: starts a wire on the building under the cursor, or ends it there. */
	void ClickWire(Atlas::EView View);
	/** Lets go of the wire being run, staying in wire mode. False when there was none. */
	bool DropWire();
	const Atlas::FWireTool& GetWire() const { return Wire; }

	// Status lights (AtlasStatus.cpp)
	/** One record per machine with a light, drawn by the status material. Null when the lights are off. */
	const Atlas::FGrid* GetStatusGrid() const { return bStatusActive ? StatusGrid.Get() : nullptr; }
	/** The light on the hovered building, None when it has none or the lights are off. */
	Atlas::EStatusLight GetHoverStatus() const;
	/** atlas.StatusDump: every machine's status by class, and whether the lights agree with it. */
	void DumpStatuses();

	// Measure tool (AtlasMeasure.cpp)
	bool IsMeasuring(Atlas::EView View) const { return Measure[int32(View)].bMode; }
	void SetMeasuring(Atlas::EView View, bool bOn);
	const Atlas::FMeasureView& GetMeasure(Atlas::EView View) const { return Measure[int32(View)]; }
	/** A left press on the map in measure mode: starts a box, or a line when bLine. */
	void PressMeasure(Atlas::EView View, bool bLine);
	void MoveMeasure(Atlas::EView View);
	/** Ends the drag. A press that never moved makes the box under the cursor the active one. */
	void ReleaseMeasure(Atlas::EView View);
	void CancelMeasureDrag(Atlas::EView View);
	void SetMeasureCopies(Atlas::EView View, int32 Copies);
	void SetMeasureDirection(Atlas::EView View, Atlas::EMeasureDirection Direction);
	void RemoveActiveMeasure(Atlas::EView View);
	void ClearMeasures(Atlas::EView View);
	/** Brings heights, floors and blocked copies up to date when the boxes, buildings or filters changed. */
	void UpdateMeasureStats(Atlas::EView View);
	const TArray<FVector4f>& GetMeasureHits(Atlas::EView View, uint32& OutVersion) const
	{
		OutVersion = Measure[int32(View)].HitsVersion;
		return Measure[int32(View)].Hits;
	}

private:
	UFUNCTION()
	void OnBuildableAdded(AFGBuildable* Buildable);
	UFUNCTION()
	void OnBuildableRemoved(AFGBuildable* Buildable);

	int32 TypeFor(UClass* Class);
	int32 PaintFor(const FFactoryCustomizationData& Data);
	void QueueActor(AFGBuildable* Buildable, bool bAdded);
	void Scan();
	void LaunchJob(TSharedPtr<Atlas::FGrid> Replacement);
	void FinishJob();
	void RebuildPalette();
	void LoadSettings();
	void ApplyTypeColor(int32 Type);
	void BindBuildableDelegates(bool bBind);

	inline static UAtlasSubsystem* Active = nullptr;

	TSharedPtr<Atlas::FGrid> Grid;
	TSharedPtr<Atlas::FGrid> PendingGrid;
	TArray<Atlas::FOp> PendingOps;
	UE::Tasks::FTask Job;
	TSharedPtr<std::atomic<bool>> JobRunning;
	bool bJobActive = false;
	bool bScanned = false;
	bool bBound = false;
	bool bWantsRescan = false;
	double LastScanSeconds = 0.0;
	int32 BuiltStress = 0;

	TArray<Atlas::FType> Types;
	TMap<UClass*, int32> TypeIndex;

	TArray<FColor> PaintColors;
	TMap<FColor, int32> PaintIndex;
	TMap<UClass*, int32> SwatchIndex;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Palette;

	/** Loaded on first use, by EClipboardSound. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> ClipboardSounds;
	double LastClipboardSoundSeconds = 0.0;
	TArray<FColor> PaletteData;
	bool bPaletteDirty = true;

	bool HiddenCategories[int32(Atlas::ECategory::Count)] = {};
	TSet<FString> HiddenTypePaths;
	bool bPaintMode = false;
	bool bLayerEnabled = true;
	bool bPanelOpen = true;
	bool bFullscreen = false;
	bool bAppearanceOpen = false;
	bool bMenuDirty = true;
	Atlas::FSettings Settings;
	double LastConfigSyncSeconds = -1000.0;

	TOptional<FColor> CategoryColorOverrides[int32(Atlas::ECategory::Count)];
	TMap<FString, FColor> TypeColorOverrides;

	TArray<FVector4f> Radiation;
	uint32 RadiationVersion = 0;
	double LastRadiationSeconds = -1.0;

	void RebuildMarkers();
	TArray<FVector4f> Markers;
	uint32 MarkerVersion = 0;
	double LastMarkerSeconds = -1000.0;
	uint32 CollectibleMask = 0;
	int32 CollectibleCounts[int32(Atlas::ECollectible::Count)] = {};
	TMap<UClass*, int32> MarkerOfClass;

	void GatherCollectibles();
	void GatherWorldFeatures();
	int32 ResourceMarkerFor(UClass* Descriptor, bool bWell);
	void LoadFeatureCache();
	void SaveFeatureCache() const;
	uint32 FeatureMask = 0;
	int32 FeatureCounts[int32(Atlas::EFeature::Count)] = {};
	TArray<FResourceKind> ResourceKinds;
	TSet<FString> HiddenResourcePaths;
	/** Spore flowers and gas pillars seen so far, by actor name. They only exist while their part of the world is loaded. */
	TMap<FString, FVector> SeenSporeFlowers;
	TMap<FString, FVector> SeenGasPillars;
	bool bFeatureCacheLoaded = false;

	TWeakObjectPtr<AFGBuildable> Selected;
	/** Foundations, walls and the like have no actor; the game keeps them as instances of their class. */
	FLightweightBuildableInstanceRef SelectedLightweight;
	bool bSelectedLightweight = false;
	uint32 SelectionVersion = 0;
	Atlas::FCustomizeOptions CustomizeOptions;
	FText CustomizeError;
	/** Keeps the option icons loaded; the descriptors hold them only through soft pointers. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> CustomizeIcons;
	void RefreshCustomizeOptions();
	void SetSelectedPatternRotation(uint8 Rotation);
	/** The copied look: whole customization data, plus the material by descriptor and name, since material families differ. */
	TOptional<FFactoryCustomizationData> CopiedCustomization;
	UClass* CopiedMaterial = nullptr;
	FText CopiedMaterialName;
	/** The machine's recipe and clock, applied after the game's own paste, which leaves the recipe alone from here. */
	UClass* CopiedRecipe = nullptr;
	TOptional<float> CopiedClock;
	/** What the clipboard was copied from, so it only pastes onto the same kind of building. */
	UClass* CopiedClass = nullptr;
	Atlas::ECategory CopiedCategory = Atlas::ECategory::Other;
	bool bCopiedSettings = false; // Machine settings, recipe or clock came along, not just the look
	/** Whether the selection is a building the clipboard may paste onto. */
	bool PasteFitsSelection() const;
	UClass* SelectionClass() const;
	/** Selects the hovered building, runs Action on it, then puts the previous selection back. */
	bool ActOnHovered(TFunctionRef<void()> Action, TSet<const void*>* Done);
	FText Toast;
	bool bToastError = false;
	double ToastUntil = 0.0;
	FText ClipboardStatus;
	/** Applies new customization data to the selection and redraws it on the map. */
	bool ApplySelectedCustomization(const FFactoryCustomizationData& Data);
	FVector4f SelectionRecord = FVector4f::Zero();
	FVector2f SelectionAnchor[int32(Atlas::EView::Count)] = {};
	float SelectionAnchorRadius[int32(Atlas::EView::Count)] = {};
	TWeakPtr<SWidget> MapWidget[int32(Atlas::EView::Count)];
	float FilterLow = -200.f;
	float FilterHigh = 3895.f;

	TSet<TWeakObjectPtr<AFGBuildableManufacturer>> Manufacturers;
	TArray<Atlas::FRecipeIcon> RecipeIcons;
	TMap<UClass*, UTexture2D*> RecipeIconCache;
	double RecipeIconsSeconds = -1.0;
	bool bRecipeIconsDirty = true;

	Atlas::FHover Hover;
	Atlas::EView HoverView = Atlas::EView::Game;
	bool bPointerOverPanel[int32(Atlas::EView::Count)] = {};
	uint32 MenuVersion = 0;
	bool bHoverEveryFrame = false;

	// Belts from the map
	FVector2f MapCursorUv[int32(Atlas::EView::Count)] = {};
	float MapCursorPixelsPerMetre[int32(Atlas::EView::Count)] = {};
	TArray<Atlas::FPort> AllPorts; // Every free port, rebuilt when buildings change
	TArray<Atlas::FPort> Ports;    // Those the map shows this frame
	TArray<Atlas::FPort> NoPorts;
	uint64 PortsFrame = MAX_uint64;
	bool bPortsDirty = true;
	double PortsBuiltSeconds = -1000.0;
	Atlas::FBeltDrag BeltDrag;
	TWeakObjectPtr<class AFGHologram> BeltHologram;
	TArray<TSubclassOf<class UFGBuildGunModeDescriptor>> BeltModes;
	int32 BeltModeIndex = 0;
	uint8 BeltModeKind = 0; // The port kind BeltModeIndex was chosen for
	int32 BeltStage = 0;
	int32 BeltWaitFrames = 0;
	int32 BeltStepTries = 0;
	int32 BeltHitVariant = 0; // Which way of aiming at a port the hologram accepted, see HitAtPort
	int32 BeltSettleFrames = 0;
	FTSTicker::FDelegateHandle BeltTicker;
	void RebuildPorts();
	bool IsPortShown(const Atlas::FPort& Port);
	int32& TierFromTop(Atlas::EPortKind Kind);
	TSubclassOf<class UFGRecipe> PickBeltRecipe(FText& OutName) const;
	bool SpawnBeltHologram();
	void DestroyBeltHologram();
	bool TickBelt();
	bool PlaceBeltEnd(const Atlas::FPort& Port);
	void ValidateBelt();
	void PreviewBeltAtCursor();
	void ReadBeltPreview();
	void BuildBelt();
	class AFGCharacterPlayer* BeltPlayer() const;
	/** Takes a build's cost as the build gun does: inventory first, then the Dimensional Depot. False if short. */
	bool PayBuildCost(const TArray<struct FItemAmount>& Cost);
	uint64 MapPaintFrame[int32(Atlas::EView::Count)] = {};

	// Power wires
	Atlas::FWireTool Wire;
	TWeakObjectPtr<class AFGHologram> WireHologram;
	FTSTicker::FDelegateHandle WireTicker;
	int32 WireStage = 0;
	int32 WireWaitFrames = 0;
	int32 WireSettleFrames = 0;
	int32 WireHitVariant = 0;
	int32 WireEndVariant = 0;
	FVector4f WireHoverSource = FVector4f::Zero();
	TWeakObjectPtr<AFGBuildable> WireHoverActor;
	bool bWireHoverResolved = false;
	bool TickWire();
	bool SpawnWireHologram();
	void DestroyWireHologram();
	void ResolveWireTarget();
	void ValidateWire();
	void BuildWire();
	void SetWireStart(UFGCircuitConnectionComponent* Connection);
	/** The building whose record is under the cursor, for buildings that are actors (not foundations or walls). */
	AFGBuildable* FindHoveredActor();

	TSharedPtr<Atlas::FExportState> ExportState;

	FVector2f MapOrigin[int32(Atlas::EView::Count)] = {};
	FVector2f MapScale[int32(Atlas::EView::Count)] = {};
	/** Goes up whenever the grid changes, so the measure tool knows to look again. */
	uint32 GridChanges = 0;
	/** Goes up whenever the palette is rebuilt, which is when what the map shows changes. */
	uint32 PaletteBuilds = 0;

	// Status lights
	struct FStatusEntry
	{
		FVector4f Record = FVector4f::Zero(); // The building's record, paint bits cleared
		/** Written by the game's status event, which may fire off the game thread; read by UpdateStatusLights. */
		TSharedPtr<std::atomic<uint8>> Wanted;
		uint8 Shown = 0;
		FDelegateHandle Handle;
	};
	TMap<TWeakObjectPtr<AFGBuildable>, FStatusEntry> StatusEntries;
	TSharedPtr<Atlas::FGrid> StatusGrid;
	/** Set by any status event, so the lights are only looked through when something changed. */
	TSharedPtr<std::atomic<bool>> StatusChanged;
	bool bStatusActive = false;
	bool bStatusDirty = true;
	int8 StatusSafe = -1; // -1 not checked yet, 0 unsafe on this game version, 1 checked
	double LastStatusApplySeconds = 0.0;
	void UpdateStatusLights();
	void RebuildStatusLights();
	void ClearStatusLights();
	void AddStatusLight(AFGBuildable* Buildable);
	void RemoveStatusLight(AFGBuildable* Buildable);
	bool HasStatusLight(AFGBuildable* Buildable);
	bool IsStatusEventSafe();
	/** The building's single map record, for buildings drawn as one box. */
	bool RecordOf(AFGBuildable* Buildable, FVector4f& Out);
	void SetStatusRecord(const FVector4f& Record, uint8 Code, bool bAdd);

	// Measure tool
	Atlas::FMeasureView Measure[int32(Atlas::EView::Count)];
	/** The topmost visible foundation at a map point, for a measure box's grid. */
	bool FindFoundationAt(const FVector2f& Metres, FVector4f& Out) const;
	bool IsRecordShown(const Atlas::FDecoded& Record) const;
	void SnapDraft(Atlas::FMeasureView& State, const FVector2f& Metres) const;
	FVector2f CursorMetres(Atlas::EView View) const { return MapCursorUv[int32(View)] * Atlas::MapWidthMetres; }
};
