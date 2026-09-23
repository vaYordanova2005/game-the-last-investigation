"""
Imports the downloaded CC0 texture sets and builds the room's master surface material.

Run headless, no editor GUI:
    UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="Tools/build_surface_material.py"

This exists because the project is code-first: materials are normally authored by clicking in the
Material Editor, which is not available to this workflow. Unreal's Python API can build the same
node graph, so the graph is source code like everything else and can be regenerated at any time.

The master material M_RoomSurface is parameterised, so gameplay C++ only ever creates dynamic
instances of it and swaps textures/tint per surface. One material, every surface in the room.
"""

import os
import unreal

PROJECT_DIR = unreal.Paths.project_dir()
SOURCE_DIR = os.path.join(PROJECT_DIR, "Art", "Source", "Textures")

TEXTURE_PACKAGE = "/Game/Textures"
MATERIAL_PACKAGE = "/Game/Materials"
MASTER_NAME = "M_RoomSurface"

# Poly Haven map suffix -> (how UE should treat it, whether it is colour data)
MAP_SETTINGS = {
    "Diffuse": (unreal.TextureCompressionSettings.TC_DEFAULT, True),
    "nor_dx": (unreal.TextureCompressionSettings.TC_NORMALMAP, False),
    # AO / Roughness / Metallic packed into R/G/B. Must be linear, or the roughness comes out wrong.
    "arm": (unreal.TextureCompressionSettings.TC_MASKS, False),
}


def import_texture(file_path, asset_name):
    task = unreal.AssetImportTask()
    task.filename = file_path
    task.destination_path = TEXTURE_PACKAGE
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    imported = unreal.EditorAssetLibrary.load_asset("{}/{}".format(TEXTURE_PACKAGE, asset_name))
    return imported


def import_all_textures():
    """Every *_<map>.jpg in the source folder becomes a UTexture2D with the right compression."""
    imported = []
    if not os.path.isdir(SOURCE_DIR):
        unreal.log_error("No source texture folder at {}".format(SOURCE_DIR))
        return imported

    for file_name in sorted(os.listdir(SOURCE_DIR)):
        if not file_name.lower().endswith((".jpg", ".png")):
            continue

        stem = os.path.splitext(file_name)[0]
        # Match the longest known suffix, not the last underscore-separated token: "nor_dx" is one
        # map name containing an underscore, so rsplit("_") would see it as "dx" and skip the file.
        map_kind = next((m for m in sorted(MAP_SETTINGS, key=len, reverse=True) if stem.endswith("_" + m)), None)
        if map_kind is None:
            unreal.log_warning("Skipping {} - unrecognised map suffix".format(file_name))
            continue

        compression, srgb = MAP_SETTINGS[map_kind]
        asset_name = "T_" + stem

        texture = import_texture(os.path.join(SOURCE_DIR, file_name), asset_name)
        if not texture:
            unreal.log_error("Failed to import {}".format(file_name))
            continue

        texture.set_editor_property("compression_settings", compression)
        texture.set_editor_property("srgb", srgb)
        unreal.EditorAssetLibrary.save_loaded_asset(texture)
        imported.append(asset_name)
        unreal.log("Imported {} ({}, sRGB={})".format(asset_name, map_kind, srgb))

    return imported


def build_master_material():
    """
    The graph, in words:

        BaseColor  = Texture(BaseColorMap, UV * Tiling) * Tint
        Normal     = Texture(NormalMap,    UV * Tiling)
        AO/R/M     = Texture(ARMMap,       UV * Tiling) -> R to AO, G to Roughness, B to Metallic

    Everything the room varies is a parameter, so C++ never needs a second material.
    """
    material_path = "{}/{}".format(MATERIAL_PACKAGE, MASTER_NAME)
    if unreal.EditorAssetLibrary.does_asset_exist(material_path):
        unreal.EditorAssetLibrary.delete_asset(material_path)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = tools.create_asset(MASTER_NAME, MATERIAL_PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    lib = unreal.MaterialEditingLibrary

    # UV tiling: one scalar drives both axes, so a wall and a floor can share the material and
    # still have texels the same physical size.
    tiling = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1100, 0)
    tiling.set_editor_property("parameter_name", "Tiling")
    tiling.set_editor_property("default_value", 1.0)

    tex_coord = lib.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1100, 150)

    uv = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -900, 80)
    lib.connect_material_expressions(tex_coord, "", uv, "A")
    lib.connect_material_expressions(tiling, "", uv, "B")

    def sampler(name, x, y, sampler_type, default_asset):
        node = lib.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("sampler_type", sampler_type)
        if default_asset:
            node.set_editor_property("texture", default_asset)
        lib.connect_material_expressions(uv, "", node, "UVs")
        return node

    # Placeholders so the material compiles before C++ assigns anything. Loaded defensively: which
    # engine textures are registered varies, and a missing placeholder must not fail the build.
    def maybe_load(path):
        return unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None

    default_color = maybe_load("/Engine/EngineMaterials/DefaultDiffuse")
    default_normal = maybe_load("/Engine/EngineMaterials/FlatNormal") or maybe_load("/Engine/EngineMaterials/DefaultNormal")

    base_color_map = sampler("BaseColorMap", -650, -300, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, default_color)
    normal_map = sampler("NormalMap", -650, 100, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, default_normal)
    arm_map = sampler("ARMMap", -650, 500, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, None)

    # Tint: lets one texture set serve several surfaces (faded wallpaper vs. the same paper still
    # holding its colour) and lets the whole room be pushed cooler or warmer from code.
    tint = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -650, -520)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

    tinted = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, -300)
    lib.connect_material_expressions(base_color_map, "RGB", tinted, "A")
    lib.connect_material_expressions(tint, "", tinted, "B")

    # A roughness trim, so a surface can be made wetter (the puddle, the window sill) without a
    # second texture set.
    rough_scale = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -650, 760)
    rough_scale.set_editor_property("parameter_name", "RoughnessScale")
    rough_scale.set_editor_property("default_value", 1.0)

    rough = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 560)
    lib.connect_material_expressions(arm_map, "G", rough, "A")
    lib.connect_material_expressions(rough_scale, "", rough, "B")

    lib.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(normal_map, "", unreal.MaterialProperty.MP_NORMAL)
    lib.connect_material_property(arm_map, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    lib.connect_material_property(arm_map, "B", unreal.MaterialProperty.MP_METALLIC)

    lib.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    unreal.log("Built {}".format(material_path))
    return material


def run():
    unreal.log("=== Room surface pipeline ===")
    imported = import_all_textures()
    unreal.log("Imported {} textures".format(len(imported)))
    build_master_material()
    unreal.log("=== Done ===")


run()
