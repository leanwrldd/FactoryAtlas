#pragma once

#include "CoreMinimal.h"

struct FSlateBrush;

/**
 * A second OS window that shows the Atlas map on its own, for a second monitor.
 * Prototype: buildings, collectible and world markers, radiation and the player, with pan and zoom.
 * Nothing in it can be clicked yet.
 */
namespace Atlas::MapWindow
{
	bool IsOpen();
	void Open();
	void Close();
	void Toggle();

	/** The game's own map image, borrowed from Widget_Map the first time the in-game map opens. */
	void SetTerrainBrush(const FSlateBrush& Brush);
}
