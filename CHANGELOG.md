# Changelog

## 0.4.0

**Building from the map**
- Power wires: press Alt + W or click the Wire tile, then click one building and another to run a power line
  between them. Click the ground to place a power pole there. Each wire starts the next from where it ended, so a
  run of clicks chains buildings together. Right click drops the wire, Esc stops.
- Belts, pipes, hypertubes and railway: drag from a free port to another port, or to the ground, to build a line
  between them. R cycles the build mode, T the tier (Mk).
- Everything built from the map goes through the game's own holograms, so length limits, free connections and the
  cost are the build gun's. The cost comes from your inventory, then the Dimensional Depot.
- Allow clipping (on by default): lines built from the map may pass through other buildings.
- Single player and host only.

**Measure tool**
- Press G or click the Measure tile, then drag to measure a box. Its corners snap to the foundation grid under where
  the drag starts, so rotated builds measure in whole foundations. The label shows metres, foundations, floors and
  the height range of what is inside.
- Copies: tile up to 16 outlines of the box in any direction. A copy that would land on existing buildings turns
  red, so you can see where a build can be repeated. Hidden layers don't count as being in the way.
- Shift + drag measures a straight line. Space + drag pans the map while measuring.

**Status lights**
- A small light in each machine's corner: green producing, purple overclocked, yellow standby, red error. Zoomed far
  out, only the yellow and red ones stay. The hover label shows the status in words.
- Off by default: Mods > Atlas > Map > Status lights.

**Map**
- Second window: the whole Atlas map in a separate window, for a second monitor, with its own pan and zoom.
- Mod settings menu (Mods > Atlas) for the display options: hover label, height, coordinates, highlight, building
  outlines, recipe icons, status lights.
- Spoiler-free option that hides collectibles, resource nodes, wells, spore flowers, gas pillars and radioactivity.
- Copy and paste only between buildings it fits: a machine's settings go wherever the game's own clipboard takes
  them, and a look copied from a foundation or wall goes onto others of the same category.
- The shortcut bar sits above the game's marker bar.

## 0.2.0

- Belts, pipes, rails, roads and wires are drawn as overlapping capsules, so paths read as one smooth band.
- The map shortcuts show as a key hint bar along the bottom of the map.
- The panel no longer flashes on clicks.
- Fullscreen: the map no longer runs past the bottom of the screen.

## 0.1.0

- First release. Every building drawn live on the in-game map with GPU instancing, with layers, a height filter,
  custom and paint colors, hover info, building configuration, collectibles, resource nodes, radioactivity and PNG
  export.
