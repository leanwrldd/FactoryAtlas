// Settings a building has beyond production: station names, load modes, power switches, flow limits, lights.

#include "AtlasSubsystem.h"

#include "Buildables/FGBuildableCircuitSwitch.h"
#include "Buildables/FGBuildableDockingStation.h"
#include "Buildables/FGBuildableDroneStation.h"
#include "Buildables/FGBuildableLightSource.h"
#include "Buildables/FGBuildablePipelinePump.h"
#include "Buildables/FGBuildableRailroadStation.h"
#include "Buildables/FGBuildableTrainPlatformCargo.h"
#include "FactoryAtlas.h"
#include "FGBuildingTagInterface.h"
#include "FGDroneStationInfo.h"
#include "FGDroneSubsystem.h"
#include "FGGameState.h"
#include "FGTrainStationIdentifier.h"
#include "WheeledVehicles/FGDockingStationIdentifier.h"

using namespace Atlas;

// Named rather than anonymous: the module builds as a unity file, where anonymous helpers from other files are visible.
namespace AtlasControls
{
	/** The light color slots live in a protected member of the game state, so they are read by name. */
	TArray<FLinearColor> LightColors(UWorld* World)
	{
		AFGGameState* GameState = World ? World->GetGameState<AFGGameState>() : nullptr;
		const FArrayProperty* Property = FindFProperty<FArrayProperty>(AFGGameState::StaticClass(), TEXT("mBuildableLightColorSlots"));
		const FStructProperty* Inner = Property ? CastField<FStructProperty>(Property->Inner) : nullptr;
		if (!GameState || !Inner || Inner->Struct != TBaseStructure<FLinearColor>::Get())
		{
			return {};
		}
		return *Property->ContainerPtrToValuePtr<TArray<FLinearColor>>(GameState);
	}

	/** Whatever carries this building's name: a station identifier, a drone port's info, or the building itself. */
	UObject* TagOwner(AFGBuildable* Building)
	{
		if (AFGBuildableDroneStation* Drone = Cast<AFGBuildableDroneStation>(Building))
		{
			return Drone->GetInfo();
		}
		return Building && Building->GetClass()->ImplementsInterface(UFGBuildingTagInterface::StaticClass()) ? Building : nullptr;
	}
}

using namespace AtlasControls;

FControls UAtlasSubsystem::GetControls() const
{
	FControls Controls;
	AFGBuildable* Building = bSelectedLightweight ? nullptr : Selected.Get();
	if (!Building)
	{
		return Controls;
	}

	if (const AFGBuildableRailroadStation* Train = Cast<AFGBuildableRailroadStation>(Building))
	{
		if (const AFGTrainStationIdentifier* Identifier = Train->GetStationIdentifier())
		{
			Controls.bHasName = true;
			Controls.Name = Identifier->GetStationName();
		}
	}
	else if (const AFGBuildableDockingStation* Truck = Cast<AFGBuildableDockingStation>(Building))
	{
		if (const AFGDockingStationIdentifier* Identifier = Truck->GetStationIdentifier())
		{
			Controls.bHasName = true;
			Controls.Name = Identifier->GetStationName();
		}
	}
	else if (UObject* Tagged = TagOwner(Building))
	{
		Controls.bHasName = true;
		Controls.Name = FText::FromString(IFGBuildingTagInterface::Execute_GetBuildingTag(Tagged));
	}

	if (const AFGBuildableTrainPlatformCargo* Platform = Cast<AFGBuildableTrainPlatformCargo>(Building))
	{
		Controls.bHasLoadMode = true;
		Controls.bLoadMode = Platform->GetIsInLoadMode();
	}
	else if (const AFGBuildableDockingStation* Truck = Cast<AFGBuildableDockingStation>(Building))
	{
		Controls.bHasLoadMode = true;
		Controls.bLoadMode = Truck->GetIsInLoadMode();
	}

	if (const AFGBuildableCircuitSwitch* Switch = Cast<AFGBuildableCircuitSwitch>(Building))
	{
		Controls.bHasSwitch = true;
		Controls.bSwitchOn = Switch->IsSwitchOn();
	}

	if (const AFGBuildablePipelinePump* Pump = Cast<AFGBuildablePipelinePump>(Building); Pump && Pump->GetDefaultFlowLimit() > 0.f)
	{
		Controls.bHasFlowLimit = true;
		Controls.FlowLimit = Pump->GetUserFlowLimit();
		Controls.MaxFlow = Pump->GetDefaultFlowLimit();
	}

	if (const AFGBuildableLightSource* Light = Cast<AFGBuildableLightSource>(Building))
	{
		const FLightSourceControlData Data = Light->GetLightControlData();
		Controls.bHasLight = true;
		Controls.bLightOn = Light->IsLightEnabled();
		Controls.bNightOnly = Data.IsTimeOfDayAware;
		Controls.Intensity = Data.Intensity;
		Controls.LightColorSlot = Data.ColorSlotIndex;
		Controls.LightColors = LightColors(GetWorld());
	}
	return Controls;
}

namespace AtlasControls
{
	/** The selected actor, if this game may change it; otherwise the reason goes to the panel. */
	AFGBuildable* Editable(UWorld* World, const TWeakObjectPtr<AFGBuildable>& Selected, bool bLightweight, FText& OutError)
	{
		AFGBuildable* Building = bLightweight ? nullptr : Selected.Get();
		if (Building && World && World->GetNetMode() == NM_Client)
		{
			OutError = FText::FromString(TEXT("Only the host can change buildings."));
			return nullptr;
		}
		return Building;
	}
}

void UAtlasSubsystem::SetSelectedName(const FText& Name)
{
	AFGBuildable* Building = Editable(GetWorld(), Selected, bSelectedLightweight, CustomizeError);
	if (!Building)
	{
		return;
	}
	const FString Trimmed = Name.ToString().TrimStartAndEnd();
	if (AFGBuildableRailroadStation* Train = Cast<AFGBuildableRailroadStation>(Building))
	{
		// An empty name would leave a station nobody can find in the timetable.
		if (AFGTrainStationIdentifier* Identifier = Train->GetStationIdentifier(); Identifier && !Trimmed.IsEmpty())
		{
			Identifier->SetStationName(FText::FromString(Trimmed));
		}
	}
	else if (AFGBuildableDockingStation* Truck = Cast<AFGBuildableDockingStation>(Building))
	{
		if (AFGDockingStationIdentifier* Identifier = Truck->GetStationIdentifier(); Identifier && !Trimmed.IsEmpty())
		{
			Identifier->SetStationName(FText::FromString(Trimmed));
		}
	}
	else if (AFGBuildableDroneStation* Drone = Cast<AFGBuildableDroneStation>(Building))
	{
		// Through the drone subsystem, which is what the port's own screen uses, so the name list stays in step.
		AFGDroneSubsystem* Drones = AFGDroneSubsystem::Get(GetWorld());
		if (Drones && Drone->GetInfo() && !Trimmed.IsEmpty())
		{
			Drones->SetStationName(Drone->GetInfo(), Trimmed);
		}
	}
	else if (UObject* Tagged = TagOwner(Building))
	{
		// For switches and the like a tag is optional; an empty one hides it.
		IFGBuildingTagInterface::Execute_SetBuildingTag(Tagged, Trimmed);
		IFGBuildingTagInterface::Execute_SetHasBuildingTag(Tagged, !Trimmed.IsEmpty());
	}
	CustomizeError = FText::GetEmpty();
}

void UAtlasSubsystem::SetSelectedLoadMode(bool bLoad)
{
	AFGBuildable* Building = Editable(GetWorld(), Selected, bSelectedLightweight, CustomizeError);
	if (AFGBuildableTrainPlatformCargo* Platform = Cast<AFGBuildableTrainPlatformCargo>(Building))
	{
		Platform->SetIsInLoadMode(bLoad);
	}
	else if (AFGBuildableDockingStation* Truck = Cast<AFGBuildableDockingStation>(Building))
	{
		Truck->SetIsInLoadMode(bLoad);
	}
}

void UAtlasSubsystem::SetSelectedSwitch(bool bOn)
{
	if (AFGBuildableCircuitSwitch* Switch = Cast<AFGBuildableCircuitSwitch>(Editable(GetWorld(), Selected, bSelectedLightweight, CustomizeError)))
	{
		Switch->SetSwitchOn(bOn);
	}
}

void UAtlasSubsystem::SetSelectedFlowLimit(float Limit)
{
	if (AFGBuildablePipelinePump* Pump = Cast<AFGBuildablePipelinePump>(Editable(GetWorld(), Selected, bSelectedLightweight, CustomizeError)))
	{
		// -1 is the game's "no limit", which is also what its own slider sends at the top.
		Pump->SetUserFlowLimit(Limit < 0.f || Limit >= Pump->GetDefaultFlowLimit() ? -1.f : FMath::Max(Limit, 0.f));
	}
}

void UAtlasSubsystem::SetSelectedLight(bool bOn, bool bNightOnly, float Intensity, int32 ColorSlot)
{
	AFGBuildableLightSource* Light = Cast<AFGBuildableLightSource>(Editable(GetWorld(), Selected, bSelectedLightweight, CustomizeError));
	if (!Light)
	{
		return;
	}
	if (Light->IsLightEnabled() != bOn)
	{
		Light->SetLightEnabled(bOn);
	}
	FLightSourceControlData Data = Light->GetLightControlData();
	const int32 Slots = LightColors(GetWorld()).Num();
	FLightSourceControlData Wanted = Data;
	Wanted.IsTimeOfDayAware = bNightOnly;
	Wanted.Intensity = FMath::Clamp(Intensity, 0.f, 1.f);
	Wanted.ColorSlotIndex = Slots > 0 ? FMath::Clamp(ColorSlot, 0, Slots - 1) : Data.ColorSlotIndex;
	if (Wanted.IsTimeOfDayAware != Data.IsTimeOfDayAware || Wanted.Intensity != Data.Intensity || Wanted.ColorSlotIndex != Data.ColorSlotIndex)
	{
		Light->SetLightControlData(Wanted);
	}
}
