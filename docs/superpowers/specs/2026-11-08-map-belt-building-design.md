# Building belts from the Atlas map

Date: 2026-11-08. Status: design, not started.

## Intent

Wire up an existing factory from the Atlas map instead of walking over with the build gun. Each building's free
conveyor ports show on the map as nodes. Dragging from one port to another builds a belt between them, exactly as
the build gun would have built it: same routing, same rules, same cost.

The point is convenience at a distance, not a cheat. The map must never build anything the build gun would
refuse, and it must charge what the build gun would charge.

## Goals

- Free conveyor ports of existing buildings drawn as nodes on the map, telling inputs from outputs.
- Drag from a port to a port of the opposite kind to build one belt between them.
- A live preview of the belt while dragging, snapping onto the port under the cursor.
- The preview turns red when the game would refuse the belt, with the game's own reason by the cursor. Releasing
  on a red preview does nothing.
- The game's belt build modes (at least default, straight and curved) can be chosen.
- Belt tier: the highest unlocked Mk by default, with a picker for lower unlocked tiers.
- Paid like the build gun: player inventory first, then the Dimensional Depot.
- Works in both the in-game map and the separate map window (`Atlas::EView::Game` and `EView::Window`).

## Non-goals

- Pipes, lifts, power lines, or any other spline.
- A belt that ends in open ground, or starting from open ground. Both ends are always existing free ports.
- Multiplayer clients. Single player (and therefore a host) only; Atlas stays client-only
  (`"RequiredOnRemote": false`) and gets no server-side piece.
- Routing around obstacles, chaining several belts, or placing poles automatically beyond what one belt hologram
  does by itself.
- Ports on buildings that don't exist yet.

## Anti-goals (would make this a failure even if it works)

- The map showing a belt as valid that the game then refuses, or the reverse. Red must mean exactly what the build
  gun would say.
- Building without paying, or paying without building.
- A save left with a broken belt: one end unattached, overlapping, or not registered with the buildable subsystem.
- Any cost while not dragging. Atlas's promise is near-zero cost to the game; port nodes and holograms must cost
  nothing unless the map is zoomed in, and the hologram exists only during a drag.
- Moving the player's camera, aim, or build gun state, or swapping what's in the build gun.

## Constraints

- Satisfactory 1.2, SML 3.12, UE 5.6. Atlas source: `Source/FactoryAtlas/`.
- Atlas today knows buildings only as packed draw records (position, half size, angle, height, type, paint) in
  `UAtlasSubsystem`'s grid. It knows nothing about ports.
- Map drawing goes through `FAtlasMapPainter` (`Private/AtlasMapPainter.cpp`), used by both views. Hover, the
  pointer-over-panel flag and the selection anchor are kept per view (`EView`). Clicks on the map reach the HUD
  through `FAtlasClickProcessor` (`Private/AtlasMapHud.cpp`), an input preprocessor that sees every click before
  the map widget does.
- Paint geometry is relative to the window being painted, while the cursor is in desktop pixels. Anything that
  converts the cursor to map coordinates must use desktop-space geometry (`GetTickSpaceGeometry()`), as the map
  window's hover does. See the comment in `AtlasMapWindow.cpp`'s `UpdateHover`.
- In the in-game map, the game's own map widget pans on a left drag and opens its marker menu on right click. A
  port drag must swallow its press, moves and release so the map neither pans nor opens anything. The click
  processor already does this for Ctrl-clicks.
- Far parts of the world may not be loaded: landscape streams in around the player. Buildings are always present.

## Prior art (the user's own code)

`C:\SF\SmartMCP\mod\Source\SmartMCP\Private\Diagnostics\SmartMCPBuild.cpp` builds belts remotely through the game's
hologram, without the build gun or camera. It is the reference for this feature. What it establishes:

- Spawn: `AFGHologram::SpawnHologramFromRecipe(Recipe, Player->GetBuildGun(), Start, Player, PreSpawn)`.
- Build mode: `GetSupportedBuildModes`, `GetDefaultBuildGunMode`, `SetBuildModeOverride`.
- Placement: a line trace straight down on the build gun's collision channel (`FindBuildGunChannel`), checked
  with `IsValidHitResult`, then `UpdateHologramPlacement(Hit)`. Disqualifiers must be reset first
  (`ResetConstructDisqualifiers`), and the hologram must be unhidden or validation adds `InvalidAimLocation`.
- Belts are spline holograms with steps (`ESplineHologramBuildStep`): place the start, an adjust-pole step that
  must not be re-placed (it would raise the pole), then place the end. `DoMultiStepPlacement(false)` advances a
  step. Children (the start pole) need a couple of frames to settle (`SettleFrames = 2`).
- Validation: show active children, reset their disqualifiers, `ValidatePlacementAndCost(BuildGunInventory)`, then
  `CanConstruct()` and `GetConstructDisqualifiers()`.
- Construct: `Live->Construct(Children, AFGBuildableSubsystem::GetNewNetConstructionID())`. Construct does not
  charge; SmartMCP's `Pay()` then removes `GetCost(true)` from the inventory and then the Dimensional Depot
  (`AFGCentralStorageSubsystem::TryRemoveItemsFromCentralStorage`), honouring `GetNoBuildCost()`.

## Approaches considered

**A. Drive the game's real belt hologram behind the map. Chosen.**
During a drag Atlas spawns the game's belt hologram, snaps its ends to the two ports, reads the game's verdict every
frame, and constructs through the hologram on release. Red always matches the build gun; routing, build modes,
tiers, clearance and cost come from the game. SmartMCP already proves the remote part works. Cost: holograms assume
a build gun, so there are quirks to handle, and game updates touching holograms may need fixes.

**B. Atlas computes the belt itself.** Own spline maths, own slope/length/clearance rules, spawn the belt buildable
directly and charge by hand. Full control and a cheap preview, but the map and the game would disagree at the edges,
which breaks the anti-goal above, and cost, undo and save integrity become ours to get right. Rejected.

**C. Drive the player's actual build gun by faking aim.** Rejected outright: it moves the camera and character.

The user picked A.

## Design, as decided

### Ports on the map

- A port is a `UFGFactoryConnectionComponent` on an actor buildable that is not connected (`IsConnected()` false)
  and is a conveyor connection (not a pipe, not hidden/internal). Lightweight buildables have no ports.
- Ports are read only for buildings on screen, and only once the map is zoomed in past a threshold. Proposed: the
  recipe-icon threshold, 1.5 px per metre (`IconMinPixelsPerMetre` in `AtlasMapPainter.cpp`). Below it, nothing
  about ports runs.
- Each node is drawn at the port's map position, with input and output told apart by colour or an arrow showing
  flow direction. Exact look is the builder's call; it must stay readable on top of the building colours and
  terrain.
- *Designer's call:* the free end of an existing belt or lift counts as a port too. It is the same kind of
  connection component, the belt hologram snaps to it the same way, and leaving it out would make a half-built
  line impossible to finish from the map.
- *Designer's call:* nodes follow the map's visibility. A building hidden by the layer list or outside the height
  filter shows no nodes, so you can't drag from something you can't see.

### The drag

1. Press on a free port node starts a port drag (left button, no modifier). That press, every move and the release
   are swallowed, so neither the game's map nor the window's map pans. *Designer's call:* a press that is released
   without moving (under the 5 px click threshold the click processor already uses) is an ordinary click on the
   building, so it still opens its settings panel.
2. While dragging, only free ports of the opposite kind are valid targets. When the cursor is over one, the
   preview snaps to it; otherwise there is no belt preview, only a line from the start port to the cursor, and
   releasing there cancels.
3. The preview is the hologram's real spline, projected onto the map and drawn by the painter. It is drawn in a
   valid colour, or red with the first disqualifier's text by the cursor.
4. Releasing on a valid target constructs. Releasing anywhere else, Escape, or the map closing cancels, and the
   hologram is destroyed.
5. Build mode and tier can be changed during the drag and the preview updates.

### Building

- Recipe: the conveyor belt recipe for the chosen Mk, from the unlocked recipes.
- The start is placed at the start port, the end at the target port, following SmartMCP's step sequence. Both ends
  must snap to the ports themselves; a belt that lands near a port but not on it counts as invalid.
- Payment as SmartMCP's `Pay()`. Validation has already refused anything the player can't afford.
- Single player only. If the world is a multiplayer session where this player lacks authority, port nodes don't
  show at all.

### Controls

- Build mode: cycled with the same input the build gun uses for build modes, plus a small on-map switch.
- Tier: a picker on the map, defaulting to the highest unlocked Mk, remembered in Atlas settings.
- Both are shown near the cursor or in the shortcut bar during a drag.

## Open, and how to settle them

These need a spike before the plan is final, because the answers change how the drag is built.

1. **Snapping to a port by trace.** SmartMCP traces straight down at a point. A port is on a building's side at
   mid-height, so a downward trace at its XY hits the roof. Does the belt hologram still snap to the nearest port
   of the hit building, or must the hologram be snapped directly to the connection component? Check
   `AFGConveyorBeltHologram`/`AFGSplineHologram` snapping (e.g. `TrySnapToActor`, `SnapToConnection`-style calls)
   in the 1.2 headers.
2. **Frame cost and settling.** SmartMCP waits `SettleFrames` between steps. A live preview must re-place the end
   every frame while dragging. Measure what re-placing and validating a belt hologram per frame costs, and whether
   the start pole's adjust step can be done once at drag start.
3. **Undo.** Does a hologram constructed this way join the game's Ctrl+Z history? If not, decide whether to
   register it with the undo subsystem or to accept no undo and say so.
4. **Far away.** Clearance against unloaded landscape can't be checked. Decide whether to refuse drags whose belt
   runs through unloaded areas, or accept that risk.
5. **Hologram visibility in the world.** The hologram must be unhidden to validate. Confirm it is invisible or
   harmless to the player when it is far away or behind them, and that it never shows up under their own build gun.

## Left to the builder

- Node and preview look: colours, sizes, arrow style, the exact threshold, how red the red is.
- Where the build mode switch and tier picker sit, as long as both are reachable without leaving the map.
- How to find unlocked belt recipes and the highest Mk.
- The internal split between the subsystem, the painter, the HUD and a new port/drag class.
- Logging, in the style Atlas already uses.
