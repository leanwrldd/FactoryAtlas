# Status lights and the measure tool

Date: 2026-11-10. Status: built, shipped in 0.4.0. Open items below still need checking in game.

Two independent features for Atlas, written up together because they came out of one conversation. Either can be
built first. Decisions the user made are stated plainly; anything marked **(my call)** was filled in by Claude and
can be changed.

---

## Part 1: Status lights

### Intent

A small light on each machine on the map showing what the machine is doing, like the indicator light on the real
building. The point is to spot a broken or stalled machine from the map without walking over to it, and to keep
that close to free, which is Atlas's whole reason for existing.

### Goals

- A dot inside each eligible building's bottom-right corner, turning with the building (the user picked placement
  B from the mockups).
- One color per game status. Colors: producing green, producing while overclocked with power shards purple, standby
  yellow, error red. (The game's `IS_PRODUCING_WITH_CRYSTAL` is power shards in the potential slots, not Somersloops.) To be checked in game before they are final (see Open item 1).
- The dot is a fixed size on screen at any zoom, about the size of the resource node dots.
- Zoomed out far enough that a building is smaller on screen than its dot, producing lights (green and purple) hide
  and only problem lights stay. The user agreed to this.
- A dot follows its building: if a layer or the height filter hides the building, its dot is hidden too.
- One on/off switch, **off by default** so the current look is unchanged. It lives in the mod settings menu
  (Mods > Atlas > Map, next to recipe icons), where the other display toggles are.
- Works in both the game map and the separate map window.

### Non-goals

- Telling "starved of input" apart from "output blocked". The game's own status has no such split; Atlas shows what
  the game shows.
- Lights on buildings the game gives no status light: foundations, walls, belts, splitters, mergers and so on.
- Belt load, power circuit view, or anything else from the earlier brainstorm.
- Any change to the game's own buildings or their lights.

### Anti-goals

- A frame-time cost that grows with the number of machines on screen. Zoomed out over a big save is exactly where
  the lights matter most.
- Polling every machine on a timer.
- A dot that disagrees with the light on the real machine.

### Constraints found in the code

- `AFGBuildable::mOnProductionStatusChanged` (FGBuildable.h:841) is public, and the header says it is "called locally
  on both server and client". `GetProductionIndicatorStatus()` is virtual on `AFGBuildableFactory` and overridden by
  generators, pumps, train stations, docking stations, power storage and jump pads.
  `AFGBuildableFactory::GetCachedProductionState()` reads the cached value.
- `EProductionStatus` has `IS_NONE, IS_PRODUCING, IS_PRODUCING_WITH_CRYSTAL, IS_STANDBY, IS_ERROR`. The headers do not
  say which real situations (unpowered, paused, no input, output full) map to standby versus error.
- Every 16-byte building record is fully packed (AtlasTypes.h). There are no spare bits for status.
- The map material treats types 981 to 1017 as pixel-sized markers that ignore the height filter. Status dots must
  NOT be plain markers, or they would ignore the height filter and layer hiding.
- `AFGBuildableConveyorAttachment` (splitters, mergers) derives from `AFGBuildableFactory`, so "is a factory" is not
  the eligibility test on its own.

### Approaches considered

- **A. A status grid alongside the building grid (chosen by the user).** A second 32 × 32 grid of records, one per
  eligible machine, with per-cell versions like `FGrid`. A status change rewrites one record and re-uploads one
  cell. Zoom-dependent hiding is done in the shader, so zooming costs nothing.
- B. Add dots to the existing marker list. Least code, but the list is rebuilt every 5 s and re-uploaded whole, so
  status lags and a big save re-uploads thousands of dots with every rebuild. Rejected.
- C. Draw dots from the CPU each frame like the recipe icons. Cost grows with machines on screen and is worst zoomed
  out. Rejected, since it works against "almost free".

### How it works (decided: approach A; details below are my calls)

- **Record (my call):** a status record is a copy of the building's own record with the 8 paint bits replaced by the
  status code (1 producing, 2 overclocked, 3 standby, 4 error; a machine reporting `IS_NONE` gets no record). Position, size, angle, height and type all come along for free, so the
  shader can place the dot at the building's corner, apply the height filter, and apply layer visibility through
  the type's palette alpha, with no new bits needed. Removal finds the record with `SameBuilding()`, which already
  ignores paint bits.
- **Draw (my call):** the status grid is drawn with a second dynamic instance of the existing map material that has
  a new scalar parameter `StatusMode = 1`. In that mode the shader:
  - puts a dot of fixed pixel radius at local (+halfX, +halfY) inset by the dot's radius, rotated with the building
    (inside the corner, per placement B);
  - colors it from a small fixed status palette, not from the paint palette, and ignores paint mode;
  - keeps the normal `ZMin`/`ZMax` test and the type's palette alpha, so the dot follows its building;
  - drops codes 1 and 2 (producing, producing overclocked) when `min(halfX, halfY)` on screen is smaller
    than the dot's radius;
  - is drawn above buildings and below the hover highlight and selection.
  `Scripts/create_assets.py` gains the parameter and the branch, and the material asset is regenerated.
- **Eligibility (my call, pending Open item 1):** a building gets a light if its class is one whose status the game
  reports (`AFGBuildableFactory` and the overriding classes above), excluding conveyor attachments and anything that
  only ever reports `IS_NONE`. A status of `IS_NONE` writes no dot.
- **Updates (my call):** bind `mOnProductionStatusChanged` per eligible building when it is added (scan or
  construct) and unbind on removal. The handler queues an op in constant time; ops are applied the same way building
  ops are now. A cell re-uploads at most every 0.25 s, so a machine flickering at the end of a cycle cannot cause a
  storm. The CPU-side status grid is kept current while the map is closed (events are rare and cheap); GPU buffers
  are created and freed with the rest of the map, as now.
- **Hover (my call, small):** the hover label gains a line with the status in words ("Standby") for eligible
  buildings.
- **Setting:** `FSettings::bStatusLights`, default false, read from the mod settings menu like `bRecipeIcons`.
- **Safety (added while building):** the status event is a plain C++ member of `AFGBuildable`, reached by header
  offset. Before binding anything, Atlas checks that the reflected properties on either side of it
  (`mDecoratorClass`, `mContainsComponents`) sit exactly where the headers say. If not, the lights stay off with a
  log warning instead of writing into the wrong memory. The initial status is read through the reflected
  `GetProductionIndicatorStatus` UFUNCTION for the same reason.

### Open items to settle in game before finishing

1. **Which situations give which status.** Add a temporary console command (for example `atlas.StatusDump`) that
   logs counts of each status per building class. Check in a save with an unpowered machine, a paused one, one with
   no input, and one with a full output. Then fix the color map and the eligibility list. The game's own physical
   lights are the reference: the dot must match them.
2. Whether clients receive status changes for distant machines. The header says the delegate fires on clients, but
   clients only have the buildings the game has sent them. Single player and host are the target; clients are
   best effort, same as the rest of Atlas.

---

## Part 2: Measure tool

### Intent

Measure a build that already exists on the map, so the user knows how much ground it takes and where copies of it
would fit when they scale it up. Not for blueprints sitting unplaced in the library (the user confirmed this).

### Goals

- **Box:** drag a rectangle on the map. Its corners snap to the foundation grid. The label shows width × length in
  metres and in foundations, for example "48 × 32 m, 6 × 4 foundations".
- **Rotated builds:** the grid comes from the foundation under the point where the drag starts, so a build at an
  angle still measures to clean foundation counts and the box turns to match.
- **Copies:** with a box drawn, set a count and a direction (left, right, up, down relative to the box), and outline
  ghosts tile out edge to edge. A ghost that would land on existing buildings turns red.
- **Distance:** a straight-line measurement for quick checks.
- **Height:** each box shows the height range of the buildings inside it.
- Measurements stay on the map until cleared or until the game closes. They are not saved.
- Works on both the game map and the separate map window. Single player and host, like the rest of Atlas.

### Non-goals

- Reading sizes from blueprint files in the library.
- Building anything, or placing real holograms of copies.
- Area for shapes other than rectangles, or a path that follows a route.
- Saving measurements with the save file.
- A gap between copies.

### Anti-goals

- A red ghost when nothing is really in the way (a false alarm from a building touching the shared edge), or a
  clear ghost over a real obstruction.
- Any per-frame cost when no measurement is on the map.

### Approaches considered

There is one sensible way: draw the boxes, ghosts and labels as ordinary Slate lines, boxes and text in the map HUD
(`AtlasMapHud.cpp`), next to the shortcut bar and hover highlight. A handful of shapes does not need the instanced GPU
path. The overlap check runs on the CPU only when a ghost changes, against the building grid cells under it.

### How it works (my calls unless stated)

- **Entry:** a Measure button at the start of the shortcut bar, and the G key. While
  measuring, the rest of the bar shows the measure controls instead of the usual shortcuts.
  Measure mode stays on until toggled off or Esc. While it is on, a left drag measures instead of panning, selecting
  or starting a belt from a port. Panning must still be possible in measure mode: the builder checks which pan inputs
  the map has besides left drag, and if there are none, holding Space while dragging pans. (Built: Space + drag pans.)
- **Box versus line:** a drag draws a box; holding Shift while dragging measures a straight line instead. A line
  shows its length in metres and in foundations, and does not snap.
- **Grid source:** use the hover lookup (`UpdateHover`) at the drag start. If the record under it is in the
  Foundations category, the grid is that record's angle with an 8 m pitch, aligned to its corner. If not, the box is
  axis-aligned, snaps to 1 m, and the foundation count is shown with one decimal ("6.5 × 4 foundations").
- **Height line:** lowest to highest building record whose centre is inside the box, plus floors counted as distinct foundation
  heights inside the box (rounded to the nearest metre): "3 floors, 104 to 116 m".
- **Several boxes:** allowed. The last drawn box is the active one; the copies controls apply to it. A small panel
  near the active box holds count (1 to 16), direction, and a clear button. Clear all is on the shortcut bar.
- **Overlap rule:** a ghost is red when any building record that is visible right now (layers and height filter)
  overlaps it by more than 0.5 m. Hiding a layer, for example belts, therefore lets the user ignore it. A belt
  leaving the original build and crossing into a ghost does count, since it really is in the way. The test is an oriented rectangle overlap against records
  in the grid cells the ghost covers. The overlapping buildings are drawn with the highlight color while the ghost is
  red, so the user can see what is in the way.
- **Both views:** each `EView` keeps its own measurements, the same way hover and selection are kept per view.

---

## Left to the builder

- Exact dot radius, inset, outline and status colors, within "about the size of resource node dots" and pending
  Open item 1.
- Data layout of the status grid, and whether it shares the building worker job or has its own.
- The measure key, panel layout and label styling, matching the existing HUD.
- Whether to build Part 1 or Part 2 first.
- Version bump and README feature lines when each part ships.
