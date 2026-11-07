#include "FactoryAtlas.h"

#include "AtlasSubsystem.h"
#include "FGLightweightBuildableSubsystem.h"
#include "Modules/ModuleManager.h"
#include "Patching/NativeHookManager.h"

DEFINE_LOG_CATEGORY(LogFactoryAtlas);

class FFactoryAtlasModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Foundations, walls and the like are lightweight instances, which raise no delegates.
		// FactoryGame's functions are link stubs in editor builds and cannot be patched there.
#if !WITH_EDITOR
		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGLightweightBuildableSubsystem, AddFromBuildableInstanceData,
			[](int32 ReturnValue, AFGLightweightBuildableSubsystem* Self, TSubclassOf<AFGBuildable> BuildableClass, FRuntimeBuildableInstanceData& Data,
				bool bFromSaveData, int32 SaveDataIndex, uint16 ConstructId, AActor* Instigator, int32 BlueprintBuildEffectIndex)
			{
				if (UAtlasSubsystem* Atlas = UAtlasSubsystem::GetActive())
				{
					Atlas->HandleLightweight(BuildableClass.Get(), Data, true);
				}
			});

		SUBSCRIBE_UOBJECT_METHOD(AFGLightweightBuildableSubsystem, InvalidateRuntimeInstanceDataForIndex,
			[](auto& Scope, AFGLightweightBuildableSubsystem* Self, TSubclassOf<AFGBuildable> BuildableClass, int32 Index)
			{
				UAtlasSubsystem* Atlas = UAtlasSubsystem::GetActive();
				if (Atlas && Atlas->IsScanned())
				{
					if (const FRuntimeBuildableInstanceData* Data = Self->GetRuntimeDataForBuildableClassAndIndex(BuildableClass, Index))
					{
						Atlas->HandleLightweight(BuildableClass.Get(), *Data, false);
					}
				}
			});
#endif
	}
};

IMPLEMENT_GAME_MODULE(FFactoryAtlasModule, FactoryAtlas);
