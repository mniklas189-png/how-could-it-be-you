"""Creates the game maps headless (Tools/CreateMaps.bat runs this):

  Map_MainMenu    empty, World Settings game mode = HCMenuGameMode
  Map_Transition  empty (seamless travel lobby <-> match)
  Map_Lobby       AHCStationBuilder with layout "Lobby"
  Kepler7/Map_Kepler7  AHCStationBuilder with layout "Kepler7"
  Ikarus/Map_Ikarus    AHCStationBuilder with layout "Ikarus" (the freighter)

Safe to run again: existing maps are left alone unless HC_FORCE=1 is set.
"""
import os
import unreal

ROOT = "/Game/HowCouldItBeYou/Maps"
FORCE = os.environ.get("HC_FORCE") == "1"

level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)


def load_class(path):
    cls = unreal.load_class(None, path)
    if cls is None:
        raise RuntimeError("[HC] Class {} not found - is the C++ module compiled?".format(path))
    return cls


def new_map(path):
    """Returns True if the map must be (re)built."""
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        # Recreating is done by CreateMaps.bat (HC_FORCE=1 deletes the files before the editor starts)
        unreal.log("[HC] {} exists, skipped".format(path))
        return False
    if not level_subsystem.new_level(path):
        raise RuntimeError("[HC] Could not create level " + path)
    return True


def save(path):
    if not level_subsystem.save_current_level():
        raise RuntimeError("[HC] Could not save " + path)
    unreal.log("[HC] Created " + path)


def world_settings():
    world = editor_subsystem.get_editor_world()
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings)
    return actors[0] if actors else None


def station_map(path, layout):
    if not new_map(path):
        return
    builder = actor_subsystem.spawn_actor_from_class(
        load_class("/Script/HowCouldItBeYou.HCStationBuilder"), unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0))
    builder.set_editor_property("LayoutName", layout)
    # set_editor_property notifies the actor (PostEditChange), which re-runs its construction script
    builder.set_actor_label("Station_" + layout)
    save(path)


def main_menu_map(path):
    if not new_map(path):
        return
    settings = world_settings()
    if settings is None:
        raise RuntimeError("[HC] No WorldSettings in " + path)
    settings.set_editor_property("default_game_mode", load_class("/Script/HowCouldItBeYou.HCMenuGameMode"))
    save(path)


def empty_map(path):
    if not new_map(path):
        return
    save(path)


main_menu_map(ROOT + "/Map_MainMenu")
empty_map(ROOT + "/Map_Transition")
station_map(ROOT + "/Map_Lobby", "Lobby")
station_map(ROOT + "/Kepler7/Map_Kepler7", "Kepler7")
station_map(ROOT + "/Ikarus/Map_Ikarus", "Ikarus")
