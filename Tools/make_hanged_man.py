"""
Builds the hanged man in the master bedroom (Room01) in Blender, headless, and exports him as an
FBX the art pipeline imports.

    blender --background --factory-startup --python Tools/make_hanged_man.py

He hangs from the ceiling hook in the middle of the room the detective wakes in. In the room he is
a dark silhouette nobody can put a name to: only the clothes say anything, and they barely do. They
are the detective's: a dark suit, a white shirt, a tie, dark shoes, short dark hair. The head is
bowed on the chest and turned from the room, so the face is never seen.

A body built out of primitives in code could only ever be a dummy, so the body and his clothes are
MakeHuman's, through MPFB (both free; the assets CC0):
    MPFB 2.0.18, installed as a Blender extension (bl_ext.user_default.mpfb)
    makehuman_system_assets_cc0.zip, unpacked into MPFB's data folder
        skins/middleage_caucasian_male, clothes/male_elegantsuit01, clothes/shoes02, hair/short02
See the decision log (2026-11-24) for where they came from and how they were installed.

The human is posed on MPFB's game_engine rig by aiming each bone at a direction (arms hanging,
toes dropped, head bowed and tilted from the knot), the armature and the clothes' masks are baked
into the meshes, and the rig is thrown away: what goes to the game is a static mesh, turned slowly
about the hook in C++ the way a hanged body turns on a twisted rope.

Frame: centimetres, front local +Y, the origin is the centre of the hook's rod at the bottom of its
bend (the point the rope rests on); the body hangs below it with its centre of mass straight under
the origin. Two models:
    bedroom_hanged_man        the body and his clothes, up to the base of the neck
    bedroom_hanged_man_head   the head, the hair, the noose and the standing rope up to the rod:
                              in the room it takes no direct light, so it is in the dark
    bedroom_hook_rope    the turn of rope over the rod and its knotted tail (does not turn)
The rope over the hook lies in the local XZ plane, across the rod, which runs along local Y.
HANG_DROP (origin to the soles) is shared with MasterBedroomActor.h (HangedManDrop).

Material slots (named for build_art.py, which wires each slot to the textures written beside the
FBX as textures/bedroom_hanged_man_<slot>_<map>.png): Skin, Suit, Shoes, Hair (with a cutout),
Rope (no textures: a flat colour set in C++). C++ darkens them all.

Previews: Saved/HangedManPreview/.
"""

import math
import os
import shutil
import sys

import addon_utils
import bpy
import numpy as np
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blender_kit as K  # noqa: E402
from blender_kit import Part, ellipsoid, tube  # noqa: E402

PREVIEW_DIR = os.path.join(K.ROOT, "Saved", "HangedManPreview")
NAME = "bedroom_hanged_man"
HEAD_NAME = "bedroom_hanged_man_head"

# From the rod's centre down to the soles of his shoes. With the rod at 291 over the boards
# (MasterBedroomActor: HookRestPoint), his feet hang 38 over them.
HANG_DROP = 253.0
ROPE_RADIUS = 0.9

# How far under the collar the darkening starts (metres), and how dark it is at the collar.
DARK_FROM = 0.50
DARK_AT_COLLAR = 0.9

SLOTS = {"Rope": ((0.45, 0.36, 0.24, 1.0), 100.0)}

# Who he is, in MakeHuman's macros: a man in his forties, tall, lean.
MACROS = (("gender", 1.0), ("age", 0.62), ("african", 0.0), ("asian", 0.0), ("caucasian", 1.0),
          ("muscle", 0.55), ("weight", 0.45), ("height", 0.62))
SKIN = "middleage_caucasian_male.mhmat"
ASSETS = (("clothes", "male_elegantsuit01.mhclo", "Clothes", "Suit"),
          ("clothes", "shoes02.mhclo", "Clothes", "Shoes"),
          ("hair", "short02.mhclo", "Hair", "Hair"))

# The pose, as directions each bone is aimed along, in Blender's frame (he faces -Y, his left is
# +X). Parents before children: each aim is measured after the parent has moved. A hanged body is
# limp: arms straight down at the sides, the hands a little in front of the thighs, the knees a
# touch bent, the feet fallen at the ankle so the toes point down, and the head dropped forward on
# the chest and away from the knot (the knot is under his right ear, so the head goes left, +X).
POSE = (
    ("spine_02", (0.0, -0.06, 1.0)),
    ("spine_03", (0.0, -0.10, 1.0)),
    ("clavicle_l", None),
    ("clavicle_r", None),
    ("upperarm_l", (0.10, -0.06, -1.0)),
    ("upperarm_r", (-0.10, -0.06, -1.0)),
    ("lowerarm_l", (0.03, -0.16, -1.0)),
    ("lowerarm_r", (-0.03, -0.14, -1.0)),
    ("hand_l", (0.0, -0.10, -1.0)),
    ("hand_r", (0.0, -0.08, -1.0)),
    ("thigh_l", (0.025, -0.05, -1.0)),
    ("thigh_r", (-0.03, 0.02, -1.0)),
    ("calf_l", (0.0, 0.08, -1.0)),
    ("calf_r", (0.0, 0.10, -1.0)),
    ("foot_l", (0.02, -0.42, -1.0)),
    ("foot_r", (-0.04, -0.36, -1.0)),
    ("neck_01", (0.12, -0.55, 1.0)),
    ("head", (0.30, -0.95, 0.55)),
)


def mpfb():
    addon_utils.enable("bl_ext.user_default.mpfb", default_set=True)
    from bl_ext.user_default.mpfb.entities.objectproperties import HumanObjectProperties
    from bl_ext.user_default.mpfb.services.assetservice import AssetService
    from bl_ext.user_default.mpfb.services.humanservice import HumanService
    from bl_ext.user_default.mpfb.services.targetservice import TargetService
    return HumanObjectProperties, AssetService, HumanService, TargetService


def aim(rig, name, direction):
    """Turns a pose bone about its head so it points along direction (armature space)."""
    bone = rig.pose.bones[name]
    current = (bone.tail - bone.head).normalized()
    if direction is None:
        # The shoulders drop a little when the arms hang dead.
        direction = current + Vector((0.0, 0.0, -0.18))
    turn = current.rotation_difference(Vector(direction).normalized()).to_matrix().to_4x4()
    head = bone.head.copy()
    bone.matrix = Matrix.Translation(head) @ turn @ Matrix.Translation(-head) @ bone.matrix
    bpy.context.view_layer.update()


def build_human():
    props, assets, humans, targets = mpfb()
    body = humans.create_human()
    for key, value in MACROS:
        props.set_value(key, value, entity_reference=body)
    targets.reapply_macro_details(body)
    humans.set_character_skin(assets.find_asset_absolute_path(SKIN, asset_subdir="skins"), body, skin_type="GAMEENGINE")
    rig = humans.add_builtin_rig(body, "game_engine")
    slot_of = {body.name: "Skin"}
    for subdir, file_name, kind, slot in ASSETS:
        path = assets.find_asset_absolute_path(file_name, asset_subdir=subdir)
        if path is None:
            raise RuntimeError("{} not found: unpack makehuman_system_assets into MPFB's data folder".format(file_name))
        piece = humans.add_mhclo_asset(path, body, asset_type=kind, material_type="GAMEENGINE")
        slot_of[piece.name] = slot
    bpy.context.view_layer.update()

    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="POSE")
    for name, direction in POSE:
        aim(rig, name, direction)
    bpy.ops.object.mode_set(mode="OBJECT")
    bpy.context.view_layer.update()

    neck = rig.pose.bones["neck_01"]
    head = rig.pose.bones["head"]
    # The middle of the face: a third of the way up the head bone and 6cm in front of it, front
    # being his -Y squared off the bowed bone. MasterBedroomActor.h (HangedManFace) holds the
    # printed result, for the shadow over the face.
    up = (head.tail - head.head).normalized()
    front = Vector((0.0, -1.0, 0.0))
    front = (front - up * front.dot(up)).normalized()
    landmarks = {
        "neck_low": rig.matrix_world @ neck.head,
        "neck_high": rig.matrix_world @ neck.tail,
        "head_top": rig.matrix_world @ head.tail,
        "face": rig.matrix_world @ (head.head.lerp(head.tail, 0.35) + front * 0.06),
    }

    # Bake the pose, the clothes' fitting and the masks (helper geometry, skin under the clothes)
    # into plain meshes, one per piece, with their slot's name on their material.
    depsgraph = bpy.context.evaluated_depsgraph_get()
    baked = []
    for obj in [o for o in bpy.context.scene.objects if o.type == "MESH" and o.name in slot_of]:
        mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph), depsgraph=depsgraph)
        mesh.transform(obj.matrix_world)
        slot = slot_of[obj.name]
        for index, material in enumerate(mesh.materials):
            if material is not None:
                material.name = slot if index == 0 else "{}{}".format(slot, index)
        copy = bpy.data.objects.new(slot + "_baked", mesh)
        bpy.context.scene.collection.objects.link(copy)
        baked.append(copy)
    for obj in [o for o in bpy.context.scene.objects if o not in baked]:
        bpy.data.objects.remove(obj, do_unlink=True)
    return baked, landmarks


def height_mask(obj, low, high, strength, size=1024):
    """
    How much of each texel of obj's texture to darken, as a mask in its UV space: nothing below
    low, strength at high (Blender metres, eased between), worked out per vertex from where the
    vertex hangs and drawn triangle by triangle into the UV layout. Baked into the colour it takes
    the light off him by height, so the suit and the shirt sink into the dark towards the collar
    and meet the unlit head with no edge.
    """
    mesh = obj.data
    z = np.array([v.co.z for v in mesh.vertices], dtype=np.float64)
    t = np.clip((z - low) / (high - low), 0.0, 1.0)
    weight = strength * t * t * (3.0 - 2.0 * t)
    mask = np.zeros((size, size), dtype=np.float32)
    uv_layer = mesh.uv_layers.active.data
    mesh.calc_loop_triangles()
    for tri in mesh.loop_triangles:
        w = weight[list(tri.vertices)]
        if w.max() <= 0.0:
            continue
        uv = np.array([uv_layer[i].uv[:] for i in tri.loops]) % 1.0 * size
        x0, y0 = np.floor(uv.min(axis=0)).astype(int)
        x1, y1 = np.ceil(uv.max(axis=0)).astype(int)
        x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, size - 1), min(y1, size - 1)
        if x1 < x0 or y1 < y0:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        (ax, ay), (bx, by), (cx, cy) = uv
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-9:
            continue
        l0 = ((by - cy) * (xs - cx) + (cx - bx) * (ys - cy)) / den
        l1 = ((cy - ay) * (xs - cx) + (ax - cx) * (ys - cy)) / den
        l2 = 1.0 - l0 - l1
        inside = (l0 >= -0.03) & (l1 >= -0.03) & (l2 >= -0.03)
        value = l0 * w[0] + l1 * w[1] + l2 * w[2]
        block = mask[y0:y1 + 1, x0:x1 + 1]
        block[inside] = np.maximum(block[inside], value[inside])
    # Grow into the seams a little, so no island's border shows lighter than its middle.
    for _ in range(3):
        mask = np.maximum.reduce([mask, np.roll(mask, 1, 0), np.roll(mask, -1, 0), np.roll(mask, 1, 1), np.roll(mask, -1, 1)])
    return mask


def write_png(path, rgba, width, height):
    image = bpy.data.images.new(os.path.basename(path), width, height, alpha=True)
    image.pixels[:] = rgba.ravel()
    image.filepath_raw = path
    image.file_format = "PNG"
    image.save()
    return image


def export_textures(objects, folder, name, darken=None):
    """
    Each slot's colour and normal map, and the hair's alpha as a cutout, named for build_art.py.
    A slot MakeHuman ships no normal map for gets a flat one: left without, build_art's instance
    falls back to the master's default normal, which is the cracked wall's, and he came out
    looking like cracked, burnt leather (the 09-20 lesson, again).
    """
    out = os.path.join(folder, "textures")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    done = set()
    normals = {m.name.lower() for o in objects for m in o.data.materials if m is not None}
    for obj in objects:
        for material in obj.data.materials:
            if material is None or material.name in done or not material.use_nodes:
                continue
            done.add(material.name)
            slot = material.name.lower()
            for node in material.node_tree.nodes:
                if node.type != "TEX_IMAGE" or node.image is None:
                    continue
                targets = [link.to_node for link in node.outputs["Color"].links]
                if any(t.type == "NORMAL_MAP" for t in targets):
                    kind = "nor_gl"
                elif any(t.type == "BSDF_PRINCIPLED" for t in targets):
                    kind = "diff"
                else:
                    print("HANGED skipped image", material.name, node.image.name, [t.type for t in targets])
                    continue
                image = node.image
                source = bpy.path.abspath(image.filepath)
                path = os.path.join(out, "{}_{}_{}{}".format(name, slot, kind, os.path.splitext(source)[1].lower()))
                normals.discard(slot) if kind == "nor_gl" else None
                if kind == "diff" and darken and slot in darken:
                    image.reload()
                    w, h = image.size
                    pixels = np.array(image.pixels[:], dtype=np.float32).reshape(h, w, 4)
                    shade = darken[slot]
                    if shade.shape != (h, w):
                        shade = np.kron(shade, np.ones((h // shade.shape[0], w // shade.shape[1]), dtype=np.float32))
                    pixels[:, :, :3] *= (1.0 - shade)[:, :, None]
                    path = os.path.splitext(path)[0] + ".png"
                    write_png(path, pixels, w, h)
                    # The colour alone could not do it: the suit is near-black already, and what
                    # stayed bright at the shoulders was its sheen, which ignores the colour. The
                    # material has no specular input, but a metal's reflection is its colour, and
                    # a near-black metal reflects nothing: the same mask, as metallic, puts the
                    # sheen out towards the collar. AO 1, the neutral roughness 0.55.
                    small = darken[slot]
                    arm = np.stack([np.ones_like(small), np.full_like(small, 0.55), small, np.ones_like(small)], axis=-1)
                    write_png(os.path.join(out, "{}_{}_arm.png".format(name, slot)), arm, small.shape[1], small.shape[0])
                else:
                    shutil.copyfile(source, path)
                if kind == "diff" and slot == "hair":
                    image.reload()
                    pixels = np.array(image.pixels[:], dtype=np.float32).reshape(-1, 4)
                    alpha = bpy.data.images.new("hair_alpha", image.size[0], image.size[1], alpha=False)
                    mask = np.repeat(pixels[:, 3:4], 4, axis=1)
                    mask[:, 3] = 1.0
                    alpha.pixels[:] = mask.ravel()
                    alpha.filepath_raw = os.path.join(out, "{}_{}_opacity.png".format(name, slot))
                    alpha.file_format = "PNG"
                    alpha.save()
                print("HANGED texture", os.path.basename(path), image.size[0], image.size[1])
    flat = np.tile(np.array([0.5, 0.5, 1.0, 1.0], dtype=np.float32), (64, 64, 1))
    for slot in sorted(normals):
        write_png(os.path.join(out, "{}_{}_nor_gl.png".format(name, slot)), flat, 64, 64)
        print("HANGED flat normal for", slot)


def noose(neck_low, neck_high):
    """The noose round the neck, its knot under his right ear, and the standing rope to the rod."""
    rope = Part("Rope", smooth=True)
    up = (neck_high - neck_low).normalized()
    centre = neck_low.lerp(neck_high, 0.55)
    side = Vector((1.0, 0.0, 0.0))
    side = (side - up * side.dot(up)).normalized()
    other = up.cross(side)
    ring = []
    for k in range(33):
        a = 2.0 * math.pi * k / 32.0
        # Pulled up into a point under the knot (-X), as a noose is drawn tight by the drop.
        lift = 3.2 * max(0.0, -math.cos(a)) ** 2
        ring.append(centre + (side * math.cos(a) * 6.3 + other * math.sin(a) * 6.0) + up * lift)
    tube(rope, ring, ROPE_RADIUS, sides=10)
    knot_bottom = centre - side * 7.0 + up * 3.4 - other * 0.8
    end = Vector((-1.2, 0.0, -0.5))
    to_hook = (end - knot_bottom).normalized()
    knot_top = knot_bottom + to_hook * 10.0
    for k in range(8):
        c = knot_bottom.lerp(knot_top, k / 7.0)
        coil = to_hook.to_track_quat("Z", "Y").to_matrix().to_4x4()
        ellipsoid(rope, c, (2.2, 2.2, 0.95), matrix=coil, rings=6, sides=12)
    tube(rope, [knot_top.lerp(end, i / 16.0) for i in range(17)], ROPE_RADIUS, sides=10)
    return [rope]


def hook_rope():
    """The turn of rope over the hook's rod, in the XZ plane, and its tail tied off and hanging."""
    rope = Part("Rope", smooth=True)
    around = 0.8 + ROPE_RADIUS
    path = [Vector((-1.2, 0.0, -1.5))]
    for k in range(13):
        a = math.radians(190.0 - k * 200.0 / 12.0)
        path.append(Vector((math.cos(a) * around, 0.0, math.sin(a) * around)))
    path += [Vector((2.0, 0.3, -4.0)), Vector((2.3, 0.6, -7.5))]
    tube(rope, path, ROPE_RADIUS, sides=10)
    ellipsoid(rope, Vector((1.8, 0.2, -6.0)), (2.4, 2.0, 2.4), rings=8, sides=12)
    tube(rope, [Vector((2.3, 0.6, -7.5)), Vector((2.8, 1.0, -12.0)), Vector((3.0, 1.6, -17.0))],
         [ROPE_RADIUS, ROPE_RADIUS * 0.9, ROPE_RADIUS * 0.7], sides=10)
    return [rope]


def split_head(obj, cut_z, axis0, axis1, radius, name):
    """
    Moves the head's faces of obj into a new object: every face lying wholly above cut_z, or
    within radius of the head bone (axis0..axis1), all in Blender metres. Cut by height alone, the
    bowed head left its chin, mouth and jaw under the cut, lit, under a black skull.
    """
    import bmesh

    def in_head(co):
        along = axis1 - axis0
        t = max(0.0, min(1.0, (co - axis0).dot(along) / along.length_squared))
        return co.z > cut_z or (co - (axis0 + along * t)).length < radius

    whole = bmesh.new()
    whole.from_mesh(obj.data)
    upper = whole.copy()
    bmesh.ops.delete(whole, geom=[f for f in whole.faces if all(in_head(v.co) for v in f.verts)], context="FACES")
    bmesh.ops.delete(upper, geom=[f for f in upper.faces if not all(in_head(v.co) for v in f.verts)], context="FACES")
    whole.to_mesh(obj.data)
    mesh = bpy.data.meshes.new(name)
    upper.to_mesh(mesh)
    for material in obj.data.materials:
        mesh.materials.append(material)
    piece = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(piece)
    return piece


def join(objects, name):
    for other in bpy.context.scene.objects:
        other.select_set(other in objects)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    joined = bpy.context.view_layer.objects.active
    joined.name = name
    joined.data.name = name
    return joined


def to_game(point_m, shift):
    """A point in Blender metres (after shift) into the game frame in centimetres (Y mirrored)."""
    p = point_m + shift
    return Vector((p.x * 100.0, -p.y * 100.0, p.z * 100.0))


def preview(objects):
    """Workbench renders with the textures on, lifted so the rod is 2.91 m up."""
    for obj in objects:
        obj.location = (0.0, 0.0, 2.91)
    os.makedirs(PREVIEW_DIR, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "TEXTURE"
    scene.display.shading.show_shadows = True
    scene.render.resolution_x, scene.render.resolution_y = 900, 1200
    scene.world = bpy.data.worlds.new("preview")
    target = bpy.data.objects.new("target", None)
    scene.collection.objects.link(target)
    camera = bpy.data.objects.new("camera", bpy.data.cameras.new("camera"))
    camera.data.lens = 40.0
    scene.collection.objects.link(camera)
    camera.constraints.new("TRACK_TO").target = target
    scene.camera = camera
    for label, location, aim_at in (("front", (0.0, -3.2, 1.4), (0.0, 0.0, 1.3)),
                                    ("side", (3.2, 0.0, 1.4), (0.0, 0.0, 1.3)),
                                    ("three_quarter", (2.0, -2.4, 1.7), (0.0, 0.0, 1.3)),
                                    ("head", (0.7, -1.1, 1.9), (0.0, 0.0, 1.6)),
                                    ("hook", (0.25, -0.35, 2.85), (0.0, 0.0, 2.85))):
        camera.location = location
        target.location = aim_at
        scene.render.filepath = os.path.join(PREVIEW_DIR, "hanged_{}.png".format(label))
        bpy.ops.render.render(write_still=True)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    pieces, landmarks = build_human()

    # Hang him: the centre of mass straight under the origin, the soles HANG_DROP below it. The
    # body's mean is a fair stand-in for the centre of mass of a body hanging straight.
    body = next(o for o in pieces if o.name.startswith("Skin"))
    points = [v.co for v in body.data.vertices]
    cx = sum(p.x for p in points) / len(points)
    cy = sum(p.y for p in points) / len(points)
    sole = min(v.co.z for o in pieces for v in o.data.vertices)
    shift = Vector((-cx, -cy, -sole - HANG_DROP * 0.01))
    for obj in pieces:
        obj.data.transform(Matrix.Translation(shift))

    rope = K.bake(noose(to_game(landmarks["neck_low"], shift), to_game(landmarks["neck_high"], shift)), "noose", SLOTS)

    # The head goes into a model of its own, with the hair and the noose: in the room it takes no
    # direct light (MasterBedroomActor gives it no lighting channel), so from the collar up he is
    # in the dark whatever the lantern does, and only the room's bounced light finds the shape of
    # him. The skin is cut where the neck meets the shoulders, under the shirt's collar, and
    # everything within a hand of the head bone goes with the head (the bowed chin hangs lower
    # than that cut).
    cut = (landmarks["neck_low"] + shift).z + 0.01
    head = split_head(body, cut, landmarks["neck_high"] + shift, landmarks["head_top"] + shift, 0.15, "Head_baked")
    hair = next(o for o in pieces if o.name.startswith("Hair"))
    torso = [o for o in pieces if o is not hair]

    folder = os.path.join(K.MODEL_DIR, NAME)
    head_folder = os.path.join(K.MODEL_DIR, HEAD_NAME)
    os.makedirs(folder, exist_ok=True)
    os.makedirs(head_folder, exist_ok=True)
    # The light goes off him by height: from the middle of his chest up the suit, the shirt, the
    # tie and the neck darken towards the collar, where they meet the unlit head.
    collar = (landmarks["neck_low"] + shift).z
    darken = {}
    for obj in torso:
        for material in obj.data.materials:
            if material is not None and material.name.lower() in ("suit", "skin"):
                darken[material.name.lower()] = height_mask(obj, collar - DARK_FROM, collar - 0.01, DARK_AT_COLLAR)
    export_textures(torso, folder, NAME, darken)
    export_textures([head, hair], head_folder, HEAD_NAME)

    man = join(torso, NAME)
    top = join([head, hair, rope], HEAD_NAME)
    for obj, where in ((man, folder), (top, head_folder)):
        K.export(obj)
        open(os.path.join(where, "GENERATED"), "w").close()
        dims = obj.dimensions
        print("HANGED baked {} ({} verts, {:.1f} x {:.1f} x {:.1f} cm, slots {})".format(
            obj.name, len(obj.data.vertices), dims.x * 100, dims.y * 100, dims.z * 100, [m.name for m in obj.data.materials]))
    print("HANGED head top {:.1f} cm under the rod".format(-to_game(landmarks["head_top"], shift).z))
    face = to_game(landmarks["face"], shift)
    print("HANGED face at ({:.1f}, {:.1f}, {:.1f}) cm from the rod".format(face.x, face.y, face.z))

    loop = K.bake(hook_rope(), "bedroom_hook_rope", SLOTS)
    K.export(loop)
    open(os.path.join(K.MODEL_DIR, "bedroom_hook_rope", "GENERATED"), "w").close()

    preview([man, top, loop])


if __name__ == "__main__":
    main()
