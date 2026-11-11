# Brings the game's own effect pictures into the project: the blood splatters drawn by MakeBlood.py, the slime drawn by
# MakeSlime.py and the decal materials that put them on the floor. The results (/Game/HowCouldItBeYou/Effects) belong
# to the game and are part of the repository, so this only has to run again after pictures were changed; it imports
# only pictures that are newer than their asset (or have none yet) and writes only materials that do not exist yet:
#   UnrealEditor-Cmd.exe <project> -run=pythonscript -script=<this file>
import os

import unreal

FOLDER = "/Game/HowCouldItBeYou/Effects"
SOURCE = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "..", "Tools", "Setup", "Effects")
CONTENT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()), "HowCouldItBeYou", "Effects")

# (picture, asset, is a normal map)
PICTURES = [("blood_%d.png" % i, "T_Blood_%d" % i, False) for i in range(3)] \
    + [("slime_%d.png" % i, "T_Slime_%d" % i, False) for i in range(3)] \
    + [("slime_%d_n.png" % i, "T_Slime_%d_N" % i, True) for i in range(3)]


def outdated(picture, asset):
    package = os.path.join(CONTENT, asset + ".uasset")
    return not os.path.exists(package) or os.path.getmtime(os.path.join(SOURCE, picture)) > os.path.getmtime(package)


def import_pictures():
    todo = [entry for entry in PICTURES if outdated(entry[0], entry[1])]
    tasks = []
    for picture, asset, _ in todo:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", os.path.normpath(os.path.join(SOURCE, picture)))
        task.set_editor_property("destination_path", FOLDER)
        task.set_editor_property("destination_name", asset)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        tasks.append(task)
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    for picture, asset, is_normal in todo:
        texture = unreal.load_asset("%s/%s" % (FOLDER, asset))
        if texture and is_normal:
            texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            texture.set_editor_property("srgb", False)
            unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False)
        unreal.log_warning("HC effects: %s -> %s %s" % (picture, asset, "ok" if texture else "FAILED"))
    return all(unreal.EditorAssetLibrary.does_asset_exist("%s/%s" % (FOLDER, asset)) for _, asset, _ in PICTURES)


def new_decal_material(name):
    path = "%s/%s" % (FOLDER, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log("HC effects: %s exists, left as it is" % path)
        return None
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    return material


def finish(material):
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    unreal.log("HC effects: %s written" % material.get_path_name())


def make_blood_material():
    material = new_decal_material("M_BloodDecal")
    if not material:
        return
    library = unreal.MaterialEditingLibrary
    picture = library.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -500, 0)
    picture.set_editor_property("parameter_name", "Splat")
    picture.set_editor_property("texture", unreal.load_asset(FOLDER + "/T_Blood_0"))
    library.connect_material_property(picture, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    library.connect_material_property(picture, "A", unreal.MaterialProperty.MP_OPACITY)
    # Wet: smooth, not metallic
    rough = library.create_material_expression(material, unreal.MaterialExpressionConstant, -500, 320)
    rough.set_editor_property("r", 0.14)
    library.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    finish(material)


def make_slime_material():
    # Splat: colour and coverage, Bumps: normal map, Fade: 1 fresh .. 0 dried up, Glow: how much it shines by itself
    material = new_decal_material("M_SlimeDecal")
    if not material:
        return
    library = unreal.MaterialEditingLibrary
    picture = library.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -800, -200)
    picture.set_editor_property("parameter_name", "Splat")
    picture.set_editor_property("texture", unreal.load_asset(FOLDER + "/T_Slime_0"))
    bumps = library.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -800, 300)
    bumps.set_editor_property("parameter_name", "Bumps")
    bumps.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    bumps.set_editor_property("texture", unreal.load_asset(FOLDER + "/T_Slime_0_N"))
    fade = library.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -500, 120)
    fade.set_editor_property("parameter_name", "Fade")
    fade.set_editor_property("default_value", 1.0)
    glow = library.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -500, -40)
    glow.set_editor_property("parameter_name", "Glow")
    glow.set_editor_property("default_value", 0.0)
    coverage = library.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, 80)
    library.connect_material_expressions(picture, "A", coverage, "A")
    library.connect_material_expressions(fade, "", coverage, "B")
    shine = library.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, -120)
    library.connect_material_expressions(picture, "RGB", shine, "A")
    library.connect_material_expressions(glow, "", shine, "B")
    library.connect_material_property(picture, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    library.connect_material_property(coverage, "", unreal.MaterialProperty.MP_OPACITY)
    library.connect_material_property(shine, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.connect_material_property(bumps, "RGB", unreal.MaterialProperty.MP_NORMAL)
    # Slick and wet, a little more than blood
    rough = library.create_material_expression(material, unreal.MaterialExpressionConstant, -500, 520)
    rough.set_editor_property("r", 0.07)
    library.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    finish(material)


if import_pictures():
    make_blood_material()
    make_slime_material()
else:
    unreal.log_error("HC effects: pictures missing, no materials written")
