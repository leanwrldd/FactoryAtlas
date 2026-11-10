#include "AtlasSubsystem.h"

#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableFactory.h"
#include "Buildables/FGBuildableJumppad.h"
#include "Buildables/FGBuildablePipelinePump.h"
#include "Buildables/FGBuildablePowerStorage.h"
#include "FactoryAtlas.h"
#include "FGBuildableSubsystem.h"
#include "HAL/IConsoleManager.h"

using namespace Atlas;

/**
 * Status lights. Each machine with a light in the game gets one record in a second grid, a copy of its building
 * record with the paint bits holding its status code. The game raises an event when a machine's status changes;
 * the handler only stores the new code, and UpdateStatusLights applies what changed a few times a second, so a
 * cell's GPU buffer is uploaded again only when a machine in it changed.
 */
namespace AtlasStatusDetail
{
	constexpr double ApplySeconds = 0.25;

	FAutoConsoleCommand CmdStatusDump(TEXT("atlas.StatusDump"),
		TEXT("Logs every machine's production status by class, and whether the status lights agree."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (UAtlasSubsystem* Atlas = UAtlasSubsystem::GetActive())
			{
				Atlas->DumpStatuses();
			}
		}));

	uint8 CodeOf(EProductionStatus Status)
	{
		switch (Status)
		{
		case EProductionStatus::IS_PRODUCING: return uint8(EStatusLight::Producing);
		case EProductionStatus::IS_PRODUCING_WITH_CRYSTAL: return uint8(EStatusLight::Overclocked);
		case EProductionStatus::IS_STANDBY: return uint8(EStatusLight::Standby);
		case EProductionStatus::IS_ERROR: return uint8(EStatusLight::Error);
		default: return uint8(EStatusLight::None);
		}
	}

	/**
	 * The status as the machine's own light shows it, through the reflected function rather than the virtual
	 * call, so a game patch that adds virtual functions can't make this call the wrong one.
	 */
	EProductionStatus ReadStatus(AFGBuildable* Buildable)
	{
		static UFunction* Function = AFGBuildableFactory::StaticClass()->FindFunctionByName(TEXT("GetProductionIndicatorStatus"));
		if (!Function || !Buildable)
		{
			return EProductionStatus::IS_NONE;
		}
		const FProperty* Return = Function->GetReturnProperty();
		uint8* Params = static_cast<uint8*>(FMemory_Alloca(FMath::Max<int32>(Function->ParmsSize, 1)));
		FMemory::Memzero(Params, Function->ParmsSize);
		Buildable->ProcessEvent(Function, Params);
		return Return ? EProductionStatus(*Return->ContainerPtrToValuePtr<uint8>(Params)) : EProductionStatus::IS_NONE;
	}
}
using namespace AtlasStatusDetail;

bool UAtlasSubsystem::IsStatusEventSafe()
{
	if (StatusSafe >= 0)
	{
		return StatusSafe == 1;
	}

	// The status event is a plain member of AFGBuildable, reached at the offset these headers were built with.
	// A game patch that moved AFGBuildable's members would put it somewhere else, so the reflected properties
	// on each side of it must be exactly where the headers say before anything is bound to it.
	const UClass* Class = AFGBuildable::StaticClass();
	const FProperty* Before = FindFProperty<FProperty>(Class, TEXT("mDecoratorClass"));
	const FProperty* After = FindFProperty<FProperty>(Class, TEXT("mContainsComponents"));
	const bool bBefore = Before && Before->GetOffset_ForInternal() == int32(STRUCT_OFFSET(AFGBuildable, mDecoratorClass));
	const bool bAfter = After && After->GetOffset_ForInternal() == int32(STRUCT_OFFSET(AFGBuildable, mContainsComponents));
	const bool bFunction = AFGBuildableFactory::StaticClass()->FindFunctionByName(TEXT("GetProductionIndicatorStatus")) != nullptr;
	StatusSafe = (bBefore && bAfter && bFunction) ? 1 : 0;
	if (StatusSafe == 0)
	{
		UE_LOG(LogFactoryAtlas, Warning, TEXT("Status lights are off: this game version's AFGBuildable layout differs from the one Atlas was built for (before %d, after %d, function %d)"),
			bBefore ? 1 : 0, bAfter ? 1 : 0, bFunction ? 1 : 0);
	}
	return StatusSafe == 1;
}

bool UAtlasSubsystem::HasStatusLight(AFGBuildable* Buildable)
{
	if (!Buildable || !Buildable->IsA<AFGBuildableFactory>())
	{
		return false;
	}
	// These override the game's indicator but sit in categories that otherwise have no light.
	if (Buildable->IsA<AFGBuildablePowerStorage>() || Buildable->IsA<AFGBuildablePipelinePump>() || Buildable->IsA<AFGBuildableJumppad>())
	{
		return true;
	}
	switch (Types[TypeFor(Buildable->GetClass())].Category)
	{
	case ECategory::Production:
	case ECategory::Extraction:
	case ECategory::Generators:
	case ECategory::Stations:
		return true;
	default:
		return false;
	}
}

void UAtlasSubsystem::SetStatusRecord(const FVector4f& Record, uint8 Code, bool bAdd)
{
	const int32 CellIndex = CellOf(Record);
	TArray<FVector4f>& Cell = StatusGrid->Cells[CellIndex];
	const int32 Found = Cell.IndexOfByPredicate([&Record](const FVector4f& Other) { return SameBuilding(Record, Other); });
	if (bAdd && Code != 0)
	{
		const FVector4f Lit = WithPaint(Record, Code);
		if (Found == INDEX_NONE)
		{
			Cell.Add(Lit);
		}
		else if (Cell[Found] == Lit)
		{
			return;
		}
		else
		{
			Cell[Found] = Lit;
		}
	}
	else if (Found != INDEX_NONE)
	{
		Cell.RemoveAtSwap(Found, EAllowShrinking::No);
	}
	else
	{
		return;
	}
	StatusGrid->Versions[CellIndex]++;
}

void UAtlasSubsystem::AddStatusLight(AFGBuildable* Buildable)
{
	if (!HasStatusLight(Buildable) || StatusEntries.Contains(Buildable))
	{
		return;
	}
	FVector4f Record;
	if (!RecordOf(Buildable, Record))
	{
		return;
	}

	FStatusEntry& Entry = StatusEntries.Add(Buildable);
	Entry.Record = WithPaint(Record, 0);
	Entry.Shown = CodeOf(ReadStatus(Buildable));
	Entry.Wanted = MakeShared<std::atomic<uint8>>(Entry.Shown);
	// Only atomics are touched here: the event may come from the factory tick, off the game thread.
	Entry.Handle = Buildable->mOnProductionStatusChanged.AddLambda(
		[Wanted = Entry.Wanted, Changed = StatusChanged](EProductionStatus Status)
		{
			Wanted->store(CodeOf(Status), std::memory_order_relaxed);
			Changed->store(true, std::memory_order_release);
		});
	SetStatusRecord(Entry.Record, Entry.Shown, true);
}

void UAtlasSubsystem::RemoveStatusLight(AFGBuildable* Buildable)
{
	FStatusEntry Entry;
	if (!StatusEntries.RemoveAndCopyValue(Buildable, Entry))
	{
		return;
	}
	Buildable->mOnProductionStatusChanged.Remove(Entry.Handle);
	SetStatusRecord(Entry.Record, 0, false);
}

void UAtlasSubsystem::ClearStatusLights()
{
	for (TPair<TWeakObjectPtr<AFGBuildable>, FStatusEntry>& Pair : StatusEntries)
	{
		if (AFGBuildable* Buildable = Pair.Key.Get())
		{
			Buildable->mOnProductionStatusChanged.Remove(Pair.Value.Handle);
		}
	}
	StatusEntries.Reset();
	StatusGrid.Reset();
	bStatusActive = false;
}

void UAtlasSubsystem::RebuildStatusLights()
{
	const double Start = FPlatformTime::Seconds();
	// Each rebuild starts its versions far past the last one's, so a painter still holding buffers from an
	// earlier grid, even one from before the lights were last turned off, sees every cell as changed.
	static uint32 Generation = 0;
	Generation += 1u << 20;
	ClearStatusLights();
	StatusGrid = MakeShared<FGrid>();
	for (int32 i = 0; i < GridCells; i++)
	{
		StatusGrid->Versions[i] = Generation;
	}
	if (!StatusChanged)
	{
		StatusChanged = MakeShared<std::atomic<bool>>(false);
	}

	AFGBuildableSubsystem* Buildables = AFGBuildableSubsystem::Get(GetWorld());
	if (!Buildables)
	{
		return;
	}
	for (AFGBuildable* Buildable : Buildables->GetAllBuildablesRef())
	{
		if (IsValid(Buildable) && !Buildable->GetIsLightweightTemporary() && !Buildable->ManagedByLightweightBuildableSubsystem())
		{
			AddStatusLight(Buildable);
		}
	}
	bStatusActive = true;
	bStatusDirty = false;
	LastStatusApplySeconds = FPlatformTime::Seconds();
	UE_LOG(LogFactoryAtlas, Display, TEXT("Status lights: %d machines in %.2f ms"), StatusEntries.Num(), (FPlatformTime::Seconds() - Start) * 1000.0);
}

void UAtlasSubsystem::UpdateStatusLights()
{
	const bool bWanted = Settings.bStatusLights && bScanned;
	if (!bWanted)
	{
		if (bStatusActive)
		{
			ClearStatusLights();
			bStatusDirty = true;
		}
		return;
	}
	if (!IsStatusEventSafe())
	{
		return;
	}
	if (!bStatusActive || bStatusDirty)
	{
		RebuildStatusLights();
		return;
	}

	const double Now = FPlatformTime::Seconds();
	if (Now - LastStatusApplySeconds < ApplySeconds || !StatusChanged->exchange(false, std::memory_order_acquire))
	{
		return;
	}
	LastStatusApplySeconds = Now;
	for (TPair<TWeakObjectPtr<AFGBuildable>, FStatusEntry>& Pair : StatusEntries)
	{
		FStatusEntry& Entry = Pair.Value;
		const uint8 Wanted = Entry.Wanted->load(std::memory_order_relaxed);
		if (Wanted != Entry.Shown)
		{
			Entry.Shown = Wanted;
			SetStatusRecord(Entry.Record, Wanted, true);
		}
	}
}

EStatusLight UAtlasSubsystem::GetHoverStatus() const
{
	if (!bStatusActive || !StatusGrid || !Hover.bValid || Hover.bMarker)
	{
		return EStatusLight::None;
	}
	for (const FVector4f& Record : StatusGrid->Cells[CellOf(Hover.Source)])
	{
		if (SameBuilding(Record, Hover.Source))
		{
			return EStatusLight(FDecoded(Record).Paint);
		}
	}
	return EStatusLight::None;
}

void UAtlasSubsystem::DumpStatuses()
{
	AFGBuildableSubsystem* Buildables = AFGBuildableSubsystem::Get(GetWorld());
	if (!Buildables)
	{
		UE_LOG(LogFactoryAtlas, Display, TEXT("StatusDump: no buildable subsystem"));
		return;
	}

	static const TCHAR* const RawNames[] = { TEXT("none"), TEXT("producing"), TEXT("crystal"), TEXT("standby"), TEXT("error") };
	struct FTally
	{
		int32 Count[5] = {};
		int32 Paused[5] = {};
		bool bLight = false;
	};
	TMap<FString, FTally> ByClass;
	const FBoolProperty* PausedProperty = FindFProperty<FBoolProperty>(AFGBuildableFactory::StaticClass(), TEXT("mIsProductionPaused"));
	int32 Tracked = 0;
	int32 Mismatched = 0;
	for (AFGBuildable* Buildable : Buildables->GetAllBuildablesRef())
	{
		if (!IsValid(Buildable) || !Buildable->IsA<AFGBuildableFactory>())
		{
			continue;
		}
		const int32 Raw = FMath::Clamp(int32(ReadStatus(Buildable)), 0, 4);
		FTally& Tally = ByClass.FindOrAdd(Buildable->GetClass()->GetName());
		Tally.Count[Raw]++;
		Tally.bLight = HasStatusLight(Buildable);
		if (PausedProperty && PausedProperty->GetPropertyValue_InContainer(Buildable))
		{
			Tally.Paused[Raw]++;
		}
		if (const FStatusEntry* Entry = StatusEntries.Find(Buildable))
		{
			Tracked++;
			if (Entry->Shown != CodeOf(EProductionStatus(Raw)))
			{
				Mismatched++;
			}
		}
	}

	ByClass.KeySort([](const FString& A, const FString& B) { return A < B; });
	for (const TPair<FString, FTally>& Pair : ByClass)
	{
		FString Line;
		for (int32 i = 0; i < 5; i++)
		{
			if (Pair.Value.Count[i] > 0)
			{
				Line += FString::Printf(TEXT(" %s %d"), RawNames[i], Pair.Value.Count[i]);
				if (Pair.Value.Paused[i] > 0)
				{
					Line += FString::Printf(TEXT(" (%d paused)"), Pair.Value.Paused[i]);
				}
			}
		}
		UE_LOG(LogFactoryAtlas, Display, TEXT("StatusDump %s [%s]:%s"), *Pair.Key, Pair.Value.bLight ? TEXT("light") : TEXT("no light"), *Line);
	}
	UE_LOG(LogFactoryAtlas, Display, TEXT("StatusDump: lights %s, layout check %d, %d machines tracked, %d shown differently from the game"),
		bStatusActive ? TEXT("on") : TEXT("off"), int32(StatusSafe), Tracked, Mismatched);
}
