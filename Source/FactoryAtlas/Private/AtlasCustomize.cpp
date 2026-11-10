// Colors, patterns and materials for the building selected on the map.

#include "AtlasSubsystem.h"

#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "FactoryAtlas.h"
#include "FGBlueprintFunctionLibrary.h"
#include "FGBuildableSubsystem.h"
#include "FGCentralStorageSubsystem.h"
#include "FGCharacterPlayer.h"
#include "FGColorInterface.h"
#include "FGCustomizationRecipe.h"
#include "FGDismantleInterface.h"
#include "FGFactoryColoringTypes.h"
#include "FGGameState.h"
#include "FGInventoryComponent.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGPlayerController.h"
#include "FGPlayerState.h"
#include "FGRecipeManager.h"

using namespace Atlas;

// Named rather than anonymous: the module builds as a unity file, where anonymous helpers from other files are visible.
namespace AtlasCustomize
{
	const TCHAR* const CustomSwatchPath = TEXT("/Game/FactoryGame/Buildable/-Shared/Customization/Swatches/SwatchDesc_Custom.SwatchDesc_Custom_C");
	const TCHAR* const PatternRemoverPath = TEXT("/Game/FactoryGame/Buildable/-Shared/Customization/Patterns/PatternDesc_Remover.PatternDesc_Remover_C");

	using FBuildableMap = TMap<TSubclassOf<AFGBuildable>, TSubclassOf<UFGRecipe>>;

	/** No recipe means the game does not gate it. */
	bool IsUnlocked(AFGRecipeManager* Recipes, UClass* Desc)
	{
		const TSubclassOf<UFGCustomizationRecipe> Recipe = Desc ? Recipes->GetCustomizationRecipeFromDesc(Desc) : nullptr;
		return !Recipe || Recipes->IsCustomizationRecipeAvailable(Recipe);
	}

	/** Everything the game offers of one kind: its collection, plus whatever an unlocked recipe hands out, which reaches modded ones. */
	TArray<UClass*> AllOfKind(UWorld* World, AFGRecipeManager* Recipes, UClass* Kind)
	{
		TArray<UClass*> Out;
		const auto Add = [&Out, Kind](UClass* Desc)
		{
			if (Desc && Desc->IsChildOf(Kind))
			{
				Out.AddUnique(Desc);
			}
		};
		if (const AFGBuildableSubsystem* Buildables = AFGBuildableSubsystem::Get(World))
		{
			if (const TSubclassOf<UFGFactoryCustomizationCollection> Collection = Buildables->GetCollectionForCustomizationClass(Kind))
			{
				for (const TSubclassOf<UFGFactoryCustomizationDescriptor>& Desc : GetDefault<UFGFactoryCustomizationCollection>(Collection)->GetCustomizations())
				{
					Add(Desc);
				}
			}
		}
		TArray<TSubclassOf<UFGCustomizationRecipe>> Available;
		Recipes->GetAllAvailableCustomizationRecipes(Available);
		for (const TSubclassOf<UFGCustomizationRecipe>& Recipe : Available)
		{
			Add(UFGCustomizationRecipe::GetCustomizationDescriptor(Recipe));
		}
		if (Kind->IsChildOf<UFGFactoryCustomizationDescriptor_Material>())
		{
			// Building recipes point to the materials their variants belong to, locked ones included.
			for (const TSubclassOf<UFGRecipe>& Recipe : Recipes->GetAllRecipes())
			{
				if (const TSubclassOf<UFGCustomizationRecipe> Customization = *Recipe ? UFGRecipe::GetMaterialCustomizationRecipe(Recipe) : nullptr)
				{
					Add(UFGCustomizationRecipe::GetCustomizationDescriptor(Customization));
				}
			}
		}
		return Out;
	}

	bool IsRemover(const UClass* Desc)
	{
		return Desc && Desc->GetName().Contains(TEXT("Remover"));
	}

	/** Some customizations are limited to certain buildings. bEmptyMeansAny: an empty list allows everything. */
	bool AllowsClass(UClass* Desc, const UClass* Target, bool bEmptyMeansAny)
	{
		bool bRestricted = false;
		for (const TSubclassOf<AFGBuildable>& Allowed : GetDefault<UFGFactoryCustomizationDescriptor>(Desc)->mValidBuildables)
		{
			if (!Allowed)
			{
				continue; // An unset slot, not a restriction
			}
			bRestricted = true;
			if (Target->IsChildOf(Allowed))
			{
				return true;
			}
		}
		return !bRestricted && bEmptyMeansAny;
	}

	bool ReadBool(const UObject* Object, const TCHAR* Name)
	{
		const FBoolProperty* Property = FindFProperty<FBoolProperty>(Object->GetClass(), Name);
		return Property && Property->GetPropertyValue_InContainer(Object);
	}

	FText DescName(UClass* Desc)
	{
		return UFGItemDescriptor::GetItemName(TSubclassOf<UFGItemDescriptor>(Desc));
	}

	FBuildableMap& MapOf(UClass* Material)
	{
		return GetMutableDefault<UFGFactoryCustomizationDescriptor_Material>(Material)->GetBuildableMap();
	}

	/** The building a recipe makes. GetBuildableClassFromRecipe asserts on recipes with no product, which exist. */
	UClass* ProductClass(const TSubclassOf<UFGRecipe>& Recipe)
	{
		if (!Recipe || UFGRecipe::GetProducts(Recipe).IsEmpty())
		{
			return nullptr;
		}
		return AFGBuildable::GetBuildableClassFromRecipe(Recipe);
	}

	/**
	 * The recipe of Class's variant in Material, or nullptr. A material's map may list only one base shape per
	 * family, so a variant finds its base through whichever material lists the variant itself.
	 */
	TSubclassOf<UFGRecipe> VariantRecipe(UClass* Material, UClass* Class, const TSubclassOf<UFGRecipe>& BuiltWith, const TArray<UClass*>& All)
	{
		if (const TSubclassOf<UFGRecipe>* Direct = MapOf(Material).Find(Class); Direct && *Direct)
		{
			return *Direct;
		}
		for (UClass* Other : All)
		{
			for (const TPair<TSubclassOf<AFGBuildable>, TSubclassOf<UFGRecipe>>& Pair : MapOf(Other))
			{
				if (!Pair.Key || !Pair.Value || (Pair.Value != BuiltWith && ProductClass(Pair.Value) != Class))
				{
					continue;
				}
				if (const TSubclassOf<UFGRecipe>* Found = MapOf(Material).Find(Pair.Key); Found && *Found)
				{
					return *Found;
				}
			}
		}
		return nullptr;
	}

	AFGCharacterPlayer* LocalPlayer(UWorld* World)
	{
		AFGPlayerController* Controller = Cast<AFGPlayerController>(World->GetFirstPlayerController());
		return Controller ? Cast<AFGCharacterPlayer>(Controller->GetControlledCharacter()) : nullptr;
	}

	/** Item totals: what a change costs and what it gives back. */
	struct FLedger
	{
		TMap<TSubclassOf<UFGItemDescriptor>, int64> Net;

		void Add(UWorld* World, const TSubclassOf<UFGRecipe>& Recipe, int32 Sign)
		{
			if (Recipe)
			{
				for (const FItemAmount& Item : UFGRecipe::GetIngredients(World, Recipe))
				{
					Net.FindOrAdd(Item.ItemClass) += int64(Item.Amount) * Sign;
				}
			}
		}
	};

	int64 Available(UWorld* World, UFGInventoryComponent* Inventory, const TSubclassOf<UFGItemDescriptor>& Item)
	{
		const AFGCentralStorageSubsystem* Depot = AFGCentralStorageSubsystem::Get(World);
		return Inventory->GetNumItems(Item) + (Depot ? Depot->GetNumItemsFromCentralStorage(Item) : 0);
	}

	/** False with a reason when the player can't pay. Pays from the inventory first, then the Dimensional Depot, as the build gun does. */
	bool CanPay(UWorld* World, const FLedger& Ledger, AFGCharacterPlayer* Player, FText& OutError)
	{
		UFGInventoryComponent* Inventory = Player ? Player->GetInventory() : nullptr;
		if (!Inventory)
		{
			OutError = FText::FromString(TEXT("No player character to pay for this."));
			return false;
		}
		if (Inventory->GetNoBuildCost())
		{
			return true;
		}
		for (const TPair<TSubclassOf<UFGItemDescriptor>, int64>& Pair : Ledger.Net)
		{
			const int64 Have = Available(World, Inventory, Pair.Key);
			if (Pair.Value > Have)
			{
				OutError = FText::FromString(FString::Printf(TEXT("Needs %lld %s, you have %lld."), Pair.Value,
					*UFGItemDescriptor::GetItemName(Pair.Key).ToString(), Have));
				return false;
			}
		}
		return true;
	}

	/** Refunds that don't fit in the inventory drop in a crate, as dismantling does. */
	void Pay(UWorld* World, const FLedger& Ledger, AFGCharacterPlayer* Player)
	{
		UFGInventoryComponent* Inventory = Player->GetInventory();
		if (Inventory->GetNoBuildCost())
		{
			return;
		}
		AFGCentralStorageSubsystem* Depot = AFGCentralStorageSubsystem::Get(World);
		TArray<FInventoryStack> Left;
		for (const TPair<TSubclassOf<UFGItemDescriptor>, int64>& Pair : Ledger.Net)
		{
			const int32 Amount = int32(FMath::Clamp<int64>(FMath::Abs(Pair.Value), 0, MAX_int32));
			if (Pair.Value > 0)
			{
				const int32 FromInventory = FMath::Min(Amount, Inventory->GetNumItems(Pair.Key));
				if (FromInventory > 0)
				{
					Inventory->Remove(Pair.Key, FromInventory);
				}
				if (Amount > FromInventory && Depot)
				{
					Depot->TryRemoveItemsFromCentralStorage(Pair.Key, Amount - FromInventory);
				}
			}
			else if (Pair.Value < 0)
			{
				const FInventoryStack Stack(Amount, Pair.Key);
				const int32 Added = Inventory->AddStack(Stack, true);
				if (Added < Amount)
				{
					Left.Add(FInventoryStack(Amount - Added, Pair.Key));
				}
			}
		}
		if (!Left.IsEmpty())
		{
			FDismantleHelpers::DropRefundOnGroundNoActor(World, Player->GetActorLocation(), nullptr, Left, Player);
		}
	}
}

using namespace AtlasCustomize;

void UAtlasSubsystem::RefreshCustomizeOptions()
{
	CustomizeOptions = FCustomizeOptions();
	CustomizeIcons.Reset();

	UWorld* World = GetWorld();
	AFGRecipeManager* Recipes = World ? AFGRecipeManager::Get(World) : nullptr;
	AFGBuildable* Actor = bSelectedLightweight ? nullptr : Selected.Get();
	UClass* Class = bSelectedLightweight ? SelectedLightweight.GetBuildableClass().Get() : (Actor ? Actor->GetClass() : nullptr);
	if (!Recipes || !Class)
	{
		return;
	}
	// Lightweights have no actor, so their class defaults answer for them, as the game's own paint code does.
	UObject* Target = Actor ? static_cast<UObject*>(Actor) : Class->GetDefaultObject();
	IFGColorInterface* Colorable = Cast<IFGColorInterface>(Target);
	const auto KeepIcon = [this](UClass* Desc)
	{
		UTexture2D* Icon = GetDefault<UFGFactoryCustomizationDescriptor>(Desc)->GetIcon();
		if (Icon)
		{
			CustomizeIcons.Add(Icon);
		}
		return Icon;
	};

	// Colors. Some buildings pick the painted part from where the paint gun hits, which has no meaning from the map.
	const bool bColor = Colorable && Colorable->GetCanBeColored_Native() && !Colorable->IsColorApplicationDeferred();
	if (bColor)
	{
		AFGGameState* GameState = World->GetGameState<AFGGameState>();
		UClass* Custom = LoadClass<UFGFactoryCustomizationDescriptor_Swatch>(nullptr, CustomSwatchPath);
		if (Custom && IsUnlocked(Recipes, Custom) && AllowsClass(Custom, Class, true))
		{
			CustomizeOptions.CustomSwatch = Custom;
			for (UClass* Finish : AllOfKind(World, Recipes, UFGFactoryCustomizationDescriptor_PaintFinish::StaticClass()))
			{
				if (IsUnlocked(Recipes, Finish) && AllowsClass(Finish, Class, true))
				{
					FCustomizeOption& Option = CustomizeOptions.Finishes.AddDefaulted_GetRef();
					Option.Desc = Finish;
					Option.Name = DescName(Finish);
				}
			}
		}
		for (UClass* Swatch : AllOfKind(World, Recipes, UFGFactoryCustomizationDescriptor_Swatch::StaticClass()))
		{
			// The custom swatch and finishes are offered through the color editor instead.
			if (Swatch == Custom || Swatch->IsChildOf<UFGFactoryCustomizationDescriptor_PaintFinish>()
				|| !IsUnlocked(Recipes, Swatch) || !AllowsClass(Swatch, Class, true))
			{
				continue;
			}
			FCustomizeOption& Option = CustomizeOptions.Swatches.AddDefaulted_GetRef();
			Option.Desc = Swatch;
			Option.Name = DescName(Swatch);
			const int32 Slot = UFGBlueprintFunctionLibrary::GetIDForCustomizationDesc(TSubclassOf<UFGFactoryCustomizationDescriptor>(Swatch));
			if (GameState && Slot >= 0 && Slot < 255)
			{
				const FFactoryCustomizationColorSlot Colors = GameState->GetBuildingColorDataForSlot(uint8(Slot));
				Option.Primary = Colors.PrimaryColor;
				Option.Secondary = Colors.SecondaryColor;
			}
		}
	}

	// Patterns need coloring too, the building's own say-so, and a pattern that lists the building.
	const bool bPattern = bColor && ReadBool(Class->GetDefaultObject(), TEXT("mAllowPatterning"))
		&& IFGColorInterface::Execute_GetCanBePatterned(Target);
	if (bPattern)
	{
		UClass* Remover = LoadClass<UFGFactoryCustomizationDescriptor_Pattern>(nullptr, PatternRemoverPath);
		for (UClass* Pattern : AllOfKind(World, Recipes, UFGFactoryCustomizationDescriptor_Pattern::StaticClass()))
		{
			if (IsRemover(Pattern))
			{
				Remover = Remover ? Remover : Pattern;
				continue;
			}
			if (!IsUnlocked(Recipes, Pattern) || !AllowsClass(Pattern, Class, false))
			{
				continue;
			}
			FCustomizeOption& Option = CustomizeOptions.Patterns.AddDefaulted_GetRef();
			Option.Desc = Pattern;
			Option.Name = DescName(Pattern);
			Option.Icon = KeepIcon(Pattern);
		}
		if (!CustomizeOptions.Patterns.IsEmpty())
		{
			FCustomizeOption None;
			None.Desc = Remover;
			None.Name = FText::FromString(TEXT("None"));
			CustomizeOptions.Patterns.Insert(MoveTemp(None), 0);
		}
	}

	// Materials. A material is a different building class, so only lightweights can swap: they are plain data.
	const FRuntimeBuildableInstanceData* Live = bSelectedLightweight ? SelectedLightweight.ResolveBuildableInstanceData() : nullptr;
	if (Live)
	{
		const TArray<UClass*> All = AllOfKind(World, Recipes, UFGFactoryCustomizationDescriptor_Material::StaticClass());
		TSet<UClass*> Seen;
		for (UClass* Material : All)
		{
			if (IsRemover(Material) || !IsUnlocked(Recipes, Material))
			{
				continue;
			}
			const TSubclassOf<UFGRecipe> Recipe = VariantRecipe(Material, Class, Live->BuiltWithRecipe, All);
			UClass* Variant = ProductClass(Recipe);
			if (!Variant || Seen.Contains(Variant) || !GetDefault<AFGBuildable>(Variant)->ManagedByLightweightBuildableSubsystem()
				|| (Variant != Class && !Recipes->IsRecipeAvailable(Recipe)))
			{
				continue;
			}
			Seen.Add(Variant);
			if (Variant == Class)
			{
				CustomizeOptions.CurrentMaterial = CustomizeOptions.Materials.Num();
			}
			FCustomizeOption& Option = CustomizeOptions.Materials.AddDefaulted_GetRef();
			Option.Desc = Material;
			Option.Name = DescName(Material);
			Option.Icon = KeepIcon(Material);
		}
		// One choice is no choice.
		if (CustomizeOptions.Materials.Num() < 2)
		{
			CustomizeOptions.Materials.Reset();
			CustomizeOptions.CurrentMaterial = INDEX_NONE;
		}
		else if (Live->BlueprintProxy)
		{
			CustomizeOptions.MaterialNote = FText::FromString(TEXT("Part of a blueprint, so its material can only be changed at the building."));
		}
		else if (World->GetNetMode() != NM_Standalone && World->GetNetMode() != NM_ListenServer)
		{
			CustomizeOptions.MaterialNote = FText::FromString(TEXT("Only the host can change materials."));
		}
		else if (World->GetNumPlayerControllers() > 1)
		{
			CustomizeOptions.MaterialNote = FText::FromString(TEXT("Materials can only be changed here with no other players connected."));
		}
	}
}

FCustomizeState UAtlasSubsystem::GetCustomizeState() const
{
	FCustomizeState State;
	const FFactoryCustomizationData* Data = nullptr;
	if (bSelectedLightweight)
	{
		Data = SelectedLightweight.GetCustomizationData();
	}
	else if (AFGBuildable* Actor = Selected.Get())
	{
		Data = &Actor->GetCustomizationData_Native();
	}
	if (Data)
	{
		State.Swatch = Data->SwatchDesc.Get();
		State.Pattern = IsRemover(Data->PatternDesc.Get()) ? nullptr : Data->PatternDesc.Get();
		State.Material = Data->MaterialDesc.Get();
		State.PatternRotation = Data->PatternRotation;
		State.bCustom = State.Swatch && State.Swatch == CustomizeOptions.CustomSwatch;
		State.CustomPrimary = Data->OverrideColorData.PrimaryColor;
		State.CustomSecondary = Data->OverrideColorData.SecondaryColor;
		State.Finish = Data->OverrideColorData.PaintFinish.Get();
	}
	return State;
}

bool UAtlasSubsystem::ApplySelectedCustomization(const FFactoryCustomizationData& InData)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		CustomizeError = FText::FromString(TEXT("Only the host can change buildings."));
		return false;
	}

	// The data carries a cache resolved from its descriptors (color slot, pattern id) that the meshes read.
	// Copied or edited data still holds the old building's cache, and lightweights draw straight from it, so a
	// foundation would keep its old look. Rebuilding the cache from the new descriptors fixes that.
	FFactoryCustomizationData Data = InData;
	if (AFGGameState* GameState = World->GetGameState<AFGGameState>())
	{
		Data.Data.Reset();
		Data.Initialize(GameState);
	}

	if (bSelectedLightweight)
	{
		AFGLightweightBuildableSubsystem* Lightweights = AFGLightweightBuildableSubsystem::Get(World);
		const FRuntimeBuildableInstanceData* Live = SelectedLightweight.ResolveBuildableInstanceData();
		UClass* Class = SelectedLightweight.GetBuildableClass();
		const TArray<FRuntimeBuildableInstanceData>* Instances = Lightweights ? Lightweights->GetAllLightweightBuildableInstances().Find(Class) : nullptr;
		if (!Live || !Instances)
		{
			return false;
		}
		// Repaints raise no event, so the map record is swapped by hand: out with the old paint, in with the new.
		HandleLightweight(Class, *Live, false);
		Lightweights->SetCustomizationDataOnInstance(Class, Data, int32(Live - Instances->GetData()));
		if (const FRuntimeBuildableInstanceData* Updated = SelectedLightweight.ResolveBuildableInstanceData())
		{
			HandleLightweight(Class, *Updated, true);
		}
		return true;
	}

	AFGBuildable* Actor = Selected.Get();
	IFGColorInterface* Colorable = Cast<IFGColorInterface>(Actor);
	if (!Colorable)
	{
		return false;
	}
	QueueActor(Actor, false);
	// Data is already the whole result; combining would keep a slot it should replace.
	Colorable->SetCustomizationData_Native(Data, true);
	QueueActor(Actor, true);
	return true;
}

namespace AtlasCustomize
{
	/** The selection's current customization data, copied, or nothing if it is gone. */
	TOptional<FFactoryCustomizationData> CurrentData(bool bLightweight, const FLightweightBuildableInstanceRef& Lightweight, const TWeakObjectPtr<AFGBuildable>& Actor)
	{
		if (bLightweight)
		{
			if (const FFactoryCustomizationData* Data = Lightweight.GetCustomizationData())
			{
				return *Data;
			}
		}
		else if (AFGBuildable* Building = Actor.Get())
		{
			return Building->GetCustomizationData_Native();
		}
		return {};
	}

	/** What the game charges for applying one customization, through its recipe. Most are free. */
	bool PayForDesc(UWorld* World, UClass* Desc, FText& OutError)
	{
		AFGRecipeManager* Recipes = AFGRecipeManager::Get(World);
		const TSubclassOf<UFGCustomizationRecipe> Recipe = Recipes && Desc ? Recipes->GetCustomizationRecipeFromDesc(Desc) : nullptr;
		if (!Recipe || UFGRecipe::GetIngredients(World, Recipe).IsEmpty())
		{
			return true;
		}
		FLedger Ledger;
		Ledger.Add(World, Recipe, 1);
		AFGCharacterPlayer* Player = LocalPlayer(World);
		if (!CanPay(World, Ledger, Player, OutError))
		{
			return false;
		}
		Pay(World, Ledger, Player);
		return true;
	}
}

void UAtlasSubsystem::SetSelectedSwatch(UClass* Swatch)
{
	TOptional<FFactoryCustomizationData> Data = CurrentData(bSelectedLightweight, SelectedLightweight, Selected);
	const bool bOffered = CustomizeOptions.Swatches.ContainsByPredicate([Swatch](const FCustomizeOption& Option) { return Option.Desc == Swatch; });
	if (!Data || !Swatch || !bOffered || Data->SwatchDesc.Get() == Swatch)
	{
		return;
	}
	Data->SwatchDesc = Swatch;
	// A preset swatch carries its own colors; leftovers from a custom color would override them.
	Data->OverrideColorData = FFactoryCustomizationColorSlot();
	if (PayForDesc(GetWorld(), Swatch, CustomizeError) && ApplySelectedCustomization(*Data))
	{
		CustomizeError = FText::GetEmpty();
	}
}

void UAtlasSubsystem::SetSelectedCustomColor(const FLinearColor& Primary, const FLinearColor& Secondary, UClass* Finish)
{
	TOptional<FFactoryCustomizationData> Data = CurrentData(bSelectedLightweight, SelectedLightweight, Selected);
	UClass* Custom = CustomizeOptions.CustomSwatch;
	const bool bFinishOffered = !Finish || CustomizeOptions.Finishes.ContainsByPredicate([Finish](const FCustomizeOption& Option) { return Option.Desc == Finish; });
	if (!Data || !Custom || !bFinishOffered)
	{
		return;
	}

	// Pure black reads as "no override" to anything that checks for one, the map's paint mode included.
	const auto Opaque = [](FLinearColor Color)
	{
		Color.A = 1.f;
		if (Color.R + Color.G + Color.B <= 0.f)
		{
			Color = FLinearColor(0.0003f, 0.0003f, 0.0003f, 1.f);
		}
		return Color;
	};
	FFactoryCustomizationColorSlot Colors(Opaque(Primary), Opaque(Secondary));
	Colors.PaintFinish = Finish;
	const bool bSwatchChanged = Data->SwatchDesc.Get() != Custom;
	const bool bFinishChanged = Data->OverrideColorData.PaintFinish.Get() != Finish;
	if (!bSwatchChanged && Data->OverrideColorData == Colors)
	{
		return;
	}
	Data->SwatchDesc = Custom;
	Data->OverrideColorData = Colors;

	UWorld* World = GetWorld();
	const bool bPaid = (!bSwatchChanged || PayForDesc(World, Custom, CustomizeError))
		&& (!bFinishChanged || !Finish || PayForDesc(World, Finish, CustomizeError));
	if (bPaid && ApplySelectedCustomization(*Data))
	{
		CustomizeError = FText::GetEmpty();
	}
}

void UAtlasSubsystem::SetSelectedPattern(UClass* Pattern)
{
	TOptional<FFactoryCustomizationData> Data = CurrentData(bSelectedLightweight, SelectedLightweight, Selected);
	const bool bOffered = CustomizeOptions.Patterns.ContainsByPredicate([Pattern](const FCustomizeOption& Option) { return Option.Desc == Pattern; });
	if (!Data || !bOffered || Data->PatternDesc.Get() == Pattern)
	{
		return;
	}
	Data->PatternDesc = Pattern;
	if (PayForDesc(GetWorld(), Pattern, CustomizeError) && ApplySelectedCustomization(*Data))
	{
		CustomizeError = FText::GetEmpty();
	}
}

void UAtlasSubsystem::RotateSelectedPattern()
{
	// The paint gun turns patterns in quarter turns.
	SetSelectedPatternRotation(uint8((GetCustomizeState().PatternRotation + 1) % 4));
}

void UAtlasSubsystem::SetSelectedPatternRotation(uint8 Rotation)
{
	TOptional<FFactoryCustomizationData> Data = CurrentData(bSelectedLightweight, SelectedLightweight, Selected);
	if (!Data || !Data->PatternDesc || IsRemover(Data->PatternDesc.Get()) || Data->PatternRotation == Rotation % 4)
	{
		return;
	}
	Data->PatternRotation = uint8(Rotation % 4);
	if (ApplySelectedCustomization(*Data))
	{
		CustomizeError = FText::GetEmpty();
	}
}

void UAtlasSubsystem::SetSelectedMaterial(UClass* Material)
{
	UWorld* World = GetWorld();
	const int32 Chosen = CustomizeOptions.Materials.IndexOfByPredicate([Material](const FCustomizeOption& Option) { return Option.Desc == Material; });
	if (!World || !bSelectedLightweight || Chosen == INDEX_NONE || Chosen == CustomizeOptions.CurrentMaterial || !CustomizeOptions.MaterialNote.IsEmpty())
	{
		return;
	}

	AFGRecipeManager* Recipes = AFGRecipeManager::Get(World);
	AFGLightweightBuildableSubsystem* Lightweights = AFGLightweightBuildableSubsystem::Get(World);
	const FRuntimeBuildableInstanceData* Live = SelectedLightweight.ResolveBuildableInstanceData();
	UClass* OldClass = SelectedLightweight.GetBuildableClass();
	if (!Recipes || !Lightweights || !Live || Live->BlueprintProxy)
	{
		return;
	}
	const TArray<UClass*> All = AllOfKind(World, Recipes, UFGFactoryCustomizationDescriptor_Material::StaticClass());
	const TSubclassOf<UFGRecipe> Recipe = VariantRecipe(Material, OldClass, Live->BuiltWithRecipe, All);
	UClass* Variant = ProductClass(Recipe);
	if (!Variant || Variant == OldClass)
	{
		return;
	}

	// The new variant costs its recipe and the material's own; the old one is refunded, as dismantling it would be.
	FLedger Ledger;
	Ledger.Add(World, Recipe, 1);
	Ledger.Add(World, Recipes->GetCustomizationRecipeFromDesc(Material), 1);
	Ledger.Add(World, Live->BuiltWithRecipe, -1);
	AFGCharacterPlayer* Player = LocalPlayer(World);
	if (!CanPay(World, Ledger, Player, CustomizeError))
	{
		return;
	}

	const FTransform Transform = Live->Transform;
	FFactoryCustomizationData Customization = Live->CustomizationData;
	const TSubclassOf<UFGRecipe> OldRecipe = Live->BuiltWithRecipe;
	const FFGDynamicStruct TypeSpecificData = Live->TypeSpecificData;
	const FPlayerInfoHandle BuiltBy = Live->BuiltBy;

	// Swapped by removing the instance and adding one of the other class; if the add fails the old one goes back.
	FLightweightBuildableInstanceRef Old = SelectedLightweight;
	if (!Old.Remove())
	{
		CustomizeError = FText::FromString(TEXT("The game would not remove the old building."));
		return;
	}
	FFactoryCustomizationData NewCustomization = Customization;
	NewCustomization.MaterialDesc = Material;
	FRuntimeBuildableInstanceData Replacement(Transform, NewCustomization, Recipe, nullptr);
	Replacement.TypeSpecificData = TypeSpecificData;
	Replacement.BuiltBy = BuiltBy;
	const int32 NewIndex = Lightweights->AddFromBuildableInstanceData(Variant, Replacement);
	if (NewIndex == INDEX_NONE)
	{
		FRuntimeBuildableInstanceData Original(Transform, Customization, OldRecipe, nullptr);
		Original.TypeSpecificData = TypeSpecificData;
		Original.BuiltBy = BuiltBy;
		const int32 Restored = Lightweights->AddFromBuildableInstanceData(OldClass, Original);
		if (Restored != INDEX_NONE)
		{
			SelectedLightweight.Initialize(Lightweights, OldClass, Restored);
		}
		CustomizeError = FText::FromString(TEXT("The game refused the new material."));
		UE_LOG(LogFactoryAtlas, Warning, TEXT("Material change of %s to %s failed"), *OldClass->GetName(), *Variant->GetName());
		return;
	}

	Pay(World, Ledger, Player);
	SelectedLightweight.Initialize(Lightweights, Variant, NewIndex);
	CustomizeError = FText::GetEmpty();
	UE_LOG(LogFactoryAtlas, Display, TEXT("Material of %s changed to %s"), *OldClass->GetName(), *Variant->GetName());
	RefreshCustomizeOptions();
	SelectionVersion++;
}

namespace AtlasCustomize
{
	AFGPlayerState* LocalPlayerState(UWorld* World)
	{
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		return Controller ? Controller->GetPlayerState<AFGPlayerState>() : nullptr;
	}

	bool CanUseFactoryClipboard(AFGBuildable* Actor)
	{
		return Actor && IFGFactoryClipboardInterface::Execute_CanUseFactoryClipboard(Actor);
	}

	/**
	 * Whether the game's clipboard holds settings this building can take. The clipboard is a protected map keyed
	 * by each building's mapping class, so it is read through reflection rather than the SML header's layout.
	 */
	bool FactoryClipboardHolds(AFGPlayerState* PlayerState, AFGBuildable* Actor)
	{
		if (!PlayerState || !CanUseFactoryClipboard(Actor))
		{
			return false;
		}
		const FMapProperty* Property = FindFProperty<FMapProperty>(AFGPlayerState::StaticClass(), TEXT("mFactoryClipboard"));
		const FObjectPropertyBase* ValueProperty = Property ? CastField<FObjectPropertyBase>(Property->ValueProp) : nullptr;
		if (!ValueProperty || !Property->KeyProp->IsA<FClassProperty>())
		{
			return false;
		}
		UClass* Key = IFGFactoryClipboardInterface::Execute_GetClipboardMappingClass(Actor).Get();
		FScriptMapHelper Map(Property, Property->ContainerPtrToValuePtr<void>(PlayerState));
		const uint8* Value = Key ? Map.FindValueFromHash(&Key) : nullptr;
		return Value && ValueProperty->GetObjectPropertyValue(Value) != nullptr;
	}

	bool Offers(const TArray<FCustomizeOption>& Options, UClass* Desc)
	{
		return Options.ContainsByPredicate([Desc](const FCustomizeOption& Option) { return Option.Desc == Desc; });
	}

	/** Several descriptors can share a material's name, one per building family, so the name is the fallback. */
	int32 FindMaterial(const FCustomizeOptions& Options, UClass* Desc, const FText& Name)
	{
		const int32 Exact = Options.Materials.IndexOfByPredicate([Desc](const FCustomizeOption& Option) { return Option.Desc == Desc; });
		if (Exact != INDEX_NONE || Name.IsEmpty())
		{
			return Exact;
		}
		return Options.Materials.IndexOfByPredicate([&Name](const FCustomizeOption& Option) { return Option.Name.EqualTo(Name); });
	}

	/** What a paste would do to a building with these options. */
	struct FPastePlan
	{
		bool bFactory = false;
		bool bColor = false;
		bool bCustomColor = false;
		bool bPattern = false;
		int32 Material = INDEX_NONE;

		bool Any() const { return bFactory || bColor || bPattern || Material != INDEX_NONE; }
	};

	FPastePlan PlanPaste(const FCustomizeOptions& Options, const TOptional<FFactoryCustomizationData>& Copied, UClass* CopiedMaterial,
		const FText& CopiedMaterialName, bool bFactoryClipboard)
	{
		FPastePlan Plan;
		Plan.bFactory = bFactoryClipboard;
		if (!Copied)
		{
			return Plan;
		}
		UClass* Swatch = Copied->SwatchDesc.Get();
		const UClass* CustomSwatch = LoadClass<UFGFactoryCustomizationDescriptor_Swatch>(nullptr, CustomSwatchPath);
		Plan.bCustomColor = Swatch && Swatch == CustomSwatch;
		Plan.bColor = Plan.bCustomColor ? Options.CustomSwatch != nullptr : (Swatch && Offers(Options.Swatches, Swatch));

		// "No pattern" pastes too, as the building's "None".
		UClass* Pattern = Copied->PatternDesc.Get();
		Plan.bPattern = (!Pattern || IsRemover(Pattern)) ? !Options.Patterns.IsEmpty() : Offers(Options.Patterns, Pattern);

		if (CopiedMaterial && Options.MaterialNote.IsEmpty())
		{
			Plan.Material = FindMaterial(Options, CopiedMaterial, CopiedMaterialName);
		}
		return Plan;
	}

	/** "Copied settings, color and pattern." */
	FText JoinParts(const TCHAR* Verb, const TArray<FString>& Parts)
	{
		if (Parts.IsEmpty())
		{
			return FText::FromString(FString::Printf(TEXT("Nothing %s."), *FString(Verb).ToLower()));
		}
		FString Text = Verb;
		for (int32 i = 0; i < Parts.Num(); i++)
		{
			Text += i == 0 ? TEXT(" ") : (i == Parts.Num() - 1 ? TEXT(" and ") : TEXT(", "));
			Text += Parts[i];
		}
		return FText::FromString(Text + TEXT("."));
	}
}

bool UAtlasSubsystem::ActOnHovered(TFunctionRef<void()> Action, TSet<const void*>* Done)
{
	// Everything a selection carries, so it can come back exactly, without the panel rebuilding.
	const bool bHadSelection = HasSelection();
	const void* OldIdentity = GetSelectionIdentity();
	const TWeakObjectPtr<AFGBuildable> OldActor = Selected;
	const FLightweightBuildableInstanceRef OldLightweight = SelectedLightweight;
	const bool bOldLightweight = bSelectedLightweight;
	const FVector4f OldRecord = SelectionRecord;
	FCustomizeOptions OldOptions = CustomizeOptions;
	TArray<TObjectPtr<UTexture2D>> OldIcons = CustomizeIcons;
	const uint32 OldVersion = SelectionVersion;
	const FText OldStatus = ClipboardStatus;
	const FText OldError = CustomizeError;
	const auto Restore = [&]()
	{
		Selected = OldActor;
		SelectedLightweight = OldLightweight;
		bSelectedLightweight = bOldLightweight;
		SelectionRecord = OldRecord;
		CustomizeOptions = MoveTemp(OldOptions);
		CustomizeIcons = MoveTemp(OldIcons);
		SelectionVersion = OldVersion;
		ClipboardStatus = OldStatus;
		CustomizeError = OldError;
	};

	if (!SelectHovered())
	{
		Restore();
		return false;
	}
	const void* Identity = GetSelectionIdentity();
	if (Done)
	{
		bool bAlready = false;
		Done->Add(Identity, &bAlready);
		if (bAlready)
		{
			Restore();
			return false;
		}
	}

	ClipboardStatus = FText::GetEmpty();
	CustomizeError = FText::GetEmpty();
	Action();
	ShowToast(CustomizeError.IsEmpty() ? ClipboardStatus : CustomizeError, !CustomizeError.IsEmpty());

	// Ctrl-clicking the building whose panel is open keeps it open, now showing the change.
	if (bHadSelection && Identity == OldIdentity)
	{
		return true;
	}
	Restore();
	return true;
}

bool UAtlasSubsystem::CopyHovered()
{
	return ActOnHovered([this]() { CopySelected(); }, nullptr);
}

bool UAtlasSubsystem::PasteHovered(TSet<const void*>* Done)
{
	return ActOnHovered([this]() { PasteToSelected(); }, Done);
}

void UAtlasSubsystem::ShowToast(const FText& Text, bool bError)
{
	Toast = Text;
	bToastError = bError;
	ToastUntil = Text.IsEmpty() ? 0.0 : FPlatformTime::Seconds() + (bError ? 3.0 : 1.6);
}

void UAtlasSubsystem::PlayClipboardSound(EClipboardSound Sound)
{
	// Imported by Scripts/import_sounds.py. Plain engine sounds, not Wwise events, so they need no sound bank.
	static const TCHAR* const SoundPaths[] = {
		TEXT("/FactoryAtlas/Sounds/S_Atlas_Copy.S_Atlas_Copy"),
		TEXT("/FactoryAtlas/Sounds/S_Atlas_Paste.S_Atlas_Paste"),
		TEXT("/FactoryAtlas/Sounds/S_Atlas_Error.S_Atlas_Error"),
	};
	static_assert(UE_ARRAY_COUNT(SoundPaths) == int32(EClipboardSound::Count), "One path per sound");

	// A drag-paste can cross several buildings in one frame; one sound for them is enough.
	const double Now = FPlatformTime::Seconds();
	if (Now - LastClipboardSoundSeconds < 0.06)
	{
		return;
	}
	if (ClipboardSounds.Num() == 0)
	{
		for (const TCHAR* Path : SoundPaths)
		{
			USoundBase* Loaded = LoadObject<USoundBase>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Loaded)
			{
				UE_LOG(LogFactoryAtlas, Warning, TEXT("Missing sound %s"), Path);
			}
			ClipboardSounds.Add(Loaded);
		}
	}
	if (USoundBase* Asset = ClipboardSounds[int32(Sound)])
	{
		LastClipboardSoundSeconds = Now;
		UGameplayStatics::PlaySound2D(GetWorld(), Asset);
	}
}

bool UAtlasSubsystem::GetToast(FText& OutText, bool& bOutError, float& OutAlpha) const
{
	const double Left = ToastUntil - FPlatformTime::Seconds();
	if (Left <= 0.0 || Toast.IsEmpty())
	{
		return false;
	}
	OutText = Toast;
	bOutError = bToastError;
	OutAlpha = float(FMath::Clamp(Left / 0.4, 0.0, 1.0)); // Fades over its last moments
	return true;
}

const void* UAtlasSubsystem::GetSelectionIdentity() const
{
	if (bSelectedLightweight)
	{
		return SelectedLightweight.ResolveBuildableInstanceData();
	}
	return Selected.Get();
}

bool UAtlasSubsystem::CanCopySelected() const
{
	const bool bLook = !CustomizeOptions.Swatches.IsEmpty() || CustomizeOptions.CustomSwatch
		|| !CustomizeOptions.Patterns.IsEmpty() || !CustomizeOptions.Materials.IsEmpty();
	return HasSelection() && (bLook || (!bSelectedLightweight && CanUseFactoryClipboard(Selected.Get())));
}

void UAtlasSubsystem::CopySelected()
{
	if (!CanCopySelected())
	{
		return;
	}
	TArray<FString> Parts;
	AFGBuildable* Actor = bSelectedLightweight ? nullptr : Selected.Get();
	AFGPlayerState* PlayerState = LocalPlayerState(GetWorld());
	if (PlayerState && CanUseFactoryClipboard(Actor))
	{
		PlayerState->CopyFactoryClipboard(Actor);
		Parts.Add(TEXT("settings"));
	}
	const FFactoryState Machine = GetSelectionState();
	CopiedRecipe = Machine.bHasRecipes ? Machine.Recipe : nullptr;
	CopiedClock.Reset();
	if (Machine.bCanChangeClock)
	{
		CopiedClock = Machine.Pending;
	}
	if (CopiedRecipe)
	{
		Parts.Add(TEXT("recipe"));
	}
	if (CopiedClock)
	{
		Parts.Add(TEXT("clock speed"));
	}

	// A copy replaces the whole clipboard, so a building without a look clears the copied one.
	const bool bColor = !CustomizeOptions.Swatches.IsEmpty() || CustomizeOptions.CustomSwatch;
	const bool bPattern = !CustomizeOptions.Patterns.IsEmpty();
	CopiedCustomization.Reset();
	if (bColor || bPattern || !CustomizeOptions.Materials.IsEmpty())
	{
		CopiedCustomization = CurrentData(bSelectedLightweight, SelectedLightweight, Selected);
	}
	CopiedMaterial = nullptr;
	CopiedMaterialName = FText::GetEmpty();
	if (CustomizeOptions.Materials.IsValidIndex(CustomizeOptions.CurrentMaterial))
	{
		CopiedMaterial = CustomizeOptions.Materials[CustomizeOptions.CurrentMaterial].Desc;
		CopiedMaterialName = CustomizeOptions.Materials[CustomizeOptions.CurrentMaterial].Name;
	}
	if (CopiedCustomization && bColor)
	{
		Parts.Add(TEXT("color"));
	}
	if (CopiedCustomization && bPattern)
	{
		Parts.Add(TEXT("pattern"));
	}
	if (CopiedMaterial)
	{
		Parts.Add(TEXT("material"));
	}
	CopiedClass = SelectionClass();
	CopiedCategory = CopiedClass ? Types[TypeFor(CopiedClass)].Category : ECategory::Other;
	bCopiedSettings = (PlayerState && CanUseFactoryClipboard(Actor)) || CopiedRecipe || CopiedClock.IsSet();
	ClipboardStatus = JoinParts(TEXT("Copied"), Parts);
}

UClass* UAtlasSubsystem::SelectionClass() const
{
	if (bSelectedLightweight)
	{
		return SelectedLightweight.GetBuildableClass().Get();
	}
	const AFGBuildable* Actor = Selected.Get();
	return Actor ? Actor->GetClass() : nullptr;
}

bool UAtlasSubsystem::PasteFitsSelection() const
{
	// A constructor's color doesn't belong on a foundation. The same building always takes it. A machine's copy
	// also goes wherever the game's own clipboard accepts it. A copy that is only a look, from a foundation, wall,
	// belt and the like, goes onto anything of the same map category: one foundation onto another size of foundation.
	UClass* Target = SelectionClass();
	if (!Target || !CopiedClass)
	{
		return false;
	}
	if (Target == CopiedClass)
	{
		return true;
	}
	if (bCopiedSettings)
	{
		AFGBuildable* Actor = bSelectedLightweight ? nullptr : Selected.Get();
		return FactoryClipboardHolds(LocalPlayerState(GetWorld()), Actor);
	}
	const int32* Type = TypeIndex.Find(Target);
	if (!Type || !Types.IsValidIndex(*Type) || Types[*Type].Category != CopiedCategory)
	{
		return false;
	}
	// A machine has settings of its own, so it never takes a look-only copy from something else.
	AFGBuildable* Actor = bSelectedLightweight ? nullptr : Selected.Get();
	return !CanUseFactoryClipboard(Actor);
}

bool UAtlasSubsystem::CanPasteToSelected() const
{
	if (!HasSelection() || !PasteFitsSelection())
	{
		return false;
	}
	AFGBuildable* Actor = bSelectedLightweight ? nullptr : Selected.Get();
	// Whether the recipe fits is only checked when pasting; asking the machine every frame would cost too much.
	const bool bMachine = Actor && ((CopiedRecipe && Actor->IsA<AFGBuildableManufacturer>()) || (CopiedClock && GetSelectionState().bCanChangeClock));
	const bool bFactory = bMachine || FactoryClipboardHolds(LocalPlayerState(GetWorld()), Actor);
	return PlanPaste(CustomizeOptions, CopiedCustomization, CopiedMaterial, CopiedMaterialName, bFactory).Any();
}

void UAtlasSubsystem::PasteToSelected()
{
	if (!HasSelection())
	{
		return;
	}
	if (!PasteFitsSelection())
	{
		CustomizeError = CopiedClass
			? FText::Format(FText::FromString(TEXT("Copied from a {0}; that doesn't paste onto this.")), Types[TypeFor(CopiedClass)].Name)
			: FText::FromString(TEXT("Nothing copied."));
		return;
	}
	AFGBuildable* Actor = bSelectedLightweight ? nullptr : Selected.Get();
	AFGPlayerState* PlayerState = LocalPlayerState(GetWorld());
	FPastePlan Plan = PlanPaste(CustomizeOptions, CopiedCustomization, CopiedMaterial, CopiedMaterialName, FactoryClipboardHolds(PlayerState, Actor));
	TArray<FString> Parts;
	CustomizeError = FText::GetEmpty();
	// Whatever pasted before a refusal stays pasted, and the status says what that was.
	const auto Finish = [this, &Parts]() { ClipboardStatus = JoinParts(TEXT("Pasted"), Parts); };

	if (Plan.bFactory)
	{
		// The game's own paste: it moves shards and items the same way Ctrl+V at the building does.
		PlayerState->PasteFactoryClipboard(Actor);
		Parts.Add(TEXT("settings"));
	}

	// Called from here rather than from the machine's own screen, the game's paste leaves the recipe as it was,
	// so the recipe and clock are set the same way the panel sets them.
	if (Actor && CopiedRecipe && Actor->IsA<AFGBuildableManufacturer>() && GetSelectionState().Recipe != CopiedRecipe)
	{
		const bool bRuns = GetSelectedRecipeOptions().ContainsByPredicate([this](const FRecipeOption& Option) { return Option.Recipe == CopiedRecipe; });
		if (!bRuns)
		{
			CustomizeError = FText::Format(FText::FromString(TEXT("This machine can't make {0}.")),
				UFGRecipe::GetRecipeName(TSubclassOf<UFGRecipe>(CopiedRecipe)));
			return Finish();
		}
		if (!SetSelectedRecipe(CopiedRecipe))
		{
			CustomizeError = FText::FromString(TEXT("The game would not change the recipe."));
			return Finish();
		}
		Parts.Add(TEXT("recipe"));
	}
	if (Actor && CopiedClock && GetSelectionState().bCanChangeClock)
	{
		// Clamped to what the shards in this machine allow.
		SetSelectedClock(CopiedClock.GetValue());
		Parts.AddUnique(TEXT("clock speed"));
	}

	// The material first: it swaps the building for its variant, which keeps the color and pattern.
	if (Plan.Material != INDEX_NONE && Plan.Material != CustomizeOptions.CurrentMaterial)
	{
		UClass* Wanted = CustomizeOptions.Materials[Plan.Material].Desc;
		SetSelectedMaterial(Wanted);
		const bool bSwapped = CustomizeOptions.Materials.IsValidIndex(CustomizeOptions.CurrentMaterial)
			&& CustomizeOptions.Materials[CustomizeOptions.CurrentMaterial].Desc == Wanted;
		if (!bSwapped)
		{
			if (CustomizeError.IsEmpty())
			{
				CustomizeError = FText::FromString(TEXT("The material could not be changed."));
			}
			return Finish();
		}
		Parts.Add(TEXT("material"));
		Plan = PlanPaste(CustomizeOptions, CopiedCustomization, CopiedMaterial, CopiedMaterialName, false);
	}
	if (!CopiedCustomization)
	{
		return Finish();
	}

	const FFactoryCustomizationData Copied = CopiedCustomization.GetValue();
	if (Plan.bColor)
	{
		if (Plan.bCustomColor)
		{
			UClass* PaintFinish = Copied.OverrideColorData.PaintFinish.Get();
			SetSelectedCustomColor(Copied.OverrideColorData.PrimaryColor, Copied.OverrideColorData.SecondaryColor,
				Offers(CustomizeOptions.Finishes, PaintFinish) ? PaintFinish : nullptr);
		}
		else
		{
			SetSelectedSwatch(Copied.SwatchDesc.Get());
		}
		if (!CustomizeError.IsEmpty())
		{
			return Finish();
		}
		Parts.Add(TEXT("color"));
	}

	if (Plan.bPattern)
	{
		UClass* Pattern = Copied.PatternDesc.Get();
		const bool bNone = !Pattern || IsRemover(Pattern);
		SetSelectedPattern(bNone ? CustomizeOptions.Patterns[0].Desc : Pattern);
		if (!bNone && CustomizeError.IsEmpty())
		{
			SetSelectedPatternRotation(Copied.PatternRotation);
		}
		if (!CustomizeError.IsEmpty())
		{
			return Finish();
		}
		Parts.Add(TEXT("pattern"));
	}
	Finish();
}
