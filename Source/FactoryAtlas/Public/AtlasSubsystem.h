#pragma once

#include "CoreMinimal.h"
#include "AtlasTypes.h"
#include "FGFactoryColoringTypes.h"
#include "FGLightweightBuildableSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tasks/Task.h"
#include "AtlasSubsystem.generated.h"

class AFGBuildable;
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
		float Opacity = 1.f;
		int32 ExportSize = 8192;
		int32 ZoomFactor = 4; // Times the game's own zoom limit
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
	bool IsCollectibleShown(Atlas::ECollectible Kind) const { return (CollectibleMask >> int32(Kind)) & 1; }
	void SetCollectibleShown(Atlas::ECollectible Kind, bool bShown);
	bool AnyMarkersShown() const { return CollectibleMask != 0 || FeatureMask != 0; }
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
	bool IsFeatureShown(Atlas::EFeature Feature) const { return (FeatureMask >> int32(Feature)) & 1; }
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
	void SetSelectionAnchor(const FVector2f& Absolute, float RadiusPixels) { SelectionAnchor = Absolute; SelectionAnchorRadius = RadiusPixels; }
	const FVector2f& GetSelectionAnchor() const { return SelectionAnchor; }
	float GetSelectionAnchorRadius() const { return SelectionAnchorRadius; }
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
	void SetMapWidget(const TSharedPtr<SWidget>& Widget) { MapWidget = Widget; }
	bool IsPointOnMap(const FVector2f& ScreenPosition) const;

	/** Lowest and highest building on the map, in metres. */
	FVector2f GetHeightBounds() const;
	FVector2f GetHeightFilter() const { return FVector2f(FilterLow, FilterHigh); }
	void SetHeightFilter(float Low, float High);

	UTexture2D* GetPalette();
	void FlushPalette();

	/** True once after the list of building types or their counts change. */
	bool ConsumeMenuDirty();

	// Hover
	void UpdateHover(FVector2f Uv, float PixelsPerMetre);
	void ClearHover() { Hover = Atlas::FHover(); }
	const Atlas::FHover& GetHover() const { return Hover; }
	void SetPointerOverPanel(bool bOver);
	bool IsPointerOverPanel() const { return bPointerOverPanel; }
	/** During a drag-paste the hover is worked out every frame rather than a few times a second, so no building is skipped. */
	void SetHoverEveryFrame(bool bEveryFrame) { bHoverEveryFrame = bEveryFrame; }
	bool WantsHoverEveryFrame() const { return bHoverEveryFrame; }

	// Export
	void StartExport();
	FText GetExportStatus() const;

	// Recipe icons
	/** Production buildings with a recipe set. Rebuilt at most twice a second, so recipe changes show up quickly. */
	const TArray<Atlas::FRecipeIcon>& GetRecipeIcons();

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
	/** Selects the hovered building, runs Action on it, then puts the previous selection back. */
	bool ActOnHovered(TFunctionRef<void()> Action, TSet<const void*>* Done);
	FText Toast;
	bool bToastError = false;
	double ToastUntil = 0.0;
	FText ClipboardStatus;
	/** Applies new customization data to the selection and redraws it on the map. */
	bool ApplySelectedCustomization(const FFactoryCustomizationData& Data);
	FVector4f SelectionRecord = FVector4f::Zero();
	FVector2f SelectionAnchor = FVector2f::ZeroVector;
	float SelectionAnchorRadius = 0.f;
	TWeakPtr<SWidget> MapWidget;
	float FilterLow = -200.f;
	float FilterHigh = 3895.f;

	TSet<TWeakObjectPtr<AFGBuildableManufacturer>> Manufacturers;
	TArray<Atlas::FRecipeIcon> RecipeIcons;
	TMap<UClass*, UTexture2D*> RecipeIconCache;
	double RecipeIconsSeconds = -1.0;
	bool bRecipeIconsDirty = true;

	Atlas::FHover Hover;
	bool bPointerOverPanel = false;
	bool bHoverEveryFrame = false;

	TSharedPtr<Atlas::FExportState> ExportState;
};
