# Replaces the game's built-in placeholder sounds with real recordings. Run with Tools\SetupSounds.bat.
# Put WAV files into the folder "Sounds" next to "Game", named after the sound they replace:
#   Scream.wav  Stinger.wav  Thump.wav  Shapeshift.wav  Alarm.wav  Door.wav  Victory.wav
# Several recordings of one sound: Scream.wav, Scream2.wav, Scream3.wav - the game alternates between them.
# Only the files that exist are replaced; delete a file and run again to get the built-in sound back.
# Bought or licensed recordings are NOT part of the public repository: the folder "Sounds" and the imported
# assets (/Game/ThirdParty/Sounds) are ignored by git and only travel inside the packaged game.
import os

import unreal

NAMES = ["Scream", "Stinger", "Thump", "Shapeshift", "Alarm", "Door", "Victory"]
SOURCE = os.path.normpath(os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "..", "Sounds"))
TARGET = "/Game/ThirdParty/Sounds"
MODEL_SET = "/Game/HowCouldItBeYou/Models/DA_Models"


def main():
    if not unreal.EditorAssetLibrary.does_asset_exist(MODEL_SET):
        unreal.log_error("HC sounds: %s is missing - run Tools\\SetupModels.bat first" % MODEL_SET)
        return
    files = {}
    if os.path.isdir(SOURCE):
        for name in os.listdir(SOURCE):
            stem, extension = os.path.splitext(name)
            if extension.lower() in (".wav", ".flac", ".ogg", ".aif", ".aiff"):
                files[stem.lower()] = os.path.join(SOURCE, name)
    sounds = {}
    # A sound may have up to three recordings (Scream.wav, Scream2.wav, Scream3.wav); the game alternates between them
    for name in [base + number for base in NAMES for number in ("", "2", "3")]:
        path = files.get(name.lower())
        if not path:
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", path)
        task.set_editor_property("destination_path", TARGET)
        task.set_editor_property("destination_name", "S_" + name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        asset = unreal.load_asset("%s/S_%s" % (TARGET, name)) if unreal.EditorAssetLibrary.does_asset_exist("%s/S_%s" % (TARGET, name)) else None
        if isinstance(asset, unreal.SoundBase):
            sounds[name] = asset
        else:
            unreal.log_warning("HC sounds: could not import %s" % path)
    model_set = unreal.load_asset(MODEL_SET)
    model_set.set_editor_property("sounds", sounds)
    unreal.EditorAssetLibrary.save_loaded_asset(model_set, only_if_is_dirty=False)
    unreal.log_warning("HC sounds: %d real sounds in use %s, folder %s" % (len(sounds), sorted(sounds.keys()), SOURCE))


main()
