"""
Turns everything in Art/Source/ into game-ready assets, headless.

    UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="Tools/build_art.py"

The project is code-first: no editor GUI, so the Content Browser and Material Editor are off the
table as authoring tools. Unreal's Python API does the same work, which means the whole art
pipeline is a script in the repo and can be re-run from scratch at any time:

    Tools/fetch_assets.py   downloads the CC0 source art
    Tools/build_art.py      imports it and builds materials  <- this file
    C++                     loads the results by path and assembles the room

What it produces:
    /Game/Materials/M_RoomSurface   one parameterised master material
    /Game/Materials/M_RoomGlass     translucent window glass
    /Game/Materials/M_RoomEmissive  unlit glow, for the lightning
    /Game/Materials/MI_<set>        an instance per texture set (floor, wallpaper, plaster, ...)
    /Game/Textures/T_*              the imported maps
    /Game/Meshes/<Model>            the imported props, with their materials already assigned
"""

import os
import unreal

PROJECT_DIR = unreal.Paths.project_dir()
TEXTURE_SOURCE = os.path.join(PROJECT_DIR, "Art", "Source", "Textures")
MODEL_SOURCE = os.path.join(PROJECT_DIR, "Art", "Source", "Models")

TEXTURE_PACKAGE = "/Game/Textures"
MATERIAL_PACKAGE = "/Game/Materials"
MESH_PACKAGE = "/Game/Meshes"
MASTER_PATH = MATERIAL_PACKAGE + "/M_RoomSurface"
MASKED_PATH = MATERIAL_PACKAGE + "/M_RoomSurfaceMasked"
GLASS_PATH = MATERIAL_PACKAGE + "/M_RoomGlass"
EMISSIVE_PATH = MATERIAL_PACKAGE + "/M_RoomEmissive"

ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MAT_LIB = unreal.MaterialEditingLibrary
ASSET_LIB = unreal.EditorAssetLibrary

# Map-name suffix -> (compression, is colour data). Names cover both the texture library's
# convention (Diffuse/nor_dx/arm) and the model downloads' convention (diff/nor_gl/arm).
MAP_KINDS = {
    "Diffuse": ("basecolor", unreal.TextureCompressionSettings.TC_DEFAULT, True),
    "diff": ("basecolor", unreal.TextureCompressionSettings.TC_DEFAULT, True),
    "nor_dx": ("normal", unreal.TextureCompressionSettings.TC_NORMALMAP, False),
    "nor_gl": ("normal", unreal.TextureCompressionSettings.TC_NORMALMAP, False),
    # AO / Roughness / Metallic packed into R/G/B. Linear, or the roughness comes out gamma-wrong.
    "arm": ("arm", unreal.TextureCompressionSettings.TC_MASKS, False),
    # Cutout mask. Models that have one need it: the lantern's glass and wire handle are modelled
    # as flat geometry with the shape punched out by alpha, so an opaque material turns the glass
    # into a solid dome — which then blocks the lamp's own light.
    "opacity": ("opacity", unreal.TextureCompressionSettings.TC_MASKS, False),
}

# Fallback tiling per set, used when nothing overrides it. In practice C++ computes tiling from
# each part's real size (see FRoomBuilder::Surface), because a texture photographed at 1m square
# has to repeat four times across a 4m floorboard or it stretches into smeared mush.
TILING = {
    "old_wooden_floor_02": 2.0,
    "decrepit_wallpaper": 2.0,
    "clay_plaster": 2.0,
    "ceiling_interior": 2.0,
    "weathered_brown_planks": 2.0,
    "raw_plank_wall": 2.0,
    "rough_linen": 2.0,
    "green_metal_rust": 1.5,
}


def suffix_of(stem):
    """Longest matching map suffix. Plain rsplit fails on 'nor_dx', which contains an underscore."""
    for name in sorted(MAP_KINDS, key=len, reverse=True):
        if stem.endswith("_" + name):
            return name
    return None


def import_texture(file_path, asset_name, map_kind):
    _, compression, srgb = MAP_KINDS[map_kind]
    asset_path = "{}/{}".format(TEXTURE_PACKAGE, asset_name)

    if not ASSET_LIB.does_asset_exist(asset_path):
        task = unreal.AssetImportTask()
        task.filename = file_path
        task.destination_path = TEXTURE_PACKAGE
        task.destination_name = asset_name
        task.automated = True
        task.replace_existing = True
        task.save = True
        ASSET_TOOLS.import_asset_tasks([task])

    texture = ASSET_LIB.load_asset(asset_path)
    if not texture:
        unreal.log_error("Failed to import " + file_path)
        return None

    texture.set_editor_property("compression_settings", compression)
    texture.set_editor_property("srgb", srgb)
    ASSET_LIB.save_loaded_asset(texture)
    return texture


def build_master_material(name=None, masked=False, defaults=None):
    """
    BaseColor = Texture(BaseColorMap, UV * Tiling) * Tint
    Normal    = Texture(NormalMap,    UV * Tiling)
    ARM       = Texture(ARMMap,       UV * Tiling) -> R:AO  G:Roughness  B:Metallic

    Every knob the room turns is a parameter, so one master material covers every surface and C++
    only ever makes instances of it.

    `defaults` must supply a real texture for every sampler, keyed basecolor/normal/arm. This is
    not cosmetic. A TextureSampleParameter2D with no texture is given /Engine/.../DefaultTexture by
    the engine, which is an sRGB colour texture; the normal and mask samplers then fail their type
    check, the *whole master* fails to compile, and every surface in the room silently falls back
    to WorldGridMaterial — the grey checkerboard. That is exactly what happened here, and it looks
    enough like "a texture" at a glance to hide for a long time. The defaults come from the
    project's own imported maps, so their compression settings match their samplers by construction.
    """
    name = name or "M_RoomSurface"
    defaults = defaults or {}
    path = "{}/{}".format(MATERIAL_PACKAGE, name)
    if ASSET_LIB.does_asset_exist(path):
        ASSET_LIB.delete_asset(path)

    material = ASSET_TOOLS.create_asset(name, MATERIAL_PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    if masked:
        # Masked, not translucent: a cutout is one alpha test per pixel and still writes depth, so
        # the glass keeps receiving shadows and Lumen keeps bouncing off it properly.
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        material.set_editor_property("two_sided", True)

    # Tiling is a 2-vector, not a scalar: the room is built from slabs and boards that are long in
    # one axis and narrow in the other, and a single number would squash the grain on every one of
    # them. C++ sets this per part from the part's own dimensions so texel density stays even
    # across the whole room.
    tiling = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -1100, 0)
    tiling.set_editor_property("parameter_name", "TilingXY")
    tiling.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 0.0, 1.0))

    tiling_rg = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionComponentMask, -950, 0)
    tiling_rg.set_editor_property("r", True)
    tiling_rg.set_editor_property("g", True)
    tiling_rg.set_editor_property("b", False)
    tiling_rg.set_editor_property("a", False)
    MAT_LIB.connect_material_expressions(tiling, "", tiling_rg, "")

    tex_coord = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1100, 150)

    uv = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, -900, 80)
    MAT_LIB.connect_material_expressions(tex_coord, "", uv, "A")
    MAT_LIB.connect_material_expressions(tiling_rg, "", uv, "B")

    def sampler(name, x, y, sampler_type, default_asset):
        node = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        node.set_editor_property("parameter_name", name)
        # Order matters: the texture has to be set before the sampler type, or the engine resets
        # the type to whatever suits the texture it is holding at the time.
        if default_asset:
            node.set_editor_property("texture", default_asset)
        else:
            unreal.log_error("Master {}: sampler {} has no default texture; the material will not compile".format(material.get_name(), name))
        node.set_editor_property("sampler_type", sampler_type)
        MAT_LIB.connect_material_expressions(uv, "", node, "UVs")
        return node

    base_color_map = sampler("BaseColorMap", -650, -300, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
                             defaults.get("basecolor"))
    normal_map = sampler("NormalMap", -650, 100, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                         defaults.get("normal"))
    # Masks, not Linear Color: the AO/Roughness/Metallic pack is imported as TC_MASKS, and the
    # sampler type has to agree with the compression settings or the material fails to compile.
    arm_map = sampler("ARMMap", -650, 500, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
                      defaults.get("arm"))

    # Tint lets one texture set serve several surfaces — the same wallpaper faded and unfaded — and
    # lets the whole room be pushed colder from code without re-authoring anything.
    tint = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -650, -520)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

    tinted = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, -300)
    MAT_LIB.connect_material_expressions(base_color_map, "RGB", tinted, "A")
    MAT_LIB.connect_material_expressions(tint, "", tinted, "B")

    # A roughness trim, so a surface can be made wet (the puddle, the sill under the broken pane)
    # without a second texture set.
    rough_scale = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -650, 760)
    rough_scale.set_editor_property("parameter_name", "RoughnessScale")
    rough_scale.set_editor_property("default_value", 1.0)

    rough = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 560)
    MAT_LIB.connect_material_expressions(arm_map, "G", rough, "A")
    MAT_LIB.connect_material_expressions(rough_scale, "", rough, "B")

    MAT_LIB.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MAT_LIB.connect_material_property(normal_map, "", unreal.MaterialProperty.MP_NORMAL)
    MAT_LIB.connect_material_property(arm_map, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    MAT_LIB.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MAT_LIB.connect_material_property(arm_map, "B", unreal.MaterialProperty.MP_METALLIC)

    if masked:
        opacity_map = sampler("OpacityMap", -650, 900, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
                              defaults.get("arm"))
        MAT_LIB.connect_material_property(opacity_map, "R", unreal.MaterialProperty.MP_OPACITY_MASK)

    MAT_LIB.recompile_material(material)
    ASSET_LIB.save_loaded_asset(material)
    unreal.log("Built " + path)
    return material


def build_glass_master():
    """
    The window glass. Translucent, not masked — the room's one surface that has to be *seen
    through*, because the storm is the scene's second light source and its only view.

        BaseColor = Tint
        Opacity   = lerp(Opacity, 1, Fresnel)      glass goes opaque at a grazing angle
        Roughness = lerp(Rough, Rough + Smear, Noise)   decades of grime, in patches

    The fresnel term is what stops the panes reading as holes: seen straight on the glass is
    nearly clear, and towards the edge of the window it turns into a pale sheet catching the
    lightning, exactly as real glass does.
    """
    if ASSET_LIB.does_asset_exist(GLASS_PATH):
        ASSET_LIB.delete_asset(GLASS_PATH)

    material = ASSET_TOOLS.create_asset("M_RoomGlass", MATERIAL_PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("two_sided", True)
    # Lit translucency. The default volumetric mode gives a flat, unlit sheet; surface mode is what
    # lets a lightning flash actually glint off the pane. The two surface modes are tried in order
    # of preference and the enum is looked up by name, because these entries have been renamed
    # between engine versions and a hard reference would break the whole pipeline over a lighting
    # nicety.
    for mode_name in ("TLM_SURFACE_PER_PIXEL_LIGHTING", "TLM_SURFACE"):
        mode = getattr(unreal.TranslucencyLightingMode, mode_name, None)
        if mode is None:
            continue
        material.set_editor_property("translucency_lighting_mode", mode)
        unreal.log("Glass: translucency lighting mode {}".format(mode_name))
        break
    else:
        unreal.log_warning("Glass: no surface translucency lighting mode found; glass will be unlit")

    tint = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, -300)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.16, 0.20, 0.23, 1.0))

    opacity = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 0)
    opacity.set_editor_property("parameter_name", "Opacity")
    opacity.set_editor_property("default_value", 0.32)

    fresnel = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionFresnel, -700, 160)
    fresnel.set_editor_property("exponent", 3.5)
    fresnel.set_editor_property("base_reflect_fraction", 0.04)

    edge_opacity = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -400, 60)
    MAT_LIB.connect_material_expressions(opacity, "", edge_opacity, "A")
    MAT_LIB.connect_material_expressions(fresnel, "", edge_opacity, "Alpha")
    edge_opacity.set_editor_property("const_b", 1.0)

    rough_base = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 400)
    rough_base.set_editor_property("parameter_name", "RoughnessBase")
    rough_base.set_editor_property("default_value", 0.08)

    smear = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 500)
    smear.set_editor_property("parameter_name", "RoughnessSmear")
    smear.set_editor_property("default_value", 0.45)

    rough_dirty = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionAdd, -520, 450)
    MAT_LIB.connect_material_expressions(rough_base, "", rough_dirty, "A")
    MAT_LIB.connect_material_expressions(smear, "", rough_dirty, "B")

    # World-position noise rather than a UV texture: the panes are separate slabs of geometry, so
    # noise in world space runs across the whole window as one continuous film of dirt instead of
    # restarting in every pane.
    noise = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionNoise, -700, 620)
    noise.set_editor_property("scale", 0.06)
    noise.set_editor_property("levels", 3)
    noise.set_editor_property("output_min", 0.0)
    noise.set_editor_property("output_max", 1.0)

    rough = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -300, 460)
    MAT_LIB.connect_material_expressions(rough_base, "", rough, "A")
    MAT_LIB.connect_material_expressions(rough_dirty, "", rough, "B")
    MAT_LIB.connect_material_expressions(noise, "", rough, "Alpha")

    MAT_LIB.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MAT_LIB.connect_material_property(edge_opacity, "", unreal.MaterialProperty.MP_OPACITY)
    MAT_LIB.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    MAT_LIB.recompile_material(material)
    ASSET_LIB.save_loaded_asset(material)
    unreal.log("Built " + GLASS_PATH)
    return material


def build_emissive_master():
    """
    Unlit glow: EmissiveColor = Tint * Intensity, and nothing else.

    Unlit on purpose — a lightning bolt is not a surface being lit, it is the light. Shading it
    would make it dim when the room is dim, which is exactly backwards.
    """
    if ASSET_LIB.does_asset_exist(EMISSIVE_PATH):
        ASSET_LIB.delete_asset(EMISSIVE_PATH)

    material = ASSET_TOOLS.create_asset("M_RoomEmissive", MATERIAL_PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

    tint = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -600, -100)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.8, 0.88, 1.0, 1.0))

    intensity = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -600, 100)
    intensity.set_editor_property("parameter_name", "Intensity")
    intensity.set_editor_property("default_value", 1.0)

    glow = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 0)
    MAT_LIB.connect_material_expressions(tint, "", glow, "A")
    MAT_LIB.connect_material_expressions(intensity, "", glow, "B")

    MAT_LIB.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    MAT_LIB.recompile_material(material)
    ASSET_LIB.save_loaded_asset(material)
    unreal.log("Built " + EMISSIVE_PATH)
    return material


def make_instance(name, master, maps, tiling=1.0, masked_master=None):
    """One MaterialInstanceConstant wired to a set of maps. Re-created each run so the script is idempotent."""
    path = "{}/{}".format(MATERIAL_PACKAGE, name)
    if ASSET_LIB.does_asset_exist(path):
        ASSET_LIB.delete_asset(path)

    instance = ASSET_TOOLS.create_asset(name, MATERIAL_PACKAGE, unreal.MaterialInstanceConstant,
                                        unreal.MaterialInstanceConstantFactoryNew())

    # A set that ships an opacity map is a cutout and has to hang off the masked master.
    use_masked = bool(masked_master and maps.get("opacity"))
    MAT_LIB.set_material_instance_parent(instance, masked_master if use_masked else master)

    for slot, parameter in (("basecolor", "BaseColorMap"), ("normal", "NormalMap"), ("arm", "ARMMap"), ("opacity", "OpacityMap")):
        if maps.get(slot):
            MAT_LIB.set_material_instance_texture_parameter_value(instance, parameter, maps[slot])

    MAT_LIB.set_material_instance_vector_parameter_value(instance, "TilingXY", unreal.LinearColor(tiling, tiling, 0.0, 1.0))
    ASSET_LIB.save_loaded_asset(instance)
    return instance


def import_surface_textures():
    """Imports the flat texture library and returns {set name: {slot: texture}}."""
    if not os.path.isdir(TEXTURE_SOURCE):
        unreal.log_error("No textures at " + TEXTURE_SOURCE)
        return {}

    sets = {}
    for file_name in sorted(os.listdir(TEXTURE_SOURCE)):
        if not file_name.lower().endswith((".jpg", ".png")):
            continue

        stem = os.path.splitext(file_name)[0]
        map_kind = suffix_of(stem)
        if not map_kind:
            unreal.log_warning("Skipping {} — unrecognised map suffix".format(file_name))
            continue

        set_name = stem[: -(len(map_kind) + 1)]
        texture = import_texture(os.path.join(TEXTURE_SOURCE, file_name), "T_" + stem, map_kind)
        if texture:
            sets.setdefault(set_name, {})[MAP_KINDS[map_kind][0]] = texture

    return sets


def pick_master_defaults(sets):
    """
    One real texture per sampler, for the master to hold as its default.

    Taken from the project's own library rather than from /Engine, because the engine's stand-in
    textures are colour textures and a colour texture in a normal or mask sampler is a material
    compile error. Any set will do — every instance overrides all three — so this prefers the
    plaster, which is the most neutral surface in the room, and falls back to whatever is there.
    """
    defaults = {}
    preferred = ["clay_plaster", "decrepit_wallpaper", "old_wooden_floor_02"]
    order = [name for name in preferred if name in sets] + sorted(sets.keys())

    for slot in ("basecolor", "normal", "arm"):
        for set_name in order:
            texture = sets.get(set_name, {}).get(slot)
            if texture:
                defaults[slot] = texture
                unreal.log("Master default {} = {}".format(slot, texture.get_name()))
                break
        else:
            unreal.log_error("No texture available for the {} sampler's default".format(slot))

    return defaults


def build_surface_instances(master, sets):
    """One material instance per texture set."""
    instances = {}
    for set_name, maps in sorted(sets.items()):
        instances[set_name] = make_instance("MI_" + set_name, master, maps, TILING.get(set_name, 2.0))
        unreal.log("Surface MI_{} ({} maps)".format(set_name, len(maps)))

    return instances


def import_mesh(fbx_path, asset_name):
    asset_path = "{}/{}".format(MESH_PACKAGE, asset_name)
    if ASSET_LIB.does_asset_exist(asset_path):
        return ASSET_LIB.load_asset(asset_path)

    task = unreal.AssetImportTask()
    task.filename = fbx_path
    task.destination_path = MESH_PACKAGE
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = True

    try:
        options = unreal.FbxImportUI()
        options.set_editor_property("import_mesh", True)
        options.set_editor_property("import_as_skeletal", False)
        # The source materials are useless to us — Poly Haven ships the maps as loose files and we
        # rebuild them as instances of our own master, so importing them would only add clutter.
        options.set_editor_property("import_materials", False)
        options.set_editor_property("import_textures", False)
        mesh_data = options.static_mesh_import_data
        mesh_data.set_editor_property("combine_meshes", True)
        mesh_data.set_editor_property("generate_lightmap_u_vs", False)  # everything here is dynamically lit
        task.options = options
    except Exception as error:
        unreal.log_warning("FbxImportUI unavailable ({}), importing with defaults".format(error))

    ASSET_TOOLS.import_asset_tasks([task])
    return ASSET_LIB.load_asset(asset_path)


MASKED_MASTER = None


def build_models(master):
    """Imports each prop and wires its own textures onto instances of the master material."""
    if not os.path.isdir(MODEL_SOURCE):
        unreal.log_warning("No models at " + MODEL_SOURCE)
        return

    mesh_subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)

    for model_id in sorted(os.listdir(MODEL_SOURCE)):
        folder = os.path.join(MODEL_SOURCE, model_id)
        if not os.path.isdir(folder):
            continue

        fbx = next((f for f in sorted(os.listdir(folder)) if f.lower().endswith(".fbx")), None)
        if not fbx:
            unreal.log_warning("No fbx in " + folder)
            continue

        # Model textures are named <Model>_<slot>_<map>_<res>. Group them by slot so each of the
        # mesh's material slots gets the maps that belong to it.
        by_slot = {}
        texture_folder = os.path.join(folder, "textures")
        if os.path.isdir(texture_folder):
            for file_name in sorted(os.listdir(texture_folder)):
                if not file_name.lower().endswith((".jpg", ".png")):
                    continue

                stem = os.path.splitext(file_name)[0]
                parts = stem.split("_")
                if len(parts) < 2:
                    continue
                if parts[-1].endswith("k") and parts[-1][:-1].isdigit():
                    parts = parts[:-1]  # drop the trailing resolution tag

                map_kind = suffix_of("_".join(parts))
                if not map_kind:
                    continue

                slot = "_".join(parts[: -len(map_kind.split("_"))])
                slot = slot[len(model_id):].strip("_") or "default"

                texture = import_texture(os.path.join(texture_folder, file_name), "T_" + stem, map_kind)
                if texture:
                    by_slot.setdefault(slot.lower(), {})[MAP_KINDS[map_kind][0]] = texture

        mesh = import_mesh(os.path.join(folder, fbx), model_id)
        if not mesh:
            unreal.log_error("Failed to import mesh " + model_id)
            continue

        # Simple collision. Without it a line trace by channel passes straight through the prop,
        # so the detective could look at a cabinet and get no interaction prompt.
        try:
            if not mesh.get_editor_property("body_setup") or len(mesh.get_editor_property("body_setup").aggregate_geom.box_elems) == 0:
                mesh_subsystem.add_simple_collisions(mesh, unreal.ScriptingCollisionShapeType.BOX)
        except Exception as error:
            unreal.log_warning("Could not add collision to {} ({})".format(model_id, error))

        slot_names = [str(m.material_slot_name).lower() for m in mesh.static_materials]
        for index, slot_name in enumerate(slot_names):
            # Match the mesh's slot to a texture group: exact name first, then containment, then
            # whatever single group exists (the common case — one material per prop).
            maps = by_slot.get(slot_name)
            if not maps:
                maps = next((v for k, v in by_slot.items() if k in slot_name or slot_name in k), None)
            if not maps and len(by_slot) == 1:
                maps = list(by_slot.values())[0]
            if not maps:
                unreal.log_warning("{} slot '{}' has no textures".format(model_id, slot_name))
                continue

            instance = make_instance("MI_{}_{}".format(model_id, slot_name or index), master, maps, 1.0, MASKED_MASTER)
            mesh.set_material(index, instance)

        ASSET_LIB.save_loaded_asset(mesh)
        unreal.log("Mesh {} ({} slots, {} texture groups)".format(model_id, len(slot_names), len(by_slot)))


def run():
    global MASKED_MASTER
    unreal.log("=== Room art pipeline ===")

    # Textures first: the masters cannot be built until there is a correctly typed texture to put
    # in each of their samplers.
    sets = import_surface_textures()
    defaults = pick_master_defaults(sets)

    master = build_master_material(defaults=defaults)
    MASKED_MASTER = build_master_material("M_RoomSurfaceMasked", masked=True, defaults=defaults)
    build_glass_master()
    build_emissive_master()

    build_surface_instances(master, sets)
    build_models(master)
    unreal.log("=== Done ===")


run()
