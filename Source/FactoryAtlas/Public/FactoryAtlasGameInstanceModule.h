#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "FactoryAtlasGameInstanceModule.generated.h"

class UWidgetBlueprintHookData;

/// Adds the building layer to the map widget's tree. No native hooks: SML's widget hook does the work.
UCLASS()
class FACTORYATLAS_API UFactoryAtlasGameInstanceModule : public UGameInstanceModule
{
	GENERATED_BODY()
public:
	UFactoryAtlasGameInstanceModule();

	virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UWidgetBlueprintHookData> MapHook;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetBlueprintHookData> HudHook;
};
