#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "AtlasModConfig.generated.h"

namespace Atlas { struct FSettings; }

/// The in-game mod settings (Mods > Atlas). Built entirely in C++, no Blueprint asset.
/// Holds the display toggles; everything else on the map's Settings tab stays there.
UCLASS()
class FACTORYATLAS_API UAtlasModConfig : public UModConfiguration
{
	GENERATED_BODY()
public:
	UAtlasModConfig(const FObjectInitializer& ObjectInitializer);

	/// Copies the config's values into Settings. Returns false, leaving Settings alone, if the config can't be read.
	static bool Read(const UObject* WorldContext, Atlas::FSettings& Settings);
};
