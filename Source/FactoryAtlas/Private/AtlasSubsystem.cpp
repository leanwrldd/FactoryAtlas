#include "AtlasSubsystem.h"

#include "Algo/Sort.h"
#include "Async/ParallelFor.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableConveyorAttachment.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildableDockingStation.h"
#include "Buildables/FGBuildableFoundation.h"
#include "Buildables/FGBuildableGenerator.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Buildables/FGBuildablePipeHyper.h"
#include "Buildables/FGBuildablePipeline.h"
#include "Buildables/FGBuildablePipelineAttachment.h"
#include "Buildables/FGBuildablePowerPole.h"
#include "Buildables/FGBuildableRailroadTrack.h"
#include "Buildables/FGBuildableResourceExtractorBase.h"
#include "Buildables/FGBuildableStair.h"
#include "Buildables/FGBuildableStorage.h"
#include "Buildables/FGBuildableTrainPlatform.h"
#include "Buildables/FGBuildableWalkway.h"
#include "Buildables/FGBuildableWall.h"
#include "Buildables/FGBuildableWire.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "FactoryAtlas.h"
#include "FGBlueprintFunctionLibrary.h"
#include "FGBuildableBeam.h"
#include "FGBuildablePillar.h"
#include "FGBuildableSubsystem.h"
#include "FGGameState.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGSplineBuildableInterface.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "InstanceData.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Tasks/Task.h"
#include "Components/SplineComponent.h"
#include "FGRadioactivitySubsystem.h"
#include "FGScannableSubsystem.h"
#include "EngineUtils.h"
#include "FGGasPillar.h"
#include "FGSporeFlower.h"
#include "Resources/FGResourceDescriptor.h"
#include "Resources/FGResourceNodeBase.h"
#include "Buildables/FGBuildableFactory.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGRecipe.h"
#include "ItemAmount.h"
#include "Resources/FGItemDescriptor.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "FGWorldScannableData.h"
#include "WheeledVehicles/FGVehiclePathNode.h"
#include "WheeledVehicles/FGVehiclePathSegment.h"

using namespace Atlas;

namespace
{
	TAutoConsoleVariable<int32> CVarStress(TEXT("atlas.Stress"), 0, TEXT("Extra fake buildings, to test GPU cost at scale."));

	constexpr int32 MaxQueuedOps = 200000;
	constexpr double PaintRefreshSeconds = 30.0;
	constexpr float MaxSegmentHalfCm = 19000.f;

	FString SettingsPath()
	{
		return FPaths::ProjectSavedDir() / TEXT("FactoryAtlas") / TEXT("Settings.txt");
	}

	FBox MeshBounds(UClass* Class, const AFGBuildable* Default)
	{
		FBox Box(ForceInit);
		const auto AddMesh = [&Box](const UStaticMesh* Mesh, const FTransform& Transform)
		{
			if (Mesh)
			{
				Box += Mesh->GetBoundingBox().TransformBy(Transform);
			}
		};
		const auto AddComponent = [&AddMesh](const UObject* Object)
		{
			if (const UStaticMeshComponent* Component = Cast<UStaticMeshComponent>(Object))
			{
				AddMesh(Component->GetStaticMesh(), Component->GetRelativeTransform());
			}
		};

		// Most buildings are drawn from this list rather than from components.
		if (const UAbstractInstanceDataObject* Instances = Default->GetLightweightInstanceData())
		{
			for (const FInstanceData& Instance : Instances->GetInstanceData())
			{
				AddMesh(Instance.StaticMesh, Instance.RelativeTransform);
			}
		}
		if (Box.IsValid)
		{
			return Box;
		}

		for (UClass* Current = Class; Current; Current = Current->GetSuperClass())
		{
			if (const UBlueprintGeneratedClass* Blueprint = Cast<UBlueprintGeneratedClass>(Current))
			{
				if (UInheritableComponentHandler* Inherited = Blueprint->InheritableComponentHandler)
				{
					TArray<UActorComponent*> Templates;
					Inherited->GetAllTemplates(Templates, true);
					for (const UActorComponent* Template : Templates)
					{
						AddComponent(Template);
					}
				}
				if (const USimpleConstructionScript* Script = Blueprint->SimpleConstructionScript)
				{
					for (const USCS_Node* Node : Script->GetAllNodes())
					{
						if (Node)
						{
							AddComponent(Node->ComponentTemplate);
						}
					}
				}
			}

			TArray<UObject*> Subobjects;
			Current->GetDefaultObjectSubobjects(Subobjects);
			for (const UObject* Subobject : Subobjects)
			{
				AddComponent(Subobject);
			}
		}
		return Box;
	}

	FText DisplayNameOf(UClass* Class, const AFGBuildable* Default)
	{
		static const FTextProperty* Property = FindFProperty<FTextProperty>(AFGBuildable::StaticClass(), TEXT("mDisplayName"));
		if (Property)
		{
			const FText Name = Property->GetPropertyValue_InContainer(Default);
			if (!Name.IsEmpty())
			{
				return Name;
			}
		}

		FString Fallback = Class->GetName();
		Fallback.RemoveFromEnd(TEXT("_C"));
		Fallback.RemoveFromStart(TEXT("Build_"), ESearchCase::IgnoreCase);
		return FText::FromString(Fallback.Replace(TEXT("_"), TEXT(" ")));
	}

	void Classify(UClass* Class, FType& Type)
	{
		const AFGBuildable* Default = GetDefault<AFGBuildable>(Class);
		const FString ClassName = Class->GetName();
		Type.Class = Class;
		Type.Name = DisplayNameOf(Class, Default);

		if (ClassName.Contains(TEXT("FlowIndicator")))
		{
			Type.Kind = EKind::Skip;
			return;
		}

		// Path nodes are only the joints; the segments between them are drawn as the path.
		if (Class->IsChildOf<AFGVehiclePathNode>() || ClassName.Contains(TEXT("VehiclePathNode")))
		{
			Type.Kind = EKind::Skip;
			Type.Category = ECategory::Paths;
			return;
		}

		bool bSplineLike = false;
		if (Class->IsChildOf<AFGVehiclePathSegment>() || (ClassName.Contains(TEXT("Road")) && !ClassName.Contains(TEXT("Intersection"))))
		{
			bSplineLike = true;
			Type.Kind = EKind::SplineComponent;
			Type.Category = ECategory::Paths;
			Type.HalfWidthCm = ClassName.Contains(TEXT("Road")) ? 300.f : 110.f;
		}
		else if (Class->ImplementsInterface(UFGSplineBuildableInterface::StaticClass()))
		{
			bSplineLike = true;
			Type.Kind = EKind::Spline;
			if (Class->IsChildOf<AFGBuildableConveyorBelt>()) { Type.Category = ECategory::Belts; Type.HalfWidthCm = 65.f; }
			else if (Class->IsChildOf<AFGBuildablePipeHyper>()) { Type.Category = ECategory::Hypertubes; Type.HalfWidthCm = 90.f; }
			else if (Class->IsChildOf<AFGBuildablePipeline>()) { Type.Category = ECategory::Pipes; Type.HalfWidthCm = 55.f; }
			else if (Class->IsChildOf<AFGBuildableRailroadTrack>()) { Type.Category = ECategory::Rails; Type.HalfWidthCm = 170.f; }
			else if (ClassName.Contains(TEXT("Road")) || ClassName.Contains(TEXT("VehiclePath"))) { Type.Category = ECategory::Paths; Type.HalfWidthCm = 150.f; }
			else { Type.Category = ECategory::Other; Type.HalfWidthCm = 50.f; }
		}
		else if (Class->IsChildOf<AFGBuildableWire>())
		{
			bSplineLike = true;
			Type.Kind = EKind::Wire;
			Type.Category = ECategory::PowerLines;
			Type.HalfWidthCm = 12.f;
		}
		else if (Class->IsChildOf<AFGBuildableBeam>())
		{
			bSplineLike = true;
			Type.Kind = EKind::Beam;
			Type.Category = ECategory::Structures;
			Type.HalfWidthCm = 30.f;
		}

		if (!bSplineLike)
		{
			if (ClassName.Contains(TEXT("Roof"))) { Type.Category = ECategory::Roofs; }
			else if (Class->IsChildOf<AFGBuildableFoundation>()) { Type.Category = ECategory::Foundations; }
			else if (Class->IsChildOf<AFGBuildableWall>()) { Type.Category = ECategory::Walls; }
			else if (Class->IsChildOf<AFGBuildablePillar>() || Class->IsChildOf<AFGBuildableStair>() || Class->IsChildOf<AFGBuildableWalkway>()
				|| ClassName.Contains(TEXT("Ladder")) || ClassName.Contains(TEXT("Fence")) || ClassName.Contains(TEXT("Railing"))) { Type.Category = ECategory::Structures; }
			else if (Class->IsChildOf<AFGBuildableConveyorLift>()) { Type.Category = ECategory::Lifts; }
			else if (Class->IsChildOf<AFGBuildableManufacturer>()) { Type.Category = ECategory::Production; }
			else if (Class->IsChildOf<AFGBuildableResourceExtractorBase>()) { Type.Category = ECategory::Extraction; }
			else if (Class->IsChildOf<AFGBuildableGenerator>()) { Type.Category = ECategory::Generators; }
			else if (Class->IsChildOf<AFGBuildablePowerPole>()) { Type.Category = ECategory::PowerLines; }
			else if (Class->IsChildOf<AFGBuildableStorage>()) { Type.Category = ECategory::Storage; }
			else if (Class->IsChildOf<AFGBuildableConveyorAttachment>() || Class->IsChildOf<AFGBuildablePipelineAttachment>()) { Type.Category = ECategory::Logistics; }
			else if (Class->IsChildOf<AFGBuildableTrainPlatform>() || Class->IsChildOf<AFGBuildableDockingStation>()) { Type.Category = ECategory::Stations; }
			else if (ClassName.Contains(TEXT("Road")) || ClassName.Contains(TEXT("VehiclePath"))) { Type.Category = ECategory::Paths; }

			FBox Box = Default->GetCombinedClearanceBox();
			if (!Box.IsValid)
			{
				Box = MeshBounds(Class, Default);
			}
			if (Box.IsValid)
			{
				const FVector Extent = Box.GetExtent();
				Type.Center = Box.GetCenter();
				Type.Half = FVector2f(FMath::Clamp(float(Extent.X), 10.f, 20000.f), FMath::Clamp(float(Extent.Y), 10.f, 5000.f));
			}
			else
			{
				UE_LOG(LogFactoryAtlas, Display, TEXT("No size found for %s, using the default"), *ClassName);
			}
		}
	}

	/** A small brightness shift per type keeps types apart inside one category. */
	FColor ShiftColor(const FColor& Base, const FString& Key)
	{
		const float Shift = 0.88f + float(GetTypeHash(Key) % 25) / 100.f;
		return FColor(uint8(FMath::Min(Base.R * Shift, 255.f)), uint8(FMath::Min(Base.G * Shift, 255.f)), uint8(FMath::Min(Base.B * Shift, 255.f)));
	}

	template <typename FSink>
	void EmitSpan(const FVector& P0, const FVector& T0, const FVector& P1, const FVector& T1, const FType& Type, int32 TypeId, int32 Paint, FSink& Sink);

	template <typename FSink>
	void EmitSegment(const FVector& A, const FVector& B, const FType& Type, int32 TypeId, int32 Paint, FSink& Sink)
	{
		const FVector Delta = B - A;
		const float HalfLength = float(Delta.Size2D() * 0.5) + 15.f;
		const float Radians = float(FMath::Atan2(Delta.Y, Delta.X));
		const int32 Pieces = FMath::Max(1, FMath::CeilToInt32(HalfLength / MaxSegmentHalfCm));

		FVector4f Record;
		for (int32 Piece = 0; Piece < Pieces; Piece++)
		{
			const FVector Center = A + Delta * ((Piece + 0.5) / Pieces);
			if (EncodeWorld(Center, HalfLength / Pieces, Type.HalfWidthCm, Radians, TypeId, Paint, Record))
			{
				Sink(Record);
			}
		}
	}

	template <typename FSink>
	void EmitBox(const FTransform& Transform, const FType& Type, int32 TypeId, int32 Paint, FSink& Sink)
	{
		const FVector Forward = Transform.GetRotation().GetForwardVector();
		FVector4f Record;
		if (EncodeWorld(Transform.TransformPositionNoScale(Type.Center), Type.Half.X, Type.Half.Y, float(FMath::Atan2(Forward.Y, Forward.X)), TypeId, Paint, Record))
		{
			Sink(Record);
		}
	}

	template <typename FSink>
	void EmitSpline(AFGBuildable* Buildable, const FType& Type, int32 TypeId, int32 Paint, FSink& Sink)
	{
		const IFGSplineBuildableInterface* Spline = Cast<IFGSplineBuildableInterface>(Buildable);
		if (!Spline)
		{
			return;
		}

		const FTransform& Transform = Buildable->GetActorTransform();
		const TArray<FSplinePointData>& Points = Spline->GetSplinePointData();
		for (int32 i = 0; i + 1 < Points.Num(); i++)
		{
			EmitSpan(Transform.TransformPosition(Points[i].Location), Transform.TransformVector(Points[i].LeaveTangent),
				Transform.TransformPosition(Points[i + 1].Location), Transform.TransformVector(Points[i + 1].ArriveTangent),
				Type, TypeId, Paint, Sink);
		}
	}

	template <typename FSink>
	void EmitSpan(const FVector& P0, const FVector& T0, const FVector& P1, const FVector& T1, const FType& Type, int32 TypeId, int32 Paint, FSink& Sink)
	{
		const FVector Chord = P1 - P0;
		const double Length = Chord.Size();
		if (Length < 1.0)
		{
			return;
		}
		const FVector Direction = Chord / Length;
		const bool bStraight = T0.GetSafeNormal().Dot(Direction) > 0.9995 && T1.GetSafeNormal().Dot(Direction) > 0.9995;
		const int32 Steps = bStraight ? 1 : FMath::Clamp(FMath::CeilToInt32(Length / 300.0), 2, 12);

		FVector Previous = P0;
		for (int32 Step = 1; Step <= Steps; Step++)
		{
			const FVector Next = FMath::CubicInterp(P0, T0, P1, T1, double(Step) / Steps);
			EmitSegment(Previous, Next, Type, TypeId, Paint, Sink);
			Previous = Next;
		}
	}

	/** Truck paths and similar keep their shape in a spline component rather than in FactoryGame's spline data. */
	template <typename FSink>
	void EmitSplineComponent(AFGBuildable* Buildable, const FType& Type, int32 TypeId, int32 Paint, FSink& Sink)
	{
		const USplineComponent* Spline = Buildable->FindComponentByClass<USplineComponent>();
		if (!Spline || Spline->GetNumberOfSplinePoints() < 2)
		{
			FType Fallback = Type;
			Fallback.Half = FVector2f(Type.HalfWidthCm, Type.HalfWidthCm);
			EmitBox(Buildable->GetActorTransform(), Fallback, TypeId, Paint, Sink);
			return;
		}

		const ESplineCoordinateSpace::Type World = ESplineCoordinateSpace::World;
		const int32 Count = Spline->GetNumberOfSplinePoints();
		const int32 Spans = Spline->IsClosedLoop() ? Count : Count - 1;
		for (int32 i = 0; i < Spans; i++)
		{
			const int32 Next = (i + 1) % Count;
			EmitSpan(Spline->GetLocationAtSplinePoint(i, World), Spline->GetLeaveTangentAtSplinePoint(i, World),
				Spline->GetLocationAtSplinePoint(Next, World), Spline->GetArriveTangentAtSplinePoint(Next, World),
				Type, TypeId, Paint, Sink);
		}
	}

	template <typename FSink>
	void EmitActor(AFGBuildable* Buildable, const FType& Type, int32 TypeId, int32 Paint, FSink& Sink)
	{
		switch (Type.Kind)
		{
		case EKind::Spline:
			EmitSpline(Buildable, Type, TypeId, Paint, Sink);
			break;
		case EKind::SplineComponent:
			EmitSplineComponent(Buildable, Type, TypeId, Paint, Sink);
			break;
		case EKind::Wire:
		{
			const AFGBuildableWire* Wire = CastChecked<AFGBuildableWire>(Buildable);
			EmitSegment(Wire->GetConnectionLocation(0), Wire->GetConnectionLocation(1), Type, TypeId, Paint, Sink);
			break;
		}
		case EKind::Skip:
			break;
		default:
			EmitBox(Buildable->GetActorTransform(), Type, TypeId, Paint, Sink);
			break;
		}
	}

	template <typename FSink>
	void EmitLightweight(const FRuntimeBuildableInstanceData& Data, const FType& Type, int32 TypeId, int32 Paint, FSink& Sink)
	{
		if (Type.Kind == EKind::Skip)
		{
			return;
		}
		if (Type.Kind == EKind::Beam)
		{
			const FBuildableBeamLightweightData* Beam = Data.TypeSpecificData.GetValuePtr<FBuildableBeamLightweightData>();
			const FVector Start = Data.Transform.GetLocation();
			const FVector End = Start + Data.Transform.GetRotation().GetForwardVector() * (Beam ? Beam->BeamLength : 100.f);
			EmitSegment(Start, End, Type, TypeId, Paint, Sink);
			return;
		}
		EmitBox(Data.Transform, Type, TypeId, Paint, Sink);
	}

	void RunJob(FGrid& Grid, TArray<FOp>& Ops, bool bSortEverything)
	{
		TArray<TArray<int32>> OpsByCell;
		OpsByCell.SetNum(GridCells);
		for (int32 i = 0; i < Ops.Num(); i++)
		{
			OpsByCell[CellOf(Ops[i].Record)].Add(i);
		}

		ParallelFor(GridCells, [&](int32 CellIndex)
		{
			TArray<FVector4f>& Cell = Grid.Cells[CellIndex];
			const TArray<int32>& CellOps = OpsByCell[CellIndex];
			if (!bSortEverything && CellOps.IsEmpty())
			{
				return;
			}

			for (const int32 OpIndex : CellOps)
			{
				const FOp& Op = Ops[OpIndex];
				if (Op.bAdd)
				{
					Cell.Add(Op.Record);
				}
				else
				{
					const int32 Found = Cell.IndexOfByPredicate([&Op](const FVector4f& Other) { return SameBuilding(Op.Record, Other); });
					if (Found != INDEX_NONE)
					{
						Cell.RemoveAtSwap(Found, EAllowShrinking::No);
					}
				}
			}

			// Painter's order: the highest thing at a spot is what the map shows.
			Algo::SortBy(Cell, &DecodeZ);
			Grid.Versions[CellIndex]++;
		});

		int32 Low = ZMaxValue;
		int32 High = 0;
		int64 Total = 0;
		for (const TArray<FVector4f>& Cell : Grid.Cells)
		{
			Total += Cell.Num();
			if (Cell.Num() > 0)
			{
				Low = FMath::Min(Low, DecodeZ(Cell[0]));
				High = FMath::Max(High, DecodeZ(Cell.Last()));
			}
		}
		Grid.ZLow = FMath::Min(Low, High);
		Grid.ZHigh = High;
		Grid.Total = Total;
	}
}

bool UAtlasSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

bool UAtlasSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UAtlasSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Active = this;
	Grid = MakeShared<FGrid>();
	PaintColors.Add(FColor::Transparent); // Index 0 means unpainted
	LoadSettings();
}

void UAtlasSubsystem::Deinitialize()
{
	if (bJobActive)
	{
		Job.Wait();
		bJobActive = false;
	}
	BindBuildableDelegates(false);
	if (Active == this)
	{
		Active = nullptr;
	}
	Super::Deinitialize();
}

void UAtlasSubsystem::BindBuildableDelegates(bool bBind)
{
	if (bBound == bBind)
	{
		return;
	}
	AFGBuildableSubsystem* Buildables = AFGBuildableSubsystem::Get(GetWorld());
	if (!Buildables)
	{
		return;
	}

	// Found by name through reflection, because game patches have moved members away from the offsets
	// the SML headers compile to (seen on AFGBlueprintSubsystem in 1.2.5).
	const auto Apply = [this, Buildables, bBind](const TCHAR* PropertyName, const TCHAR* FunctionName)
	{
		const FMulticastDelegateProperty* Property = FindFProperty<FMulticastDelegateProperty>(AFGBuildableSubsystem::StaticClass(), PropertyName);
		if (!Property)
		{
			UE_LOG(LogFactoryAtlas, Warning, TEXT("%s not found, live updates for buildings are off"), PropertyName);
			return;
		}
		FScriptDelegate Delegate;
		Delegate.BindUFunction(this, FunctionName);
		if (bBind)
		{
			Property->AddDelegate(MoveTemp(Delegate), Buildables);
		}
		else
		{
			Property->RemoveDelegate(Delegate, Buildables);
		}
	};
	Apply(TEXT("mBuildableAddedDelegate"), TEXT("OnBuildableAdded"));
	Apply(TEXT("mBuildableRemovedDelegate"), TEXT("OnBuildableRemoved"));
	bBound = bBind;
}

void UAtlasSubsystem::ApplyTypeColor(int32 TypeId)
{
	if (!Types.IsValidIndex(TypeId))
	{
		return;
	}
	FType& Type = Types[TypeId];
	const FString Key = Type.Class ? Type.Class->GetPathName() : FString(TEXT("Other"));
	if (const FColor* Chosen = TypeColorOverrides.Find(Key))
	{
		Type.Color = *Chosen;
	}
	else
	{
		Type.Color = ShiftColor(GetCategoryDisplayColor(Type.Category), Key);
	}
	bPaletteDirty = true;
}

FColor UAtlasSubsystem::GetCategoryDisplayColor(ECategory Category) const
{
	const TOptional<FColor>& Chosen = CategoryColorOverrides[int32(Category)];
	return Chosen.IsSet() ? Chosen.GetValue() : CategoryColor(Category);
}

bool UAtlasSubsystem::HasTypeColor(int32 TypeId) const
{
	return Types.IsValidIndex(TypeId) && Types[TypeId].Class && TypeColorOverrides.Contains(Types[TypeId].Class->GetPathName());
}

void UAtlasSubsystem::SetCategoryColor(ECategory Category, TOptional<FColor> Color)
{
	CategoryColorOverrides[int32(Category)] = Color;
	for (int32 i = 0; i < Types.Num(); i++)
	{
		if (Types[i].Category == Category)
		{
			ApplyTypeColor(i);
		}
	}
	SaveSettings();
}

void UAtlasSubsystem::SetTypeColor(int32 TypeId, TOptional<FColor> Color)
{
	if (!Types.IsValidIndex(TypeId) || !Types[TypeId].Class)
	{
		return;
	}
	const FString Key = Types[TypeId].Class->GetPathName();
	if (Color.IsSet())
	{
		TypeColorOverrides.Add(Key, Color.GetValue());
	}
	else
	{
		TypeColorOverrides.Remove(Key);
	}
	ApplyTypeColor(TypeId);
	SaveSettings();
}

void UAtlasSubsystem::ResetColors()
{
	for (TOptional<FColor>& Chosen : CategoryColorOverrides)
	{
		Chosen.Reset();
	}
	TypeColorOverrides.Reset();
	for (int32 i = 0; i < Types.Num(); i++)
	{
		ApplyTypeColor(i);
	}
	SaveSettings();
}

const TArray<FVector4f>& UAtlasSubsystem::GetRadiation(uint32& OutVersion)
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastRadiationSeconds >= 1.0)
	{
		LastRadiationSeconds = Now;
		Radiation.Reset();
		RadiationVersion++;

		// mSources is private but reflected. Its value type is declared in the public header.
		static const FMapProperty* SourcesProperty = FindFProperty<FMapProperty>(AFGRadioactivitySubsystem::StaticClass(), TEXT("mSources"));
		AFGRadioactivitySubsystem* Subsystem = AFGRadioactivitySubsystem::Get(GetWorld());
		if (Subsystem && SourcesProperty)
		{
			using FSources = TMap<TObjectPtr<UObject>, FRadioactiveSource>;
			const FSources& Sources = *SourcesProperty->ContainerPtrToValuePtr<FSources>(Subsystem);

			// The game's own formula (AFGRadioactivitySubsystem::calculateIntensity): amount * decay / (4 pi d^2) * exp(-falloff d).
			// The falloff is the value the game's header recommends; the circle ends where the intensity falls to DisplayThreshold.
			constexpr float Falloff = 0.08f;
			constexpr float DisplayThreshold = 0.05f;
			const auto Intensity = [](float Strength, float Distance)
			{
				return Strength / (4.f * UE_PI * Distance * Distance) * FMath::Exp(-Falloff * Distance);
			};

			for (const TPair<TObjectPtr<UObject>, FRadioactiveSource>& Pair : Sources)
			{
				const FRadioactiveSource& Source = Pair.Value;
				const USceneComponent* Root = Source.AttachRoot.Get();
				for (const FRadioactiveEmitter& Emitter : Source.Emitters)
				{
					const float Strength = float(Emitter.ItemAmount) * Emitter.ItemDecay;
					if (Strength <= 0.f || Intensity(Strength, 1.f) < DisplayThreshold)
					{
						continue;
					}

					float Low = 1.f, High = 600.f;
					for (int32 Step = 0; Step < 20; Step++)
					{
						const float Mid = (Low + High) * 0.5f;
						(Intensity(Strength, Mid) > DisplayThreshold ? Low : High) = Mid;
					}

					const FVector Location = Root ? Root->GetComponentTransform().TransformPosition(Emitter.LocalLocation) : Emitter.CachedWorldLocation;
					const float RadiusCm = Low * 100.f / RadiationSizeScale;
					FVector4f Record;
					if (EncodeWorld(Location, RadiusCm, RadiusCm, 0.f, RadiationType, 0, Record))
					{
						Radiation.Add(Record);
					}
				}
			}
		}
	}
	OutVersion = RadiationVersion;
	return Radiation;
}

const TArray<FVector4f>& UAtlasSubsystem::GetMarkers(uint32& OutVersion)
{
	// Collecting something is rare, so a few seconds of delay is fine and keeps this cheap.
	const double Now = FPlatformTime::Seconds();
	if (Now - LastMarkerSeconds >= 5.0)
	{
		LastMarkerSeconds = Now;
		RebuildMarkers();
	}
	OutVersion = MarkerVersion;
	return Markers;
}

void UAtlasSubsystem::RebuildMarkers()
{
	Markers.Reset();
	MarkerVersion++;
	GatherCollectibles();
	if (FeatureMask != 0)
	{
		GatherWorldFeatures();
		static bool bLogged = false;
		if (!bLogged)
		{
			bLogged = true;
			UE_LOG(LogFactoryAtlas, Display, TEXT("World features: %d resource nodes, %d well nodes, %d spore flowers and %d gas pillars known, %d resource kinds"),
				FeatureCounts[0], FeatureCounts[1], FeatureCounts[2], FeatureCounts[3], ResourceKinds.Num());
		}
	}
	bMenuDirty = true;
}

void UAtlasSubsystem::GatherCollectibles()
{
	for (int32& Count : CollectibleCounts)
	{
		Count = 0;
	}

	// The object scanner keeps every pickup and drop pod in the world, loaded or not, in these reflected members.
	AFGScannableSubsystem* Scannables = AFGScannableSubsystem::Get(GetWorld());
	if (!Scannables)
	{
		return;
	}
	static const FArrayProperty* PickupsProperty = FindFProperty<FArrayProperty>(AFGScannableSubsystem::StaticClass(), TEXT("mAvailableItemPickups"));
	static const FArrayProperty* PodsProperty = FindFProperty<FArrayProperty>(AFGScannableSubsystem::StaticClass(), TEXT("mAvailableDropPods"));
	static const FSetProperty* DestroyedProperty = FindFProperty<FSetProperty>(AFGScannableSubsystem::StaticClass(), TEXT("mDestroyedPickups"));
	static const FSetProperty* LootedProperty = FindFProperty<FSetProperty>(AFGScannableSubsystem::StaticClass(), TEXT("mLootedDropPods"));
	if (!PickupsProperty || !PodsProperty || !DestroyedProperty || !LootedProperty)
	{
		UE_LOG(LogFactoryAtlas, Warning, TEXT("Scannable lists not found, collectibles are off"));
		return;
	}

	const TArray<FWorldScannableData>& Pickups = *PickupsProperty->ContainerPtrToValuePtr<TArray<FWorldScannableData>>(Scannables);
	const TArray<FWorldScannableData>& Pods = *PodsProperty->ContainerPtrToValuePtr<TArray<FWorldScannableData>>(Scannables);
	const TSet<FGuid>& Destroyed = *DestroyedProperty->ContainerPtrToValuePtr<TSet<FGuid>>(Scannables);
	const TSet<FGuid>& Looted = *LootedProperty->ContainerPtrToValuePtr<TSet<FGuid>>(Scannables);

	const auto Add = [this](const FVector& Location, int32 Marker)
	{
		CollectibleCounts[int32(CollectibleOfMarker(Marker))]++;
		FVector4f Record;
		const float Half = MarkerRadiusPixels * 10.f; // Encoded in decimetres, read back as pixels
		if (EncodeWorld(Location, Half, Half, 0.f, Marker, 0, Record))
		{
			Markers.Add(Record);
		}
	};

	const bool bFirstTime = MarkerOfClass.IsEmpty();
	for (const FWorldScannableData& Pickup : Pickups)
	{
		UClass* Class = Pickup.ActorClass.Get();
		if (!Class || Destroyed.Contains(Pickup.ActorGuid))
		{
			continue;
		}

		int32* Marker = MarkerOfClass.Find(Class);
		if (!Marker)
		{
			const FString Name = Class->GetName();
			int32 Kind = INDEX_NONE;
			if (Name.Contains(TEXT("WAT1"))) { Kind = MarkerSomersloop; }
			else if (Name.Contains(TEXT("WAT2"))) { Kind = MarkerMercerSphere; }
			else if (Name.Contains(TEXT("Crystal_mk3"))) { Kind = MarkerSlugPurple; }
			else if (Name.Contains(TEXT("Crystal_mk2"))) { Kind = MarkerSlugYellow; }
			else if (Name.Contains(TEXT("Crystal"))) { Kind = MarkerSlugBlue; }
			Marker = &MarkerOfClass.Add(Class, Kind);
			UE_LOG(LogFactoryAtlas, Display, TEXT("Scannable pickup class %s -> %s"), *Name, Kind == INDEX_NONE ? TEXT("not shown") : MarkerName(Kind));
		}
		if (*Marker != INDEX_NONE)
		{
			Add(Pickup.ActorLocation, *Marker);
		}
	}

	for (const FWorldScannableData& Pod : Pods)
	{
		if (!Looted.Contains(Pod.ActorGuid))
		{
			Add(Pod.ActorLocation, MarkerHardDrive);
		}
	}

	if (bFirstTime)
	{
		UE_LOG(LogFactoryAtlas, Display, TEXT("Collectibles left: %d Somersloops, %d Mercer Spheres, %d hard drives, %d power slugs (%d pickups, %d drop pods in the world)"),
			CollectibleCounts[0], CollectibleCounts[1], CollectibleCounts[2], CollectibleCounts[3], Pickups.Num(), Pods.Num());
	}
}

void UAtlasSubsystem::SetCollectibleShown(ECollectible Kind, bool bShown)
{
	const uint32 Bit = 1u << int32(Kind);
	CollectibleMask = bShown ? (CollectibleMask | Bit) : (CollectibleMask & ~Bit);
	bPaletteDirty = true;
	LastMarkerSeconds = -1000.0; // Refresh on the next paint
	SaveSettings();
}

namespace
{
	/** Calls a reflected getter with no arguments. Reflection keeps working when a game patch moves members. */
	template <typename TValue>
	bool CallGetter(UObject* Object, const TCHAR* Name, TValue& Out)
	{
		UFunction* Function = Object ? Object->FindFunction(FName(Name)) : nullptr;
		if (!Function || Function->ParmsSize != sizeof(TValue) || Function->NumParms != 1)
		{
			return false;
		}
		TValue Value{};
		Object->ProcessEvent(Function, &Value);
		Out = Value;
		return true;
	}

	/** Calls a reflected function that takes one argument and returns nothing. */
	template <typename TValue>
	bool CallSetter(UObject* Object, const TCHAR* Name, TValue Value)
	{
		UFunction* Function = Object ? Object->FindFunction(FName(Name)) : nullptr;
		if (!Function || Function->ParmsSize != sizeof(TValue) || Function->NumParms != 1)
		{
			return false;
		}
		Object->ProcessEvent(Function, &Value);
		return true;
	}

	bool ReadBoolProperty(const UObject* Object, const TCHAR* Name)
	{
		const FBoolProperty* Property = FindFProperty<FBoolProperty>(Object->GetClass(), Name);
		return Property && Property->GetPropertyValue_InContainer(Object);
	}

	/** "30 Iron Ore  >  30 Iron Ingot", per minute at the given clock speed. */
	FString DescribeRecipe(UObject* Building, UClass* Recipe, float Potential)
	{
		if (!Recipe)
		{
			return FString();
		}

		float Cycle = UFGRecipe::GetManufacturingDuration(Recipe);
		if (UFunction* Function = Building->FindFunction(TEXT("GetProductionCycleTimeForRecipe")))
		{
			struct { TSubclassOf<UFGRecipe> Recipe; float ReturnValue; } Params{ Recipe, 0.f };
			if (Function->ParmsSize == sizeof(Params))
			{
				Building->ProcessEvent(Function, &Params);
				Cycle = Params.ReturnValue > 0.f ? Params.ReturnValue : Cycle;
			}
		}
		const float PerMinute = Cycle > 0.f ? 60.f / Cycle * FMath::Max(Potential, 0.01f) : 0.f;

		const auto List = [PerMinute](const TArray<FItemAmount>& Items)
		{
			TArray<FString> Parts;
			for (const FItemAmount& Item : Items)
			{
				// Fluids are stored in litres; the game shows them in cubic metres.
				const float Amount = UFGItemDescriptor::GetForm(Item.ItemClass) == EResourceForm::RF_SOLID ? Item.Amount : Item.Amount / 1000.f;
				Parts.Add(FString::Printf(TEXT("%s %s"), *FText::AsNumber(FMath::RoundToFloat(Amount * PerMinute * 10.f) / 10.f).ToString(),
					*UFGItemDescriptor::GetItemName(Item.ItemClass).ToString()));
			}
			return FString::Join(Parts, TEXT(", "));
		};
		return FString::Printf(TEXT("%s  >  %s  per min"), *List(UFGRecipe::GetIngredients(Building, Recipe)), *List(UFGRecipe::GetProducts(Recipe)));
	}
}

bool UAtlasSubsystem::SelectHovered()
{
	if (!Hover.bValid || Hover.bMarker || !Types.IsValidIndex(Hover.Type))
	{
		return false;
	}
	UClass* Class = Types[Hover.Type].Class;
	AFGBuildableSubsystem* Buildables = AFGBuildableSubsystem::Get(GetWorld());
	if (!Class || !Buildables || !Class->IsChildOf<AFGBuildableFactory>())
	{
		return false;
	}

	// Records keep no actor handle, so find the actor whose own record is the one under the cursor.
	const FType& Type = Types[Hover.Type];
	for (AFGBuildable* Buildable : Buildables->GetAllBuildablesRef())
	{
		if (!Buildable || Buildable->GetClass() != Class)
		{
			continue;
		}
		bool bMatch = false;
		auto Sink = [this, &bMatch](const FVector4f& Record) { bMatch |= SameBuilding(Record, Hover.Source); };
		EmitActor(Buildable, Type, Hover.Type, 0, Sink);
		if (bMatch)
		{
			Selected = Buildable;
			const FDecoded Record(Hover.Source);
			SelectionRecord = Encode(Record.X, Record.Y, Record.Z, Record.HalfX, Record.HalfY, Record.Angle, HighlightType, 0);
			return true;
		}
	}
	return false;
}

void UAtlasSubsystem::ClearSelection()
{
	Selected.Reset();
}

FFactoryState UAtlasSubsystem::GetSelectionState() const
{
	FFactoryState State;
	AFGBuildable* Building = Selected.Get();
	if (!Building)
	{
		return State;
	}

	State.bValid = true;
	State.bAuthority = Building->HasAuthority();
	if (const int32* TypeId = TypeIndex.Find(Building->GetClass()))
	{
		State.Name = Types[*TypeId].Name;
	}

	CallGetter(Building, TEXT("GetCanChangePotential"), State.bCanChangeClock);
	CallGetter(Building, TEXT("GetCurrentPotential"), State.Current);
	CallGetter(Building, TEXT("GetPendingPotential"), State.Pending);
	CallGetter(Building, TEXT("GetCurrentMinPotential"), State.Min);
	CallGetter(Building, TEXT("GetCurrentMaxPotential"), State.Max);
	CallGetter(Building, TEXT("GetCurrentProductionBoost"), State.Boost);
	CallGetter(Building, TEXT("GetCurrentMaxProductionBoost"), State.MaxBoost);
	CallGetter(Building, TEXT("IsProductionPaused"), State.bPaused);
	CallGetter(Building, TEXT("GetProductivity"), State.Productivity);
	State.bCanBoost = ReadBoolProperty(Building, TEXT("mCanChangeProductionBoost"));

	// Each power shard adds 50 percentage points on top of 100%.
	State.Shards = FMath::Max(0, FMath::RoundToInt32((State.Max - 1.f) / 0.5f));
	State.Max = FMath::Max(State.Max, State.Min);

	if (Building->IsA<AFGBuildableManufacturer>())
	{
		State.bHasRecipes = true;
		TSubclassOf<UFGRecipe> Recipe;
		if (CallGetter(Building, TEXT("GetCurrentRecipe"), Recipe) && Recipe)
		{
			State.Recipe = Recipe;
			State.RecipeName = UFGRecipe::GetRecipeName(Recipe);
			State.RecipeSummary = DescribeRecipe(Building, Recipe, State.Pending);
		}
	}
	return State;
}

TArray<FRecipeOption> UAtlasSubsystem::GetSelectedRecipeOptions() const
{
	TArray<FRecipeOption> Options;
	AFGBuildable* Building = Selected.Get();
	UFunction* Function = Building ? Building->FindFunction(TEXT("GetAvailableRecipes")) : nullptr;
	if (!Function)
	{
		return Options;
	}

	struct { TArray<TSubclassOf<UFGRecipe>> Recipes; } Params;
	if (Function->ParmsSize != sizeof(Params))
	{
		return Options;
	}
	Building->ProcessEvent(Function, &Params);

	float Potential = 1.f;
	CallGetter(Building, TEXT("GetPendingPotential"), Potential);
	for (const TSubclassOf<UFGRecipe>& Recipe : Params.Recipes)
	{
		if (Recipe)
		{
			Options.Add({ Recipe.Get(), UFGRecipe::GetRecipeName(Recipe), DescribeRecipe(Building, Recipe, Potential) });
		}
	}
	Options.Sort([](const FRecipeOption& A, const FRecipeOption& B) { return A.Name.CompareTo(B.Name) < 0; });
	return Options;
}

bool UAtlasSubsystem::SetSelectedRecipe(UClass* Recipe)
{
	AFGBuildable* Building = Selected.Get();
	if (!Building || !Building->HasAuthority() || !Building->IsA<AFGBuildableManufacturer>() || !Recipe)
	{
		return false;
	}

	// Same order as the machine's own screen: hand the old items to the player (a crate drops for any
	// that do not fit), then switch.
	APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	AFGPlayerController* Player = Cast<AFGPlayerController>(Controller);
	AFGCharacterPlayer* Pawn = Player ? Cast<AFGCharacterPlayer>(Player->GetControlledCharacter()) : nullptr;
	if (!Pawn)
	{
		UE_LOG(LogFactoryAtlas, Warning, TEXT("Recipe change skipped: no player character to receive the machine's items"));
		return false;
	}

	for (const TCHAR* Name : { TEXT("MoveOrDropInputInventory"), TEXT("MoveOrDropOutputInventory") })
	{
		UFunction* Function = Building->FindFunction(Name);
		struct { AFGCharacterPlayer* Pawn; bool ReturnValue; } Params{ Pawn, false };
		if (!Function || Function->ParmsSize != sizeof(Params))
		{
			UE_LOG(LogFactoryAtlas, Warning, TEXT("Recipe change skipped: %s not found"), Name);
			return false;
		}
		Building->ProcessEvent(Function, &Params);
	}

	TSubclassOf<UFGRecipe> NewRecipe = Recipe;
	if (!CallSetter(Building, TEXT("SetRecipe"), NewRecipe))
	{
		return false;
	}
	UE_LOG(LogFactoryAtlas, Display, TEXT("Recipe of %s set to %s"), *Building->GetName(), *Recipe->GetName());
	return true;
}

void UAtlasSubsystem::SetSelectedClock(float Potential)
{
	AFGBuildable* Building = Selected.Get();
	if (!Building || !Building->HasAuthority())
	{
		return;
	}
	const FFactoryState State = GetSelectionState();
	if (!State.bCanChangeClock)
	{
		return;
	}
	// The game's own slider works in steps of 0.0001; match it so values look the same in its UI.
	const float Clamped = FMath::RoundToFloat(FMath::Clamp(Potential, State.Min, State.Max) * 10000.f) / 10000.f;
	CallSetter(Building, TEXT("SetPendingPotential"), Clamped);
}

void UAtlasSubsystem::SetSelectedPaused(bool bPaused)
{
	AFGBuildable* Building = Selected.Get();
	if (Building && Building->HasAuthority())
	{
		CallSetter(Building, TEXT("SetIsProductionPaused"), bPaused);
	}
}

bool UAtlasSubsystem::IsPointOnMap(const FVector2f& ScreenPosition) const
{
	const TSharedPtr<SWidget> Map = MapWidget.Pin();
	if (!Map)
	{
		return true;
	}
	FSlateApplication& Slate = FSlateApplication::Get();
	const FWidgetPath Path = Slate.LocateWindowUnderMouse(FVector2D(ScreenPosition), Slate.GetInteractiveTopLevelWindows());
	return Path.IsValid() && Path.ContainsWidget(Map.Get());
}

FText UAtlasSubsystem::GetHoverName() const
{
	if (Hover.Type >= MarkerFirst && Hover.Type <= MarkerLast)
	{
		return FText::FromString(MarkerName(Hover.Type));
	}
	if (Hover.Type == SporeFlowerType)
	{
		return FText::FromString(TEXT("Spore Flower"));
	}
	if (Hover.Type == GasPillarType)
	{
		return FText::FromString(TEXT("Gas Pillar"));
	}
	if (Hover.Type >= ResourceFirst && Hover.Type <= ResourceLast)
	{
		const int32 Index = Hover.Type - ResourceFirst;
		if (!ResourceKinds.IsValidIndex(Index))
		{
			return FText::GetEmpty();
		}
		const FResourceKind& Kind = ResourceKinds[Index];
		static const TCHAR* Purities[] = { TEXT("Impure"), TEXT("Normal"), TEXT("Pure") };
		FString Label;
		if (Hover.Flags & 8)
		{
			Label = FString::Printf(TEXT("%s well, pressurizer spot"), *Kind.Name.ToString());
		}
		else
		{
			Label = FString::Printf(TEXT("%s %s%s"), Purities[FMath::Clamp((Hover.Flags >> 1) & 3, 0, 2)], *Kind.Name.ToString(),
				Kind.bWell ? TEXT(" well") : TEXT(""));
		}
		if (Hover.Flags & 1)
		{
			Label += TEXT(" (in use)");
		}
		return FText::FromString(Label);
	}
	return Types.IsValidIndex(Hover.Type) ? Types[Hover.Type].Name : FText::GetEmpty();
}

bool UAtlasSubsystem::IsMarkerShown(int32 Type) const
{
	if (Type >= MarkerFirst && Type <= MarkerLast)
	{
		return IsCollectibleShown(CollectibleOfMarker(Type));
	}
	if (Type == SporeFlowerType)
	{
		return IsFeatureShown(EFeature::SporeFlowers);
	}
	if (Type == GasPillarType)
	{
		return IsFeatureShown(EFeature::GasPillars);
	}
	if (Type >= ResourceFirst && Type <= ResourceLast && ResourceKinds.IsValidIndex(Type - ResourceFirst))
	{
		const FResourceKind& Kind = ResourceKinds[Type - ResourceFirst];
		return IsFeatureShown(Kind.bWell ? EFeature::ResourceWells : EFeature::ResourceNodes) && !IsResourceHidden(Kind.Descriptor);
	}
	return false;
}

void UAtlasSubsystem::SetFeatureShown(EFeature Feature, bool bShown)
{
	const uint32 Bit = 1u << int32(Feature);
	FeatureMask = bShown ? (FeatureMask | Bit) : (FeatureMask & ~Bit);
	bPaletteDirty = true;
	LastMarkerSeconds = -1000.0;
	SaveSettings();
}

bool UAtlasSubsystem::IsResourceHidden(UClass* Descriptor) const
{
	return Descriptor && HiddenResourcePaths.Contains(Descriptor->GetPathName());
}

void UAtlasSubsystem::SetResourceHidden(UClass* Descriptor, bool bHidden)
{
	if (!Descriptor)
	{
		return;
	}
	if (bHidden)
	{
		HiddenResourcePaths.Add(Descriptor->GetPathName());
	}
	else
	{
		HiddenResourcePaths.Remove(Descriptor->GetPathName());
	}
	bPaletteDirty = true;
	SaveSettings();
}

int32 UAtlasSubsystem::ResourceMarkerFor(UClass* Descriptor, bool bWell)
{
	for (int32 i = 0; i < ResourceKinds.Num(); i++)
	{
		if (ResourceKinds[i].Descriptor == Descriptor && ResourceKinds[i].bWell == bWell)
		{
			return ResourceFirst + i;
		}
	}
	if (ResourceFirst + ResourceKinds.Num() > ResourceLast)
	{
		return INDEX_NONE;
	}
	FResourceKind& Kind = ResourceKinds.AddDefaulted_GetRef();
	Kind.Descriptor = Descriptor;
	Kind.Name = UFGItemDescriptor::GetItemName(Descriptor);
	Kind.Color = ResourceColor(Descriptor->GetName());
	Kind.bWell = bWell;
	bPaletteDirty = true;
	return ResourceFirst + ResourceKinds.Num() - 1;
}

void UAtlasSubsystem::GatherWorldFeatures()
{
	for (int32& Count : FeatureCounts)
	{
		Count = 0;
	}
	for (FResourceKind& Kind : ResourceKinds)
	{
		Kind.Count = 0;
	}
	if (!bFeatureCacheLoaded)
	{
		LoadFeatureCache();
	}

	const auto AddMarker = [this](const FVector& Location, float Pixels, int32 Marker, int32 Flags)
	{
		FVector4f Record;
		if (EncodeWorld(Location, Pixels * 10.f, Pixels * 10.f, 0.f, Marker, Flags, Record))
		{
			Markers.Add(Record);
		}
	};

	// Resource nodes stay loaded across the whole map: the node manager pairs every one with its mesh at load.
	constexpr uint8 NodeTypeWellSatellite = 1, NodeTypeWellCore = 2, NodeTypeGeyser = 3, NodeTypeDeposit = 4;
	static const float PurityPixels[3] = { 5.f, 6.5f, 8.f };
	for (TActorIterator<AFGResourceNodeBase> It(GetWorld()); It; ++It)
	{
		AFGResourceNodeBase* Node = *It;
		uint8 NodeType = 0;
		CallGetter(Node, TEXT("GetResourceNodeType"), NodeType);
		if (NodeType >= NodeTypeDeposit)
		{
			continue; // Deposits are the small rocks mined by hand
		}

		TSubclassOf<UFGResourceDescriptor> Resource;
		CallGetter(Node, TEXT("GetResourceClass"), Resource);
		if (!Resource)
		{
			continue;
		}

		const bool bWell = NodeType == NodeTypeWellSatellite || NodeType == NodeTypeWellCore;
		FeatureCounts[int32(bWell ? EFeature::ResourceWells : EFeature::ResourceNodes)]++;
		const int32 Marker = ResourceMarkerFor(Resource, bWell);
		if (Marker == INDEX_NONE)
		{
			continue;
		}
		ResourceKinds[Marker - ResourceFirst].Count++;

		bool bOccupied = false;
		CallGetter(Node, TEXT("IsOccupied"), bOccupied);
		uint8 Purity = 1;
		CallGetter(Node, TEXT("GetResourcePurity"), Purity);
		Purity = FMath::Min<uint8>(Purity, 2);

		const bool bCore = NodeType == NodeTypeWellCore;
		const int32 Flags = (bOccupied ? 1 : 0) | (Purity << 1) | (bCore ? 8 : 0) | (NodeType == NodeTypeGeyser ? 16 : 0);
		AddMarker(Node->GetActorLocation(), bCore ? 9.f : PurityPixels[Purity], Marker, Flags);
	}

	// Spore flowers and gas pillars only exist while their part of the world is loaded, so remember every one seen.
	bool bCacheChanged = false;
	for (TActorIterator<AFGSporeFlower> It(GetWorld()); It; ++It)
	{
		const FString Key = It->GetName();
		if (!SeenSporeFlowers.Contains(Key))
		{
			SeenSporeFlowers.Add(Key, It->GetActorLocation());
			bCacheChanged = true;
		}
	}
	for (TActorIterator<AFGGasPillar> It(GetWorld()); It; ++It)
	{
		const FString Key = It->GetName();
		if (!SeenGasPillars.Contains(Key))
		{
			SeenGasPillars.Add(Key, It->GetActorLocation());
			bCacheChanged = true;
		}
	}
	for (const TPair<FString, FVector>& Pair : SeenSporeFlowers)
	{
		AddMarker(Pair.Value, 5.f, SporeFlowerType, 0);
	}
	for (const TPair<FString, FVector>& Pair : SeenGasPillars)
	{
		AddMarker(Pair.Value, 6.f, GasPillarType, 0);
	}
	FeatureCounts[int32(EFeature::SporeFlowers)] = SeenSporeFlowers.Num();
	FeatureCounts[int32(EFeature::GasPillars)] = SeenGasPillars.Num();
	if (bCacheChanged)
	{
		SaveFeatureCache();
	}
}

void UAtlasSubsystem::LoadFeatureCache()
{
	bFeatureCacheLoaded = true;
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectSavedDir() / TEXT("FactoryAtlas") / TEXT("WorldFeatures.txt"))))
	{
		return;
	}
	for (const FString& Line : Lines)
	{
		TArray<FString> Parts;
		if (Line.ParseIntoArray(Parts, TEXT("|")) != 5)
		{
			continue;
		}
		const FVector Location(FCString::Atod(*Parts[2]), FCString::Atod(*Parts[3]), FCString::Atod(*Parts[4]));
		(Parts[0] == TEXT("S") ? SeenSporeFlowers : SeenGasPillars).Add(Parts[1], Location);
	}
}

void UAtlasSubsystem::SaveFeatureCache() const
{
	TArray<FString> Lines;
	for (const TPair<FString, FVector>& Pair : SeenSporeFlowers)
	{
		Lines.Add(FString::Printf(TEXT("S|%s|%.0f|%.0f|%.0f"), *Pair.Key, Pair.Value.X, Pair.Value.Y, Pair.Value.Z));
	}
	for (const TPair<FString, FVector>& Pair : SeenGasPillars)
	{
		Lines.Add(FString::Printf(TEXT("G|%s|%.0f|%.0f|%.0f"), *Pair.Key, Pair.Value.X, Pair.Value.Y, Pair.Value.Z));
	}
	FFileHelper::SaveStringArrayToFile(Lines, *(FPaths::ProjectSavedDir() / TEXT("FactoryAtlas") / TEXT("WorldFeatures.txt")));
}

int32 UAtlasSubsystem::TypeFor(UClass* Class)
{
	if (const int32* Found = TypeIndex.Find(Class))
	{
		return *Found;
	}

	if (Types.Num() >= OverflowType)
	{
		if (Types.Num() == OverflowType)
		{
			FType& Overflow = Types.AddDefaulted_GetRef();
			Overflow.Name = FText::FromString(TEXT("Other buildings"));
			ApplyTypeColor(OverflowType);
		}
		TypeIndex.Add(Class, OverflowType);
		return OverflowType;
	}

	FType Type;
	Classify(Class, Type);
	Type.bHidden = HiddenTypePaths.Contains(Class->GetPathName());
	const int32 Index = Types.Add(MoveTemp(Type));
	TypeIndex.Add(Class, Index);
	ApplyTypeColor(Index);
	return Index;
}

int32 UAtlasSubsystem::PaintFor(const FFactoryCustomizationData& Data)
{
	UClass* Swatch = Data.SwatchDesc.Get();
	if (!Swatch)
	{
		return 0;
	}

	const FLinearColor& Override = Data.OverrideColorData.PrimaryColor;
	const bool bCustom = Override.R + Override.G + Override.B > 0.f;
	if (!bCustom)
	{
		if (const int32* Found = SwatchIndex.Find(Swatch))
		{
			return *Found;
		}
	}

	FLinearColor Linear = Override;
	if (!bCustom)
	{
		AFGGameState* GameState = GetWorld()->GetGameState<AFGGameState>();
		const int32 Slot = UFGBlueprintFunctionLibrary::GetIDForCustomizationDesc(Data.SwatchDesc);
		if (!GameState || Slot < 0 || Slot > 255)
		{
			SwatchIndex.Add(Swatch, 0);
			return 0;
		}
		Linear = GameState->GetBuildingColorDataForSlot(uint8(Slot)).PrimaryColor;
	}

	const FColor Color = Linear.ToFColor(true);
	int32 Index = 0;
	if (const int32* Existing = PaintIndex.Find(Color))
	{
		Index = *Existing;
	}
	else if (PaintColors.Num() < MaxPaints)
	{
		Index = PaintColors.Add(Color);
		PaintIndex.Add(Color, Index);
		bPaletteDirty = true;
	}

	if (!bCustom)
	{
		SwatchIndex.Add(Swatch, Index);
	}
	return Index;
}

void UAtlasSubsystem::OnMapOpened()
{
	// Repaints raise no event, so paint mode refreshes the whole map when it is out of date.
	if (bPaintMode && bScanned && FPlatformTime::Seconds() - LastScanSeconds > PaintRefreshSeconds)
	{
		bWantsRescan = true;
	}
}

void UAtlasSubsystem::EnsureScanned()
{
	const int32 Stress = FMath::Max(0, CVarStress.GetValueOnGameThread());
	if ((!bScanned || bWantsRescan || Stress != BuiltStress) && !bJobActive)
	{
		Scan();
	}
}

void UAtlasSubsystem::Scan()
{
	AFGBuildableSubsystem* Buildables = AFGBuildableSubsystem::Get(GetWorld());
	AFGLightweightBuildableSubsystem* Lightweights = AFGLightweightBuildableSubsystem::Get(GetWorld());
	if (!Buildables || !Lightweights)
	{
		return;
	}

	const double Start = FPlatformTime::Seconds();
	bWantsRescan = false;
	PendingOps.Reset();
	for (FType& Type : Types)
	{
		Type.Count = 0;
	}

	TSharedPtr<FGrid> Fresh = MakeShared<FGrid>();
	for (int32 i = 0; i < GridCells; i++)
	{
		Fresh->Versions[i] = Grid->Versions[i] + 1;
	}
	auto Sink = [&Fresh](const FVector4f& Record) { Fresh->Cells[CellOf(Record)].Add(Record); };

	int64 Actors = 0;
	for (AFGBuildable* Buildable : Buildables->GetAllBuildablesRef())
	{
		if (!Buildable || Buildable->GetIsLightweightTemporary() || Buildable->ManagedByLightweightBuildableSubsystem())
		{
			continue;
		}
		const int32 TypeId = TypeFor(Buildable->GetClass());
		FType& Type = Types[TypeId];
		EmitActor(Buildable, Type, TypeId, PaintFor(Buildable->GetCustomizationData_Native()), Sink);
		Type.Count++;
		Actors++;
	}

	int64 Instances = 0;
	for (const auto& Pair : Lightweights->GetAllLightweightBuildableInstances())
	{
		UClass* Class = Pair.Key.Get();
		if (!Class)
		{
			continue;
		}
		const int32 TypeId = TypeFor(Class);
		FType& Type = Types[TypeId];
		for (const FRuntimeBuildableInstanceData& Data : Pair.Value)
		{
			if (Data.IsValidOnLoad())
			{
				EmitLightweight(Data, Type, TypeId, PaintFor(Data.CustomizationData), Sink);
				Type.Count++;
				Instances++;
			}
		}
	}

	const int32 Stress = FMath::Max(0, CVarStress.GetValueOnGameThread());
	BuiltStress = Stress;
	if (Stress > 0)
	{
		FRandomStream Random(1234);
		const int32 FakeType = Types.Num() > 0 ? 0 : 1;
		for (int32 i = 0; i < Stress; i++)
		{
			FVector4f Record;
			const FVector Center(MapWestCm + Random.FRand() * MapWidthCm, MapNorthCm + Random.FRand() * MapHeightCm, Random.FRandRange(0.f, 40000.f));
			if (EncodeWorld(Center, 400.f, 400.f, Random.FRand() * UE_TWO_PI, FakeType, 0, Record))
			{
				Sink(Record);
			}
		}
	}

	BindBuildableDelegates(true);

	bScanned = true;
	bMenuDirty = true;
	LastScanSeconds = FPlatformTime::Seconds();
	UE_LOG(LogFactoryAtlas, Display, TEXT("Scanned %lld actors and %lld lightweight buildings (%d types, %d paints, %d stress) in %.2f ms"),
		Actors, Instances, Types.Num(), PaintColors.Num() - 1, Stress, (LastScanSeconds - Start) * 1000.0);

	LaunchJob(Fresh);
}

void UAtlasSubsystem::LaunchJob(TSharedPtr<FGrid> Replacement)
{
	check(!bJobActive);
	bJobActive = true;
	PendingGrid = Replacement;
	JobRunning = MakeShared<std::atomic<bool>>(true);

	const bool bReplace = Replacement.IsValid();
	TSharedPtr<FGrid> Target = bReplace ? Replacement : Grid;
	Job = UE::Tasks::Launch(UE_SOURCE_LOCATION,
		[Target, Ops = MoveTemp(PendingOps), bReplace, Running = JobRunning]() mutable
		{
			const double Start = FPlatformTime::Seconds();
			RunJob(*Target, Ops, bReplace);
			UE_LOG(LogFactoryAtlas, Display, TEXT("Worker: %d changes, %lld records, %.2f ms"), Ops.Num(), Target->Total, (FPlatformTime::Seconds() - Start) * 1000.0);
			Running->store(false);
		});
	PendingOps.Reset();
}

void UAtlasSubsystem::FinishJob()
{
	bJobActive = false;
	if (PendingGrid)
	{
		Grid = MoveTemp(PendingGrid);
		PendingGrid.Reset();
	}
	bMenuDirty = true;
	if (Hover.bValid)
	{
		ClearHover();
	}
}

void UAtlasSubsystem::Pump()
{
	if (bJobActive && !JobRunning->load())
	{
		FinishJob();
	}
	if (!bJobActive && !PendingOps.IsEmpty())
	{
		LaunchJob(nullptr);
	}
}

void UAtlasSubsystem::QueueActor(AFGBuildable* Buildable, bool bAdded)
{
	if (!bScanned || !Buildable || Buildable->GetIsLightweightTemporary() || Buildable->ManagedByLightweightBuildableSubsystem())
	{
		return;
	}

	const int32 TypeId = TypeFor(Buildable->GetClass());
	FType& Type = Types[TypeId];
	auto Sink = [this, bAdded](const FVector4f& Record) { PendingOps.Add({ Record, bAdded }); };
	EmitActor(Buildable, Type, TypeId, bAdded ? PaintFor(Buildable->GetCustomizationData_Native()) : 0, Sink);
	Type.Count = FMath::Max(0, Type.Count + (bAdded ? 1 : -1));
	bMenuDirty = true;

	if (PendingOps.Num() > MaxQueuedOps)
	{
		Pump();
	}
}

void UAtlasSubsystem::OnBuildableAdded(AFGBuildable* Buildable)
{
	QueueActor(Buildable, true);
}

void UAtlasSubsystem::OnBuildableRemoved(AFGBuildable* Buildable)
{
	QueueActor(Buildable, false);
}

void UAtlasSubsystem::HandleLightweight(UClass* Class, const FRuntimeBuildableInstanceData& Data, bool bAdded)
{
	if (!bScanned || !Class)
	{
		return;
	}

	const int32 TypeId = TypeFor(Class);
	FType& Type = Types[TypeId];
	auto Sink = [this, bAdded](const FVector4f& Record) { PendingOps.Add({ Record, bAdded }); };
	EmitLightweight(Data, Type, TypeId, bAdded ? PaintFor(Data.CustomizationData) : 0, Sink);
	Type.Count = FMath::Max(0, Type.Count + (bAdded ? 1 : -1));
	bMenuDirty = true;

	if (PendingOps.Num() > MaxQueuedOps)
	{
		Pump();
	}
}

void UAtlasSubsystem::SetCategoryHidden(ECategory Category, bool bHidden)
{
	HiddenCategories[int32(Category)] = bHidden;
	bPaletteDirty = true;
	SaveSettings();
}

void UAtlasSubsystem::SetTypeHidden(int32 Type, bool bHidden)
{
	if (!Types.IsValidIndex(Type) || !Types[Type].Class)
	{
		return;
	}
	Types[Type].bHidden = bHidden;
	const FString Path = Types[Type].Class->GetPathName();
	if (bHidden)
	{
		HiddenTypePaths.Add(Path);
	}
	else
	{
		HiddenTypePaths.Remove(Path);
	}
	bPaletteDirty = true;
	SaveSettings();
}

void UAtlasSubsystem::SetPaintMode(bool bEnabled)
{
	if (bPaintMode != bEnabled)
	{
		bPaintMode = bEnabled;
		SaveSettings();
	}
}

void UAtlasSubsystem::SetLayerEnabled(bool bEnabled)
{
	if (bLayerEnabled != bEnabled)
	{
		bLayerEnabled = bEnabled;
		SaveSettings();
	}
}

void UAtlasSubsystem::SetPanelOpen(bool bOpen)
{
	if (bPanelOpen != bOpen)
	{
		bPanelOpen = bOpen;
		SaveSettings();
	}
}

FVector2f UAtlasSubsystem::GetHeightBounds() const
{
	const float Low = ZToMetres(Grid->ZLow);
	const float High = ZToMetres(Grid->ZHigh);
	return FVector2f(Low, FMath::Max(High, Low + 1.f));
}

void UAtlasSubsystem::SetHeightFilter(float Low, float High)
{
	FilterLow = FMath::Min(Low, High);
	FilterHigh = FMath::Max(Low, High);
}

bool UAtlasSubsystem::ConsumeMenuDirty()
{
	const bool bWasDirty = bMenuDirty;
	bMenuDirty = false;
	return bWasDirty;
}

UTexture2D* UAtlasSubsystem::GetPalette()
{
	if (!Palette)
	{
		Palette = UTexture2D::CreateTransient(MaxTypes, 2, PF_B8G8R8A8);
		Palette->Filter = TF_Nearest;
		Palette->SRGB = false;
		Palette->NeverStream = true;
		Palette->CompressionSettings = TC_VectorDisplacementmap;
		Palette->UpdateResource();
		bPaletteDirty = true;
	}
	return Palette;
}

void UAtlasSubsystem::RebuildPalette()
{
	PaletteData.SetNumZeroed(MaxTypes * 2);
	for (int32 i = 0; i < Types.Num(); i++)
	{
		const FType& Type = Types[i];
		FColor Color = Type.Color;
		Color.A = (Type.bHidden || IsCategoryHidden(Type.Category)) ? 0 : 255;
		PaletteData[i] = Color;
	}
	PaletteData[HighlightType] = FColor(226, 142, 66);
	PaletteData[RadiationType] = FColor(120, 255, 60);
	for (int32 Marker = MarkerFirst; Marker <= MarkerLast; Marker++)
	{
		FColor Color = MarkerColor(Marker);
		Color.A = IsMarkerShown(Marker) ? 255 : 0;
		PaletteData[Marker] = Color;
	}
	for (int32 i = 0; i < ResourceKinds.Num(); i++)
	{
		FColor Color = ResourceKinds[i].Color;
		Color.A = IsMarkerShown(ResourceFirst + i) ? 255 : 0;
		PaletteData[ResourceFirst + i] = Color;
	}
	PaletteData[SporeFlowerType] = FColor(255, 110, 200, IsMarkerShown(SporeFlowerType) ? 255 : 0);
	PaletteData[GasPillarType] = FColor(150, 230, 110, IsMarkerShown(GasPillarType) ? 255 : 0);
	for (int32 i = 1; i < PaintColors.Num(); i++)
	{
		FColor Color = PaintColors[i];
		Color.A = 255;
		PaletteData[MaxTypes + i] = Color;
	}
}

void UAtlasSubsystem::FlushPalette()
{
	UTexture2D* Texture = GetPalette();
	if (!bPaletteDirty || !Texture)
	{
		return;
	}
	bPaletteDirty = false;
	RebuildPalette();

	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, MaxTypes, 2);
	const int32 Bytes = MaxTypes * 2 * sizeof(FColor);
	uint8* Copy = static_cast<uint8*>(FMemory::Malloc(Bytes));
	FMemory::Memcpy(Copy, PaletteData.GetData(), Bytes);
	Texture->UpdateTextureRegions(0, 1, Region, MaxTypes * sizeof(FColor), sizeof(FColor), Copy,
		[](uint8* Data, const FUpdateTextureRegion2D* UsedRegion)
		{
			FMemory::Free(Data);
			delete UsedRegion;
		});
}

void UAtlasSubsystem::SetPointerOverPanel(bool bOver)
{
	bPointerOverPanel = bOver;
	if (bOver)
	{
		ClearHover();
	}
}

void UAtlasSubsystem::ResetSettings()
{
	Settings = FSettings();
	SaveSettings();
}

void UAtlasSubsystem::UpdateHover(FVector2f Uv, float PixelsPerMetre)
{
	Hover.bValid = false;
	Hover.bMarker = false;
	if (bPointerOverPanel || Uv.X < 0.f || Uv.X >= 1.f || Uv.Y < 0.f || Uv.Y >= 1.f)
	{
		return;
	}

	const float Tolerance = 2.f / FMath::Max(PixelsPerMetre, 0.01f);
	const float WorldX = Uv.X * MapWidthMetres;
	const float WorldY = Uv.Y * MapWidthMetres;

	// Collectible markers are drawn on top, so they win. Their size is in pixels, not metres.
	if (AnyMarkersShown())
	{
		float BestDistance = TNumericLimits<float>::Max();
		for (const FVector4f& Packed : Markers)
		{
			const FDecoded Record(Packed);
			if (!IsMarkerShown(Record.Type))
			{
				continue;
			}
			// Markers grow with purity, so reach for the drawn size.
			const float MarkerReach = (Record.HalfX + 2.f) / FMath::Max(PixelsPerMetre, 0.0001f);
			const float ReachHere = FMath::Min(MarkerReach * MarkerReach, BestDistance);
			const float DX = float(Record.X) / PositionMax * MapWidthMetres - WorldX;
			const float DY = float(Record.Y) / PositionMax * MapWidthMetres - WorldY;
			const float Distance = DX * DX + DY * DY;
			if (Distance <= ReachHere)
			{
				BestDistance = Distance;
				Hover.bValid = true;
				Hover.bMarker = true;
				Hover.Type = Record.Type;
				Hover.Flags = Record.Paint;
				Hover.HeightMetres = ZToMetres(Record.Z);
				Hover.WorldMetres = FVector2f(
					float((MapWestCm + double(Record.X) / PositionMax * MapWidthCm) / 100.0),
					float((MapNorthCm + double(Record.Y) / PositionMax * MapHeightCm) / 100.0));
			}
		}
		if (Hover.bValid)
		{
			return;
		}
	}

	const FGrid* Current = GetGrid();
	if (!Current || !bLayerEnabled)
	{
		return;
	}
	const int32 Center = int32(Uv.X * GridSize);
	const int32 Middle = int32(Uv.Y * GridSize);
	const int32 ZLowLimit = MetresToZ(FilterLow);
	const int32 ZHighLimit = MetresToZ(FilterHigh);
	int32 BestZ = -1;

	for (int32 CellY = FMath::Max(0, Middle - 1); CellY <= FMath::Min(GridSize - 1, Middle + 1); CellY++)
	{
		for (int32 CellX = FMath::Max(0, Center - 1); CellX <= FMath::Min(GridSize - 1, Center + 1); CellX++)
		{
			const TArray<FVector4f>& Cell = Current->Cells[CellY * GridSize + CellX];
			for (int32 i = Cell.Num() - 1; i >= 0; i--)
			{
				const int32 Z = DecodeZ(Cell[i]);
				if (Z <= BestZ)
				{
					break; // Cells are sorted, nothing lower can win
				}
				if (Z < ZLowLimit || Z > ZHighLimit)
				{
					continue;
				}

				const FDecoded Record(Cell[i]);
				const float HalfX = Record.HalfX * 0.1f + Tolerance;
				const float HalfY = Record.HalfY * 0.1f + Tolerance;
				const float DX = float(Record.X) / PositionMax * MapWidthMetres - WorldX;
				const float DY = float(Record.Y) / PositionMax * MapWidthMetres - WorldY;
				const float Reach = HalfX + HalfY;
				if (DX * DX + DY * DY > Reach * Reach)
				{
					continue;
				}
				if (!Types.IsValidIndex(Record.Type) || Types[Record.Type].bHidden || IsCategoryHidden(Types[Record.Type].Category))
				{
					continue;
				}

				float Sin, Cos;
				FMath::SinCos(&Sin, &Cos, Record.Angle / 1024.f * UE_TWO_PI);
				if (FMath::Abs(DX * Cos + DY * Sin) <= HalfX && FMath::Abs(-DX * Sin + DY * Cos) <= HalfY)
				{
					BestZ = Z;
					Hover.bValid = true;
					Hover.Type = Record.Type;
					Hover.HeightMetres = ZToMetres(Z);
					Hover.WorldMetres = FVector2f(
						float((MapWestCm + double(Record.X) / PositionMax * MapWidthCm) / 100.0),
						float((MapNorthCm + double(Record.Y) / PositionMax * MapHeightCm) / 100.0));
					Hover.Record = Encode(Record.X, Record.Y, Z, Record.HalfX, Record.HalfY, Record.Angle, HighlightType, 0);
					Hover.Source = Cell[i];
					break;
				}
			}
		}
	}
}

void UAtlasSubsystem::StartExport()
{
	const FGrid* Current = GetGrid();
	if (!Current || (ExportState && ExportState->Stage.load() == 1))
	{
		return;
	}

	// The PNG encoder must be loaded on the game thread before a worker can use it.
	FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	FlushPalette();
	TSharedPtr<FGrid> Snapshot = MakeShared<FGrid>();
	for (int32 i = 0; i < GridCells; i++)
	{
		Snapshot->Cells[i] = Current->Cells[i];
	}

	TSharedPtr<FExportState> State = MakeShared<FExportState>();
	State->Stage.store(1);
	State->Path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / FString::Printf(TEXT("Atlas_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
	ExportState = State;

	UE::Tasks::Launch(UE_SOURCE_LOCATION,
		[Snapshot, State, Colors = PaletteData, bPaint = bPaintMode, Size = Settings.ExportSize, ZLow = MetresToZ(FilterLow), ZHigh = MetresToZ(FilterHigh)]()
		{
			const double PixelsPerMetre = double(Size) / MapWidthMetres;
			TArray64<FColor> Image;
			Image.Init(FColor::Transparent, int64(Size) * Size);

			for (const TArray<FVector4f>& Cell : Snapshot->Cells)
			{
				for (const FVector4f& Packed : Cell)
				{
					const FDecoded Record(Packed);
					FColor Color = Colors[Record.Type];
					if (Color.A == 0 || Record.Z < ZLow || Record.Z > ZHigh)
					{
						continue;
					}
					if (bPaint && Record.Paint > 0)
					{
						Color = Colors[MaxTypes + Record.Paint];
					}

					const float CenterX = float(Record.X) / PositionMax * Size;
					const float CenterY = float(Record.Y) / PositionMax * Size;
					const float HalfX = FMath::Max(float(Record.HalfX * 0.1 * PixelsPerMetre), 0.5f);
					const float HalfY = FMath::Max(float(Record.HalfY * 0.1 * PixelsPerMetre), 0.5f);
					float Sin, Cos;
					FMath::SinCos(&Sin, &Cos, Record.Angle / 1024.f * UE_TWO_PI);
					const float ExtentX = FMath::Abs(HalfX * Cos) + FMath::Abs(HalfY * Sin);
					const float ExtentY = FMath::Abs(HalfX * Sin) + FMath::Abs(HalfY * Cos);

					const int32 MinX = FMath::Max(0, FMath::FloorToInt32(CenterX - ExtentX));
					const int32 MaxX = FMath::Min(Size - 1, FMath::CeilToInt32(CenterX + ExtentX));
					const int32 MinY = FMath::Max(0, FMath::FloorToInt32(CenterY - ExtentY));
					const int32 MaxY = FMath::Min(Size - 1, FMath::CeilToInt32(CenterY + ExtentY));
					for (int32 Y = MinY; Y <= MaxY; Y++)
					{
						for (int32 X = MinX; X <= MaxX; X++)
						{
							const float DX = X + 0.5f - CenterX;
							const float DY = Y + 0.5f - CenterY;
							if (FMath::Abs(DX * Cos + DY * Sin) <= HalfX && FMath::Abs(-DX * Sin + DY * Cos) <= HalfY)
							{
								Image[int64(Y) * Size + X] = Color;
							}
						}
					}
				}
			}

			TArray64<uint8> Compressed;
			FImageUtils::PNGCompressImageArray(Size, Size, TArrayView64<const FColor>(Image.GetData(), Image.Num()), Compressed);
			const bool bSaved = Compressed.Num() > 0 && FFileHelper::SaveArrayToFile(Compressed, *State->Path);
			UE_LOG(LogFactoryAtlas, Display, TEXT("Export %s: %s"), bSaved ? TEXT("saved") : TEXT("failed"), *State->Path);
			State->Stage.store(bSaved ? 2 : 3);
		});
}

FText UAtlasSubsystem::GetExportStatus() const
{
	if (!ExportState)
	{
		return FText::GetEmpty();
	}
	switch (ExportState->Stage.load())
	{
	case 1:
		return FText::FromString(TEXT("Exporting..."));
	case 2:
		return FText::FromString(FString::Printf(TEXT("Saved to %s"), *FPaths::GetCleanFilename(ExportState->Path)));
	case 3:
		return FText::FromString(TEXT("Export failed"));
	default:
		return FText::GetEmpty();
	}
}

void UAtlasSubsystem::SaveSettings() const
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("paint=%d"), bPaintMode ? 1 : 0));
	Lines.Add(FString::Printf(TEXT("layer=%d"), bLayerEnabled ? 1 : 0));
	Lines.Add(FString::Printf(TEXT("panel=%d"), bPanelOpen ? 1 : 0));
	Lines.Add(FString::Printf(TEXT("hoverLabel=%d"), Settings.bHoverLabel ? 1 : 0));
	Lines.Add(FString::Printf(TEXT("hoverHeight=%d"), Settings.bHoverHeight ? 1 : 0));
	Lines.Add(FString::Printf(TEXT("hoverCoordinates=%d"), Settings.bHoverCoordinates ? 1 : 0));
	Lines.Add(FString::Printf(TEXT("highlight=%d"), Settings.bHighlight ? 1 : 0));
	Lines.Add(FString::Printf(TEXT("outlines=%d"), Settings.bOutlines ? 1 : 0));
	Lines.Add(FString::Printf(TEXT("opacity=%.2f"), Settings.Opacity));
	Lines.Add(FString::Printf(TEXT("exportSize=%d"), Settings.ExportSize));
	Lines.Add(FString::Printf(TEXT("radiation=%d"), Settings.bRadiation ? 1 : 0));
	Lines.Add(FString::Printf(TEXT("zoom=%d"), Settings.ZoomFactor));
	Lines.Add(FString::Printf(TEXT("collectibles=%u"), CollectibleMask));
	Lines.Add(FString::Printf(TEXT("features=%u"), FeatureMask));
	for (const FString& Path : HiddenResourcePaths)
	{
		Lines.Add(FString::Printf(TEXT("hiddenResource=%s"), *Path));
	}
	for (int32 i = 0; i < int32(ECategory::Count); i++)
	{
		if (CategoryColorOverrides[i].IsSet())
		{
			Lines.Add(FString::Printf(TEXT("categoryColor=%s|%s"), CategoryName(ECategory(i)), *CategoryColorOverrides[i].GetValue().ToHex()));
		}
	}
	for (const TPair<FString, FColor>& Pair : TypeColorOverrides)
	{
		Lines.Add(FString::Printf(TEXT("typeColor=%s|%s"), *Pair.Key, *Pair.Value.ToHex()));
	}
	for (int32 i = 0; i < int32(ECategory::Count); i++)
	{
		if (HiddenCategories[i])
		{
			Lines.Add(FString::Printf(TEXT("hiddenCategory=%s"), CategoryName(ECategory(i))));
		}
	}
	for (const FString& Path : HiddenTypePaths)
	{
		Lines.Add(FString::Printf(TEXT("hiddenType=%s"), *Path));
	}
	FFileHelper::SaveStringArrayToFile(Lines, *SettingsPath());
}

void UAtlasSubsystem::LoadSettings()
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *SettingsPath()))
	{
		return;
	}

	for (const FString& Line : Lines)
	{
		FString Key, Value;
		if (!Line.Split(TEXT("="), &Key, &Value))
		{
			continue;
		}
		if (Key == TEXT("paint")) { bPaintMode = Value == TEXT("1"); }
		else if (Key == TEXT("layer")) { bLayerEnabled = Value == TEXT("1"); }
		else if (Key == TEXT("panel")) { bPanelOpen = Value == TEXT("1"); }
		else if (Key == TEXT("hoverLabel")) { Settings.bHoverLabel = Value == TEXT("1"); }
		else if (Key == TEXT("hoverHeight")) { Settings.bHoverHeight = Value == TEXT("1"); }
		else if (Key == TEXT("hoverCoordinates")) { Settings.bHoverCoordinates = Value == TEXT("1"); }
		else if (Key == TEXT("highlight")) { Settings.bHighlight = Value == TEXT("1"); }
		else if (Key == TEXT("outlines")) { Settings.bOutlines = Value == TEXT("1"); }
		else if (Key == TEXT("opacity")) { Settings.Opacity = FMath::Clamp(FCString::Atof(*Value), 0.2f, 1.f); }
		else if (Key == TEXT("exportSize")) { Settings.ExportSize = FCString::Atoi(*Value) == 4096 ? 4096 : 8192; }
		else if (Key == TEXT("radiation")) { Settings.bRadiation = Value == TEXT("1"); }
		else if (Key == TEXT("collectibles")) { CollectibleMask = uint32(FCString::Atoi(*Value)) & ((1u << int32(ECollectible::Count)) - 1); }
		else if (Key == TEXT("features")) { FeatureMask = uint32(FCString::Atoi(*Value)) & ((1u << int32(EFeature::Count)) - 1); }
		else if (Key == TEXT("hiddenResource")) { HiddenResourcePaths.Add(Value); }
		else if (Key == TEXT("zoom")) { Settings.ZoomFactor = FMath::Clamp(FCString::Atoi(*Value), 1, 8); }
		else if (Key == TEXT("hiddenType")) { HiddenTypePaths.Add(Value); }
		else if (Key == TEXT("typeColor") || Key == TEXT("categoryColor"))
		{
			FString Name, Hex;
			if (!Value.Split(TEXT("|"), &Name, &Hex))
			{
				continue;
			}
			const FColor Color = FColor::FromHex(Hex);
			if (Key == TEXT("typeColor"))
			{
				TypeColorOverrides.Add(Name, Color);
			}
			else
			{
				for (int32 i = 0; i < int32(ECategory::Count); i++)
				{
					if (Name == CategoryName(ECategory(i)))
					{
						CategoryColorOverrides[i] = Color;
					}
				}
			}
		}
		else if (Key == TEXT("hiddenCategory"))
		{
			for (int32 i = 0; i < int32(ECategory::Count); i++)
			{
				if (Value == CategoryName(ECategory(i)))
				{
					HiddenCategories[i] = true;
				}
			}
		}
	}
}
