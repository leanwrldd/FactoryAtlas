# Imports Scripts/Sounds/*.wav into /FactoryAtlas/Sounds, at the paths the C++ loads (AtlasSubsystem.cpp,
# SoundPaths). Make the WAVs first with Scripts/synth_sounds.py. Re-runnable: existing sounds are reimported in place.
#   UnrealEditor-Cmd.exe C:\SF\SML\FactoryGame.uproject -run=pythonscript
#     -script=C:\SF\Mods\FactoryAtlas\Scripts\import_sounds.py -unattended -nosplash
import glob
import os
import unreal

SOURCE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Sounds")
DESTINATION = "/FactoryAtlas/Sounds"

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary

files = sorted(glob.glob(os.path.join(SOURCE_DIR, "*.wav")))
if not files:
    unreal.log_error(f"ATLAS: no WAVs in {SOURCE_DIR}; run synth_sounds.py first")

for path in files:
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = DESTINATION
    task.automated = True
    task.replace_existing = True
    task.save = False
    tools.import_asset_tasks([task])
    imported = list(task.imported_object_paths)
    if not imported:
        unreal.log_error(f"ATLAS: import failed: {path}")
        continue
    sound = lib.load_asset(imported[0])
    sound.set_editor_property("looping", False)
    # The project default is Bink Audio, which has no decoder in the editor and may not in the game. PCM always
    # decodes, and these are a few KB each.
    sound.set_editor_property("sound_asset_compression_type", unreal.SoundAssetCompressionType.PCM)
    lib.save_loaded_asset(sound, only_if_is_dirty=False)
    unreal.log(f"ATLAS: sound {sound.get_path_name()} {sound.get_editor_property('duration'):.2f} s")

unreal.log("ATLAS: sound import finished")
