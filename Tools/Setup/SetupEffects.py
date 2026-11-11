# Brings the game's own effect pictures into the project: the blood splatters drawn by MakeBlood.py and the decal
# material that puts them on the floor. The results (/Game/HowCouldItBeYou/Effects) belong to the game and are part
# of the repository, so this only has to run again after the pictures were changed:
#   UnrealEditor-Cmd.exe <project> -run=pythonscript -script=<this file>
import os

import unreal

FOLDER = "/Game/HowCouldItBeYou/Effects"
SOURCE = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "..", "Tools", "Setup", "Effects")
MATERIAL = FOLDER + "/M_BloodDecal"


def import_pictures():
    tasks = []
    for index in range(3):
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", os.path.normpath(os.path.join(SOURCE, "blood_%d.png" % index)))
        task.set_editor_property("destination_path", FOLDER)
        task.set_editor_property("destination_name", "T_Blood_%d" % index)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    return [unreal.load_asset("%s/T_Blood_%d" % (FOLDER, index)) for index in range(3)]


def make_material(first_picture):
    if unreal.EditorAssetLibrary.does_asset_exist(MATERIAL):
        unreal.log("HC effects: %s exists, left as it is" % MATERIAL)
        return
    library = unreal.MaterialEditingLibrary
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_BloodDecal", FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    picture = library.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -500, 0)
    picture.set_editor_property("parameter_name", "Splat")
    picture.set_editor_property("texture", first_picture)
    library.connect_material_property(picture, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    library.connect_material_property(picture, "A", unreal.MaterialProperty.MP_OPACITY)
    # Wet: smooth, not metallic
    rough = library.create_material_expression(material, unreal.MaterialExpressionConstant, -500, 320)
    rough.set_editor_property("r", 0.14)
    library.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    library.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    unreal.log("HC effects: %s written" % MATERIAL)


pictures = import_pictures()
unreal.log_warning("HC effects: pictures %s" % [str(p.get_name()) if p else None for p in pictures])
if pictures[0]:
    make_material(pictures[0])
