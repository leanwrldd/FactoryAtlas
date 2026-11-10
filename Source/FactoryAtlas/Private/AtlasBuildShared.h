#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class AActor;
class UFGConstructDisqualifier;

// Helpers for building from the map, defined in AtlasBelts.cpp and shared with the power wires in AtlasWires.cpp.
namespace AtlasBeltDetail
{
	/** Clearance overlaps are what "allow clipping" lets through. */
	bool IsClippingDisqualifier(const TSubclassOf<UFGConstructDisqualifier>& Disqualifier);
	/** The build gun's own trace channel. False when the game has none by that name. */
	bool FindBuildGunChannel(ECollisionChannel& Out);
	/** The building a hit belongs to, past the abstract instance manager most buildings collide through. */
	AActor* HitOwner(const FHitResult& Hit);
	/** Names the building a hit belongs to, so a hologram can snap to it. */
	void ResolveForPlacement(FHitResult& Hit);
}
