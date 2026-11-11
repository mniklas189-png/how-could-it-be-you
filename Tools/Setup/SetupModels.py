# Wires the store model packs into the game. Run with Tools\SetupModels.bat after adding the packs to the project:
#   - Survival Character (Arberry)  -> /Game/Survival_Character
#   - Whisper (Leks)                -> /Game/Whisper
#   - Modular SciFi kit (optional)  -> /Game/ModSciInteriors
# The packs are NOT part of the public repository (store licence); this script only writes the small asset
# /Game/HowCouldItBeYou/Models/DA_Models that points at them, and caps their textures at 2048 px so the
# download of the game stays small.
import unreal

MAX_TEXTURE = 2048
MODEL_SET = "/Game/HowCouldItBeYou/Models/DA_Models"


def cap_textures():
    # Everything the model set pulls in (meshes -> materials -> textures), whatever pack it comes from
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=False, include_hard_package_references=True)
    seen, todo, changed = set(), [MODEL_SET], 0
    while todo:
        package = todo.pop()
        for dep in registry.get_dependencies(package, options) or []:
            dep = str(dep)
            if dep.startswith("/Game/") and dep not in seen:
                seen.add(dep)
                todo.append(dep)
    for package in sorted(seen):
        if "/Textures/" not in package:
            continue
        texture = unreal.load_asset(package)
        if not isinstance(texture, unreal.Texture2D):
            continue
        biggest = max(texture.blueprint_get_size_x(), texture.blueprint_get_size_y())
        if biggest > MAX_TEXTURE and texture.get_editor_property("max_texture_size") != MAX_TEXTURE:
            texture.set_editor_property("max_texture_size", MAX_TEXTURE)
            unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False)
            changed += 1
    unreal.log_warning("HC models: %d packages used, capped %d textures at %d px" % (len(seen), changed, MAX_TEXTURE))


def optional(path):
    return unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None


def write_model_set():
    if unreal.EditorAssetLibrary.does_asset_exist(MODEL_SET):
        model_set = unreal.load_asset(MODEL_SET)
    else:
        folder, name = MODEL_SET.rsplit("/", 1)
        # The class is not exposed to Python by name, so it is loaded by its path
        model_class = unreal.load_class(None, "/Script/HowCouldItBeYou.HCModelSet")
        model_set = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, model_class, unreal.DataAssetFactory())
    character = unreal.load_asset("/Game/Survival_Character/Meshes/SK_Survival_Character")
    anim_class = unreal.load_object(None, "/Game/Survival_Character/Demo/Characters/Mannequins/Animations/ABP_Manny.ABP_Manny_C")
    monster = unreal.load_asset("/Game/Whisper/Base_Mesh/SK_Whisper")
    attack = unreal.load_asset("/Game/Whisper/Animations/Anim_Whisper_Attack1")
    model_set.set_editor_property("character_mesh", character)
    model_set.set_editor_property("character_anim_class", anim_class)
    model_set.set_editor_property("tint_slots", ["Jacket1", "Backpack1"])
    model_set.set_editor_property("tint_parameter", "Tint")
    model_set.set_editor_property("monster_mesh", monster)
    model_set.set_editor_property("monster_attack", attack)
    # Station pieces from the "Modular SciFi" kit (ModSciInteriors); the station stays greybox without it
    model_set.set_editor_property("floor_mesh", optional("/Game/ModSciInteriors/Meshes/SM_Floor_Main"))
    model_set.set_editor_property("ceiling_mesh", optional("/Game/ModSciInteriors/Meshes/SM_Ceiling_Main"))
    model_set.set_editor_property("wall_mesh", optional("/Game/ModSciInteriors/Meshes/SM_WVA_MainWall"))
    model_set.set_editor_property("wall_trim_mesh", optional("/Game/ModSciInteriors/Meshes/SM_WVA_LowerWall"))
    model_set.set_editor_property("ceiling_light_mesh", optional("/Game/ModSciInteriors/Meshes/SM_Light_B"))
    unreal.EditorAssetLibrary.save_loaded_asset(model_set, only_if_is_dirty=False)
    unreal.log("HC models: %s written (character %s, anim %s, monster %s, attack %s)" % (MODEL_SET, character, anim_class, monster, attack))


write_model_set()
cap_textures()
