# Atlas

A [Satisfactory](https://www.satisfactorygame.com/) mod that turns the in-game map into a proper factory map.

Every building you own is drawn live on the map, with layers, a height filter, your own colors and
paint, hover info, building configuration and image export. It is built to cost the game almost nothing.

![Atlas on the map: buildings drawn by category, hover info, and the machine configuration panel](docs/screenshot-map.webp)

Built with [SML](https://ficsit.app/mod/SML) (Satisfactory Mod Loader) 3.12 for Satisfactory 1.2.
Mod reference: `FactoryAtlas`.

## Features

**Buildings**
- Every building drawn to scale and rotated: foundations, walls, machines, belts, lifts, pipes,
  hypertubes, railways, truck paths, power lines and modded buildings
- Layers by category and by building type, shown or hidden instantly
- One height range slider to show a single floor of a tall factory
- Category colors, your own colors per category or per building type, or the buildings' real
  paint colors
- Updates live as you build and dismantle

**On the map**
- Hover a building to see its name, height and coordinates, with a highlight
- Click a machine to configure it: clock speed (within the power shards already inside it),
  recipe, production pause, plus Somersloop status, shard count and productivity
- Radioactivity around radioactive items
- Collectibles left to find: Somersloops, Mercer Spheres, hard drives and power slugs
- Resource nodes (sized by purity, hollow when in use), resource wells, geysers, spore flowers
  and gas pillars
- Higher zoom limit than the base game (up to 8x)
- Fullscreen mode: the map fills the whole map screen, without the game's window frame and filter list
- Export the map as a PNG image (4096 or 8192 px)
- Status lights (optional): a light in each machine's corner, green producing, purple overclocked,
  yellow standby, red error
- A second window with the whole map in it, for a second monitor
- Spoiler-free option that hides collectibles, resources and hazards

**Building from the map** (single player and host)
- Power wires: Alt + W, then click one building and another to connect them, or the ground for a pole
- Belts, pipes, hypertubes and railway: drag from a free port to another port or to the ground
- Built through the game's own holograms, so limits and costs are the build gun's

**Measuring**
- Press G and drag a box: size in metres and foundations, floors and height range, snapped to the
  foundation grid even on rotated builds
- Tile copies of the box to see where a build can be repeated; copies that hit existing buildings turn red
- Shift + drag for a straight line

Settings are saved between sessions. The menu sits on the right of the map screen, and display
options are under Mods > Atlas in the game's settings.

## How it stays light

Earlier map mods painted every building into one large image, which costs hundreds of MB of video
memory for as long as you play and has to be redrawn when you build. Atlas draws nothing until the
map is open:

- Each building is a 16-byte record. The GPU draws all of them in a handful of instanced draw
  calls through a UI material, so there is no render target at all.
- Panning, zooming, layers, colors and the height filter only change a few shader parameters.
  None of them redraw anything.
- Building changes are queued in constant time and applied on a worker thread.
- Video memory is used only while the map is open and is freed when it closes.

In testing, a million extra buildings drawn at full zoom out cost about 0.3 ms of CPU per frame.

## Not yet tested

- **Multiplayer:** building configuration and recipe changes work for the host only. Clients see
  only the buildings the game has sent them, which may be just the ones nearby.
- **Dedicated servers:** the mod is client-side and not required on servers.
- **Spore flowers and gas pillars** only exist while their part of the world is loaded, so Atlas
  remembers each one it has seen and the map fills in as you explore.

## Console commands

- `atlas.Stress <count>` adds fake buildings to test performance. `0` removes them.
- `Atlas.Window` opens or closes the map in a separate window.
- `atlas.StatusDump` logs every machine's status by type, and whether the status lights agree with it.

## Building

1. Junction the repo into an SML 3.12 project as `Mods/GameFeatures/FactoryAtlas`.
2. Build the editor target (`FactoryEditor`).
3. Generate the game feature data and the map material:
   `UnrealEditor-Cmd.exe <SML>\FactoryGame.uproject -run=pythonscript -script=<repo>\Scripts\create_assets.py -unattended -nosplash`
4. Import the Ctrl-click sounds (the WAVs are in `Scripts/Sounds`; `Scripts/synth_sounds.py` regenerates them):
   `UnrealEditor-Cmd.exe <SML>\FactoryGame.uproject -run=pythonscript -script=<repo>\Scripts\import_sounds.py -unattended -nosplash`
   It exits with code 1 because of a harmless "Decoder for AudioFormat 'BINKA' not found" check; the sounds are saved as PCM.
5. Package with Alpakit.

## Credits

Inspired by [Cartograph](https://ficsit.app/mod/Cartograph) by yeshjho. Atlas is written from
scratch and shares no code with it. The map bounds are the ones Cartograph worked out with help from
[SCIM](https://satisfactory-calculator.com/).
