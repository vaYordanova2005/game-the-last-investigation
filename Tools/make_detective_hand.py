"""
Builds the detective's left hand and forearm, closed on the lantern's bail, in Blender, headless,
and exports it as an FBX the art pipeline imports.

    blender --background --factory-startup --python Tools/make_detective_hand.py

The only part of him the player ever sees. It used to be a sleeve, a cuff and five boxes, which
read as a toy; this is the same MakeHuman body the hanged man is made from (Tools/make_hanged_man.py,
MPFB and the CC0 system assets, see the 2026-11-24 log), in the same dark suit, with the fingers
curled round a wire and the wrist bent a little under the lantern's weight. Only the forearm and
the hand are kept: the elbow is behind the camera's edge.

Frame (centimetres): the ORIGIN is where the bail's wire passes through the fist. Local +X runs
along the wire, from the index finger to the little finger; local +Z is the back of the hand, the
way the lantern does not hang. The forearm runs off towards the elbow from there; the script prints
its direction, which DetectiveCharacter.cpp turns towards the bottom-left corner of the frame.
The lantern's own origin (Tools/make_lantern.py) is the top of its bail, so the two meet at the
grip with no offset.

Material slots: Skin, Suit (textures written beside the FBX for build_art.py).

Previews: Saved/HandPreview/.
"""

import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blender_kit as K  # noqa: E402
import make_hanged_man as H  # noqa: E402

PREVIEW_DIR = os.path.join(K.ROOT, "Saved", "HandPreview")
NAME = "detective_hand"

ASSETS = (("clothes", "male_elegantsuit01.mhclo", "Clothes", "Suit"),)

# How far each finger closes at each joint, in degrees about the joint's own X (towards the palm):
# a fist round a wire under load, the index a little more open than the rest, the little finger
# tightest.
CURL = {
    "index": (52.0, 78.0, 40.0),
    "middle": (60.0, 82.0, 42.0),
    "ring": (64.0, 84.0, 42.0),
    "pinky": (70.0, 84.0, 40.0),
}
# The thumb comes round over the index's middle joint: (bone, (x, y, z) degrees).
THUMB = (("thumb_01", (8.0, 0.0, -22.0)), ("thumb_02", (22.0, 0.0, -6.0)), ("thumb_03", (30.0, 0.0, 0.0)))
# The lantern pulls the wrist down a little and towards the little finger.
WRIST = (24.0, 0.0, 8.0)

# Everything within this of the forearm-and-hand line is kept, from this far past the elbow.
KEEP_RADIUS = 0.10
KEEP_FROM = 0.03


def build():
    props, assets, humans, targets = H.mpfb()
    body = humans.create_human()
    for key, value in H.MACROS:
        props.set_value(key, value, entity_reference=body)
    targets.reapply_macro_details(body)
    humans.set_character_skin(assets.find_asset_absolute_path(H.SKIN, asset_subdir="skins"), body, skin_type="GAMEENGINE")
    rig = humans.add_builtin_rig(body, "game_engine")
    slot_of = {body.name: "Skin"}
    for subdir, file_name, kind, slot in ASSETS:
        path = assets.find_asset_absolute_path(file_name, asset_subdir=subdir)
        piece = humans.add_mhclo_asset(path, body, asset_type=kind, material_type="GAMEENGINE")
        slot_of[piece.name] = slot
    bpy.context.view_layer.update()

    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="POSE")
    for bone in rig.pose.bones:
        bone.rotation_mode = "XYZ"
    rig.pose.bones["hand_l"].rotation_euler = [math.radians(a) for a in WRIST]
    for finger, angles in CURL.items():
        for joint, angle in enumerate(angles):
            rig.pose.bones["{}_0{}_l".format(finger, joint + 1)].rotation_euler = (math.radians(angle), 0.0, 0.0)
    for name, angles in THUMB:
        rig.pose.bones[name + "_l"].rotation_euler = [math.radians(a) for a in angles]
    bpy.ops.object.mode_set(mode="OBJECT")
    bpy.context.view_layer.update()

    def world(name, end="head"):
        bone = rig.pose.bones[name]
        return rig.matrix_world @ (bone.head if end == "head" else bone.tail)

    marks = {
        "elbow": world("lowerarm_l"),
        "wrist": world("hand_l"),
        "index_knuckle": world("index_01_l"),
        "pinky_knuckle": world("pinky_01_l"),
        "tip": world("middle_03_l", "tail"),
        # The middle of the curl the wire lies in.
        "grip": (world("middle_01_l") + world("middle_02_l") + world("middle_03_l") + world("middle_03_l", "tail")) * 0.25,
        "palm": (rig.matrix_world.to_3x3() @ rig.pose.bones["hand_l"].matrix.to_3x3()).col[2].normalized(),
    }

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
    return baked, marks


def keep_arm(obj, marks):
    """Throws away every face of obj that is not on the forearm or the hand."""
    elbow, wrist, tip = marks["elbow"], marks["wrist"], marks["tip"]
    axis = (wrist - elbow).normalized()

    def near(co):
        if (co - elbow).dot(axis) < KEEP_FROM:
            return False
        for a, b in ((elbow, wrist), (wrist, tip)):
            ab = b - a
            t = max(0.0, min(1.0, (co - a).dot(ab) / ab.length_squared))
            if (co - (a + ab * t)).length < KEEP_RADIUS:
                return True
        return False

    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if not all(near(v.co) for v in f.verts)], context="FACES")
    bm.to_mesh(obj.data)
    bm.free()


def hand_frame(marks):
    """Blender-space matrix of the frame the hand is exported in (see the module docstring)."""
    x = (marks["pinky_knuckle"] - marks["index_knuckle"]).normalized()
    back = -marks["palm"]
    z = (back - x * back.dot(x)).normalized()
    # Mirrored on export, so the game's Y is Blender's -Y: build Blender's Y so that the game frame
    # comes out as X along the wire, Z the back of the hand.
    y = z.cross(x)
    frame = Matrix.Identity(4)
    for i, v in enumerate((x, y, z)):
        frame[0][i], frame[1][i], frame[2][i] = v.x, v.y, v.z
    frame.translation = marks["grip"]
    return frame


def to_game(v):
    return Vector((v.x * 100.0, -v.y * 100.0, v.z * 100.0))


def preview(obj):
    os.makedirs(PREVIEW_DIR, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "TEXTURE"
    scene.display.shading.show_shadows = True
    scene.render.resolution_x, scene.render.resolution_y = 900, 900
    scene.world = bpy.data.worlds.new("preview")
    # A stand-in for the bail's wire through the fist, along X.
    wire = bpy.data.objects.new("wire", bpy.data.meshes.new("wire"))
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=12, radius1=0.003, radius2=0.003, depth=0.2,
                          matrix=Matrix.Rotation(math.radians(90.0), 4, "Y"))
    bm.to_mesh(wire.data)
    scene.collection.objects.link(wire)
    target = bpy.data.objects.new("target", None)
    scene.collection.objects.link(target)
    camera = bpy.data.objects.new("camera", bpy.data.cameras.new("camera"))
    camera.data.lens = 50.0
    scene.collection.objects.link(camera)
    camera.constraints.new("TRACK_TO").target = target
    scene.camera = camera
    for label, location in (("side", (0.0, -0.5, 0.05)), ("along", (0.5, -0.05, 0.1)),
                            ("above", (0.1, -0.2, 0.45)), ("below", (0.15, -0.3, -0.3)),
                            ("axis", (0.32, 0.0, 0.0)), ("axis_far", (-0.32, 0.0, 0.0))):
        camera.location = location
        target.location = (0.0, 0.0, 0.0)
        scene.render.filepath = os.path.join(PREVIEW_DIR, "hand_{}.png".format(label))
        bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(wire)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    pieces, marks = build()
    for obj in pieces:
        keep_arm(obj, marks)

    frame = hand_frame(marks)
    inverse = frame.inverted()
    for obj in pieces:
        obj.data.transform(inverse)
    forearm = inverse.to_3x3() @ (marks["elbow"] - marks["wrist"])
    wrist = inverse @ marks["wrist"]
    tip = inverse @ marks["tip"]

    folder = os.path.join(K.MODEL_DIR, NAME)
    os.makedirs(folder, exist_ok=True)
    H.export_textures(pieces, folder, NAME)
    hand = H.join(pieces, NAME)
    K.export(hand)
    open(os.path.join(folder, "GENERATED"), "w").close()

    d = hand.dimensions
    print("HAND baked {} ({} verts, {:.1f} x {:.1f} x {:.1f} cm, slots {})".format(
        hand.name, len(hand.data.vertices), d.x * 100, d.y * 100, d.z * 100, [m.name if m else None for m in hand.data.materials]))
    f = to_game(forearm.normalized() * 0.01)
    print("HAND forearm direction (game) ({:.3f}, {:.3f}, {:.3f})".format(f.x, f.y, f.z))
    print("HAND wrist (game) {}  fingertip (game) {}".format(tuple(round(c, 1) for c in to_game(wrist)), tuple(round(c, 1) for c in to_game(tip))))
    print("HAND elbow cut (game) {}".format(tuple(round(c, 1) for c in to_game(inverse @ marks["elbow"]))))
    preview(hand)


if __name__ == "__main__":
    main()
