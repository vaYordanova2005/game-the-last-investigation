"""
Builds the detective's hurricane lantern in Blender, headless, and exports it as FBX models the art
pipeline imports.

    blender --background --factory-startup --python Tools/make_lantern.py

The lantern he carries through the whole game, held by the top of its bail at arm's length, the
way the user's reference has it: an old tin hurricane lantern gone brown with rust, a bulging glass
globe in a cage of bent wire, a pierced chimney under a wide cap, two side tubes from the fount to
the top, a wire bail, and inside the globe a candle burned half down on a dish, a run of wax down
one side. Poly Haven's Lantern_01 (the old one) is a different shape of lamp and a size nobody
would carry; this is the one in the reference.

Frame: centimetres, +Z up, the bail in the local XZ plane. The ORIGIN is the middle of the bail's
wire at its top, where the hand closes on it, so the lantern hangs straight down from the hand's
grip point. Shared with DetectiveCharacter.h: BAIL_TOP (the fount's bottom is BAIL_TOP under the
origin) and the flame's root (FLAME_ROOT under the origin).

Two models:
    detective_lantern         the lantern, slots Tin, Rust, Glass, Wax, Wick
    detective_lantern_flame   the candle flame, a teardrop with its origin at its root (slot Flame),
                              scaled and turned in C++ as it gutters

Previews: Saved/LanternPreview/.
"""

import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blender_kit as K  # noqa: E402
from blender_kit import Part, ellipsoid, lathe, tube  # noqa: E402

PREVIEW_DIR = os.path.join(K.ROOT, "Saved", "LanternPreview")
NAME = "detective_lantern"
FLAME_NAME = "detective_lantern_flame"

# Authored from the fount's bottom up; everything is lowered by BAIL_TOP at the end.
BAIL_TOP = 36.0
WICK_TOP = 11.3
FLAME_ROOT = BAIL_TOP - WICK_TOP

# slot -> (preview colour, texel size in cm). The lantern is small, and its tin was photographed
# a metre across: at 45cm a repeat the rust spots are the size they are on a real lantern.
SLOTS = {
    "Tin": ((0.30, 0.20, 0.12, 1.0), 45.0),
    "Rust": ((0.22, 0.12, 0.06, 1.0), 30.0),
    "Glass": ((0.75, 0.80, 0.78, 1.0), 100.0),
    "Wax": ((0.85, 0.78, 0.62, 1.0), 20.0),
    "Wick": ((0.03, 0.03, 0.03, 1.0), 20.0),
    "Flame": ((1.0, 0.75, 0.3, 1.0), 10.0),
}

# The globe: a neck of NECK at both ends, bulging to NECK + BULGE a little below its middle.
GLOBE_LOW = 6.0
GLOBE_HIGH = 21.0
NECK = 3.9
BULGE = 2.7
GLASS = 0.22


def globe_radius(z):
    t = (z - GLOBE_LOW) / (GLOBE_HIGH - GLOBE_LOW)
    # Fuller below the middle, the way a hurricane globe is blown.
    s = math.sin(math.pi * t ** 0.85)
    return NECK + BULGE * max(0.0, s) ** 0.75


def closed_lathe(part, profile, sides=40):
    """
    A turned shell whose profile is a closed loop of (radius, z): every ring is joined to the next
    and the last to the first, nothing is capped. Glass has to be a closed shell with one winding
    (the 10-08 lesson); a lathe with caps puts a disc across the globe's mouth.
    """
    rings = [[part.bm.verts.new((r * math.cos(2 * math.pi * k / sides), r * math.sin(2 * math.pi * k / sides), z))
              for k in range(sides)] for r, z in profile]
    for i in range(len(rings)):
        a, b = rings[i], rings[(i + 1) % len(rings)]
        for k in range(sides):
            part.bm.faces.new((a[k], a[(k + 1) % sides], b[(k + 1) % sides], b[k]))


def fount():
    """The tin fount at the bottom: a squat drum with rolled rims and a collar the globe sits in."""
    tin = Part("Tin", smooth=True)
    lathe(tin, [(0.0, 0.0), (7.4, 0.0), (8.1, 0.25), (8.45, 0.8), (8.2, 1.25), (8.0, 1.4),
                (8.15, 4.1), (8.5, 4.5), (8.3, 4.95), (7.6, 5.25), (5.4, 5.55), (4.55, 5.7),
                (4.45, 6.6), (4.7, 6.85), (4.3, 6.9), (0.0, 6.9)], Matrix.Identity(4), sides=48)
    # The filler cap on the shoulder, and the screw of the globe lift beside it.
    lathe(tin, [(0.0, 5.0), (1.25, 5.0), (1.25, 5.9), (1.05, 6.15), (0.0, 6.15)],
          Matrix.Translation((6.3, 0.0, 0.0)) @ Matrix.Rotation(math.radians(-14.0), 4, "Y"), sides=20)
    return tin


def side_tubes():
    """The two air tubes from the fount's shoulder up to the top, bent in under the cap."""
    rust = Part("Rust", smooth=True)
    for side in (-1.0, 1.0):
        path = [(side * 8.0, 0.0, 3.0), (side * 9.05, 0.0, 3.6), (side * 9.2, 0.0, 5.0),
                (side * 9.2, 0.0, 19.0), (side * 9.0, 0.0, 21.0), (side * 8.0, 0.0, 22.6), (side * 6.6, 0.0, 23.4)]
        tube(rust, K.catmull(path, 4), 0.55, sides=12)
        # The ear the bail turns in.
        ellipsoid(rust, Vector((side * 9.55, 0.0, 20.2)), (0.45, 0.9, 0.9), rings=6, sides=12)
        # The clip that holds the tube to the fount.
        ellipsoid(rust, Vector((side * 8.55, 0.0, 3.9)), (0.7, 1.1, 0.6), rings=6, sides=12)
    return rust


def globe():
    """The glass globe: one closed shell, outside up and inside down."""
    glass = Part("Glass", smooth=True)
    steps = 28
    outer = [(globe_radius(GLOBE_LOW + (GLOBE_HIGH - GLOBE_LOW) * i / steps), GLOBE_LOW + (GLOBE_HIGH - GLOBE_LOW) * i / steps)
             for i in range(steps + 1)]
    inner = [(r - GLASS, z) for r, z in reversed(outer)]
    # A rolled lip at each mouth, so the glass has a thickness the light catches on the rim.
    closed_lathe(glass, outer + inner, sides=48)
    return glass


def guard():
    """The wire cage round the globe: three bent loops (six uprights) and a ring at the bulge."""
    rust = Part("Rust", smooth=True)
    clear = 0.75
    for k in range(6):
        a = math.pi * 2.0 * k / 6.0 + math.pi / 6.0
        path = []
        for i in range(17):
            z = GLOBE_LOW + 0.8 + (GLOBE_HIGH - GLOBE_LOW - 1.6) * i / 16.0
            r = globe_radius(z) + clear
            path.append((r * math.cos(a), r * math.sin(a), z))
        tube(rust, path, 0.2, sides=8)
    zb = GLOBE_LOW + (GLOBE_HIGH - GLOBE_LOW) * 0.42
    rb = globe_radius(zb) + clear + 0.15
    tube(rust, [(rb * math.cos(2 * math.pi * i / 40), rb * math.sin(2 * math.pi * i / 40), zb) for i in range(41)], 0.22, sides=8)
    for z in (GLOBE_LOW + 0.7, GLOBE_HIGH - 0.7):
        r = globe_radius(z) + clear
        tube(rust, [(r * math.cos(2 * math.pi * i / 40), r * math.sin(2 * math.pi * i / 40), z) for i in range(41)], 0.24, sides=8)
    return rust


def top():
    """The collar over the globe, the pierced chimney, the wide cap and its dome."""
    tin = Part("Tin", smooth=True)
    lathe(tin, [(3.55, 20.5), (4.5, 20.5), (4.75, 21.5), (5.2, 22.0), (4.35, 22.25),
                (4.3, 24.0), (7.0, 24.15), (7.45, 24.45), (7.3, 24.75), (6.6, 24.9),
                (4.2, 25.6), (2.6, 26.9), (1.6, 27.5), (1.0, 27.7), (0.0, 27.75)],
          Matrix.Identity(4), sides=48)
    # A row of holes round the chimney for the draught.
    cutter = bmesh.new()
    for k in range(12):
        a = 2.0 * math.pi * k / 12.0
        m = Matrix.Translation((4.3 * math.cos(a), 4.3 * math.sin(a), 23.15)) @ Matrix.Rotation(a, 4, "Z") @ Matrix.Rotation(math.radians(90.0), 4, "Y")
        sub = Part("Tin")
        lathe(sub, [(0.0, -1.2), (0.42, -1.2), (0.42, 1.2), (0.0, 1.2)], m, sides=10)
        sub.bm.to_mesh(tmp := bpy.data.meshes.new("hole"))
        cutter.from_mesh(tmp)
        bpy.data.meshes.remove(tmp)
    tin.cutter = cutter
    # The small ring on the dome a lantern is hung up by.
    ring = Part("Rust", smooth=True)
    tube(ring, [(1.6 * math.cos(2 * math.pi * i / 24), 0.0, 28.9 + 1.2 * math.sin(2 * math.pi * i / 24)) for i in range(25)], 0.18, sides=8)
    return [tin, ring]


def bail():
    """
    The wire bail from one ear over the top to the other, in the XZ plane, its middle a little
    flattened where hands have held it for fifty years. Its top is BAIL_TOP.
    """
    rust = Part("Rust", smooth=True)
    path = []
    for i in range(41):
        t = math.pi * i / 40.0
        x = 9.55 * math.cos(t)
        # A superellipse: shoulders rounder than a half circle, the top flatter.
        s = math.sin(t)
        z = 20.2 + (BAIL_TOP - 20.2) * (abs(s) ** 0.62)
        path.append((x, 0.0, z))
    tube(rust, path, 0.3, sides=10)
    # The turned-in ends through the ears.
    for side in (-1.0, 1.0):
        tube(rust, [(side * 9.55, 0.0, 20.2), (side * 10.3, 0.0, 20.2), (side * 10.3, 0.0, 19.4)], 0.3, sides=10)
    return rust


def candle():
    """A candle burned half down on a dish, its top hollowed round the wick, a run of wax down it."""
    wax = Part("Wax", smooth=True)
    lathe(wax, [(0.0, 6.9), (3.0, 6.9), (3.2, 7.15), (2.9, 7.25), (0.0, 7.25)], Matrix.Identity(4), sides=32)
    lathe(wax, [(0.0, 7.2), (1.38, 7.2), (1.4, 10.5), (1.32, 10.95), (1.12, 10.85), (0.6, 10.6), (0.0, 10.55)],
          Matrix.Identity(4), sides=28)
    # Wax that ran over the lip and set: down one side and pooled on the dish.
    for i, (dz, rr) in enumerate(((10.6, 0.42), (10.0, 0.36), (9.3, 0.34), (8.6, 0.31), (7.9, 0.3))):
        ellipsoid(wax, Vector((1.33, 0.25, dz)), (rr * 0.7, rr, rr * 1.3), rings=6, sides=10)
    ellipsoid(wax, Vector((1.9, 0.3, 7.3)), (0.9, 0.8, 0.18), rings=6, sides=14)
    ellipsoid(wax, Vector((-0.8, 1.5, 7.27)), (0.6, 0.5, 0.12), rings=6, sides=12)
    wick = Part("Wick", smooth=True)
    tube(wick, [(0.0, 0.0, 10.5), (0.02, 0.0, 10.9), (0.1, 0.0, 11.15), (0.22, 0.0, WICK_TOP)], [0.11, 0.1, 0.09, 0.07], sides=6)
    return [wax, wick]


def flame():
    """A candle flame: a teardrop, widest a quarter of the way up, its root at the origin."""
    part = Part("Flame", smooth=True)
    profile = [(0.0, 0.0)]
    for i in range(1, 16):
        t = i / 16.0
        r = 0.52 * math.sin(math.pi * min(1.0, t / 0.3) * 0.5) * (1.0 - t) ** 0.9 * (1.0 + 0.6 * (1.0 - t))
        profile.append((max(r, 0.01), 2.6 * t))
    profile.append((0.0, 2.6))
    lathe(part, profile, Matrix.Identity(4), sides=16)
    return part


def lowered(parts):
    for part in parts:
        bmesh.ops.translate(part.bm, vec=(0.0, 0.0, -BAIL_TOP), verts=part.bm.verts)
        if part.cutter is not None:
            bmesh.ops.translate(part.cutter, vec=(0.0, 0.0, -BAIL_TOP), verts=part.cutter.verts)
    return parts


def drop_empty_slots(obj, into="Tin"):
    """The chimney's boolean brings the cutter's empty slot along for the holes' walls: put them in the tin."""
    mesh = obj.data
    names = [m.name if m else None for m in mesh.materials]
    target = names.index(into)
    empty = [i for i, n in enumerate(names) if n is None]
    for poly in mesh.polygons:
        if poly.material_index in empty:
            poly.material_index = target
    for i in reversed(empty):
        for poly in mesh.polygons:
            if poly.material_index > i:
                poly.material_index -= 1
        mesh.materials.pop(index=i)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    parts = [fount(), side_tubes(), globe(), guard(), *top(), bail(), *candle()]
    lantern = K.bake(lowered(parts), NAME, SLOTS)
    drop_empty_slots(lantern)
    K.export(lantern)
    open(os.path.join(K.MODEL_DIR, NAME, "GENERATED"), "w").close()

    fire = K.bake([flame()], FLAME_NAME, SLOTS)
    K.export(fire)
    open(os.path.join(K.MODEL_DIR, FLAME_NAME, "GENERATED"), "w").close()
    fire.location = (0.0, 0.0, -FLAME_ROOT * 0.01)

    for obj in (lantern, fire):
        d = obj.dimensions
        print("LANTERN baked {} ({} verts, {:.1f} x {:.1f} x {:.1f} cm, slots {})".format(
            obj.name, len(obj.data.vertices), d.x * 100, d.y * 100, d.z * 100, [m.name if m else None for m in obj.data.materials]))
    print("LANTERN flame root {:.1f} cm under the bail".format(FLAME_ROOT))

    K.preview([lantern, fire], PREVIEW_DIR, "lantern",
              (("front", (0.0, -0.75, -0.12), (0.0, 0.0, -0.2)),
               ("three_quarter", (0.45, -0.55, 0.05), (0.0, 0.0, -0.2)),
               ("above", (0.25, -0.35, 0.2), (0.0, 0.0, -0.15))), resolution=(900, 1100))


if __name__ == "__main__":
    main()
