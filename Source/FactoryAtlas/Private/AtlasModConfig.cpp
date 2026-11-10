#include "AtlasModConfig.h"

#include "AtlasSubsystem.h"
#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/WidgetExtension/CP_Bool.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "FactoryAtlas.h"

namespace
{
	const TCHAR* const ModReference = TEXT("FactoryAtlas");

	// The bare native UConfigProperty classes save fine but draw no widget in the mod settings menu;
	// the widget logic only exists on SML's Blueprint subclasses.
	UClass* LoadPropertyClass(const TCHAR* Path, UClass* Fallback)
	{
		UClass* Loaded = StaticLoadClass(UConfigProperty::StaticClass(), nullptr, Path);
		if (!Loaded)
		{
			UE_LOG(LogFactoryAtlas, Error, TEXT("Could not load %s; the mod settings menu will show no widget for it"), Path);
			return Fallback;
		}
		return Loaded;
	}

	UClass* SectionClass()
	{
		static UClass* Cached = LoadPropertyClass(
			TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertySection.BP_ConfigPropertySection_C"),
			UConfigPropertySection::StaticClass());
		return Cached;
	}

	UClass* BoolClass()
	{
		static UClass* Cached = LoadPropertyClass(
			TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/BP_ConfigPropertyBool.BP_ConfigPropertyBool_C"),
			UConfigPropertyBool::StaticClass());
		return Cached;
	}

	UCP_Section* AddSection(UConfigPropertySection* Parent, FName Name, const TCHAR* Header)
	{
		UCP_Section* Section = NewObject<UCP_Section>(Parent, SectionClass(), Name);
		Section->DisplayName = FText::FromString(Header);
		Section->HasHeader = true;
		Section->HeaderText = FText::FromString(Header);
		Section->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
		Parent->SectionProperties.Add(Name.ToString(), Section);
		return Section;
	}

	void AddBool(UConfigPropertySection* Section, FName Name, const TCHAR* Label, const TCHAR* Tooltip, bool bDefault)
	{
		UCP_Bool* Property = NewObject<UCP_Bool>(Section, BoolClass(), Name);
		Property->DisplayName = FText::FromString(Label);
		Property->Tooltip = FText::FromString(Tooltip);
		Property->DefaultValue = bDefault;
		Property->Value = bDefault;
		// Defaults to true on the Blueprint property classes, which locks editing to the main menu.
		Property->bRequiresWorldReload = false;
		Section->SectionProperties.Add(Name.ToString(), Property);
	}

	void ReadBool(const UConfigPropertySection* Section, const TCHAR* Name, bool& Out)
	{
		if (const UConfigPropertyBool* Property = Cast<UConfigPropertyBool>(Section ? Section->SectionProperties.FindRef(Name) : nullptr))
		{
			Out = Property->Value;
		}
	}
}

UAtlasModConfig::UAtlasModConfig(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ConfigId.ModReference = ModReference;
	DisplayName = FText::FromString(TEXT("Atlas"));
	Description = FText::FromString(TEXT("What the map shows. Changes apply immediately."));

	UCP_Section* Root = NewObject<UCP_Section>(this, SectionClass(), TEXT("RootSection"));
	Root->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
	RootSection = Root;

	const Atlas::FSettings Defaults;

	UCP_Section* Hover = AddSection(Root, TEXT("Hover"), TEXT("Hover"));
	AddBool(Hover, TEXT("Label"), TEXT("Show building label"),
		TEXT("Shows the name of the building under the cursor."), Defaults.bHoverLabel);
	AddBool(Hover, TEXT("Height"), TEXT("Show height"),
		TEXT("Adds the hovered building's height to its label."), Defaults.bHoverHeight);
	AddBool(Hover, TEXT("Coordinates"), TEXT("Show coordinates"),
		TEXT("Adds the hovered building's coordinates to its label."), Defaults.bHoverCoordinates);
	AddBool(Hover, TEXT("Highlight"), TEXT("Highlight hovered building"),
		TEXT("Outlines the building under the cursor."), Defaults.bHighlight);

	UCP_Section* Map = AddSection(Root, TEXT("Map"), TEXT("Map"));
	AddBool(Map, TEXT("Outlines"), TEXT("Building outlines"),
		TEXT("Draws an outline around every building."), Defaults.bOutlines);
	AddBool(Map, TEXT("RecipeIcons"), TEXT("Recipe icons when zoomed in"),
		TEXT("Shows what each production building makes once the map is zoomed in far enough."), Defaults.bRecipeIcons);
	AddBool(Map, TEXT("StatusLights"), TEXT("Status lights"),
		TEXT("A light in each machine's corner: green producing, purple overclocked, yellow standby, red error. Zoomed far out, only the yellow and red ones stay."),
		Defaults.bStatusLights);
	AddBool(Map, TEXT("HideSpoilers"), TEXT("Hide collectibles, resources and hazards"),
		TEXT("Spoiler-free map: no collectibles, resource nodes, wells, spore flowers, gas pillars or radioactivity."), Defaults.bHideSpoilers);

	UCP_Section* Building = AddSection(Root, TEXT("Building"), TEXT("Building from the map"));
	AddBool(Building, TEXT("AllowClipping"), TEXT("Allow clipping"),
		TEXT("Belts, pipes, hypertubes and tracks dragged between ports on the map may pass through other buildings. Everything else the build gun checks still applies."),
		Defaults.bAllowClipping);
}

bool UAtlasModConfig::Read(const UObject* WorldContext, Atlas::FSettings& Settings)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UConfigManager* Manager = GameInstance ? GameInstance->GetSubsystem<UConfigManager>() : nullptr;
	if (!Manager)
	{
		return false;
	}

	FConfigId Id;
	Id.ModReference = ModReference;
	const UConfigPropertySection* Root = Manager->GetConfigurationRootSection(Id);
	if (!Root)
	{
		return false;
	}

	const UConfigPropertySection* Hover = Cast<UConfigPropertySection>(Root->SectionProperties.FindRef(TEXT("Hover")));
	ReadBool(Hover, TEXT("Label"), Settings.bHoverLabel);
	ReadBool(Hover, TEXT("Height"), Settings.bHoverHeight);
	ReadBool(Hover, TEXT("Coordinates"), Settings.bHoverCoordinates);
	ReadBool(Hover, TEXT("Highlight"), Settings.bHighlight);

	const UConfigPropertySection* Map = Cast<UConfigPropertySection>(Root->SectionProperties.FindRef(TEXT("Map")));
	ReadBool(Map, TEXT("Outlines"), Settings.bOutlines);
	ReadBool(Map, TEXT("RecipeIcons"), Settings.bRecipeIcons);
	ReadBool(Map, TEXT("StatusLights"), Settings.bStatusLights);
	ReadBool(Map, TEXT("HideSpoilers"), Settings.bHideSpoilers);

	const UConfigPropertySection* Building = Cast<UConfigPropertySection>(Root->SectionProperties.FindRef(TEXT("Building")));
	ReadBool(Building, TEXT("AllowClipping"), Settings.bAllowClipping);
	return true;
}
