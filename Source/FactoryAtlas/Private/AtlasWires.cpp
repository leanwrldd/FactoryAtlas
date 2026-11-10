// Power wires from the map, like Factorio's wire shortcut: in wire mode, click a building, then another, and a power
// line runs between them. Clicking the ground puts a power pole there instead, as the build gun does. Each wire then
// starts the next from where it ended, so a run of clicks chains buildings together.
//
// Everything goes through the game's own wire hologram, so the length limit, the cost, the free connection check and
// the automatic pole are the build gun's. Single player and host only, like belts: the hologram is driven here, on
// the machine that has authority.

#include "AtlasSubsystem.h"

#include "AtlasBuildShared.h"
#include "AtlasTypes.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableWire.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Equipment/FGBuildGun.h"
#include "FactoryAtlas.h"
#include "FGBuildableSubsystem.h"
#include "FGCharacterPlayer.h"
#include "FGCircuitConnectionComponent.h"
#include "FGConstructDisqualifier.h"
#include "FGRecipe.h"
#include "FGRecipeManager.h"
#include "Hologram/FGHologram.h"
#include "Hologram/FGWireHologram.h"
#include "ItemAmount.h"
#include "Resources/FGBuildingDescriptor.h"
#include "Resources/FGItemDescriptor.h"

using namespace Atlas;

namespace AtlasWireDetail
{
	constexpr int32 WireSettle = 2;          // Frames for the hologram and its pole to catch up after a change
	constexpr int32 WireHitVariants = 3;     // 0 a trace onto the building, 1 a hit made up from the actor, 2 also set directly
	constexpr uint64 WireMapGoneFrames = 30; // A view not painted for this long was closed

	/** The unlocked recipe that builds a power line. The plain power line wins over any other wire. */
	TSubclassOf<UFGRecipe> WireRecipe(UWorld* World)
	{
		AFGRecipeManager* Recipes = World ? AFGRecipeManager::Get(World) : nullptr;
		if (!Recipes)
		{
			return nullptr;
		}
		TSubclassOf<UFGRecipe> Best;
		FString BestName;
		bool bBestPlain = false;
		for (const TSubclassOf<UFGRecipe>& Recipe : Recipes->GetAllAvailableRecipes())
		{
			const TArray<FItemAmount> Products = UFGRecipe::GetProducts(Recipe);
			if (Products.IsEmpty() || !Products[0].ItemClass || !Products[0].ItemClass->IsChildOf(UFGBuildingDescriptor::StaticClass()))
			{
				continue;
			}
			const TSubclassOf<AFGBuildable> Buildable = UFGBuildingDescriptor::GetBuildableClass(TSubclassOf<UFGBuildingDescriptor>(Products[0].ItemClass));
			if (!Buildable || !Buildable->IsChildOf(AFGBuildableWire::StaticClass()))
			{
				continue;
			}
			const FString Name = Buildable->GetName();
			const bool bPlain = Name.Contains(TEXT("PowerLine"));
			if (!Best || (bPlain && !bBestPlain) || (bPlain == bBestPlain && Name < BestName))
			{
				Best = Recipe;
				BestName = Name;
				bBestPlain = bPlain;
			}
		}
		return Best;
	}

	/** The power connection on a building nearest a point, one with a free slot first. Hidden ones, such as a track's, don't count. */
	UFGCircuitConnectionComponent* PickConnection(const AActor* Actor, const FVector& Near)
	{
		if (!Actor)
		{
			return nullptr;
		}
		TInlineComponentArray<UFGCircuitConnectionComponent*> Connections(Actor);
		UFGCircuitConnectionComponent* Best = nullptr;
		double BestDistance = 0.0;
		bool bBestFree = false;
		for (UFGCircuitConnectionComponent* Connection : Connections)
		{
			if (!Connection || Connection->IsHidden())
			{
				continue;
			}
			const bool bFree = Connection->GetNumFreeConnections() > 0;
			const double Distance = FVector::DistSquared(Connection->GetComponentLocation(), Near);
			if (!Best || (bFree && !bBestFree) || (bFree == bBestFree && Distance < BestDistance))
			{
				Best = Connection;
				BestDistance = Distance;
				bBestFree = bFree;
			}
		}
		return Best;
	}

	/**
	 * What the build gun sees aimed at a building's power connection. Variant 0 is a real trace down onto the
	 * building, naming it past the instance manager it collides through; 1 and up a hit made up from the actor.
	 */
	FHitResult WireHit(AActor* Owner, const FVector& Location, const AFGHologram* Hologram, int32 Variant)
	{
		FHitResult Hit;
		if (!Owner)
		{
			return Hit;
		}
		UWorld* World = Owner->GetWorld();
		ECollisionChannel Channel;
		if (Variant == 0 && World && AtlasBeltDetail::FindBuildGunChannel(Channel))
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(AtlasWire), true);
			if (Hologram)
			{
				Params.AddIgnoredActor(Hologram);
				for (AFGHologram* Child : Hologram->GetHologramChildren())
				{
					Params.AddIgnoredActor(Child);
				}
			}
			const FVector Top = Location + FVector(0.0, 0.0, 300.0);
			const FVector Bottom = Location - FVector(0.0, 0.0, 300.0);
			// Whatever else is in the way, such as a roof or a walkway, is stepped past.
			for (int32 Try = 0; Try < 4; Try++)
			{
				FHitResult Trace;
				if (!World->LineTraceSingleByChannel(Trace, Top, Bottom, Channel, Params))
				{
					break;
				}
				if (AtlasBeltDetail::HitOwner(Trace) == Owner)
				{
					Trace.HitObjectHandle = FActorInstanceHandle(Owner);
					return Trace;
				}
				if (!Trace.GetComponent())
				{
					break;
				}
				Params.AddIgnoredComponent(Trace.GetComponent());
			}
		}
		UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Owner->GetRootComponent());
		if (!Primitive)
		{
			Primitive = Owner->FindComponentByClass<UPrimitiveComponent>();
		}
		Hit.bBlockingHit = true;
		Hit.HitObjectHandle = FActorInstanceHandle(Owner);
		Hit.Component = Primitive;
		Hit.Location = Location;
		Hit.ImpactPoint = Location;
		Hit.Normal = FVector::UpVector;
		Hit.ImpactNormal = FVector::UpVector;
		Hit.TraceStart = Location + FVector(0.0, 0.0, 300.0);
		Hit.TraceEnd = Location - FVector(0.0, 0.0, 300.0);
		Hit.Distance = 300.f;
		return Hit;
	}

	FVector UvToWorld(const FVector2f& Uv, double Z)
	{
		return FVector(MapWestCm + double(Uv.X) * MapWidthCm, MapNorthCm + double(Uv.Y) * MapHeightCm, Z);
	}

	bool IsSnappedTo(const AFGWireHologram* Live, int32 End, const AActor* Owner)
	{
		const UFGCircuitConnectionComponent* Connection = Live ? Live->GetConnection(End) : nullptr;
		return Connection && Owner && Connection->GetOwner() == Owner;
	}
}
using namespace AtlasWireDetail;

void UAtlasSubsystem::SetWiring(EView View, bool bOn)
{
	if (!bOn)
	{
		if (!Wire.bMode || Wire.View != View)
		{
			return;
		}
		if (WireTicker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(WireTicker);
			WireTicker.Reset();
		}
		DestroyWireHologram();
		Wire = FWireTool();
		return;
	}
	if (!CanBuildBelts())
	{
		ShowToast(FText::FromString(TEXT("Power lines can only be built from the map in single player or as the host.")), true);
		PlayClipboardSound(EClipboardSound::Error);
		return;
	}
	if (Wire.bMode)
	{
		SetWiring(Wire.View, false);
	}
	SetMeasuring(View, false);
	CancelBeltDrag();
	Wire = FWireTool();
	Wire.bMode = true;
	Wire.View = View;
	if (const TSubclassOf<UFGRecipe> Recipe = WireRecipe(GetWorld()))
	{
		Wire.WireName = UFGRecipe::GetRecipeName(Recipe);
	}
	bWireHoverResolved = false;
	WireHoverActor.Reset();
	if (!WireTicker.IsValid())
	{
		WireTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float) { return TickWire(); }));
	}
}

bool UAtlasSubsystem::DropWire()
{
	if (!Wire.bHasStart)
	{
		return false;
	}
	DestroyWireHologram();
	Wire.bHasStart = false;
	Wire.Start.Reset();
	Wire.StartOwner.Reset();
	Wire.bReady = false;
	Wire.bValid = false;
	Wire.Reason = FText::GetEmpty();
	return true;
}

void UAtlasSubsystem::DestroyWireHologram()
{
	if (AFGHologram* Live = WireHologram.Get())
	{
		Live->Destroy();
	}
	WireHologram.Reset();
}

void UAtlasSubsystem::SetWireStart(UFGCircuitConnectionComponent* Connection)
{
	Wire.Start = Connection;
	Wire.StartOwner = Cast<AFGBuildable>(Connection->GetOwner());
	Wire.StartLocation = Connection->GetComponentLocation();
	Wire.bHasStart = true;
	WireHitVariant = 0;
	SpawnWireHologram();
}

bool UAtlasSubsystem::SpawnWireHologram()
{
	DestroyWireHologram();
	Wire.bReady = false;
	Wire.bValid = false;
	Wire.bClipping = false;
	Wire.Reason = FText::GetEmpty();
	Wire.CostText.Reset();
	WireStage = 0;
	WireWaitFrames = WireSettle;
	WireSettleFrames = WireSettle;
	WireEndVariant = 0;

	AFGCharacterPlayer* Player = BeltPlayer();
	const TSubclassOf<UFGRecipe> Recipe = WireRecipe(GetWorld());
	if (!Player || !Player->GetBuildGun() || !Recipe)
	{
		Wire.Reason = FText::FromString(Recipe ? TEXT("Can't build right now.") : TEXT("No power line unlocked yet."));
		Wire.bReady = true;
		WireStage = -1;
		return false;
	}
	Wire.WireName = UFGRecipe::GetRecipeName(Recipe);
	AFGHologram* Live = AFGHologram::SpawnHologramFromRecipe(Recipe, Player->GetBuildGun(), Wire.StartLocation, Player);
	if (!Live || !Live->IsA<AFGWireHologram>())
	{
		if (Live)
		{
			Live->Destroy();
		}
		Wire.Reason = FText::FromString(TEXT("The game made no power line hologram."));
		Wire.bReady = true;
		WireStage = -1;
		return false;
	}
	WireHologram = Live;
	return true;
}

void UAtlasSubsystem::ResolveWireTarget()
{
	// Hover finds the record under the cursor a few times a second; the building behind it is looked up only when
	// that record changes.
	const bool bOver = Hover.bValid && !Hover.bMarker && HoverView == Wire.View;
	if (!bOver)
	{
		bWireHoverResolved = false;
		WireHoverActor.Reset();
	}
	else if (!bWireHoverResolved || Hover.Source != WireHoverSource)
	{
		WireHoverSource = Hover.Source;
		WireHoverActor = FindHoveredActor();
		bWireHoverResolved = true;
	}
	AFGBuildable* Actor = bOver ? WireHoverActor.Get() : nullptr;
	const FVector2f Uv = MapCursorUv[int32(Wire.View)];
	const FVector Cursor = UvToWorld(Uv, Actor ? Actor->GetActorLocation().Z : Wire.StartLocation.Z);
	UFGCircuitConnectionComponent* Connection = PickConnection(Actor, Cursor);
	const bool bOnStart = Wire.bHasStart && Actor && Actor == Wire.StartOwner.Get();

	const bool bHasTarget = Connection && !bOnStart;
	const bool bHasGround = !bHasTarget && !bOnStart;
	const bool bChanged = bHasTarget != Wire.bHasTarget || bHasGround != Wire.bHasGround || (bHasTarget && Actor != Wire.Target.Get());
	Wire.bHasTarget = bHasTarget;
	Wire.bHasGround = bHasGround;
	Wire.Target = bHasTarget ? Actor : nullptr;
	Wire.TargetConnection = bHasTarget ? Connection : nullptr;
	Wire.EndLocation = bHasTarget ? Connection->GetComponentLocation() : Cursor;
	if (bChanged)
	{
		WireSettleFrames = WireSettle;
		WireEndVariant = 0;
		Wire.bReady = false;
	}
}

bool UAtlasSubsystem::TickWire()
{
	if (!Wire.bMode)
	{
		WireTicker.Reset();
		return false;
	}
	// The map was closed, or building from it stopped being possible. The hologram is in the world, so nothing may
	// be left behind.
	if (GFrameCounter - MapPaintFrame[int32(Wire.View)] > WireMapGoneFrames || !CanBuildBelts())
	{
		WireTicker.Reset();
		DestroyWireHologram();
		Wire = FWireTool();
		return false;
	}

	ResolveWireTarget();
	AFGWireHologram* Live = Cast<AFGWireHologram>(WireHologram.Get());
	if (!Wire.bHasStart || WireStage < 0 || !Live)
	{
		return true;
	}
	if (--WireWaitFrames > 0)
	{
		return true;
	}
	WireWaitFrames = 1;

	switch (WireStage)
	{
	case 0:
	{
		// The start, on the building the first click picked.
		Live->ResetConstructDisqualifiers();
		Live->SetActorHiddenInGame(false);
		Live->UpdateHologramPlacement(WireHit(Wire.StartOwner.Get(), Wire.StartLocation, Live, FMath::Min(WireHitVariant, 1)));
		if (WireHitVariant >= 2)
		{
			Live->SetConnection(0, Wire.Start.Get());
		}
		WireStage = 1;
		WireWaitFrames = WireSettle;
		return true;
	}
	case 1:
		if (!IsSnappedTo(Live, 0, Wire.StartOwner.Get()))
		{
			UE_LOG(LogFactoryAtlas, Display, TEXT("Wire: aim %d didn't snap to %s (snapped to %s)"), WireHitVariant,
				*GetNameSafe(Wire.StartOwner.Get()), *GetNameSafe(Live->GetConnection(0)));
			if (++WireHitVariant < WireHitVariants)
			{
				WireStage = 0;
				return true;
			}
			Wire.Reason = FText::FromString(TEXT("Can't start a power line on that building."));
			Wire.bReady = true;
			Wire.bValid = false;
			WireStage = -1;
			return true;
		}
		// The game may have picked another of the building's connections, nearer where it was aimed.
		Wire.Start = Live->GetConnection(0);
		Wire.StartLocation = Wire.Start->GetComponentLocation();
		Live->DoMultiStepPlacement(false);
		WireStage = 2;
		WireWaitFrames = WireSettle;
		return true;
	default:
		break;
	}

	// The end follows the cursor every frame.
	if (Wire.bHasTarget)
	{
		Live->ResetConstructDisqualifiers();
		Live->SetActorHiddenInGame(false);
		Live->UpdateHologramPlacement(WireHit(Wire.Target.Get(), Wire.EndLocation, Live, FMath::Min(WireEndVariant, 1)));
		if (WireEndVariant >= 2)
		{
			Live->SetConnection(1, Wire.TargetConnection.Get());
		}
	}
	else if (Wire.bHasGround)
	{
		// Where the build gun would land looking straight down at the cursor, within the map's height filter and
		// no more than 20 m above the start, so a pole lands on the floor being worked on rather than a roof.
		UWorld* World = GetWorld();
		ECollisionChannel Channel;
		FHitResult Hit;
		bool bHit = false;
		if (World && AtlasBeltDetail::FindBuildGunChannel(Channel))
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(AtlasWireGround), true);
			Params.AddIgnoredActor(Live);
			for (AFGHologram* Child : Live->GetHologramChildren())
			{
				Params.AddIgnoredActor(Child);
			}
			const double Top = FMath::Min(double(FilterHigh) * 100.0 + 50.0, Wire.StartLocation.Z + 2000.0);
			const double Bottom = double(FilterLow) * 100.0 - 50.0;
			const FVector2f Uv = MapCursorUv[int32(Wire.View)];
			bHit = World->LineTraceSingleByChannel(Hit, UvToWorld(Uv, Top), UvToWorld(Uv, Bottom), Channel, Params) && Live->IsValidHitResult(Hit);
		}
		if (!bHit)
		{
			Wire.bReady = true;
			Wire.bValid = false;
			Wire.Reason = FText::FromString(TEXT("Nothing to build on here."));
			Wire.CostText.Reset();
			return true;
		}
		AtlasBeltDetail::ResolveForPlacement(Hit);
		Wire.EndLocation = Hit.ImpactPoint;
		Live->ResetConstructDisqualifiers();
		Live->SetActorHiddenInGame(false);
		Live->UpdateHologramPlacement(Hit);
	}
	else
	{
		// Over the building the wire starts from.
		Wire.bReady = true;
		Wire.bValid = false;
		Wire.Reason = FText::GetEmpty();
		Wire.CostText.Reset();
		return true;
	}

	if (WireSettleFrames > 0)
	{
		WireSettleFrames--;
		return true;
	}
	ValidateWire();
	// Aimed at a building but not on it: try the next way of aiming before giving the verdict.
	if (Wire.bHasTarget && !IsSnappedTo(Live, 1, Wire.Target.Get()) && WireEndVariant + 1 < WireHitVariants && !Wire.bValid)
	{
		WireEndVariant++;
		WireSettleFrames = WireSettle;
		Wire.bReady = false;
	}
	return true;
}

void UAtlasSubsystem::ValidateWire()
{
	AFGWireHologram* Live = Cast<AFGWireHologram>(WireHologram.Get());
	AFGCharacterPlayer* Player = BeltPlayer();
	if (!Live || !Player || !Player->GetBuildGun())
	{
		return;
	}
	// Validation refuses anything hidden, and children such as the automatic pole stay hidden unless shown.
	Live->ResetConstructDisqualifiers();
	for (AFGHologram* Child : Live->GetHologramChildren())
	{
		if (Child && !Child->IsDisabled())
		{
			Child->SetActorHiddenInGame(false);
			Child->ResetConstructDisqualifiers();
		}
	}
	Live->ValidatePlacementAndCost(Player->GetBuildGun()->GetInventory());

	TArray<TSubclassOf<UFGConstructDisqualifier>> Disqualifiers;
	Live->GetConstructDisqualifiers(Disqualifiers);
	for (AFGHologram* Child : Live->GetHologramChildren())
	{
		if (Child && !Child->IsDisabled())
		{
			TArray<TSubclassOf<UFGConstructDisqualifier>> ChildDisqualifiers;
			Child->GetConstructDisqualifiers(ChildDisqualifiers);
			Disqualifiers.Append(ChildDisqualifiers);
		}
	}
	// With clipping allowed, only what isn't a clearance overlap can refuse it, as for belts.
	TArray<TSubclassOf<UFGConstructDisqualifier>> Blocking;
	for (const TSubclassOf<UFGConstructDisqualifier>& Disqualifier : Disqualifiers)
	{
		if (!Settings.bAllowClipping || !AtlasBeltDetail::IsClippingDisqualifier(Disqualifier))
		{
			Blocking.Add(Disqualifier);
		}
	}

	const bool bSnapped = !Wire.bHasTarget || IsSnappedTo(Live, 1, Wire.Target.Get());
	Wire.bReady = true;
	Wire.bClipping = false;
	if (bSnapped && Live->CanConstruct())
	{
		Wire.bValid = true;
		Wire.Reason = FText::GetEmpty();
	}
	else if (bSnapped && Blocking.IsEmpty() && !Disqualifiers.IsEmpty())
	{
		Wire.bValid = true;
		Wire.bClipping = true;
		Wire.Reason = FText::GetEmpty();
	}
	else
	{
		Wire.bValid = false;
		Wire.Reason = !Blocking.IsEmpty() ? UFGConstructDisqualifier::GetDisqualifyingText(Blocking[0])
			: FText::FromString(bSnapped ? TEXT("Can't build this power line.") : TEXT("Can't connect to that building."));
	}

	if (const UFGCircuitConnectionComponent* End = Live->GetConnection(1))
	{
		Wire.EndLocation = End->GetComponentLocation();
	}
	Wire.LengthMetres = float(FVector::Dist(Wire.StartLocation, Wire.EndLocation) / 100.0);
	TArray<FString> Parts;
	for (const FItemAmount& Item : Live->GetCost(true))
	{
		if (Item.ItemClass && Item.Amount > 0)
		{
			Parts.Add(FString::Printf(TEXT("%d %s"), Item.Amount, *UFGItemDescriptor::GetItemName(Item.ItemClass).ToString()));
		}
	}
	Wire.CostText = FString::Join(Parts, TEXT(", "));
}

void UAtlasSubsystem::ClickWire(EView View)
{
	if (!IsWiring(View))
	{
		return;
	}
	ResolveWireTarget();
	if (!Wire.bHasStart)
	{
		UFGCircuitConnectionComponent* Connection = Wire.bHasTarget ? Wire.TargetConnection.Get() : nullptr;
		if (!Connection)
		{
			ShowToast(FText::FromString(TEXT("Click a building with a power connection to start a wire.")), true);
			PlayClipboardSound(EClipboardSound::Error);
			return;
		}
		if (Connection->GetNumFreeConnections() <= 0)
		{
			ShowToast(FText::Format(FText::FromString(TEXT("{0} has no free power connection.")), GetHoverName()), true);
			PlayClipboardSound(EClipboardSound::Error);
			return;
		}
		SetWireStart(Connection);
		PlayClipboardSound(EClipboardSound::Copy);
		return;
	}
	if (Wire.bHasTarget || Wire.bHasGround)
	{
		BuildWire();
	}
}

void UAtlasSubsystem::BuildWire()
{
	AFGWireHologram* Live = Cast<AFGWireHologram>(WireHologram.Get());
	AFGBuildableSubsystem* Buildables = AFGBuildableSubsystem::Get(GetWorld());
	if (!Live || !Buildables || WireStage < 2)
	{
		if (!Wire.Reason.IsEmpty())
		{
			ShowToast(Wire.Reason, true);
			PlayClipboardSound(EClipboardSound::Error);
		}
		return;
	}
	if (!Wire.bReady)
	{
		ShowToast(FText::FromString(TEXT("Still checking that wire, click again.")));
		return;
	}
	ValidateWire();
	if (!Wire.bValid)
	{
		UE_LOG(LogFactoryAtlas, Display, TEXT("Wire: click refused: %s"), *Wire.Reason.ToString());
		ShowToast(Wire.Reason, true);
		PlayClipboardSound(EClipboardSound::Error);
		return;
	}
	if (Wire.bClipping)
	{
		// Clipping allowed: drop the clearance complaints so the game goes ahead. Nothing else was left.
		Live->ResetConstructDisqualifiers();
		for (AFGHologram* Child : Live->GetHologramChildren())
		{
			if (Child)
			{
				Child->ResetConstructDisqualifiers();
			}
		}
	}

	const bool bToGround = !Wire.bHasTarget;
	UFGCircuitConnectionComponent* EndConnection = bToGround ? nullptr : Live->GetConnection(1);
	// The click that ends the wire; one with an automatic pole may ask for one more.
	bool bDone = false;
	for (int32 Step = 0; Step < 3 && !bDone; Step++)
	{
		bDone = Live->DoMultiStepPlacement(false);
	}
	if (!bDone)
	{
		UE_LOG(LogFactoryAtlas, Warning, TEXT("Wire: not finished after 3 steps"));
		ShowToast(FText::FromString(TEXT("The power line wasn't finished.")), true);
		PlayClipboardSound(EClipboardSound::Error);
		SpawnWireHologram();
		return;
	}

	const TArray<FItemAmount> Cost = Live->GetCost(true);
	TArray<AActor*> Children;
	AActor* Built = Live->Construct(Children, Buildables->GetNewNetConstructionID());
	if (!Built)
	{
		UE_LOG(LogFactoryAtlas, Warning, TEXT("Wire: Construct returned nothing"));
		ShowToast(FText::FromString(TEXT("The game built nothing.")), true);
		PlayClipboardSound(EClipboardSound::Error);
		SpawnWireHologram();
		return;
	}
	if (!PayBuildCost(Cost))
	{
		UE_LOG(LogFactoryAtlas, Warning, TEXT("Wire: built %s but couldn't take its whole cost"), *Built->GetName());
	}

	// The next wire starts where this one ended: on the building, or on the pole just placed.
	UFGCircuitConnectionComponent* Next = EndConnection;
	if (bToGround)
	{
		for (AActor* Child : Children)
		{
			if (Child && Child != Built)
			{
				if (UFGCircuitConnectionComponent* Found = PickConnection(Child, Child->GetActorLocation()))
				{
					Next = Found;
					break;
				}
			}
		}
	}
	UE_LOG(LogFactoryAtlas, Display, TEXT("Wire: built %s from %s to %s (%.0f m)%s"), *Built->GetName(), *GetNameSafe(Wire.StartOwner.Get()),
		bToGround ? TEXT("a new pole") : *GetNameSafe(Wire.Target.Get()), Wire.LengthMetres, Wire.bClipping ? TEXT(", clipping") : TEXT(""));
	PlayClipboardSound(EClipboardSound::Paste);

	const FText Name = Wire.WireName.IsEmpty() ? FText::FromString(TEXT("Power Line")) : Wire.WireName;
	if (IsValid(Next) && Next->GetNumFreeConnections() > 0)
	{
		ShowToast(FText::Format(FText::FromString(bToGround ? TEXT("Built {0} and a power pole.") : TEXT("Built {0}.")), Name));
		SetWireStart(Next);
	}
	else
	{
		// Nowhere left to go on from: the wire ends here and the next click starts a new one.
		ShowToast(FText::Format(FText::FromString(TEXT("Built {0}. That end has no free connection, so the chain stops.")), Name));
		DropWire();
	}
}
