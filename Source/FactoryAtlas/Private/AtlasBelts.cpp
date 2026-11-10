// Building belts, pipes, hypertubes and railway from the map: free ports drawn as nodes, and a drag from one port to
// another of the same kind that builds a line between them through the game's own hologram, so it is routed,
// checked and charged exactly as the build gun would. The one exception is "allow clipping", which lets clearance
// overlaps through. Single player only: the hologram is driven here, on the machine that has authority.
// ("Belt" in names here predates the other kinds and means any of them.)
//
// The hologram sequence follows SmartMCP's remote build (SmartMCPBuild.cpp): place the start, step past it, then
// place the end each frame and validate. Ports are reached with a build gun trace aimed into the port itself.

#include "AtlasSubsystem.h"
#include "AtlasBuildShared.h"

#include "AtlasTypes.h"
#include "AbstractInstanceManager.h"
#include "Engine/CollisionProfile.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "Equipment/FGBuildGun.h"
#include "FactoryAtlas.h"
#include "FGBuildableSubsystem.h"
#include "FGBuildGunModeDescriptor.h"
#include "FGCentralStorageSubsystem.h"
#include "FGCharacterPlayer.h"
#include "FGConstructDisqualifier.h"
#include "Buildables/FGBuildablePipeHyper.h"
#include "Buildables/FGBuildablePipeline.h"
#include "Buildables/FGBuildableRailroadTrack.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGPipeConnectionComponentHyper.h"
#include "FGRailroadTrackConnectionComponent.h"
#include "FGInventoryComponent.h"
#include "FGRecipe.h"
#include "FGRecipeManager.h"
#include "GameFramework/PlayerController.h"
#include "Hologram/FGConveyorBeltHologram.h"
#include "Hologram/FGPipelineHologram.h"
#include "Hologram/FGHologram.h"
#include "Hologram/FGHologramBuildModeDescriptor.h"
#include "Hologram/FGSplineHologram.h"
#include "ItemAmount.h"
#include "Resources/FGBuildingDescriptor.h"

using namespace Atlas;

namespace AtlasBeltDetail
{
	constexpr double PortsRefreshSeconds = 0.25; // At most this often after buildings change
	constexpr float PortReachPixels = 9.f;       // How close to a node the cursor must be
	constexpr float DragSlopPixels = 5.f;        // A press that moves less is a click
	constexpr int32 SettleFrames = 2;            // Frames for the hologram's children to catch up after a step
	constexpr float PreviewStepCm = 150.f;
	constexpr int32 PreviewMaxPoints = 96;

	struct FLineRecipe
	{
		TSubclassOf<UFGRecipe> Recipe;
		UClass* Buildable = nullptr;
		float Rank = 0.f; // Higher is the better tier: belt speed, pipe flow
		FString Name;
	};

	UClass* BuildableFor(EPortKind Kind)
	{
		switch (Kind)
		{
		case EPortKind::Belt: return AFGBuildableConveyorBelt::StaticClass();
		case EPortKind::Pipe: return AFGBuildablePipeline::StaticClass();
		case EPortKind::Hypertube: return AFGBuildablePipeHyper::StaticClass();
		case EPortKind::Rail: return AFGBuildableRailroadTrack::StaticClass();
		default: return nullptr;
		}
	}

	/** One of what this kind of port builds, for messages. */
	const TCHAR* KindNoun(EPortKind Kind)
	{
		switch (Kind)
		{
		case EPortKind::Pipe: return TEXT("pipe");
		case EPortKind::Hypertube: return TEXT("hypertube");
		case EPortKind::Rail: return TEXT("track");
		default: return TEXT("belt");
		}
	}

	const TCHAR* KindName(EPortKind Kind)
	{
		switch (Kind)
		{
		case EPortKind::Pipe: return TEXT("pipes");
		case EPortKind::Hypertube: return TEXT("hypertubes");
		case EPortKind::Rail: return TEXT("railways");
		default: return TEXT("conveyor belts");
		}
	}

	/** Unlocked recipes that build what runs between two ports of this kind, best tier first. */
	TArray<FLineRecipe> UnlockedRecipes(UWorld* World, EPortKind Kind, const UClass* Prefer = nullptr)
	{
		TArray<FLineRecipe> Out;
		AFGRecipeManager* Recipes = World ? AFGRecipeManager::Get(World) : nullptr;
		UClass* Wanted = BuildableFor(Kind);
		if (!Recipes || !Wanted)
		{
			return Out;
		}
		for (const TSubclassOf<UFGRecipe>& Recipe : Recipes->GetAllAvailableRecipes())
		{
			const TArray<FItemAmount> Products = UFGRecipe::GetProducts(Recipe);
			if (Products.IsEmpty() || !Products[0].ItemClass || !Products[0].ItemClass->IsChildOf(UFGBuildingDescriptor::StaticClass()))
			{
				continue;
			}
			const TSubclassOf<AFGBuildable> Buildable = UFGBuildingDescriptor::GetBuildableClass(TSubclassOf<UFGBuildingDescriptor>(Products[0].ItemClass));
			if (!Buildable || !Buildable->IsChildOf(Wanted))
			{
				continue;
			}
			FLineRecipe& Line = Out.AddDefaulted_GetRef();
			Line.Recipe = Recipe;
			Line.Buildable = Buildable;
			Line.Name = UFGRecipe::GetRecipeName(Recipe).ToString();
			if (Kind == EPortKind::Belt)
			{
				Line.Rank = GetDefault<AFGBuildableConveyorBelt>(Buildable)->GetSpeed();
			}
			else if (Kind == EPortKind::Pipe)
			{
				Line.Rank = GetDefault<AFGBuildablePipeline>(Buildable)->GetFlowLimit();
			}
		}
		// Ties, such as a pipe and its clean (no indicator) twin, go to the one the drag starts from: aimed at a pipe,
		// its twin is an upgrade that swaps the whole existing pipe rather than a new pipe. Then by name.
		Out.Sort([Prefer](const FLineRecipe& A, const FLineRecipe& B)
		{
			if (A.Rank != B.Rank)
			{
				return A.Rank > B.Rank;
			}
			if ((A.Buildable == Prefer) != (B.Buildable == Prefer))
			{
				return A.Buildable == Prefer;
			}
			return A.Name < B.Name;
		});
		return Out;
	}

	/** Clearance overlaps are what "allow clipping" lets through. Players, creatures and vehicles in the way still count. */
	bool IsClippingDisqualifier(const TSubclassOf<UFGConstructDisqualifier>& Disqualifier)
	{
		return Disqualifier && (Disqualifier->IsChildOf(UFGCDEncroachingClearance::StaticClass())
			|| Disqualifier->IsChildOf(UFGCDEncroachingSoftClearance::StaticClass()));
	}

	bool FindBuildGunChannel(ECollisionChannel& Out)
	{
		static int32 Cached = -2; // -2 unknown, -1 missing
		if (Cached == -2)
		{
			Cached = -1;
			const UCollisionProfile* Profile = UCollisionProfile::Get();
			for (int32 Channel = 0; Profile && Channel < ECC_MAX; Channel++)
			{
				if (Profile->ReturnChannelNameFromContainerIndex(Channel) == FName(TEXT("BuildGun")))
				{
					Cached = Channel;
					break;
				}
			}
		}
		Out = ECollisionChannel(FMath::Max(Cached, 0));
		return Cached >= 0;
	}

	/** The building a hit belongs to. Most buildings collide through the abstract instance manager, not themselves. */
	AActor* HitOwner(const FHitResult& Hit)
	{
		AActor* Actor = Hit.GetActor();
		if (AAbstractInstanceManager* Manager = Cast<AAbstractInstanceManager>(Actor))
		{
			FInstanceHandle Handle;
			if (Manager->ResolveHit(Hit, Handle))
			{
				if (AActor* Owner = AAbstractInstanceManager::GetOwnerByHandle(Handle))
				{
					return Owner;
				}
			}
		}
		return Actor;
	}

	/**
	 * Names the building a hit belongs to, so the hologram can snap to it. Foundations and walls are lightweight:
	 * no actor of their own, so the game's pool lends a temporary stand-in, as SmartFoundations does. The pool owns
	 * and retires it. Without this a belt end on a foundation knows only "some instance" and ignores its grid.
	 */
	void ResolveForPlacement(FHitResult& Hit)
	{
		AAbstractInstanceManager* Manager = Cast<AAbstractInstanceManager>(Hit.GetActor());
		if (!Manager)
		{
			return;
		}
		FInstanceHandle Handle;
		if (!Manager->ResolveHit(Hit, Handle))
		{
			return;
		}
		FLightweightBuildableInstanceRef Instance;
		if (AFGLightweightBuildableSubsystem::ResolveLightweightInstance(Handle, Instance))
		{
			AFGBuildable* Temporary = Instance.SpawnTemporaryBuildable();
			if (IsValid(Temporary) && !Temporary->IsActorBeingDestroyed() && Temporary->GetClass() == Instance.GetBuildableClass())
			{
				Hit.HitObjectHandle = FActorInstanceHandle(Temporary);
			}
			return;
		}
		if (AActor* Owner = AAbstractInstanceManager::GetOwnerByHandle(Handle); IsValid(Owner))
		{
			Hit.HitObjectHandle = FActorInstanceHandle(Owner);
		}
	}

	/**
	 * What the build gun sees when aimed straight at the port: a real trace on its channel into the port's face, so
	 * the hit carries the instanced collision the belt hologram's port snapping expects. A hit made up from the actor
	 * alone looks right but never snaps. Falls back to that only if the trace finds nothing of the port's building.
	 */
	// 0-2: one trace each (into the face, down 40 cm inside, down 1 m inside), naming the building as
	// SmartFoundations does for placement. 3: the first of those that hits, as the raw hit. 4: made up from the actor.
	// Each is a fallback for the last: a pipe aimed at one end can snap to its other end, which a different aim avoids.
	constexpr int32 HitVariants = 5;

	FHitResult HitAtPort(const FPort& Port, const AFGHologram* Hologram, int32 Variant)
	{
		FHitResult Hit;
		AFGBuildable* Owner = Port.Owner.Get();
		if (!Owner)
		{
			return Hit;
		}
		ECollisionChannel Channel;
		UWorld* World = Owner->GetWorld();
		if (World && FindBuildGunChannel(Channel))
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(AtlasBeltPort), true);
			if (Hologram)
			{
				Params.AddIgnoredActor(Hologram);
				for (AFGHologram* Child : Hologram->GetHologramChildren())
				{
					Params.AddIgnoredActor(Child);
				}
			}
			const FVector Normal = Port.Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
			// Into the port's face, for a building's side. Then straight down a little inside the end, for a pipe or
			// track end: a trace along a pipe runs down its open bore and hits nothing, and a track lies flat.
			const FVector Near = Port.Location - Normal * 40.0;
			const FVector Far = Port.Location - Normal * 100.0;
			const TPair<FVector, FVector> Segments[] = {
				{ Port.Location + Normal * 60.0, Port.Location - Normal * 60.0 },
				{ Near + FVector(0.0, 0.0, 250.0), Near - FVector(0.0, 0.0, 150.0) },
				{ Far + FVector(0.0, 0.0, 250.0), Far - FVector(0.0, 0.0, 150.0) },
			};
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Segments); Index++)
			{
				const TPair<FVector, FVector>& Segment = Segments[Index];
				if (Variant >= 4 || (Variant < 3 && Index != Variant))
				{
					continue;
				}
				// Whatever else is in the way, such as the pipe support over a pipe's end, is stepped past.
				FCollisionQueryParams Past = Params;
				for (int32 Try = 0; Try < 4; Try++)
				{
					FHitResult Trace;
					if (!World->LineTraceSingleByChannel(Trace, Segment.Key, Segment.Value, Channel, Past))
					{
						break;
					}
					if (HitOwner(Trace) == Owner)
					{
						// Keep the collision but name the building, except for the raw variant.
						if (Variant < 3)
						{
							Trace.HitObjectHandle = FActorInstanceHandle(Owner);
						}
						return Trace;
					}
					if (!Trace.GetComponent())
					{
						break;
					}
					Past.AddIgnoredComponent(Trace.GetComponent());
				}
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
		Hit.Location = Port.Location;
		Hit.ImpactPoint = Port.Location;
		Hit.Normal = FVector::UpVector;
		Hit.ImpactNormal = FVector::UpVector;
		Hit.TraceStart = Port.Location + FVector(0.0, 0.0, 300.0);
		Hit.TraceEnd = Port.Location - FVector(0.0, 0.0, 300.0);
		Hit.Distance = 300.f;
		return Hit;
	}

	/**
	 * What the hologram's start (0) or end (1) is attached to. AFGSplineHologram::IsConnectionSnapped is only
	 * implemented for pipes and says no for every belt, so read the hologram's own record. Belt, pipe (hypertubes
	 * included) and track holograms each keep one under the same name.
	 */
	/** Where the hologram's start (0) or end (1) sits. The start is the actor itself until the spline has points. */
	FVector SplineEnd(const AFGSplineHologram* Spline, int32 End)
	{
		const USplineComponent* Curve = Spline->FindComponentByClass<USplineComponent>();
		const int32 Points = Curve ? Curve->GetNumberOfSplinePoints() : 0;
		if (End == 0 || Points < 2)
		{
			return End == 0 || !Curve ? Spline->GetActorLocation() : Curve->GetLocationAtSplinePoint(Points - 1, ESplineCoordinateSpace::World);
		}
		return Curve->GetLocationAtSplinePoint(Points - 1, ESplineCoordinateSpace::World);
	}

	const UObject* SnappedConnection(const AFGSplineHologram* Spline, int32 End)
	{
		const FObjectPropertyBase* Property = Spline ? FindFProperty<FObjectPropertyBase>(Spline->GetClass(), TEXT("mSnappedConnectionComponents")) : nullptr;
		if (!Property || End < 0 || End >= Property->ArrayDim)
		{
			return nullptr;
		}
		return Property->GetObjectPropertyValue_InContainer(Spline, End);
	}

	bool IsSnappedTo(AFGSplineHologram* Spline, int32 End, const FPort& Port)
	{
		const UObject* Snapped = SnappedConnection(Spline, End);
		if (Snapped && Snapped == Port.Connection.Get())
		{
			return true;
		}
		// Pipes don't fill that record in while placing. A snapped pipe sits exactly on the port, an unsnapped one
		// at the aim on the pipe's surface, so where that end sits tells them apart.
		if (!Spline || !Spline->IsA<AFGPipelineHologram>())
		{
			return false;
		}
		return FVector::DistSquared(SplineEnd(Spline, End), Port.Location) < FMath::Square(25.0);
	}

	FVector2f BeltToUv(const FVector& Location)
	{
		return FVector2f(float((Location.X - MapWestCm) / MapWidthCm), float((Location.Y - MapNorthCm) / MapHeightCm));
	}

	bool IsPortConnected(const UFGConnectionComponent* Connection)
	{
		if (const UFGFactoryConnectionComponent* Factory = Cast<UFGFactoryConnectionComponent>(Connection))
		{
			return Factory->IsConnected();
		}
		if (const UFGPipeConnectionComponentBase* Pipe = Cast<UFGPipeConnectionComponentBase>(Connection))
		{
			return Pipe->IsConnected();
		}
		if (const UFGRailroadTrackConnectionComponent* Track = Cast<UFGRailroadTrackConnectionComponent>(Connection))
		{
			return Track->IsConnected();
		}
		return true; // Power and anything else: not ours
	}

	/** Fills in a port's kind, place and flow if this is a free connection a line can be built to. */
	bool DescribePort(UFGConnectionComponent* Connection, FPort& Port)
	{
		if (!Connection || IsPortConnected(Connection))
		{
			return false;
		}
		Port.Connection = Connection;
		if (const UFGFactoryConnectionComponent* Factory = Cast<UFGFactoryConnectionComponent>(Connection))
		{
			const EFactoryConnectionDirection Direction = Factory->GetDirection();
			if (Direction != EFactoryConnectionDirection::FCD_INPUT && Direction != EFactoryConnectionDirection::FCD_OUTPUT)
			{
				return false;
			}
			Port.Kind = EPortKind::Belt;
			Port.Location = Factory->GetConnectorLocation();
			Port.Normal = Factory->GetConnectorNormal();
			Port.bOutput = Direction == EFactoryConnectionDirection::FCD_OUTPUT;
			return true;
		}
		if (const UFGPipeConnectionComponentHyper* Hyper = Cast<UFGPipeConnectionComponentHyper>(Connection))
		{
			Port.Kind = EPortKind::Hypertube;
			Port.Location = Hyper->GetConnectorLocation();
			Port.Normal = Hyper->GetConnectorNormal();
			Port.bAnyFlow = true;
			return true;
		}
		if (const UFGPipeConnectionComponent* Pipe = Cast<UFGPipeConnectionComponent>(Connection))
		{
			const EPipeConnectionType Type = Pipe->GetPipeConnectionType();
			if (Type == EPipeConnectionType::PCT_SNAP_ONLY || Type == EPipeConnectionType::PCT_MAX)
			{
				return false;
			}
			Port.Kind = EPortKind::Pipe;
			Port.Location = Pipe->GetConnectorLocation();
			Port.Normal = Pipe->GetConnectorNormal();
			Port.bOutput = Type == EPipeConnectionType::PCT_PRODUCER;
			Port.bAnyFlow = Type == EPipeConnectionType::PCT_ANY;
			return true;
		}
		if (const UFGRailroadTrackConnectionComponent* Track = Cast<UFGRailroadTrackConnectionComponent>(Connection))
		{
			Port.Kind = EPortKind::Rail;
			Port.Location = Track->GetConnectorLocation();
			Port.Normal = Track->GetConnectorNormal();
			Port.bAnyFlow = true;
			return true;
		}
		return false;
	}
}
using namespace AtlasBeltDetail;

AFGCharacterPlayer* UAtlasSubsystem::BeltPlayer() const
{
	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	return Controller ? Cast<AFGCharacterPlayer>(Controller->GetPawn()) : nullptr;
}

bool UAtlasSubsystem::CanBuildBelts() const
{
	const AFGCharacterPlayer* Player = BeltPlayer();
	return bLayerEnabled && Player && Player->HasAuthority() && Player->GetBuildGun();
}

void UAtlasSubsystem::SetMapCursor(EView View, FVector2f Uv, float PixelsPerMetre)
{
	MapCursorUv[int32(View)] = Uv;
	MapCursorPixelsPerMetre[int32(View)] = PixelsPerMetre;
	MapPaintFrame[int32(View)] = GFrameCounter;
}

void UAtlasSubsystem::RebuildPorts()
{
	bPortsDirty = false;
	PortsBuiltSeconds = FPlatformTime::Seconds();
	AllPorts.Reset();
	AFGBuildableSubsystem* Buildables = AFGBuildableSubsystem::Get(GetWorld());
	if (!Buildables)
	{
		return;
	}
	int32 Counts[int32(EPortKind::Count)] = {};
	TArray<UFGConnectionComponent*> Connections;
	for (AFGBuildable* Buildable : Buildables->GetAllBuildablesRef())
	{
		if (!IsValid(Buildable) || Buildable->GetIsLightweightTemporary() || Buildable->ManagedByLightweightBuildableSubsystem())
		{
			continue;
		}
		Connections.Reset();
		Buildable->GetComponents(Connections);
		for (UFGConnectionComponent* Connection : Connections)
		{
			FPort Port;
			if (!DescribePort(Connection, Port))
			{
				continue;
			}
			Port.Owner = Buildable;
			Port.Uv = BeltToUv(Port.Location);
			Counts[int32(Port.Kind)]++;
			AllPorts.Add(Port);
		}
	}
	PortsFrame = MAX_uint64; // Filter again
	UE_LOG(LogFactoryAtlas, Display, TEXT("Ports: %d free (%d belt, %d pipe, %d hypertube, %d track) in %.2f ms"), AllPorts.Num(),
		Counts[0], Counts[1], Counts[2], Counts[3], (FPlatformTime::Seconds() - PortsBuiltSeconds) * 1000.0);
}

bool UAtlasSubsystem::IsPortShown(const FPort& Port)
{
	const UFGConnectionComponent* Connection = Port.Connection.Get();
	AFGBuildable* Owner = Port.Owner.Get();
	if (!Connection || !Owner || IsPortConnected(Connection))
	{
		return false;
	}
	// You can't drag from what the map isn't showing.
	const int32 Z = MetresToZ(float(Port.Location.Z / 100.0));
	if (Z < MetresToZ(FilterLow) || Z > MetresToZ(FilterHigh))
	{
		return false;
	}
	const int32 Type = TypeFor(Owner->GetClass());
	return !Types[Type].bHidden && !IsCategoryHidden(Types[Type].Category);
}

const TArray<FPort>& UAtlasSubsystem::GetPorts()
{
	if (!CanBuildBelts())
	{
		return NoPorts;
	}
	if (bPortsDirty && FPlatformTime::Seconds() - PortsBuiltSeconds >= PortsRefreshSeconds)
	{
		RebuildPorts();
	}
	// Filtered once a frame: connections fill up and filters change without any building being added.
	if (PortsFrame != GFrameCounter)
	{
		PortsFrame = GFrameCounter;
		Ports.Reset();
		for (const FPort& Port : AllPorts)
		{
			if (IsPortShown(Port))
			{
				Ports.Add(Port);
			}
		}
	}
	return Ports;
}

bool UAtlasSubsystem::IsTargetFor(const FPort& Candidate) const
{
	const FPort& Start = BeltDrag.Start;
	if (Candidate.Kind != Start.Kind || Candidate.Connection == Start.Connection)
	{
		return false;
	}
	// A track or tube can't loop back onto its own other end.
	if ((Start.Kind == EPortKind::Rail || Start.Kind == EPortKind::Hypertube) && Candidate.Owner == Start.Owner)
	{
		return false;
	}
	return Start.bAnyFlow || Candidate.bAnyFlow || Candidate.bOutput != Start.bOutput;
}

int32& UAtlasSubsystem::TierFromTop(EPortKind Kind)
{
	// Hypertubes and railway have a single recipe each, so they share the pipe slot harmlessly.
	return Kind == EPortKind::Belt ? Settings.BeltTierFromTop : Settings.PipeTierFromTop;
}

int32 UAtlasSubsystem::GetPortUnderCursor(EView View)
{
	const float PixelsPerMetre = MapCursorPixelsPerMetre[int32(View)];
	if (PixelsPerMetre < PortMinPixelsPerMetre)
	{
		return INDEX_NONE;
	}
	const TArray<FPort>& Shown = GetPorts();
	const FVector2f Cursor = MapCursorUv[int32(View)];
	const float ReachUv = PortReachPixels / PixelsPerMetre / MapWidthMetres;
	float Best = ReachUv * ReachUv;
	int32 Found = INDEX_NONE;
	for (int32 i = 0; i < Shown.Num(); i++)
	{
		if (BeltDrag.bActive && !IsTargetFor(Shown[i]))
		{
			continue;
		}
		const float Distance = FVector2f::DistSquared(Shown[i].Uv, Cursor);
		if (Distance <= Best)
		{
			Best = Distance;
			Found = i;
		}
	}
	return Found;
}

bool UAtlasSubsystem::PressBeltPort(EView View, const FVector2f& ScreenPosition)
{
	if (BeltDrag.bPressed || !CanBuildBelts() || IsPointerOverPanel(View) || !IsPointOnMap(ScreenPosition, View))
	{
		return false;
	}
	const int32 Index = GetPortUnderCursor(View);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	BeltDrag = FBeltDrag();
	BeltDrag.bPressed = true;
	BeltDrag.View = View;
	BeltDrag.Start = GetPorts()[Index];
	BeltDrag.PressScreen = ScreenPosition;
	return true;
}

void UAtlasSubsystem::MoveBeltPress(const FVector2f& ScreenPosition)
{
	if (!BeltDrag.bPressed || BeltDrag.bActive || FVector2f::Distance(ScreenPosition, BeltDrag.PressScreen) < DragSlopPixels)
	{
		return;
	}
	BeltDrag.bActive = true;
	ClearHover();
	SpawnBeltHologram();
	if (!BeltTicker.IsValid())
	{
		BeltTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float) { return TickBelt(); }));
	}
}

bool UAtlasSubsystem::ReleaseBeltPress()
{
	if (!BeltDrag.bPressed)
	{
		return false;
	}
	const bool bClick = !BeltDrag.bActive;
	if (!bClick && (BeltDrag.bHasTarget || BeltDrag.bHasGround) && BeltDrag.bReady && BeltDrag.bValid)
	{
		BuildBelt();
	}
	CancelBeltDrag();
	return bClick;
}

void UAtlasSubsystem::CancelBeltDrag()
{
	if (BeltTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(BeltTicker);
		BeltTicker.Reset();
	}
	DestroyBeltHologram();
	BeltDrag = FBeltDrag();
}

void UAtlasSubsystem::DestroyBeltHologram()
{
	if (AFGHologram* Live = BeltHologram.Get())
	{
		Live->Destroy();
	}
	BeltHologram.Reset();
}

TSubclassOf<UFGRecipe> UAtlasSubsystem::PickBeltRecipe(FText& OutName) const
{
	const EPortKind Kind = BeltDrag.Start.Kind;
	const AFGBuildable* StartOwner = BeltDrag.Start.Owner.Get();
	const TArray<FLineRecipe> Lines = UnlockedRecipes(GetWorld(), Kind, StartOwner ? StartOwner->GetClass() : nullptr);
	if (Lines.IsEmpty())
	{
		return nullptr;
	}
	const int32 Tier = Kind == EPortKind::Belt ? Settings.BeltTierFromTop : Kind == EPortKind::Pipe ? Settings.PipeTierFromTop : 0;
	const TSubclassOf<UFGRecipe> Recipe = Lines[FMath::Clamp(Tier, 0, Lines.Num() - 1)].Recipe;
	OutName = UFGRecipe::GetRecipeName(Recipe);
	return Recipe;
}

bool UAtlasSubsystem::SpawnBeltHologram()
{
	DestroyBeltHologram();
	BeltDrag.bReady = false;
	BeltDrag.bValid = false;
	BeltDrag.Points.Reset();
	BeltStage = 0;
	BeltWaitFrames = SettleFrames;
	BeltSettleFrames = SettleFrames;

	AFGCharacterPlayer* Player = BeltPlayer();
	const TSubclassOf<UFGRecipe> Recipe = PickBeltRecipe(BeltDrag.BeltName);
	if (!Player || !Player->GetBuildGun() || !Recipe)
	{
		BeltDrag.Reason = Recipe ? FText::FromString(TEXT("Can't build right now."))
			: FText::FromString(FString::Printf(TEXT("No %s unlocked yet."), KindName(BeltDrag.Start.Kind)));
		BeltDrag.bReady = true;
		BeltStage = -1;
		return false;
	}

	AFGHologram* Live = AFGHologram::SpawnHologramFromRecipe(Recipe, Player->GetBuildGun(), BeltDrag.Start.Location, Player);
	if (!Live || !Live->IsA<AFGSplineHologram>())
	{
		if (Live)
		{
			Live->Destroy();
		}
		BeltDrag.Reason = FText::FromString(FString::Printf(TEXT("The game made no %s hologram."), KindNoun(BeltDrag.Start.Kind)));
		BeltDrag.bReady = true;
		BeltStage = -1;
		return false;
	}
	BeltHologram = Live;

	// Belts, pipes and tracks each have their own build modes, so a remembered one only carries over within a kind.
	if (BeltModeKind != uint8(BeltDrag.Start.Kind))
	{
		BeltModeKind = uint8(BeltDrag.Start.Kind);
		BeltModeIndex = 0;
	}

	// Only the hologram's own build modes: the build gun lists its general ones too.
	TArray<TSubclassOf<UFGBuildGunModeDescriptor>> Modes;
	Live->GetSupportedBuildModes(Modes);
	BeltModes.Reset();
	for (const TSubclassOf<UFGBuildGunModeDescriptor>& Mode : Modes)
	{
		if (*Mode && Mode->IsChildOf(UFGHologramBuildModeDescriptor::StaticClass()))
		{
			BeltModes.Add(Mode);
		}
	}
	if (BeltModes.IsValidIndex(BeltModeIndex))
	{
		Live->SetBuildModeOverride(TSubclassOf<UFGHologramBuildModeDescriptor>(BeltModes[BeltModeIndex].Get()));
		BeltDrag.ModeName = UFGBuildGunModeDescriptor::GetDisplayName(BeltModes[BeltModeIndex]);
	}
	else
	{
		BeltModeIndex = 0;
		BeltDrag.ModeName = BeltModes.IsEmpty() ? FText::GetEmpty() : UFGBuildGunModeDescriptor::GetDisplayName(BeltModes[0]);
	}
	return true;
}

bool UAtlasSubsystem::PlaceBeltEnd(const FPort& Port)
{
	AFGHologram* Live = BeltHologram.Get();
	if (!Live || !Port.Owner.IsValid())
	{
		return false;
	}
	// As the build gun does every frame: disqualifiers only build up until reset, and a hidden hologram counts
	// as aimed nowhere.
	Live->ResetConstructDisqualifiers();
	Live->SetActorHiddenInGame(false);
	const FHitResult Hit = HitAtPort(Port, Live, BeltHitVariant);
	if (!Live->IsValidHitResult(Hit))
	{
		UE_LOG(LogFactoryAtlas, Verbose, TEXT("Belt: the hologram refuses the hit on %s (%s)"), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()));
	}
	Live->UpdateHologramPlacement(Hit);
	return true;
}

void UAtlasSubsystem::ValidateBelt()
{
	AFGHologram* Live = BeltHologram.Get();
	AFGSplineHologram* Spline = Cast<AFGSplineHologram>(Live);
	AFGCharacterPlayer* Player = BeltPlayer();
	if (!Spline || !Player || !Player->GetBuildGun())
	{
		return;
	}
	// Validation refuses anything hidden, and children such as the end pole stay hidden unless shown.
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

	BeltDrag.bReady = true;
	BeltDrag.bClipping = false;
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
	// With clipping allowed, only what isn't a clearance overlap can refuse the line.
	TArray<TSubclassOf<UFGConstructDisqualifier>> Blocking;
	for (const TSubclassOf<UFGConstructDisqualifier>& Disqualifier : Disqualifiers)
	{
		if (!Settings.bAllowClipping || !IsClippingDisqualifier(Disqualifier))
		{
			Blocking.Add(Disqualifier);
		}
	}
	if (Live->IsUpgrade())
	{
		BeltDrag.bValid = false;
		BeltDrag.Reason = FText::FromString(FString::Printf(TEXT("That would replace the existing %s. Press T for its type."), KindNoun(BeltDrag.Start.Kind)));
	}
	else if (BeltDrag.bHasTarget && !IsSnappedTo(Spline, 1, BeltDrag.Target))
	{
		// Somewhere near the port isn't on it: the line would end loose.
		BeltDrag.bValid = false;
		BeltDrag.Reason = FText::FromString(TEXT("Can't connect to that port."));
	}
	else if (Live->CanConstruct())
	{
		BeltDrag.bValid = true;
		BeltDrag.Reason = FText::GetEmpty();
	}
	else if (Blocking.IsEmpty() && !Disqualifiers.IsEmpty())
	{
		// Refused only for clipping into something, which the player has allowed.
		BeltDrag.bValid = true;
		BeltDrag.bClipping = true;
		BeltDrag.Reason = FText::GetEmpty();
	}
	else
	{
		BeltDrag.bValid = false;
		BeltDrag.Reason = Blocking.IsEmpty()
			? FText::FromString(FString::Printf(TEXT("Can't build this %s."), KindNoun(BeltDrag.Start.Kind)))
			: UFGConstructDisqualifier::GetDisqualifyingText(Blocking[0]);
	}

	ReadBeltPreview();

	static double LastLog = 0.0;
	if (!BeltDrag.bValid && BeltDrag.bHasTarget && FPlatformTime::Seconds() - LastLog > 1.0)
	{
		LastLog = FPlatformTime::Seconds();
		const UObject* Snapped = SnappedConnection(Spline, 1);
		UE_LOG(LogFactoryAtlas, Display, TEXT("Belt: refused onto %s on %s (%s): end sits %.0f cm from it at %s, snapped to %s, build step %d"),
			*GetNameSafe(BeltDrag.Target.Connection.Get()), *GetNameSafe(BeltDrag.Target.Owner.Get()), *BeltDrag.Reason.ToString(),
			FVector::Dist(SplineEnd(Spline, 1), BeltDrag.Target.Location), *SplineEnd(Spline, 1).ToCompactString(), *GetNameSafe(Snapped),
			int32(Spline->GetCurrentBuildStep()));
	}
}

void UAtlasSubsystem::ReadBeltPreview()
{
	// The preview follows the hologram's real spline, so it bends the way the chosen build mode does.
	BeltDrag.Points.Reset();
	const AFGHologram* Live = BeltHologram.Get();
	if (const USplineComponent* Curve = Live ? Live->FindComponentByClass<USplineComponent>() : nullptr)
	{
		const float Length = Curve->GetSplineLength();
		const int32 Count = FMath::Clamp(FMath::CeilToInt32(Length / PreviewStepCm), 1, PreviewMaxPoints);
		for (int32 i = 0; i <= Count; i++)
		{
			BeltDrag.Points.Add(Curve->GetLocationAtDistanceAlongSpline(Length * i / Count, ESplineCoordinateSpace::World));
		}
	}
}

void UAtlasSubsystem::PreviewBeltAtCursor()
{
	// No port under the cursor: aim the hologram's end where the build gun would land looking straight down at the
	// cursor, so the preview shows the real route. Nothing is validated; releasing here cancels.
	AFGHologram* Live = BeltHologram.Get();
	UWorld* World = GetWorld();
	ECollisionChannel Channel;
	if (!Live || !World || !FindBuildGunChannel(Channel))
	{
		BeltDrag.Points.Reset();
		return;
	}
	const FVector2f Uv = MapCursorUv[int32(BeltDrag.View)];
	const FVector Ground(MapWestCm + double(Uv.X) * MapWidthCm, MapNorthCm + double(Uv.Y) * MapHeightCm, BeltDrag.Start.Location.Z);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AtlasBeltCursor), true);
	Params.AddIgnoredActor(Live);
	for (AFGHologram* Child : Live->GetHologramChildren())
	{
		Params.AddIgnoredActor(Child);
	}
	FHitResult Hit;
	// Only within the map's height filter, and no more than 20 m above the start, so the end lands on the floor
	// being worked on rather than a roof above it.
	const double Top = FMath::Min(double(FilterHigh) * 100.0 + 50.0, BeltDrag.Start.Location.Z + 2000.0);
	const double Bottom = double(FilterLow) * 100.0 - 50.0;
	if (!World->LineTraceSingleByChannel(Hit, FVector(Ground.X, Ground.Y, Top), FVector(Ground.X, Ground.Y, Bottom), Channel, Params)
		|| !Live->IsValidHitResult(Hit))
	{
		// Unloaded or open air: just the straight line to the cursor, and nothing to end on.
		BeltDrag.bHasGround = false;
		BeltDrag.bReady = true;
		BeltDrag.bValid = false;
		BeltDrag.bClipping = false;
		BeltDrag.Reason = FText::FromString(TEXT("Nothing to build on here."));
		BeltDrag.Points.Reset();
		return;
	}
	ResolveForPlacement(Hit);
	Live->ResetConstructDisqualifiers();
	Live->SetActorHiddenInGame(false);
	Live->UpdateHologramPlacement(Hit);
	BeltDrag.bHasGround = true;
	// Ending on the ground is a build too, with the end pole or support the game adds, so it gets the game's verdict.
	ValidateBelt();
	static double LastLog = 0.0;
	if (!BeltDrag.bValid && FPlatformTime::Seconds() - LastLog > 1.0)
	{
		LastLog = FPlatformTime::Seconds();
		UE_LOG(LogFactoryAtlas, Display, TEXT("Belt: ground end refused (%s): hit %s at %s, start %s, end sits at %s"), *BeltDrag.Reason.ToString(),
			*GetNameSafe(Hit.GetActor()), *Hit.ImpactPoint.ToCompactString(), *BeltDrag.Start.Location.ToCompactString(),
			*SplineEnd(Cast<AFGSplineHologram>(Live), 1).ToCompactString());
	}
}

bool UAtlasSubsystem::TickBelt()
{
	if (!BeltDrag.bActive)
	{
		BeltTicker.Reset();
		return false;
	}

	// The target follows the cursor every frame; a new one needs a moment before its verdict counts.
	const int32 Index = GetPortUnderCursor(BeltDrag.View);
	const bool bHasTarget = Index != INDEX_NONE;
	const TArray<FPort>& Shown = GetPorts();
	const bool bChanged = bHasTarget != BeltDrag.bHasTarget || (bHasTarget && Shown[Index].Connection != BeltDrag.Target.Connection);
	BeltDrag.bHasTarget = bHasTarget;
	if (bHasTarget)
	{
		BeltDrag.Target = Shown[Index];
		BeltDrag.bHasGround = false;
	}
	if (bChanged)
	{
		BeltSettleFrames = SettleFrames;
		BeltDrag.bReady = false;
		if (bHasTarget)
		{
			BeltDrag.Points.Reset(); // Leaving a port keeps the free preview until the next frame replaces it
		}
	}

	AFGSplineHologram* Spline = Cast<AFGSplineHologram>(BeltHologram.Get());
	if (BeltStage < 0 || !Spline)
	{
		return true; // Failed to start; the reason is showing, and release cancels
	}
	if (--BeltWaitFrames > 0)
	{
		return true;
	}
	BeltWaitFrames = 1;

	switch (BeltStage)
	{
	case 0:
		// The start, on the port the drag began from.
		PlaceBeltEnd(BeltDrag.Start);
		BeltStage = 1;
		BeltWaitFrames = SettleFrames;
		return true;
	case 1:
		if (Spline->IsUpgrade())
		{
			// Aimed at a line of another type, the game swaps that whole line instead of starting a new one.
			UE_LOG(LogFactoryAtlas, Display, TEXT("Belt: the hologram would replace %s rather than start on it"), *GetNameSafe(Spline->GetUpgradedActor()));
			BeltDrag.Reason = FText::FromString(FString::Printf(TEXT("That would replace the existing %s. Press T for its type."), KindNoun(BeltDrag.Start.Kind)));
			BeltDrag.bReady = true;
			BeltDrag.bValid = false;
			BeltStage = -1;
			return true;
		}
		if (!IsSnappedTo(Spline, 0, BeltDrag.Start))
		{
			const FHitResult Hit = HitAtPort(BeltDrag.Start, Spline, BeltHitVariant);
			const UObject* Snapped = SnappedConnection(Spline, 0);
			UE_LOG(LogFactoryAtlas, Warning, TEXT("Belt: aim %d didn't snap to the start port %s on %s (hit %s / %s at %s, valid %d, snapped to %s on %s, start sits %.0f cm from the port at %s, port %s)"), BeltHitVariant,
				*GetNameSafe(BeltDrag.Start.Connection.Get()), *GetNameSafe(BeltDrag.Start.Owner.Get()), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()),
				*Hit.ImpactPoint.ToCompactString(), Spline->IsValidHitResult(Hit) ? 1 : 0, *GetNameSafe(Snapped), *GetNameSafe(Snapped ? Snapped->GetOuter() : nullptr),
				FVector::Dist(SplineEnd(Spline, 0), BeltDrag.Start.Location), *SplineEnd(Spline, 0).ToCompactString(), *BeltDrag.Start.Location.ToCompactString());
			if (BeltHitVariant + 1 < HitVariants)
			{
				// Try the next way of aiming at it.
				BeltHitVariant++;
				BeltStage = 0;
				return true;
			}
			BeltHitVariant = 0;
			BeltDrag.Reason = FText::FromString(FString::Printf(TEXT("Can't start a %s on that port."), KindNoun(BeltDrag.Start.Kind)));
			BeltDrag.bReady = true;
			BeltDrag.bValid = false;
			BeltStage = -1;
			return true;
		}
		UE_LOG(LogFactoryAtlas, Display, TEXT("Belt: aim %d snapped to the start port on %s"), BeltHitVariant, *GetNameSafe(BeltDrag.Start.Owner.Get()));
		BeltStepTries = 0;
		Spline->DoMultiStepPlacement(false);
		BeltStage = 2;
		BeltWaitFrames = SettleFrames;
		return true;
	case 2:
		// A line from a port has no starting pole to adjust, but step past one if the game asks. Pipes may never
		// report the step done, so after a few tries carry on to the end regardless.
		if (Spline->GetCurrentBuildStep() < ESplineHologramBuildStep::SHBS_PlacePoleOrSnapEnding && BeltStepTries < 3)
		{
			BeltStepTries++;
			Spline->DoMultiStepPlacement(false);
			BeltWaitFrames = SettleFrames;
			return true;
		}
		if (Spline->GetCurrentBuildStep() < ESplineHologramBuildStep::SHBS_PlacePoleOrSnapEnding)
		{
			UE_LOG(LogFactoryAtlas, Display, TEXT("Belt: the %s stayed at build step %d after the start; going on to the end"),
				KindNoun(BeltDrag.Start.Kind), int32(Spline->GetCurrentBuildStep()));
		}
		BeltStage = 3;
		[[fallthrough]];
	default:
		if (!BeltDrag.bHasTarget)
		{
			PreviewBeltAtCursor();
			return true;
		}
		PlaceBeltEnd(BeltDrag.Target);
		if (BeltSettleFrames > 0)
		{
			BeltSettleFrames--;
			return true;
		}
		ValidateBelt();
		return true;
	}
}

void UAtlasSubsystem::BuildBelt()
{
	AFGHologram* Live = BeltHologram.Get();
	AFGCharacterPlayer* Player = BeltPlayer();
	AFGBuildableSubsystem* Buildables = AFGBuildableSubsystem::Get(GetWorld());
	if (!Live || !Player || !Player->GetBuildGun() || !Buildables)
	{
		return;
	}
	ValidateBelt();
	if (!BeltDrag.bValid)
	{
		UE_LOG(LogFactoryAtlas, Display, TEXT("Belt: release refused: %s"), *BeltDrag.Reason.ToString());
		ShowToast(BeltDrag.Reason, true);
		PlayClipboardSound(EClipboardSound::Error);
		return;
	}
	if (BeltDrag.bClipping)
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
	// The click that ends the line; the game says when it has all it needs. A line ending on the ground has one
	// more step, adjusting its end pole, which is accepted as placed rather than moved.
	bool bDone = false;
	for (int32 Step = 0; Step < 3 && !bDone; Step++)
	{
		bDone = Live->DoMultiStepPlacement(false);
	}
	if (!bDone)
	{
		UE_LOG(LogFactoryAtlas, Warning, TEXT("Belt: the %s wasn't finished after 3 steps (build step %d)"), KindNoun(BeltDrag.Start.Kind),
			Cast<AFGSplineHologram>(Live) ? int32(Cast<AFGSplineHologram>(Live)->GetCurrentBuildStep()) : -1);
		ShowToast(FText::FromString(FString::Printf(TEXT("The %s wasn't finished."), KindNoun(BeltDrag.Start.Kind))), true);
		PlayClipboardSound(EClipboardSound::Error);
		return;
	}

	const TArray<FItemAmount> Cost = Live->GetCost(true);
	TArray<AActor*> Children;
	AActor* Built = Live->Construct(Children, Buildables->GetNewNetConstructionID());
	if (!Built)
	{
		UE_LOG(LogFactoryAtlas, Warning, TEXT("Belt: Construct returned nothing"));
		ShowToast(FText::FromString(TEXT("The game built nothing.")), true);
		PlayClipboardSound(EClipboardSound::Error);
		return;
	}

	// Construct doesn't charge.
	if (!PayBuildCost(Cost))
	{
		UE_LOG(LogFactoryAtlas, Warning, TEXT("Belt: built %s but couldn't take its whole cost"), *Built->GetName());
	}
	UE_LOG(LogFactoryAtlas, Display, TEXT("Belt: built %s from %s to %s%s"), *Built->GetName(),
		*GetNameSafe(BeltDrag.Start.Owner.Get()), BeltDrag.bHasTarget ? *GetNameSafe(BeltDrag.Target.Owner.Get()) : TEXT("the ground"),
		BeltDrag.bClipping ? TEXT(", clipping") : TEXT(""));
	ShowToast(FText::Format(FText::FromString(TEXT("Built {0}.")), BeltDrag.BeltName));
	PlayClipboardSound(EClipboardSound::Paste);
	bPortsDirty = true;
	PortsBuiltSeconds = -1000.0;
}

bool UAtlasSubsystem::PayBuildCost(const TArray<FItemAmount>& Cost)
{
	AFGCharacterPlayer* Player = BeltPlayer();
	UFGInventoryComponent* Inventory = Player && Player->GetBuildGun() ? Player->GetBuildGun()->GetInventory() : nullptr;
	if (!Inventory || Inventory->GetNoBuildCost())
	{
		return true;
	}
	bool bPaid = true;
	AFGCentralStorageSubsystem* Depot = AFGCentralStorageSubsystem::Get(GetWorld());
	for (const FItemAmount& Item : Cost)
	{
		const int32 FromInventory = FMath::Min(Item.Amount, Inventory->GetNumItems(Item.ItemClass));
		if (FromInventory > 0)
		{
			Inventory->Remove(Item.ItemClass, FromInventory);
		}
		const int32 Remaining = Item.Amount - FromInventory;
		const int32 FromDepot = Remaining > 0 && Depot ? Depot->TryRemoveItemsFromCentralStorage(Item.ItemClass, Remaining) : 0;
		bPaid &= FromDepot >= Remaining;
	}
	return bPaid;
}

void UAtlasSubsystem::CycleBeltBuildMode()
{
	AFGHologram* Live = BeltHologram.Get();
	if (!Live || BeltModes.IsEmpty())
	{
		return;
	}
	BeltModeIndex = (BeltModeIndex + 1) % BeltModes.Num();
	Live->SetBuildModeOverride(TSubclassOf<UFGHologramBuildModeDescriptor>(BeltModes[BeltModeIndex].Get()));
	BeltDrag.ModeName = UFGBuildGunModeDescriptor::GetDisplayName(BeltModes[BeltModeIndex]);
	BeltSettleFrames = SettleFrames;
	BeltDrag.bReady = false;
}

void UAtlasSubsystem::CycleBeltTier()
{
	const EPortKind Kind = BeltDrag.bPressed ? BeltDrag.Start.Kind : EPortKind::Belt;
	const AFGBuildable* StartOwner = BeltDrag.bPressed ? BeltDrag.Start.Owner.Get() : nullptr;
	const int32 Count = UnlockedRecipes(GetWorld(), Kind, StartOwner ? StartOwner->GetClass() : nullptr).Num();
	if (Count <= 1)
	{
		return;
	}
	int32& Tier = TierFromTop(Kind);
	Tier = (FMath::Clamp(Tier, 0, Count - 1) + 1) % Count;
	SaveSettings();
	// A different Mk is a different recipe, so a new hologram.
	if (BeltDrag.bActive)
	{
		SpawnBeltHologram();
	}
}
