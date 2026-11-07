#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AtlasMapHud.generated.h"

class SAtlasHud;
class IInputProcessor;

/** Hosts the layer menu and the hover label. Sits in the map screen's canvas, outside the map's zoom. */
UCLASS(NotBlueprintable)
class FACTORYATLAS_API UAtlasMapHud : public UUserWidget
{
	GENERATED_BODY()
public:
	UAtlasMapHud(const FObjectInitializer& ObjectInitializer);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TSharedPtr<SAtlasHud> Hud;
	TSharedPtr<IInputProcessor> ClickProcessor;
};
