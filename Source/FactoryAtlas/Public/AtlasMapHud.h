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

	/**
	 * Grows the map over the whole map screen, hiding the game's window frame and filter list, or puts them back.
	 * Called every frame; does nothing unless the wanted state changed.
	 */
	void UpdateFullscreen(bool bWanted);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TSharedPtr<SAtlasHud> Hud;
	TSharedPtr<IInputProcessor> ClickProcessor;

	void ApplyFullscreen();
	void RestoreFullscreen();

	bool bFullscreenApplied = false;
	/** Puts back each slot and visibility fullscreen changed, run in reverse order. */
	TArray<TFunction<void()>> FullscreenUndo;
	/** What fullscreen hid, kept hidden even if the game's own widget shows it again. */
	TArray<TWeakObjectPtr<UWidget>> FullscreenHidden;

	/** The map and every widget holding it, up to the top, for the layout log. */
	TArray<TWeakObjectPtr<UWidget>> FullscreenChain;
	/** Frames until the layout after fullscreen is logged, once it has been arranged. */
	int32 FullscreenLogFrames = 0;
	void LogFullscreenChain(const TCHAR* When) const;
};
