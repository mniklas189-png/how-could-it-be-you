# Wires the store model packs into the game. Run with Tools\SetupModels.bat after adding the packs to the project:
#   - Survival Character (Arberry)  -> /Game/Survival_Character
#   - Whisper (Leks)                -> /Game/Whisper
#   - Modular SciFi kit (optional)  -> /Game/ModSciInteriors
#   - Flash Light (Slav3106, optional) -> imported from the launcher's Fab download into /Game/ThirdParty/FlashLight
#   - the game's own models (Tools\Models\MakeModels.bat builds them with Blender into Build\Models) are imported
#     into /Game/HowCouldItBeYou/Models/Own. Those ARE part of the repository: they are our own work.
# The packs are NOT part of the public repository (store licence); this script only writes the small asset
# /Game/HowCouldItBeYou/Models/DA_Models that points at them, and caps their textures at 2048 px so the
# download of the game stays small.
import glob
import json
import os

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
    # Furniture of the Ikarus rooms (World/HCStationLayout.cpp, MakeIkarus)
    "TankBig": (KIT + "SM_WVC_Tank_A", 0.0),
    "PowerCells": (KIT + "SM_PowerCells_Floor", 0.0),
    "Gate": (KIT + "SM_WVD_Gate", 0.0),
    "Nitrogen": (ENGI + "SM_NitrogenTank", 0.0),
    "Truss": (ENGI + "SM_Truss_A", 0.0),
    "PipeValve": (ENGI + "SM_PipeValve_A", 0.0),
    "Beacon": (lamp_pack("SM_Sci_Fi_Heavy_Duty_Linear_Beacon"), 0.0),
    "Monolith": (lamp_pack("SM_Sci_Fi_Monolith_Light"), 0.0),
    "HubLight": (lamp_pack("SM_Sci_Fi_Hub_Luminaire"), 0.0),
}

# The game's own models, built with Blender (Tools/Models/MakeModels.py). Their long side lies along X and their
# front looks along +Y, so a turn of -90 brings the front to +X like the task stations: in the layouts the yaw of
# these props is the way the front faces. The two parts of a ventilation hatch are placed by AHCVent itself.
OWN_DIR = "/Game/HowCouldItBeYou/Models/Own"
OWN_FURNITURE = ["Bunk", "Locker", "MedBed", "LabBench", "Shelf", "Crate", "MessTable", "Chair", "Desk", "Planter", "CryoPod",
                 "IceCoreRack", "Pump", "Generator", "Barrier", "Bench", "Heater", "IceChunk"]
OWN_PARTS = ["VentFrame", "VentGrille"]
for own_name in OWN_FURNITURE:
    PROPS[own_name] = ("%s/SM_%s" % (OWN_DIR, own_name), -90.0)
for own_name in OWN_PARTS:
    PROPS[own_name] = ("%s/SM_%s" % (OWN_DIR, own_name), 0.0)

# Unlit screen material of the engine (texture parameter "SlateUI"): camera monitors and glowing parts. The model
# set points at it so that the cook takes it along.
SCREEN_MATERIAL = "/Engine/EngineMaterials/Widget3DPassThrough_Opaque"


# The flashlight every player holds. Fab only offers it as an FBX file, which the launcher keeps in its cache;
# it is imported once. Its own materials hang on an importer plugin material and have no switch for the lens,
# so it gets simple ones of its own: slot -> (colour, roughness, metallic).
FLASHLIGHT_DIR = "/Game/ThirdParty/FlashLight"
FLASHLIGHT = FLASHLIGHT_DIR + "/FlashLight"
FLASHLIGHT_FBX = "C:/ProgramData/Epic/EpicGamesLauncher/VaultCache/FabLibrary/Flash_Light-*/fbx/**/FlashLight.fbx"
FLASHLIGHT_LOOK = {
    "Black_Metal_001": ((0.03, 0.03, 0.032), 0.35, 0.7),
    "Glass": ((0.85, 0.85, 0.8), 0.05, 0.0),  # the lens, glows while the light is on (parameter "Glow")
    "Glass_001": ((0.8, 0.8, 0.8), 0.6, 0.0),
    "White_Plastic": ((0.53, 0.45, 0.39), 0.9, 0.0),
    "Button": ((0.008, 0.008, 0.008), 0.5, 0.0),
}
FLASHLIGHT_LENS = "Glass"
# The model lies along +Z with the lens on top and the button on +X: turned so that the lens points forward
# (+X) and the button up
FLASHLIGHT_TURN = unreal.Rotator(roll=180.0, pitch=90.0, yaw=0.0)


def flashlight_material():
    path = FLASHLIGHT_DIR + "/M_Flashlight"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    lib = unreal.MaterialEditingLibrary
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_Flashlight", FLASHLIGHT_DIR, unreal.Material, unreal.MaterialFactoryNew())

    def parameter(kind, name, value, y):
        expression = lib.create_material_expression(material, kind, -500, y)
        expression.set_editor_property("parameter_name", name)
        expression.set_editor_property("default_value", value)
        return expression

    lib.connect_material_property(parameter(unreal.MaterialExpressionVectorParameter, "Color", unreal.LinearColor(0.03, 0.03, 0.03, 1.0), -300), "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(parameter(unreal.MaterialExpressionScalarParameter, "Metallic", 0.0, -150), "", unreal.MaterialProperty.MP_METALLIC)
    lib.connect_material_property(parameter(unreal.MaterialExpressionScalarParameter, "Roughness", 0.4, 0), "", unreal.MaterialProperty.MP_ROUGHNESS)
    # Emissive = GlowColor * Glow; the game sets Glow to 1 on the lens while the flashlight is on
    glow = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, 250)
    lib.connect_material_expressions(parameter(unreal.MaterialExpressionVectorParameter, "GlowColor", unreal.LinearColor(24.0, 21.0, 17.0, 1.0), 200), "", glow, "A")
    lib.connect_material_expressions(parameter(unreal.MaterialExpressionScalarParameter, "Glow", 0.0, 350), "", glow, "B")
    lib.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    return material


def setup_flashlight():
    if not unreal.EditorAssetLibrary.does_asset_exist(FLASHLIGHT):
        found = sorted(glob.glob(FLASHLIGHT_FBX, recursive=True))
        if not found:
            unreal.log_warning("HC models: no flashlight model (Fab 'Flash Light' by Slav3106, FBX, via the launcher)")
            return None
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", found[-1])
        task.set_editor_property("destination_path", FLASHLIGHT_DIR)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        options = unreal.FbxImportUI()
        options.set_editor_property("import_mesh", True)
        options.set_editor_property("import_as_skeletal", False)
        options.set_editor_property("import_materials", True)
        options.set_editor_property("import_textures", False)
        options.set_editor_property("import_animations", False)
        options.static_mesh_import_data.set_editor_property("combine_meshes", True)
        options.static_mesh_import_data.set_editor_property("auto_generate_collision", False)
        task.set_editor_property("options", options)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        if not unreal.EditorAssetLibrary.does_asset_exist(FLASHLIGHT):
            unreal.log_error("HC models: importing %s did not give %s" % (found[-1], FLASHLIGHT))
            return None
    mesh = unreal.load_asset(FLASHLIGHT)
    parent = flashlight_material()
    lib = unreal.MaterialEditingLibrary
    for index, slot in enumerate(mesh.get_editor_property("static_materials")):
        name = str(slot.get_editor_property("material_slot_name"))
        colour, roughness, metallic = FLASHLIGHT_LOOK.get(name, ((0.05, 0.05, 0.05), 0.5, 0.0))
        path = "%s/MI_Flashlight_%s" % (FLASHLIGHT_DIR, name)
        instance = optional(path) or unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "MI_Flashlight_" + name, FLASHLIGHT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        instance.set_editor_property("parent", parent)
        lib.set_material_instance_vector_parameter_value(instance, "Color", unreal.LinearColor(colour[0], colour[1], colour[2], 1.0))
        lib.set_material_instance_scalar_parameter_value(instance, "Roughness", roughness)
        lib.set_material_instance_scalar_parameter_value(instance, "Metallic", metallic)
        unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False)
        mesh.set_material(index, instance)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
    # The importer's own materials are no longer used
    for path in unreal.EditorAssetLibrary.list_assets(FLASHLIGHT_DIR, recursive=False):
        name = path.rsplit("/", 1)[-1].split(".")[0]
        if not name.startswith(("MI_Flashlight_", "M_Flashlight", "FlashLight")):
            unreal.EditorAssetLibrary.delete_asset(path)
    return mesh


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


def allow_instancing():
    # Station pieces and props are drawn as instanced meshes. A material has to be marked for that - otherwise the
    # finished game shows the plain grey default material instead. (The editor repairs it on the fly without saving,
    # which hides the problem until the game is packaged.)
    model_set = unreal.load_asset(MODEL_SET)
    meshes = [model_set.get_editor_property(name) for name in ("floor_mesh", "ceiling_mesh", "wall_mesh", "wall_trim_mesh", "ceiling_light_mesh")]
    meshes += list(model_set.get_editor_property("props").values())
    seen, changed = set(), 0
    for mesh in meshes:
        if not mesh:
            continue
        for slot in mesh.get_editor_property("static_materials"):
            material = slot.get_editor_property("material_interface")
            base = material.get_base_material() if material else None
            if not base or base.get_path_name() in seen or not base.get_path_name().startswith("/Game/"):
                continue
            seen.add(base.get_path_name())
            if base.get_editor_property("used_with_instanced_static_meshes"):
                continue
            unreal.MaterialEditingLibrary.set_material_usage(base, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
            unreal.EditorAssetLibrary.save_loaded_asset(base, only_if_is_dirty=False)
            changed += 1
    unreal.log_warning("HC models: %d base materials checked, %d newly allowed for instanced meshes" % (len(seen), changed))


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
    flashlight = setup_flashlight()
    model_set.set_editor_property("flashlight_mesh", flashlight)
    model_set.set_editor_property("flashlight_rotation", FLASHLIGHT_TURN)
    model_set.set_editor_property("flashlight_lens_slot", FLASHLIGHT_LENS if flashlight else "")
    model_set.set_editor_property("monster_mesh", monster)
    model_set.set_editor_property("monster_attack", attack)
    # The monster walking around in its true form. Its animations must not carry the body away from the player's
    # capsule, so the root is locked.
    for prop, name in (("monster_idle", "Anim_Whisper_Idle1"), ("monster_walk", "Anim_Whisper_Walk1"), ("monster_run", "Anim_Whisper_Run")):
        sequence = optional("/Game/Whisper/Animations/" + name)
        model_set.set_editor_property(prop, sequence)
        if sequence and not sequence.get_editor_property("force_root_lock"):
            sequence.set_editor_property("force_root_lock", True)
            unreal.EditorAssetLibrary.save_loaded_asset(sequence, only_if_is_dirty=False)
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
    model_set.set_editor_property("screen_material", unreal.load_asset(SCREEN_MATERIAL))
    unreal.EditorAssetLibrary.save_loaded_asset(model_set, only_if_is_dirty=False)
    unreal.log("HC models: %s written (character %s, anim %s, monster %s, attack %s)" % (MODEL_SET, character, anim_class, monster, attack))


# ---------------------------------------------------------------------------
# The game's own models: FBX + baked textures from Build/Models -> /Game/HowCouldItBeYou/Models/Own


def own_models_source():
    folder = os.environ.get("HC_MODELS_DIR")
    if not folder:
        project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
        folder = os.path.normpath(os.path.join(project, "..", "Build", "Models"))
    return folder


def import_file(filename, destination):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", filename)
    task.set_editor_property("destination_path", destination)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])


def own_texture(source, name, linear):
    path = "%s/%s" % (OWN_DIR, name)
    import_file(os.path.join(source, name + ".png"), OWN_DIR)
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        raise RuntimeError("HC models: importing %s.png did not give %s" % (name, path))
    texture = unreal.load_asset(path)
    if linear:
        # Occlusion, roughness and metallic in the three channels: plain numbers, not colours
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False)
    return texture


def own_material(name, build):
    path = "%s/%s" % (OWN_DIR, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    lib = unreal.MaterialEditingLibrary
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, OWN_DIR, unreal.Material, unreal.MaterialFactoryNew())
    build(material, lib)
    # The props are drawn as instanced meshes (see allow_instancing)
    lib.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    lib.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    return material


def own_parameter(material, lib, kind, name, value, x, y):
    expression = lib.create_material_expression(material, kind, x, y)
    expression.set_editor_property("parameter_name", name)
    expression.set_editor_property("default_value", value)
    return expression


def own_surface_material(colour_texture, mask_texture):
    # Baked surface: colour (with dirt and wear in it) and a mask texture with occlusion / roughness / metallic.
    # Tint and RoughnessScale are there to tune the look against the station kit without baking again.
    def build(material, lib):
        colour = lib.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -700, -250)
        colour.set_editor_property("parameter_name", "BaseColor")
        colour.set_editor_property("texture", colour_texture)
        tint = own_parameter(material, lib, unreal.MaterialExpressionVectorParameter, "Tint", unreal.LinearColor(1.0, 1.0, 1.0, 1.0), -700, -450)
        tinted = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -400, -300)
        lib.connect_material_expressions(colour, "RGB", tinted, "A")
        lib.connect_material_expressions(tint, "", tinted, "B")
        lib.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)
        mask = lib.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -700, 50)
        mask.set_editor_property("parameter_name", "ORM")
        mask.set_editor_property("texture", mask_texture)
        mask.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        lib.connect_material_property(mask, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
        scale = own_parameter(material, lib, unreal.MaterialExpressionScalarParameter, "RoughnessScale", 1.0, -700, 300)
        rough = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -400, 150)
        lib.connect_material_expressions(mask, "G", rough, "A")
        lib.connect_material_expressions(scale, "", rough, "B")
        lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
        lib.connect_material_property(mask, "B", unreal.MaterialProperty.MP_METALLIC)
    return own_material("M_HCOwnSurface", build)


def own_glow_material():
    # Screens, lamps, heating coils: one colour that glows, untouched by the light of the room
    def build(material, lib):
        material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        colour = own_parameter(material, lib, unreal.MaterialExpressionVectorParameter, "Color", unreal.LinearColor(1.0, 1.0, 1.0, 1.0), -600, -100)
        strength = own_parameter(material, lib, unreal.MaterialExpressionScalarParameter, "Strength", 6.0, -600, 100)
        glow = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 0)
        lib.connect_material_expressions(colour, "", glow, "A")
        lib.connect_material_expressions(strength, "", glow, "B")
        lib.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return own_material("M_HCOwnGlow", build)


def own_glass_material():
    # Dark, glossy glass (the window of a cryo pod, sample tubes): not see-through, so it sorts and lights like metal
    def build(material, lib):
        colour = own_parameter(material, lib, unreal.MaterialExpressionVectorParameter, "Color", unreal.LinearColor(0.05, 0.08, 0.1, 1.0), -600, -200)
        lib.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
        lib.connect_material_property(own_parameter(material, lib, unreal.MaterialExpressionScalarParameter, "Roughness", 0.08, -600, 0), "", unreal.MaterialProperty.MP_ROUGHNESS)
        lib.connect_material_property(own_parameter(material, lib, unreal.MaterialExpressionScalarParameter, "Metallic", 0.35, -600, 100), "", unreal.MaterialProperty.MP_METALLIC)
        inner = own_parameter(material, lib, unreal.MaterialExpressionScalarParameter, "Glow", 0.25, -600, 250)
        glow = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 200)
        lib.connect_material_expressions(colour, "", glow, "A")
        lib.connect_material_expressions(inner, "", glow, "B")
        lib.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return own_material("M_HCOwnGlass", build)


def own_instance(name, parent):
    path = "%s/%s" % (OWN_DIR, name)
    instance = optional(path) or unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, OWN_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    instance.set_editor_property("parent", parent)
    return instance


def import_own_models():
    source = own_models_source()
    manifest_path = os.path.join(source, "manifest.json")
    if not os.path.exists(manifest_path):
        unreal.log_warning("HC models: no %s - the game's own models are used as they are in the project" % manifest_path)
        return
    with open(manifest_path) as handle:
        manifest = json.load(handle)
    force = os.environ.get("HC_FORCE_MODELS") == "1"
    lib = unreal.MaterialEditingLibrary
    library = unreal.EditorAssetLibrary
    imported = 0
    for model in manifest["models"]:
        name = model["name"]
        mesh_path = "%s/SM_%s" % (OWN_DIR, name)
        if library.does_asset_exist(mesh_path) and not force:
            continue
        colour = own_texture(source, "T_%s_BC" % name, False)
        mask = own_texture(source, "T_%s_ORM" % name, True)
        # The importer names the mesh after the file or after the object inside it, and may bring materials of its
        # own: so each file goes into a folder of its own, the mesh is moved out under its final name, the rest goes
        staging = "%s/Import_%s" % (OWN_DIR, name)
        if library.does_directory_exist(staging):
            library.delete_directory(staging)
        import_file(os.path.join(source, model["fbx"]), staging)
        meshes = [path.split(".")[0] for path in library.list_assets(staging, recursive=True)
                  if isinstance(unreal.load_asset(path.split(".")[0]), unreal.StaticMesh)]
        if len(meshes) != 1:
            raise RuntimeError("HC models: %s gave %d meshes instead of one" % (model["fbx"], len(meshes)))
        if library.does_asset_exist(mesh_path):
            library.delete_asset(mesh_path)
        if not library.rename_asset(meshes[0], mesh_path):
            raise RuntimeError("HC models: could not move %s to %s" % (meshes[0], mesh_path))
        mesh = unreal.load_asset(mesh_path)
        for index, slot in enumerate(mesh.get_editor_property("static_materials")):
            slot_name = str(slot.get_editor_property("material_slot_name"))
            if slot_name.startswith("Glow"):
                instance = own_instance("MI_%s_Glow" % name, own_glow_material())
                colour_value = model.get("glow_color", [1.0, 1.0, 1.0])
                lib.set_material_instance_vector_parameter_value(instance, "Color", unreal.LinearColor(colour_value[0], colour_value[1], colour_value[2], 1.0))
                lib.set_material_instance_scalar_parameter_value(instance, "Strength", float(model.get("glow_strength", 6.0)))
            elif slot_name.startswith("Glass"):
                instance = own_instance("MI_%s_Glass" % name, own_glass_material())
                colour_value = model.get("glass_color", [0.05, 0.08, 0.1])
                lib.set_material_instance_vector_parameter_value(instance, "Color", unreal.LinearColor(colour_value[0], colour_value[1], colour_value[2], 1.0))
            else:
                instance = own_instance("MI_%s_Body" % name, own_surface_material(colour, mask))
                lib.set_material_instance_texture_parameter_value(instance, "BaseColor", colour)
                lib.set_material_instance_texture_parameter_value(instance, "ORM", mask)
            library.save_loaded_asset(instance, only_if_is_dirty=False)
            mesh.set_material(index, instance)
        library.save_loaded_asset(mesh, only_if_is_dirty=False)
        library.delete_directory(staging)
        # The size must be the one the model was built with (a wrong unit or axis on the way shows here)
        box = mesh.get_bounding_box()
        size = [box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z]
        wanted = model["size_cm"]
        if max(abs(size[axis] - wanted[axis]) for axis in range(3)) > 2.0:
            unreal.log_error("HC models: %s is %.1f x %.1f x %.1f cm in the game, built as %.1f x %.1f x %.1f"
                             % (name, size[0], size[1], size[2], wanted[0], wanted[1], wanted[2]))
        imported += 1
    unreal.log_warning("HC models: %d of the game's own models imported from %s" % (imported, source))


import_own_models()
write_model_set()
allow_instancing()
cap_textures()
