#pragma once

#include "CoreMinimal.h"
#include "AtlasTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tasks/Task.h"
#include "AtlasSubsystem.generated.h"

class AFGBuildable;
class AFGGameState;
class UTexture2D;
struct FFactoryCustomizationData;
struct FRuntimeBuildableInstanceData;

namespace Atlas
{
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

	/** Everything on the Settings tab. Saved between sessions. */
	struct FSettings
	{
		bool bHoverLabel = true;
		bool bHoverHeight = true;
		bool bHoverCoordinates = true;
		bool bHighlight = true;
		bool bOutlines = true;
		bool bRadiation = false;
		float Opacity = 1.f;
		int32 ExportSize = 8192;
		int32 ZoomFactor = 4; // Times the game's own zoom limit
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
	/** Selects the factory under the cursor. Returns false when the hover is not a configurable building. */
	bool SelectHovered();
	void ClearSelection();
	bool HasSelection() const { return Selected.IsValid(); }
	const FVector4f& GetSelectionRecord() const { return SelectionRecord; }
	Atlas::FFactoryState GetSelectionState() const;
	void SetSelectedClock(float Potential);
	void SetSelectedPaused(bool bPaused);

	/** Recipes the selected manufacturer can run, as the game offers them at the machine. */
	TArray<Atlas::FRecipeOption> GetSelectedRecipeOptions() const;
	/** Changes the recipe the same way the machine's own screen does. Returns false if the game refused. */
	bool SetSelectedRecipe(UClass* Recipe);

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

	// Export
	void StartExport();
	FText GetExportStatus() const;

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
	TArray<FColor> PaletteData;
	bool bPaletteDirty = true;

	bool HiddenCategories[int32(Atlas::ECategory::Count)] = {};
	TSet<FString> HiddenTypePaths;
	bool bPaintMode = false;
	bool bLayerEnabled = true;
	bool bPanelOpen = true;
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
	FVector4f SelectionRecord = FVector4f::Zero();
	TWeakPtr<SWidget> MapWidget;
	float FilterLow = -200.f;
	float FilterHigh = 3895.f;

	Atlas::FHover Hover;
	bool bPointerOverPanel = false;

	TSharedPtr<Atlas::FExportState> ExportState;
};
