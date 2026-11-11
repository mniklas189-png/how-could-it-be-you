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


ENGI = "/Game/ModSci_Engineer/Meshes/"
TOOLS = "/Game/ModSci_EngiProps/Meshes/"
KIT = "/Game/ModSciInteriors/Meshes/"


def lamp_pack(name):
    return "/Game/Sci_Fi_Light/Static_Meshes/%s/Mesh/%s" % (name, name)


PROPS = {
    "Task0": (lamp_pack("SM_Sci_Fi_Info_Terminal"), -90.0),
    "Task1": (ENGI + "SM_StorageAreaNetwork", 180.0),
    "Charger": (lamp_pack("SM_Sci_Fi_Battery_Unit"), 90.0),
    "FuseBox": (TOOLS + "SM_SafetySwitch", -90.0),
    "ButtonBase": (KIT + "SM_EnergyNode_B", 0.0),
    "DoorLeaf": (ENGI + "SM_Door_Small_A", 0.0),
    "DoorGlass": (ENGI + "SM_Door_Small_A_Glass", 0.0),
    "TankCovered": (TOOLS + "SM_NitrogenTank_Covered", 0.0),
    "OxygenTank": (TOOLS + "SM_OxygenTank", 0.0),
    "Case": (TOOLS + "SM_Case_A", 0.0),
    "Toolbox": (TOOLS + "SM_Toolbox", 0.0),
    "Drum": (TOOLS + "SM_WaterDrum", 0.0),
    "Pallet": (TOOLS + "SM_PlasticPallet", 0.0),
    "WireReel": (TOOLS + "SM_WireReel_A", 0.0),
    "Extinguisher": (TOOLS + "SM_FireExtinguisher", 0.0),
    "WorkLight": (TOOLS + "SM_PortableLight", 0.0),
    "EnergyNode": (KIT + "SM_EnergyNode_A", 0.0),
    "RolledMat": (TOOLS + "SM_RubberMat_Rolled", 0.0),
    "Cable": (TOOLS + "SM_Wire_Floor_A", 0.0),
    "Bollard": (lamp_pack("SM_Sci_Fi_Bollard_Light"), 0.0),
}


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
    # Objects: stand-ins for the grey blocks and things that stand around (ModSci packs, Sci_Fi_Light).
    # The number is the turn that brings the front of the model to +X.
    props, yaws = {}, {}
    for key, (path, yaw) in PROPS.items():
        mesh = optional(path)
        if mesh:
            props[key] = mesh
            yaws[key] = yaw
        else:
            unreal.log_warning("HC models: no %s for %s" % (path, key))
    model_set.set_editor_property("props", props)
    model_set.set_editor_property("prop_yaws", yaws)
    unreal.EditorAssetLibrary.save_loaded_asset(model_set, only_if_is_dirty=False)
    unreal.log("HC models: %s written (character %s, anim %s, monster %s, attack %s)" % (MODEL_SET, character, anim_class, monster, attack))


write_model_set()
cap_textures()
