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
    /Game/Materials/M_RoomDecal     projected grime, damp and stains — ragged-edged, not rectangles
    /Game/Materials/M_RoomCrack     projected fissures, drawn from noise rather than from a texture
    /Game/Materials/M_RoomWeb       cobweb: a net of filaments, also drawn from noise
    /Game/Materials/M_RoomGlassCrack the fracture on a struck pane, from the baked crack map
    /Game/Materials/M_RoomStainedGlass  the stair window's glass, from Tools/make_stained_glass.py
    /Game/Materials/M_RoomLeadCame      the lead that glass is held in, which is what casts shadow
    /Game/Materials/M_RoomSky       the night sky outside: clouds, moon, lightning (Tools/make_storm_art.py)
    /Game/Materials/M_RoomFoliage   the forest's trees, with their wind
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
DECAL_PATH = MATERIAL_PACKAGE + "/M_RoomDecal"
CRACK_PATH = MATERIAL_PACKAGE + "/M_RoomCrack"
WEB_PATH = MATERIAL_PACKAGE + "/M_RoomWeb"
GLASS_CRACK_PATH = MATERIAL_PACKAGE + "/M_RoomGlassCrack"
STAINED_GLASS_PATH = MATERIAL_PACKAGE + "/M_RoomStainedGlass"
LEAD_CAME_PATH = MATERIAL_PACKAGE + "/M_RoomLeadCame"

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
    "cracked_concrete_wall": 2.0,
    "damaged_plaster": 2.0,
    "plastered_stone_wall": 2.0,
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


def import_texture(file_path, asset_name, map_kind, force=False):
    _, compression, srgb = MAP_KINDS[map_kind]
    asset_path = "{}/{}".format(TEXTURE_PACKAGE, asset_name)

    # force: the source is one of ours and has been re-baked (Tools/make_nursery_art.py), so the
    # asset already in the project is stale. Downloaded maps never change and are never forced.
    if force or not ASSET_LIB.does_asset_exist(asset_path):
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

    scaled_uv = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, -900, 80)
    MAT_LIB.connect_material_expressions(tex_coord, "", scaled_uv, "A")
    MAT_LIB.connect_material_expressions(tiling_rg, "", scaled_uv, "B")

    # Where in the photograph this particular piece of geometry is cut from.
    #
    # Without it the room is built out of identical crops: every basic-shape face maps 0..1, so a
    # fifty-centimetre strip of a hundred-and-sixty-centimetre wallpaper samples the same quarter
    # of the same photo as every other strip. Correct texel density, and yet the wall comes out as
    # rows of identical flat rectangles — which is exactly how it looked. C++ hands each part one
    # of a handful of offsets, picked from where the part sits in the room.
    uv_offset = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -1100, 300)
    uv_offset.set_editor_property("parameter_name", "UVOffset")
    uv_offset.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))

    offset_rg = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionComponentMask, -950, 300)
    offset_rg.set_editor_property("r", True)
    offset_rg.set_editor_property("g", True)
    offset_rg.set_editor_property("b", False)
    offset_rg.set_editor_property("a", False)
    MAT_LIB.connect_material_expressions(uv_offset, "", offset_rg, "")

    uv = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionAdd, -800, 160)
    MAT_LIB.connect_material_expressions(scaled_uv, "", uv, "A")
    MAT_LIB.connect_material_expressions(offset_rg, "", uv, "B")

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

    allow_instancing(material)
    MAT_LIB.recompile_material(material)
    ASSET_LIB.save_loaded_asset(material)
    unreal.log("Built " + path)
    return material


def allow_instancing(material):
    """
    Marks a master as usable on instanced static meshes.

    Without the flag the material simply is not compiled for that vertex factory, and in game an
    instanced mesh wearing it — or any instance of it — draws with the engine's default material
    instead: a hall full of white balusters. The editor compiles missing usages on demand, which
    is why it only shows up in -game.
    """
    MAT_LIB.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)


def rebuild_usage():
    """The flag above, on the masters already in the project, without rebuilding them."""
    for path in (MASTER_PATH, MASKED_PATH):
        material = ASSET_LIB.load_asset(path)
        if not material:
            unreal.log_error("No " + path)
            continue
        allow_instancing(material)
        MAT_LIB.recompile_material(material)
        ASSET_LIB.save_loaded_asset(material)
        unreal.log("Instancing allowed on " + path)


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



def make_decal_domain(material):
    """
    Deferred-decal domain, set defensively.

    A decal material that silently stays in the Surface domain is worse than no decal at all: it
    becomes an unlit quad hanging in front of the wall. So the domain is looked up by name and its
    absence is reported rather than swallowed.

    Nothing sets DecalBlendMode. It was the way to say what a decal writes into the G-buffer, and
    as of 5.8 it is deprecated and refuses to be set at all — a decal now uses the ordinary
    material blend mode like any other material, and Translucent on a decal means colour, normal
    and roughness blended over what is behind it, which is exactly what a stain does.
    """
    domain = getattr(unreal.MaterialDomain, "MD_DEFERRED_DECAL", None)
    if domain is None:
        unreal.log_error("No deferred-decal material domain in this engine build")
        return False

    material.set_editor_property("material_domain", domain)
    return True


def build_patch_alpha(material, x, y, noise_scale=0.05, noise_amount=0.9, contrast=2.6):
    """
    The shape of a stain: a soft round patch with its edge chewed away by noise.

    This is the whole answer to the room's rectangle problem. Damage was being drawn as slabs of
    flat colour laid on the wall, and no amount of choosing a better colour fixes a rectangle —
    the eye reads the straight edge, not the tone. A decal whose alpha is a radial falloff
    perturbed by world-space noise has no straight edge anywhere on it, and because the noise is
    sampled in world space two overlapping stains tear along the same grain instead of crossing.

    Returns the node carrying the 0..1 coverage, before the Opacity parameter scales it.
    """
    uv = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, x, y)

    centre = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionConstant2Vector, x, y + 130)
    centre.set_editor_property("r", 0.5)
    centre.set_editor_property("g", 0.5)

    distance = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionDistance, x + 200, y + 40)
    MAT_LIB.connect_material_expressions(uv, "", distance, "A")
    MAT_LIB.connect_material_expressions(centre, "", distance, "B")

    # 2.0 puts the zero point of the falloff on the edge midpoints of the decal box, so a stain
    # fills the size it was asked for rather than a circle inscribed in it.
    spread = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, x + 380, y + 40)
    MAT_LIB.connect_material_expressions(distance, "", spread, "A")
    spread.set_editor_property("const_b", 2.0)

    falloff = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionOneMinus, x + 540, y + 40)
    MAT_LIB.connect_material_expressions(spread, "", falloff, "")

    # Centred on zero, so it pushes the edge of the patch out as often as it eats into it.
    noise = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionNoise, x, y + 280)
    noise.set_editor_property("scale", noise_scale)
    noise.set_editor_property("levels", 4)
    noise.set_editor_property("output_min", -0.5)
    noise.set_editor_property("output_max", 0.5)

    noise_amount_param = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x, y + 430)
    noise_amount_param.set_editor_property("parameter_name", "EdgeNoise")
    noise_amount_param.set_editor_property("default_value", noise_amount)

    ragged = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, x + 280, y + 320)
    MAT_LIB.connect_material_expressions(noise, "", ragged, "A")
    MAT_LIB.connect_material_expressions(noise_amount_param, "", ragged, "B")

    perturbed = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionAdd, x + 700, y + 160)
    MAT_LIB.connect_material_expressions(falloff, "", perturbed, "A")
    MAT_LIB.connect_material_expressions(ragged, "", perturbed, "B")

    # Contrast turns the gentle gradient into something with a discernible, if torn, boundary —
    # damp has an edge, it just is not a straight one.
    sharpened = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, x + 860, y + 160)
    MAT_LIB.connect_material_expressions(perturbed, "", sharpened, "A")
    sharpened.set_editor_property("const_b", contrast)

    coverage = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionClamp, x + 1000, y + 160)
    MAT_LIB.connect_material_expressions(sharpened, "", coverage, "")
    coverage.set_editor_property("min_default", 0.0)
    coverage.set_editor_property("max_default", 1.0)
    return coverage


def build_decal_master(defaults=None):
    """
    Grime, damp and blown plaster, projected onto whatever is behind them.

        BaseColor = Texture(BaseColorMap) * Tint
        Roughness = ARM.G * RoughnessScale
        Normal    = Texture(NormalMap)
        Opacity   = raggedPatch * Opacity

    A decal rather than a slab of geometry for three reasons: it takes the shape of what it lands
    on instead of hovering a millimetre off it, it cannot z-fight with the wall, and its alpha can
    be any shape at all — which is what lets the damage stop being rectangular.
    """
    defaults = defaults or {}
    if ASSET_LIB.does_asset_exist(DECAL_PATH):
        ASSET_LIB.delete_asset(DECAL_PATH)

    material = ASSET_TOOLS.create_asset("M_RoomDecal", MATERIAL_PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    make_decal_domain(material)

    tiling = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -1500, -400)
    tiling.set_editor_property("parameter_name", "TilingXY")
    tiling.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 0.0, 1.0))

    tiling_rg = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionComponentMask, -1330, -400)
    tiling_rg.set_editor_property("r", True)
    tiling_rg.set_editor_property("g", True)
    MAT_LIB.connect_material_expressions(tiling, "", tiling_rg, "")

    tex_coord = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1500, -260)

    scaled_uv = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, -1150, -330)
    MAT_LIB.connect_material_expressions(tex_coord, "", scaled_uv, "A")
    MAT_LIB.connect_material_expressions(tiling_rg, "", scaled_uv, "B")

    uv_offset = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -1500, -120)
    uv_offset.set_editor_property("parameter_name", "UVOffset")
    uv_offset.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))

    offset_rg = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionComponentMask, -1330, -120)
    offset_rg.set_editor_property("r", True)
    offset_rg.set_editor_property("g", True)
    MAT_LIB.connect_material_expressions(uv_offset, "", offset_rg, "")

    uv = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionAdd, -1000, -260)
    MAT_LIB.connect_material_expressions(scaled_uv, "", uv, "A")
    MAT_LIB.connect_material_expressions(offset_rg, "", uv, "B")

    def sampler(name, x, y, sampler_type, default_asset):
        node = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        node.set_editor_property("parameter_name", name)
        if default_asset:
            node.set_editor_property("texture", default_asset)
        else:
            unreal.log_error("M_RoomDecal: sampler {} has no default texture; it will not compile".format(name))
        node.set_editor_property("sampler_type", sampler_type)
        MAT_LIB.connect_material_expressions(uv, "", node, "UVs")
        return node

    base_color_map = sampler("BaseColorMap", -800, -700, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, defaults.get("basecolor"))
    normal_map = sampler("NormalMap", -800, -400, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, defaults.get("normal"))
    arm_map = sampler("ARMMap", -800, -100, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, defaults.get("arm"))

    tint = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -800, -900)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

    tinted = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, -400, -800)
    MAT_LIB.connect_material_expressions(base_color_map, "RGB", tinted, "A")
    MAT_LIB.connect_material_expressions(tint, "", tinted, "B")

    rough_scale = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -800, 100)
    rough_scale.set_editor_property("parameter_name", "RoughnessScale")
    rough_scale.set_editor_property("default_value", 1.0)

    rough = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, -400, 0)
    MAT_LIB.connect_material_expressions(arm_map, "G", rough, "A")
    MAT_LIB.connect_material_expressions(rough_scale, "", rough, "B")

    coverage = build_patch_alpha(material, -1500, 300)

    opacity_param = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -600, 700)
    opacity_param.set_editor_property("parameter_name", "Opacity")
    opacity_param.set_editor_property("default_value", 0.85)

    opacity = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 500)
    MAT_LIB.connect_material_expressions(coverage, "", opacity, "A")
    MAT_LIB.connect_material_expressions(opacity_param, "", opacity, "B")

    MAT_LIB.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MAT_LIB.connect_material_property(normal_map, "", unreal.MaterialProperty.MP_NORMAL)
    MAT_LIB.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MAT_LIB.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)

    MAT_LIB.recompile_material(material)
    ASSET_LIB.save_loaded_asset(material)
    unreal.log("Built " + DECAL_PATH)
    return material


def build_crack_master():
    """
    Fissures in the plaster, drawn rather than photographed.

        Opacity   = thin(noise) * raggedPatch * Opacity
        BaseColor = Tint — near black, because a crack is an absence of surface

    A crack is a line, and there is no CC0 texture of *just* lines on transparency to hand. What
    there is, in every noise field, is the contour where it crosses its own midpoint: take the
    distance from that midpoint and keep only what lies very close to it, and the result is a
    branching, snaking filament of exactly the kind plaster splits along. Sharpness is how narrow
    the filament is, which is to say how fine the crack is.
    """
    if ASSET_LIB.does_asset_exist(CRACK_PATH):
        ASSET_LIB.delete_asset(CRACK_PATH)

    material = ASSET_TOOLS.create_asset("M_RoomCrack", MATERIAL_PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    make_decal_domain(material)

    tint = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, -300)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.012, 0.011, 0.010, 1.0))

    rough = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, -150)
    rough.set_editor_property("parameter_name", "RoughnessScale")
    rough.set_editor_property("default_value", 1.0)

    # World-space, so a crack runs across a corner unbroken instead of restarting on each wall.
    noise = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionNoise, -1500, 100)
    noise.set_editor_property("scale", 0.035)
    noise.set_editor_property("levels", 3)
    noise.set_editor_property("output_min", -0.5)
    noise.set_editor_property("output_max", 0.5)

    distance_from_contour = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionAbs, -1250, 100)
    MAT_LIB.connect_material_expressions(noise, "", distance_from_contour, "")

    sharpness = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1500, 260)
    sharpness.set_editor_property("parameter_name", "Sharpness")
    sharpness.set_editor_property("default_value", 26.0)

    widened = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, -1050, 140)
    MAT_LIB.connect_material_expressions(distance_from_contour, "", widened, "A")
    MAT_LIB.connect_material_expressions(sharpness, "", widened, "B")

    filament = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionOneMinus, -880, 140)
    MAT_LIB.connect_material_expressions(widened, "", filament, "")

    filament_clamped = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionClamp, -720, 140)
    MAT_LIB.connect_material_expressions(filament, "", filament_clamped, "")
    filament_clamped.set_editor_property("min_default", 0.0)
    filament_clamped.set_editor_property("max_default", 1.0)

    # The same ragged patch as the stains, used here to say where the crack network exists at all:
    # a wall covered evenly in cracks reads as crazed pottery, not as a wall that has moved.
    coverage = build_patch_alpha(material, -1500, 500, noise_scale=0.02, noise_amount=0.55, contrast=2.2)

    local = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, -400, 400)
    MAT_LIB.connect_material_expressions(filament_clamped, "", local, "A")
    MAT_LIB.connect_material_expressions(coverage, "", local, "B")

    opacity_param = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -400, 620)
    opacity_param.set_editor_property("parameter_name", "Opacity")
    opacity_param.set_editor_property("default_value", 1.0)

    opacity = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, -200, 480)
    MAT_LIB.connect_material_expressions(local, "", opacity, "A")
    MAT_LIB.connect_material_expressions(opacity_param, "", opacity, "B")

    MAT_LIB.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MAT_LIB.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MAT_LIB.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)

    MAT_LIB.recompile_material(material)
    ASSET_LIB.save_loaded_asset(material)
    unreal.log("Built " + CRACK_PATH)
    return material




def build_web_master():
    """
    Cobweb: strands, not a sheet.

        Opacity   = thin(noise) * raggedPatch * Opacity
        BaseColor = Tint

    Webs were flat opaque slabs of pale grey slung across the top corners, and at the size a web
    has to be to span a corner that is a sheet of card hanging off the ceiling — by some margin
    the worst thing in the room. The alpha uses the same noise-contour trick the cracks do, run at
    a much finer scale: what is left is a net of filaments with holes between them, which is what
    a web is. Translucent and lit, so the lantern catches the strands facing it and the rest of the
    net stays where it belongs, which is almost invisible until you are close.
    """
    if ASSET_LIB.does_asset_exist(WEB_PATH):
        ASSET_LIB.delete_asset(WEB_PATH)

    material = ASSET_TOOLS.create_asset("M_RoomWeb", MATERIAL_PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("two_sided", True)
    for mode_name in ("TLM_SURFACE_PER_PIXEL_LIGHTING", "TLM_SURFACE"):
        mode = getattr(unreal.TranslucencyLightingMode, mode_name, None)
        if mode is not None:
            material.set_editor_property("translucency_lighting_mode", mode)
            break

    tint = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, -300)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.30, 0.29, 0.27, 1.0))

    rough = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, -150)
    rough.set_editor_property("parameter_name", "RoughnessScale")
    rough.set_editor_property("default_value", 0.85)

    noise = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionNoise, -1500, 100)
    noise.set_editor_property("scale", 0.45)
    noise.set_editor_property("levels", 2)
    noise.set_editor_property("output_min", -0.5)
    noise.set_editor_property("output_max", 0.5)
    # Turbulence off, and this is the whole reason the webs were sheets rather than strands.
    #
    # The trick below takes the contour of the field — the set of points where it crosses zero —
    # and that is a thin curve only if the field is *signed*. A Noise node defaults to turbulence,
    # which folds the field at zero before remapping it, so it never crosses zero: what Abs() then
    # measures is distance from the middle of the range, and the middle of a folded field is a
    # broad region, not a line. Every web in the room came out as an opaque pale patch of exactly
    # that shape, slung across a ceiling corner a metre wide.
    noise.set_editor_property("turbulence", False)

    contour = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionAbs, -1250, 100)
    MAT_LIB.connect_material_expressions(noise, "", contour, "")

    sharpness = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1500, 260)
    sharpness.set_editor_property("parameter_name", "Sharpness")
    sharpness.set_editor_property("default_value", 11.0)

    widened = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, -1050, 140)
    MAT_LIB.connect_material_expressions(contour, "", widened, "A")
    MAT_LIB.connect_material_expressions(sharpness, "", widened, "B")

    strands = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionOneMinus, -880, 140)
    MAT_LIB.connect_material_expressions(widened, "", strands, "")

    strands_clamped = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionClamp, -720, 140)
    MAT_LIB.connect_material_expressions(strands, "", strands_clamped, "")
    strands_clamped.set_editor_property("min_default", 0.0)
    strands_clamped.set_editor_property("max_default", 1.0)

    # A web is anchored at its corners and thin in the middle; the patch falloff is what gives it
    # an outline other than the square of the geometry it is drawn on.
    coverage = build_patch_alpha(material, -1500, 500, noise_scale=0.08, noise_amount=0.7, contrast=2.0)

    netted = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, -400, 400)
    MAT_LIB.connect_material_expressions(strands_clamped, "", netted, "A")
    MAT_LIB.connect_material_expressions(coverage, "", netted, "B")

    opacity_param = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -400, 620)
    opacity_param.set_editor_property("parameter_name", "Opacity")
    opacity_param.set_editor_property("default_value", 0.55)

    opacity = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, -200, 480)
    MAT_LIB.connect_material_expressions(netted, "", opacity, "A")
    MAT_LIB.connect_material_expressions(opacity_param, "", opacity, "B")

    MAT_LIB.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MAT_LIB.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MAT_LIB.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)

    MAT_LIB.recompile_material(material)
    ASSET_LIB.save_loaded_asset(material)
    unreal.log("Built " + WEB_PATH)
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
    only = only_requested()
    for file_name in sorted(os.listdir(TEXTURE_SOURCE)):
        if not file_name.lower().endswith((".jpg", ".png")):
            continue

        stem = os.path.splitext(file_name)[0]
        map_kind = suffix_of(stem)
        if not map_kind:
            unreal.log_warning("Skipping {} — unrecognised map suffix".format(file_name))
            continue

        set_name = stem[: -(len(map_kind) + 1)]
        # A baked set named in -ArtOnly is re-imported over itself; nothing else is touched twice.
        force = bool(only and set_name.lower() in only)
        texture = import_texture(os.path.join(TEXTURE_SOURCE, file_name), "T_" + stem, map_kind, force)
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
    preferred = ["cracked_concrete_wall", "decrepit_wallpaper", "old_wooden_floor_02"]
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


# Models generated in Blender by our own scripts (Tools/make_piano.py, Tools/make_nursery.py) rather
# than downloaded. They carry hardened custom normals — flat faces kept flat right up to their
# bevels — so the import takes the normals from the file instead of recomputing them, and they are
# re-imported every run, because the script is where they are edited. The piano predates the
# marker; make_nursery.py writes an empty GENERATED file beside each FBX instead of growing a list.
GENERATED_MODELS = {"grand_piano", "piano_bench"}


def import_mesh(fbx_path, asset_name):
    asset_path = "{}/{}".format(MESH_PACKAGE, asset_name)
    generated = asset_name in GENERATED_MODELS or os.path.isfile(os.path.join(os.path.dirname(fbx_path), "GENERATED"))
    if ASSET_LIB.does_asset_exist(asset_path) and not generated:
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
        if generated:
            mesh_data.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
        task.options = options
    except Exception as error:
        unreal.log_warning("FbxImportUI unavailable ({}), importing with defaults".format(error))

    ASSET_TOOLS.import_asset_tasks([task])
    return ASSET_LIB.load_asset(asset_path)


MASKED_MASTER = None


NEUTRAL_ARM = None


def neutral_arm():
    """A flat AO/Roughness/Metallic map: white AO, mid roughness, no metal.

    Poly Haven ships models with roughness and metallic as separate maps and no packed ARM, so a
    prop slot almost never has one — and an unset ARM sampler falls through to the master's
    default, which is one of the wall sets. Every prop in the room was therefore taking its
    roughness and its ambient occlusion from a photograph of cracked concrete: shiny where the
    concrete was smooth, and shaded dark along every crack in a wall it has nothing to do with.

    Neutral is not as good as the prop's own maps, which would need the three greyscales packed
    into one texture. It is a great deal better than another surface's.
    """
    global NEUTRAL_ARM
    if NEUTRAL_ARM is None:
        path = os.path.join(TEXTURE_SOURCE, "neutral_arm.png")
        NEUTRAL_ARM = import_texture(path, "T_neutral_arm", "arm") if os.path.isfile(path) else None
    return NEUTRAL_ARM


def build_models(master):
    """Imports each prop and wires its own textures onto instances of the master material."""
    if not os.path.isdir(MODEL_SOURCE):
        unreal.log_warning("No models at " + MODEL_SOURCE)
        return

    mesh_subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)

    only = only_requested()
    for model_id in sorted(os.listdir(MODEL_SOURCE)):
        folder = os.path.join(MODEL_SOURCE, model_id)
        if not os.path.isdir(folder):
            continue
        if only and model_id.lower() not in only:
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
                # .exr belongs here, and leaving it out is why every prop in the room was wearing
                # the wall's bumps. The texture library ships jpg; the model downloads ship their
                # *colour* as jpg and everything else — normal, roughness, metallic — as exr. This
                # filter took the colour and dropped the rest, so each prop instance left its
                # NormalMap and ARMMap samplers unset and fell through to the master's defaults,
                # which are deliberately our own wall maps so the master compiles. The clock's
                # clean white dial was being rendered with cracked concrete for a normal map and
                # cracked concrete for its roughness and ambient occlusion, which is exactly what
                # it looked like: a disc of mottled stone with numbers somewhere underneath.
                if not file_name.lower().endswith((".jpg", ".png", ".exr", ".tga", ".tif", ".tiff")):
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

        # An empty NANITE file beside the FBX (Tools/make_wine_cellar.py writes them) turns Nanite on:
        # the wine racks and their bottles go into the cellar by the thousand, and drawn the ordinary
        # way every one of them would be drawn again into every face of the lantern's shadow.
        if os.path.isfile(os.path.join(folder, "NANITE")):
            try:
                nanite = mesh.get_editor_property("nanite_settings")
                if not nanite.get_editor_property("enabled"):
                    nanite.set_editor_property("enabled", True)
                    mesh.set_editor_property("nanite_settings", nanite)
                    unreal.log("Nanite on for " + model_id)
            except Exception as error:
                unreal.log_warning("Could not enable Nanite on {} ({})".format(model_id, error))

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
            if not maps and slot_name == model_id.lower():
                # A slot named after the model itself is the model's main surface, and its maps
                # are the ones with no slot name in them (fancy_picture_frame_02_diff next to
                # fancy_picture_frame_02_canvas_diff), which are grouped as "default".
                maps = by_slot.get("default")
            if not maps:
                maps = next((v for k, v in by_slot.items() if k in slot_name or slot_name in k), None)
            if not maps and len(by_slot) == 1:
                maps = list(by_slot.values())[0]
            if not maps:
                unreal.log_warning("{} slot '{}' has no textures".format(model_id, slot_name))
                continue

            if not maps.get("arm"):
                fallback = neutral_arm()
                if fallback:
                    maps = dict(maps)
                    maps["arm"] = fallback

            instance = make_instance("MI_{}_{}".format(model_id, slot_name or index), master, maps, 1.0, MASKED_MASTER)
            mesh.set_material(index, instance)

        ASSET_LIB.save_loaded_asset(mesh)
        unreal.log("Mesh {} ({} slots, {} texture groups)".format(model_id, len(slot_names), len(by_slot)))



GENERATED_SOURCE = os.path.join(PROJECT_DIR, "Art", "Source", "Generated")


def import_generated(file_name, asset_name, compression, sampler_type):
    """
    One of the maps Tools/make_glass_crack.py bakes.

    Kept out of import_texture and out of Art/Source/Textures on purpose: that folder is scanned
    as a library of photographed surface *sets*, keyed by the map suffix in the filename, and a
    pair of one-off maps dropped into it would come back out as a phantom material instance for a
    surface that does not exist. These are not a surface. They are one object's damage.
    """
    path = os.path.join(GENERATED_SOURCE, file_name)
    if not os.path.isfile(path):
        unreal.log_error("Missing {} — run: python Tools/make_glass_crack.py".format(path))
        return None

    asset_path = "{}/{}".format(TEXTURE_PACKAGE, asset_name)
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = TEXTURE_PACKAGE
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    ASSET_TOOLS.import_asset_tasks([task])

    texture = ASSET_LIB.load_asset(asset_path)
    if not texture:
        unreal.log_error("Failed to import " + path)
        return None

    texture.set_editor_property("compression_settings", compression)
    # Linear, both of them. The mask is three unrelated numbers packed into R/G/B and the normal
    # map is a direction; running either through a gamma curve is meaningless, and a sampler
    # whose type disagrees with its texture fails the whole material to WorldGridMaterial.
    texture.set_editor_property("srgb", False)
    # Clamped, so that nothing of the star repeats if a UV ever strays outside the sheet. A
    # second, ghostly impact in the corner of a pane is the kind of thing nobody can explain and
    # everybody can see.
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    # Biased one level sharper than the chain would pick. Mipmapping is an average, and the
    # average of a hairline and the glass either side of it is glass: at the distance this window
    # is usually looked at, a split lands on a third of a pixel and the correct mip is the one
    # where it has been averaged out of existence. Every other texture in the room is a surface,
    # where that average is exactly right; this one is a line, where it is the whole problem.
    texture.set_editor_property("lod_bias", -1)
    ASSET_LIB.save_loaded_asset(texture)
    return texture, sampler_type


def build_glass_crack_master():
    """
    The fracture on a struck pane, sampled from the map Tools/make_glass_crack.py bakes.

        Opacity   = Mask.R * Opacity + Mask.B * Haze
        BaseColor = Tint * lerp(0.42, 1, Mask.G)
        Normal    = the groove normals
        Roughness = RoughnessBase

    WHY A TEXTURE, in a project that builds everything else out of primitives and noise:

    This material used to draw the network itself, from the contour of a signed noise field in
    polar coordinates about the impact — the same trick that draws the cracks in the plaster and
    the cobwebs in the corners, and it is the right trick for both of those. It is the wrong one
    here, and the reason is worth keeping: the contour of a smooth field is a smooth meandering
    curve. It wanders, it closes loops, it doubles back on itself. Sharpening it, tapering it and
    fading it out only ever produced a tidier scribble, because what was wrong was not the width
    or the reach — it was that the line had no idea where the stone hit.

    A fracture is a set of very nearly straight lines out of a single point, kinking at corners,
    forking where there was energy to spare, dying where there was not, and tied to each other by
    short chords across the gaps. Straightness and a common origin are the whole of what the eye
    reads as broken glass, and neither is something a noise field has. A generator can do it in
    thirty lines; a material graph cannot do it at all.

    What stays in the material is what a texture cannot know: the tint, how far the network has
    opened, and the *lighting* — which is the other half of why a painted crack reads as painted.
    The groove normals give every split two faces tilted opposite ways, so one of them catches
    the lantern and the other does not, and which one changes as the player moves.
    """
    if ASSET_LIB.does_asset_exist(GLASS_CRACK_PATH):
        ASSET_LIB.delete_asset(GLASS_CRACK_PATH)

    mask = import_generated("glass_crack_mask.png", "T_glass_crack_mask",
                            unreal.TextureCompressionSettings.TC_BC7,
                            unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    normal = import_generated("glass_crack_nor.png", "T_glass_crack_nor",
                              unreal.TextureCompressionSettings.TC_NORMALMAP,
                              unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    if not mask or not normal:
        return

    material = ASSET_TOOLS.create_asset(
        "M_RoomGlassCrack", MATERIAL_PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("two_sided", True)
    # Forward shading, because a crack is only visible at all through specular: the fracture
    # faces are polished glass and what they do is *glint*. The cheaper translucency modes give
    # a translucent surface diffuse lighting and no highlight, which lights the whole network
    # evenly — and a network lit evenly is a drawing of one, whatever shape it is.
    for mode_name in ("TLM_SURFACE_FORWARD_SHADING", "TLM_SURFACE_PER_PIXEL_LIGHTING", "TLM_SURFACE"):
        mode = getattr(unreal.TranslucencyLightingMode, mode_name, None)
        if mode is not None:
            material.set_editor_property("translucency_lighting_mode", mode)
            break

    def scalar(name, value, x, y):
        node = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        return node

    def sampler(name, imported, x, y):
        texture, sampler_type = imported
        node = MAT_LIB.create_material_expression(
            material, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("texture", texture)
        node.set_editor_property("sampler_type", sampler_type)
        return node

    mask_node = sampler("CrackMask", mask, -900, -100)
    normal_node = sampler("CrackNormal", normal, -900, 420)

    # Opacity: the splits themselves, plus a trace of the micro-fracture haze that runs alongside
    # them. The haze is deliberately feeble — it is the knob that turns the whole star into a
    # glow the moment it is given any rope.
    opacity_param = scalar("Opacity", 0.95, -520, 60)
    haze_param = scalar("Haze", 0.07, -520, 210)

    splits = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 20)
    MAT_LIB.connect_material_expressions(mask_node, "R", splits, "A")
    MAT_LIB.connect_material_expressions(opacity_param, "", splits, "B")

    haze = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 200)
    MAT_LIB.connect_material_expressions(mask_node, "B", haze, "A")
    MAT_LIB.connect_material_expressions(haze_param, "", haze, "B")

    total = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionAdd, -120, 110)
    MAT_LIB.connect_material_expressions(splits, "", total, "A")
    MAT_LIB.connect_material_expressions(haze, "", total, "B")

    opacity = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionClamp, 60, 110)
    MAT_LIB.connect_material_expressions(total, "", opacity, "")
    opacity.set_editor_property("min_default", 0.0)
    opacity.set_editor_property("max_default", 1.0)

    # Not every stretch of a split scatters the same amount of light — some of it is a clean
    # parting the eye can barely find and some of it is crushed. That is the G channel.
    variation = MAT_LIB.create_material_expression(
        material, unreal.MaterialExpressionLinearInterpolate, -300, -180)
    variation.set_editor_property("const_a", 0.42)
    variation.set_editor_property("const_b", 1.0)
    MAT_LIB.connect_material_expressions(mask_node, "G", variation, "Alpha")

    tint = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -520, -330)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.60, 0.63, 0.68, 1.0))

    base = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, -80, -260)
    MAT_LIB.connect_material_expressions(tint, "", base, "A")
    MAT_LIB.connect_material_expressions(variation, "", base, "B")

    # Zero by default, and it should stay zero. It is here because a crack that cannot be seen at
    # all until the lantern is on it may yet turn out to be too subtle, and the honest fix for
    # that is a trace of self-illumination rather than a brighter tint — a brighter tint takes
    # the *dark* stretches up with it and flattens the whole network back into a line drawing.
    glow_param = scalar("Glow", 0.0, -80, -420)
    glow = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionMultiply, 120, -400)
    MAT_LIB.connect_material_expressions(base, "", glow, "A")
    MAT_LIB.connect_material_expressions(glow_param, "", glow, "B")

    roughness = scalar("RoughnessBase", 0.16, -80, 330)
    specular = scalar("Specular", 1.0, -80, 470)

    MAT_LIB.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MAT_LIB.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MAT_LIB.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    MAT_LIB.connect_material_property(normal_node, "", unreal.MaterialProperty.MP_NORMAL)
    MAT_LIB.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MAT_LIB.connect_material_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)

    MAT_LIB.recompile_material(material)
    ASSET_LIB.save_loaded_asset(material)
    unreal.log("Built " + GLASS_CRACK_PATH)


def only_requested():
    """-ArtOnly=a,b,c: restrict the models and surfaces stages to these asset ids.

    Both stages rebuild every material instance they touch, and re-running them to add three new
    props rewrites every prop already in the project — a hundred binary diffs that change nothing.
    Lower-cased, so the command line does not have to match Poly Haven's capitalisation.
    """
    line = unreal.SystemLibrary.get_command_line()
    for token in line.split():
        if token.lower().startswith("-artonly="):
            names = token.split("=", 1)[1].strip('"')
            return set(name.strip().lower() for name in names.split(",") if name.strip())
    return None


def rebuild_surfaces():
    """New photographed surface sets, as instances of the master already in the project.

    Every texture already imported is skipped by import_texture, and a set that already has its
    MI_ is left alone unless it is named in -ArtOnly, so this only ever adds.
    """
    master = ASSET_LIB.load_asset(MASTER_PATH)
    if not master:
        unreal.log_error("No " + MASTER_PATH + " — run the whole pipeline first")
        return
    only = only_requested()
    # A set that ships an opacity map (the wine crates' brands) hangs off the masked master.
    masked = ASSET_LIB.load_asset(MASKED_PATH)
    sets = import_surface_textures()
    for set_name, maps in sorted(sets.items()):
        # neutral_arm.png lives beside the library but is the props' fallback ARM, not a surface.
        if set_name == "neutral":
            continue
        if only and set_name.lower() not in only:
            continue
        if not only and ASSET_LIB.does_asset_exist("{}/MI_{}".format(MATERIAL_PACKAGE, set_name)):
            continue
        make_instance("MI_" + set_name, master, maps, TILING.get(set_name, 2.0), masked)
        unreal.log("Surface MI_{} ({} maps)".format(set_name, len(maps)))


def import_generated_color(file_name, asset_name, srgb, compression):
    """A baked map that is a picture rather than a set of numbers: sRGB where it is colour."""
    path = os.path.join(GENERATED_SOURCE, file_name)
    if not os.path.isfile(path):
        unreal.log_error("Missing {} — run: python Tools/make_stained_glass.py".format(path))
        return None

    asset_path = "{}/{}".format(TEXTURE_PACKAGE, asset_name)
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = TEXTURE_PACKAGE
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    ASSET_TOOLS.import_asset_tasks([task])

    texture = ASSET_LIB.load_asset(asset_path)
    if not texture:
        unreal.log_error("Failed to import " + path)
        return None
    texture.set_editor_property("compression_settings", compression)
    texture.set_editor_property("srgb", srgb)
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    ASSET_LIB.save_loaded_asset(texture)
    return texture


def build_stained_glass_masters():
    """
    The stair window, as two sheets laid one in front of the other.

    M_RoomStainedGlass — the glass. Unlit and masked:
        Emissive     = Glass.RGB * Intensity
        OpacityMask  = Glass.A            (0 where a piece has dropped out of its lead)

    Unlit, because stained glass at night is not a surface anything in the room lights. From
    inside it is dark until the sky behind it is bright, and then it is the colour of what comes
    through it — which is emission, not reflection. C++ drives Intensity from the storm every
    frame, and Lumen carries the glow onto the landing as coloured light for free.

    M_RoomLeadCame — the lead. Lit, masked by the came map, and the only one of the two that casts
    a shadow: the glass component has shadows switched off, so the storm light behind the window
    passes through the colour and is stopped only by the lead — and prints the drawing of the
    window across the stairs.
    """
    glass_map = import_generated_color("stained_glass_color.png", "T_stained_glass_color", True,
                                       unreal.TextureCompressionSettings.TC_BC7)
    lead_map = import_generated_color("stained_glass_lead.png", "T_stained_glass_lead", False,
                                      unreal.TextureCompressionSettings.TC_MASKS)
    if not glass_map or not lead_map:
        return

    for path in (STAINED_GLASS_PATH, LEAD_CAME_PATH):
        if ASSET_LIB.does_asset_exist(path):
            ASSET_LIB.delete_asset(path)

    glass = ASSET_TOOLS.create_asset("M_RoomStainedGlass", MATERIAL_PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    glass.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    glass.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    glass.set_editor_property("two_sided", True)

    sample = MAT_LIB.create_material_expression(glass, unreal.MaterialExpressionTextureSampleParameter2D, -700, 0)
    sample.set_editor_property("parameter_name", "GlassMap")
    sample.set_editor_property("texture", glass_map)
    sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)

    intensity = MAT_LIB.create_material_expression(glass, unreal.MaterialExpressionScalarParameter, -700, 300)
    intensity.set_editor_property("parameter_name", "Intensity")
    intensity.set_editor_property("default_value", 0.4)

    glow = MAT_LIB.create_material_expression(glass, unreal.MaterialExpressionMultiply, -350, 60)
    MAT_LIB.connect_material_expressions(sample, "RGB", glow, "A")
    MAT_LIB.connect_material_expressions(intensity, "", glow, "B")

    MAT_LIB.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MAT_LIB.connect_material_property(sample, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    MAT_LIB.recompile_material(glass)
    ASSET_LIB.save_loaded_asset(glass)
    unreal.log("Built " + STAINED_GLASS_PATH)

    lead = ASSET_TOOLS.create_asset("M_RoomLeadCame", MATERIAL_PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    lead.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    lead.set_editor_property("two_sided", True)

    came = MAT_LIB.create_material_expression(lead, unreal.MaterialExpressionTextureSampleParameter2D, -700, 0)
    came.set_editor_property("parameter_name", "LeadMap")
    came.set_editor_property("texture", lead_map)
    came.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)

    tint = MAT_LIB.create_material_expression(lead, unreal.MaterialExpressionVectorParameter, -700, -250)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.035, 0.034, 0.032, 1.0))

    rough = MAT_LIB.create_material_expression(lead, unreal.MaterialExpressionScalarParameter, -700, 300)
    rough.set_editor_property("parameter_name", "Roughness")
    rough.set_editor_property("default_value", 0.55)

    metal = MAT_LIB.create_material_expression(lead, unreal.MaterialExpressionScalarParameter, -700, 420)
    metal.set_editor_property("parameter_name", "Metallic")
    metal.set_editor_property("default_value", 0.6)

    MAT_LIB.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MAT_LIB.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MAT_LIB.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    MAT_LIB.connect_material_property(came, "R", unreal.MaterialProperty.MP_OPACITY_MASK)
    MAT_LIB.recompile_material(lead)
    ASSET_LIB.save_loaded_asset(lead)
    unreal.log("Built " + LEAD_CAME_PATH)


SKY_PATH = MATERIAL_PACKAGE + "/M_RoomSky"
FOLIAGE_PATH = MATERIAL_PACKAGE + "/M_RoomFoliage"

# The whole night sky, as one expression. A graph of forty nodes says the same thing less readably,
# and this is arithmetic on a direction: where on the cloud deck a ray lands, how thick the cloud is
# there, how far it is from the moon and from the strike.
SKY_CODE = """
float3 d = normalize(WP - Centre);
float el = d.z;

// The cloud deck: a flat layer overhead, so it crowds together towards the horizon and drifts
// across the sky in a straight line. Two samples at different scales and speeds.
float2 p = d.xy / (max(el, 0.0) + 0.30);
float2 uvA = p * 0.11 + Time * Drift * float2(0.0030, 0.0012);
float2 uvB = p * 0.23 + Time * Drift * float2(0.0052, 0.0031) + float2(0.37, 0.61);
float3 cA = Texture2DSample(Cloud, CloudSampler, uvA).rgb;
float3 cB = Texture2DSample(Cloud, CloudSampler, uvB).rgb;

float3 M = normalize(MoonDir);
float mdot = dot(d, M);
// Thinner round the moon: it is seen through a break, not through the belly of a cloud.
float thick = saturate(cA.r * 1.55 + (cB.g - 0.5) * 0.35 + (cB.b - 0.5) * 0.3 - 0.22 - 0.30 * pow(saturate(mdot), 24.0));
float thin = exp(-3.4 * thick);
float3 belly = float3(0.050, 0.060, 0.085) * (0.55 + 0.9 * cB.g);
float3 col = lerp(belly, float3(0.42, 0.52, 0.74), thin * 0.8);

// The moon: a disc cut by angle, its face from the map, and the light it puts into thin cloud.
float cr = 0.9990;
float disc = smoothstep(cr - 0.00012, cr + 0.00012, mdot);
float3 mr = normalize(cross(float3(0.0, 0.0, 1.0), M));
float3 mu = cross(M, mr);
float2 muv = float2(dot(d, mr), -dot(d, mu)) / sqrt(1.0 - cr * cr) * 0.5 + 0.5;
float face = Texture2DSample(Moon, MoonSampler, muv).r;
float corona = pow(saturate(mdot), 900.0) * 1.6 + pow(saturate(mdot), 90.0) * 0.5 + pow(saturate(mdot), 12.0) * 0.12;
col += float3(0.80, 0.86, 1.0) * (disc * face * 14.0 * exp(-5.0 * thick) + corona * (0.25 + thin));

float hz = smoothstep(-0.02, 0.20, el);
col = lerp(float3(0.030, 0.038, 0.055), col, hz);

// The strike: the cloud round it lit from inside, and the whole deck a little.
float3 F = normalize(FlashDir);
float fdot = dot(d, F);
float local = exp((fdot - 1.0) * 26.0) + 0.35 * exp((fdot - 1.0) * 5.0);
col += float3(0.70, 0.80, 1.0) * Flash * (0.3 + 0.7 * hz) * (local * (0.35 + 1.5 * thin + 1.0 * cA.g) * 0.5 + 0.14 * (0.25 + thin));

// And the channel itself, laid on the sky about the strike's direction.
float3 br = normalize(cross(float3(0.0, 0.0, 1.0), F));
float3 bu = cross(F, br);
float2 q = float2(dot(d, br), dot(d, bu)) / BoltSize;
float2 buv = float2(q.x * BoltFlip * 0.5 + BOLT_X, BOLT_Y - q.y * 0.5);
float inside = step(0.0, buv.x) * step(buv.x, 1.0) * step(0.0, buv.y) * step(buv.y, 1.0) * step(0.0, fdot);
float2 bolt = Texture2DSample(Bolts, BoltsSampler, saturate(buv) * 0.5 + BoltCell.xy).rg * inside;
col += float3(0.78, 0.86, 1.0) * Bolt * (bolt.r * 48.0 * (0.55 + 0.45 * thin) + bolt.g * 1.8);

return col * Brightness;
"""

# Wind, as a vertex offset. UV1.x is how far up the tree the vertex is and UV1.y how loose it is
# (Tools/make_forest.py). The whole tree leans downwind by the square of its height and rocks about
# that lean; the phase comes from where the vertex stands, so a gust runs through the wood as a wave
# rather than every tree nodding at once. Boughs and leaves shiver on top.
WIND_CODE = """
float h = UV1.x;
float loose = UV1.y;
float phase = dot(WP.xy, float2(0.0011, 0.0017));
float sway = 0.55 + 0.6 * sin(Time * 0.9 + phase) + 0.25 * sin(Time * 2.3 + phase * 1.7);
float3 offset = float3(Wind.xy, 0.0) * (h * h) * Wind.z * sway;
float f1 = sin(Time * 7.0 + WP.x * 0.031 + WP.z * 0.045);
float f2 = sin(Time * 5.3 + WP.y * 0.037 + WP.z * 0.020);
offset += float3(f1, f2, f1 * f2 - 0.3) * loose * Flutter;
return offset;
"""


# Night air between the eye and a tree: the further off it stands, the more of the sky's own grey it
# takes on. Without it a wood at night is one black cut-out however deep it is; with it the near
# trunks stand dark against the ones behind, and those against the far wall.
HAZE_CODE = """
float away = length(WP - Eye);
return Haze * saturate((away - 900.0) / HazeDistance);
"""


def import_storm_texture(file_name, asset_name, srgb, compression, clamp_x, clamp_y):
    path = os.path.join(GENERATED_SOURCE, file_name)
    if not os.path.isfile(path):
        unreal.log_error("Missing {} — run: python Tools/make_storm_art.py".format(path))
        return None
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = TEXTURE_PACKAGE
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    ASSET_TOOLS.import_asset_tasks([task])
    texture = ASSET_LIB.load_asset("{}/{}".format(TEXTURE_PACKAGE, asset_name))
    if not texture:
        unreal.log_error("Failed to import " + path)
        return None
    texture.set_editor_property("compression_settings", compression)
    texture.set_editor_property("srgb", srgb)
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP if clamp_x else unreal.TextureAddress.TA_WRAP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP if clamp_y else unreal.TextureAddress.TA_WRAP)
    ASSET_LIB.save_loaded_asset(texture)
    return texture


def custom_node(material, code, output_type, input_names, x, y):
    node = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("code", code)
    node.set_editor_property("output_type", output_type)
    inputs = []
    for name in input_names:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        inputs.append(entry)
    node.set_editor_property("inputs", inputs)
    return node


def build_storm_masters():
    """
    M_RoomSky: the night sky outside every window, one unlit dome round the house.
        Emissive = cloud deck (drifting, moonlit at its thin edges) + moon + the strike lighting the
                   cloud from inside + the lightning channel itself (SKY_CODE).
    C++ (AStormForestActor) says where the moon is and, at every strike, where the strike is.

    M_RoomFoliage: the forest. Masked, two-sided, lit.
        BaseColor = FoliageMap.RGB * Tint      OpacityMask = FoliageMap.A
        WorldPositionOffset = wind (WIND_CODE)
        Emissive = Haze * distance from the eye (HAZE_CODE)
    One material for a whole tree, bark and all: the bark is a cell of the same atlas, so a tree is
    one draw.
    """
    masks = unreal.TextureCompressionSettings.TC_MASKS
    clouds = import_storm_texture("storm_clouds.png", "T_storm_clouds", False, masks, False, False)
    bolts = import_storm_texture("storm_bolts.png", "T_storm_bolts", False, masks, True, True)
    moon = import_storm_texture("storm_moon.png", "T_storm_moon", False, masks, True, True)
    foliage = import_storm_texture("forest_foliage.png", "T_forest_foliage", True,
                                   unreal.TextureCompressionSettings.TC_DEFAULT, True, True)
    ring = import_storm_texture("forest_ring.png", "T_forest_ring", True,
                                unreal.TextureCompressionSettings.TC_DEFAULT, False, True)
    if not (clouds and bolts and moon and foliage and ring):
        return

    for path in (SKY_PATH, FOLIAGE_PATH):
        if ASSET_LIB.does_asset_exist(path):
            ASSET_LIB.delete_asset(path)

    def scalar(material, name, value, x, y):
        node = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        return node

    def vector(material, name, value, x, y):
        node = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionVectorParameter, x, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", unreal.LinearColor(value[0], value[1], value[2], 1.0))
        return node

    def texture_object(material, name, texture, x, y):
        node = MAT_LIB.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, x, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("texture", texture)
        node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        return node

    # ---- the sky ----
    sky = ASSET_TOOLS.create_asset("M_RoomSky", MATERIAL_PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    sky.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    sky.set_editor_property("two_sided", True)

    names = ["WP", "Centre", "Time", "Drift", "Cloud", "Moon", "Bolts", "MoonDir", "FlashDir", "Flash",
             "Bolt", "BoltSize", "BoltFlip", "BoltCell", "Brightness"]
    # Tools/make_storm_art.py: BOLT_ORIGIN.
    code = SKY_CODE.replace("BOLT_X", "0.5").replace("BOLT_Y", "0.25")
    body = custom_node(sky, code, unreal.CustomMaterialOutputType.CMOT_FLOAT3, names, -300, 0)
    sources = {
        "WP": MAT_LIB.create_material_expression(sky, unreal.MaterialExpressionWorldPosition, -900, -500),
        "Centre": vector(sky, "Centre", (0.0, 0.0, 0.0), -900, -400),
        "Time": MAT_LIB.create_material_expression(sky, unreal.MaterialExpressionTime, -900, -250),
        "Drift": scalar(sky, "Drift", 1.0, -900, -180),
        "Cloud": texture_object(sky, "CloudMap", clouds, -900, -100),
        "Moon": texture_object(sky, "MoonMap", moon, -900, 60),
        "Bolts": texture_object(sky, "BoltMap", bolts, -900, 220),
        "MoonDir": vector(sky, "MoonDir", (0.9, 0.15, 0.4), -900, 380),
        "FlashDir": vector(sky, "FlashDir", (1.0, 0.0, 0.4), -900, 540),
        "Flash": scalar(sky, "Flash", 0.0, -900, 700),
        "Bolt": scalar(sky, "Bolt", 0.0, -900, 780),
        "BoltSize": scalar(sky, "BoltSize", 0.3, -900, 860),
        "BoltFlip": scalar(sky, "BoltFlip", 1.0, -900, 940),
        "BoltCell": vector(sky, "BoltCell", (0.0, 0.0, 0.0), -900, 1020),
        "Brightness": scalar(sky, "Brightness", 1.0, -900, 1180),
    }
    for name in names:
        MAT_LIB.connect_material_expressions(sources[name], "", body, name)
    MAT_LIB.connect_material_property(body, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MAT_LIB.recompile_material(sky)
    ASSET_LIB.save_loaded_asset(sky)
    unreal.log("Built " + SKY_PATH)

    # ---- the forest ----
    leaf = ASSET_TOOLS.create_asset("M_RoomFoliage", MATERIAL_PACKAGE, unreal.Material, unreal.MaterialFactoryNew())
    leaf.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    leaf.set_editor_property("two_sided", True)
    leaf.set_editor_property("opacity_mask_clip_value", 0.36)

    sample = MAT_LIB.create_material_expression(leaf, unreal.MaterialExpressionTextureSampleParameter2D, -700, -200)
    sample.set_editor_property("parameter_name", "FoliageMap")
    sample.set_editor_property("texture", foliage)
    sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    tint = vector(leaf, "Tint", (1.0, 1.0, 1.0), -700, -420)
    tinted = MAT_LIB.create_material_expression(leaf, unreal.MaterialExpressionMultiply, -350, -300)
    MAT_LIB.connect_material_expressions(sample, "RGB", tinted, "A")
    MAT_LIB.connect_material_expressions(tint, "", tinted, "B")
    rough = scalar(leaf, "Roughness", 0.9, -700, 100)
    specular = scalar(leaf, "Specular", 0.15, -700, 180)

    wind_names = ["WP", "UV1", "Time", "Wind", "Flutter"]
    wind = custom_node(leaf, WIND_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, wind_names, -300, 400)
    position = MAT_LIB.create_material_expression(leaf, unreal.MaterialExpressionWorldPosition, -700, 300)
    try:
        position.set_editor_property("world_position_shader_offset",
                                     unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    except Exception as error:
        unreal.log_warning("Foliage: world position left at its default ({})".format(error))
    uv1 = MAT_LIB.create_material_expression(leaf, unreal.MaterialExpressionTextureCoordinate, -700, 400)
    uv1.set_editor_property("coordinate_index", 1)
    wind_sources = {
        "WP": position,
        "UV1": uv1,
        "Time": MAT_LIB.create_material_expression(leaf, unreal.MaterialExpressionTime, -700, 480),
        # XY the way the wind blows, Z how far (cm) it carries a treetop.
        "Wind": vector(leaf, "Wind", (0.0, -1.0, 40.0), -700, 560),
        "Flutter": scalar(leaf, "Flutter", 3.0, -700, 720),
    }
    for name in wind_names:
        MAT_LIB.connect_material_expressions(wind_sources[name], "", wind, name)

    MAT_LIB.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MAT_LIB.connect_material_property(sample, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    MAT_LIB.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MAT_LIB.connect_material_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)
    MAT_LIB.connect_material_property(wind, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    haze_names = ["WP", "Eye", "Haze", "HazeDistance"]
    haze = custom_node(leaf, HAZE_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, haze_names, -300, 800)
    haze_sources = {
        "WP": MAT_LIB.create_material_expression(leaf, unreal.MaterialExpressionWorldPosition, -700, 800),
        "Eye": MAT_LIB.create_material_expression(leaf, unreal.MaterialExpressionCameraPositionWS, -700, 880),
        "Haze": vector(leaf, "Haze", (0.0, 0.0, 0.0), -700, 960),
        "HazeDistance": scalar(leaf, "HazeDistance", 8000.0, -700, 1120),
    }
    for name in haze_names:
        MAT_LIB.connect_material_expressions(haze_sources[name], "", haze, name)
    MAT_LIB.connect_material_property(haze, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    allow_instancing(leaf)
    MAT_LIB.recompile_material(leaf)
    ASSET_LIB.save_loaded_asset(leaf)
    unreal.log("Built " + FOLIAGE_PATH)


def stage_requested():
    """-ArtStage=<name> on the command line, or None for the whole pipeline.

    The standalone masters — glass, emissive, crack, web — do not depend on the texture import or
    on each other, and re-running everything to correct one of them re-imports every texture and
    rewrites every material instance in the project. That is a hundred touched .uasset files to
    fix one node.
    """
    line = unreal.SystemLibrary.get_command_line()
    for token in line.split():
        if token.lower().startswith("-artstage="):
            return token.split("=", 1)[1].strip('"').lower()
    return None


def rebuild_models():
    """The props, against the master that is already in the project."""
    global MASKED_MASTER
    master = ASSET_LIB.load_asset(MASTER_PATH)
    if not master:
        unreal.log_error("No " + MASTER_PATH + " — run the whole pipeline first")
        return
    MASKED_MASTER = ASSET_LIB.load_asset(MASKED_PATH)
    build_models(master)


STANDALONE_STAGES = {
    "glass": build_glass_master,
    "emissive": build_emissive_master,
    "crack": build_crack_master,
    "web": build_web_master,
    "glasscrack": build_glass_crack_master,
    "models": rebuild_models,
    "surfaces": rebuild_surfaces,
    "stainedglass": build_stained_glass_masters,
    "usage": rebuild_usage,
    "storm": build_storm_masters,
}


def run():
    global MASKED_MASTER

    stage = stage_requested()
    if stage in STANDALONE_STAGES:
        unreal.log("=== Room art pipeline: {} only ===".format(stage))
        STANDALONE_STAGES[stage]()
        unreal.log("=== Done ===")
        return
    if stage:
        unreal.log_error("Unknown -ArtStage={} (known: {})".format(stage, ", ".join(sorted(STANDALONE_STAGES))))
        return

    unreal.log("=== Room art pipeline ===")

    # Textures first: the masters cannot be built until there is a correctly typed texture to put
    # in each of their samplers.
    sets = import_surface_textures()
    defaults = pick_master_defaults(sets)

    master = build_master_material(defaults=defaults)
    MASKED_MASTER = build_master_material("M_RoomSurfaceMasked", masked=True, defaults=defaults)
    build_glass_master()
    build_emissive_master()
    build_decal_master(defaults=defaults)
    build_crack_master()
    build_web_master()
    build_glass_crack_master()
    build_stained_glass_masters()
    build_storm_masters()

    build_surface_instances(master, sets)
    build_models(master)
    unreal.log("=== Done ===")


run()
