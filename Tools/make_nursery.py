"""
Builds the girl's bedroom's furniture, toys and belongings in Blender, headless, and exports each
as an FBX the art pipeline imports.

    blender --background --factory-startup --python Tools/make_nursery.py

Nothing on Poly Haven is a child's bed, a painted wardrobe, a teddy bear or a school backpack, and
the house's other rooms have already shown what boxes and spheres do to things like that: a bed of
slabs is a crate, a teddy of spheres is a clutch of eggs. Here the furniture has bevelled edges
that catch the lantern, turned spindles and legs, panelled doors and a heart cut through the back
of the chair; the soft things are metaballs, one stuffed skin that swells where the parts meet.

Outputs Art/Source/Models/<name>/<name>.fbx for every model below, with an empty GENERATED file
beside it so Tools/build_art.py re-imports it every run with its own normals, and a set of
Workbench previews in Saved/NurseryPreview/ to judge the shapes before any import.

Frame, as every prop in the house: centimetres, origin on the floor under the middle of the
footprint, and the front faces local +Y. Exceptions are noted on the model (the hinged parts have
their origin on the hinge).

Material slots are set by name in C++ (ANurseryActor). Their texel sizes here match the surface
each one is given there, whose instances have their tiling at one:
    Paint, PaintWhite, Inside  the cracked plaster photograph (RoomSurfaces::Plaster, 104cm),
                               which reads on furniture as old crazed paint
    Wood                       weathered planks (RoomSurfaces::RoughWood, 150cm)
    Fabric, Fur, Mattress...   linen (RoomSurfaces::Drapery, 34cm)
Everything else is a flat colour, where the UVs do not matter.
"""

import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blender_kit as K  # noqa: E402
from blender_kit import Part, at, box, cbox, ellipsoid, lathe, prism, prism_xz, tube, blob  # noqa: E402

PREVIEW_DIR = os.path.join(K.ROOT, "Saved", "NurseryPreview")

PAINT = 104.0
WOOD = 150.0
CLOTH = 34.0
FLAT = 100.0

SLOTS = {
    "Paint": ((0.62, 0.40, 0.42, 1.0), PAINT),
    "PaintWhite": ((0.80, 0.78, 0.72, 1.0), PAINT),
    "Inside": ((0.40, 0.30, 0.30, 1.0), PAINT),
    "Wood": ((0.40, 0.28, 0.18, 1.0), WOOD),
    "Brass": ((0.60, 0.45, 0.20, 1.0), FLAT),
    "Shadow": ((0.02, 0.02, 0.02, 1.0), FLAT),
    "Mattress": ((0.70, 0.66, 0.58, 1.0), CLOTH),
    "Roof": ((0.45, 0.22, 0.24, 1.0), FLAT),
    "Trim": ((0.85, 0.82, 0.75, 1.0), FLAT),
    "Fur": ((0.55, 0.38, 0.22, 1.0), CLOTH),
    "Pad": ((0.80, 0.66, 0.50, 1.0), CLOTH),
    "Button": ((0.03, 0.02, 0.02, 1.0), FLAT),
    "Nose": ((0.20, 0.10, 0.08, 1.0), FLAT),
    "Ribbon": ((0.75, 0.25, 0.35, 1.0), FLAT),
    "Skin": ((0.85, 0.72, 0.62, 1.0), CLOTH),
    "Dress": ((0.45, 0.55, 0.75, 1.0), CLOTH),
    "Hair": ((0.55, 0.30, 0.12, 1.0), CLOTH),
    "Shoe": ((0.20, 0.10, 0.10, 1.0), FLAT),
    "Cheek": ((0.85, 0.45, 0.45, 1.0), FLAT),
    "Fabric": ((0.55, 0.30, 0.45, 1.0), CLOTH),
    "Zip": ((0.50, 0.50, 0.52, 1.0), FLAT),
    "Charm": ((0.85, 0.60, 0.80, 1.0), CLOTH),
    "Plastic": ((0.85, 0.85, 0.88, 1.0), FLAT),
    "Cushion": ((0.25, 0.25, 0.27, 1.0), CLOTH),
    "Case": ((0.85, 0.50, 0.60, 1.0), FLAT),
    "Screen": ((0.01, 0.01, 0.012, 1.0), FLAT),
    "Lens": ((0.05, 0.05, 0.06, 1.0), FLAT),
    "Box": ((0.70, 0.45, 0.50, 1.0), PAINT),
    "Velvet": ((0.45, 0.10, 0.18, 1.0), CLOTH),
    "Mirror": ((0.30, 0.32, 0.34, 1.0), FLAT),
    "Figure": ((0.90, 0.85, 0.85, 1.0), FLAT),
    "Canvas": ((0.85, 0.82, 0.78, 1.0), CLOTH),
    "Sole": ((0.90, 0.90, 0.88, 1.0), FLAT),
    "Lace": ((0.95, 0.95, 0.95, 1.0), FLAT),
    "Metal": ((0.85, 0.60, 0.65, 1.0), FLAT),
    "Bulb": ((0.85, 0.85, 0.80, 1.0), FLAT),
}


def to_axis(direction):
    """A rotation matrix that turns +Z along direction."""
    return Vector(direction).normalized().to_track_quat("Z", "Y").to_matrix().to_4x4()


def tapered_leg(part, x, y, z0, z1, top, bottom):
    """A square leg tapering from top to bottom: eight verts, a box narrowed at its foot."""
    bm = part.bm
    lo = [bm.verts.new((x + sx * bottom * 0.5, y + sy * bottom * 0.5, z0)) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    hi = [bm.verts.new((x + sx * top * 0.5, y + sy * top * 0.5, z1)) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    bm.faces.new(lo[::-1])
    bm.faces.new(hi)
    for i in range(4):
        j = (i + 1) % 4
        bm.faces.new((lo[i], lo[j], hi[j], hi[i]))


def heart_outline(cx, cz, size, n=48):
    """A heart in the XZ plane, ccw seen from -Y."""
    points = []
    for k in range(n):
        t = k / n * math.tau
        x = 16 * math.sin(t) ** 3
        z = 13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)
        points.append((cx + x * size / 32.0, cz + z * size / 32.0))
    return points[::-1]


def knob(part, x, y, z, radius=1.4, out=(0, 1, 0)):
    """A turned knob standing out of a face along out."""
    profile = [(0.0, 0.0), (radius * 0.55, 0.0), (radius * 0.45, radius * 0.6), (radius * 0.9, radius * 1.1),
               (radius, radius * 1.6), (radius * 0.7, radius * 2.1), (0.0, radius * 2.2)]
    lathe(part, profile, Matrix.Translation((x, y, z)) @ to_axis(out), sides=16)


# ---------------------------------------------------------------------------------------------
# Furniture.
# ---------------------------------------------------------------------------------------------

def bed():
    """A single bed in white-painted wood, 96 x 200. Length along Y, head at -Y, foot at +Y."""
    parts = []
    paint = Part("PaintWhite", bevel=0.5)
    turned = Part("PaintWhite")
    hx, hy = 45.0, 97.0
    for sx in (-1, 1):
        for sy, height in ((-1, 104.0), (1, 80.0)):
            cbox(paint, sx * hx, sy * hy, height * 0.5, 5.6, 5.6, height)
            # A ball finial on a collar on every post.
            lathe(turned, [(0.0, 0.0), (3.4, 0.0), (3.4, 1.2), (2.0, 1.8), (2.4, 2.8), (3.6, 4.6), (3.7, 6.0),
                           (3.0, 7.6), (1.6, 8.6), (0.0, 8.9)], at(sx * hx, sy * hy, height), sides=24)
    # Head and foot: an arched top rail, a bottom rail, and turned spindles between.
    for sy, low, high, crest in ((-1, 46.0, 86.0, 9.0), (1, 46.0, 64.0, 5.0)):
        y0, y1 = sy * hy - 1.4, sy * hy + 1.4
        top = [(42.2 - 84.4 * k / 24, 0.0) for k in range(25)]
        top = [(x, high + 7.0 + crest * math.cos(math.pi * x / 84.4)) for x, _ in top]
        prism_xz(paint, [(-42.2, high), (42.2, high)] + top, y0, y1)
        cbox(paint, 0.0, sy * hy, low - 4.0, 84.4, 2.8, 8.0)
        count = 9 if sy < 0 else 7
        for k in range(count):
            x = -36.0 + 72.0 * k / (count - 1)
            span = high - low
            profile = [(0.0, 0.0), (1.5, 0.0), (1.5, span * 0.08), (1.0, span * 0.14), (1.25, span * 0.3),
                       (1.4, span * 0.5), (1.25, span * 0.7), (1.0, span * 0.86), (1.5, span * 0.92), (1.5, span), (0.0, span)]
            lathe(turned, profile, at(x, sy * hy, low), sides=14)
    # Side rails and the boards the mattress lies on.
    for sx in (-1, 1):
        cbox(paint, sx * (hx - 0.5), 0.0, 30.0, 2.6, 2 * hy - 5.6, 14.0)
    slats = Part("Wood", bevel=0.2)
    for k in range(7):
        cbox(slats, 0.0, -84.0 + 28.0 * k, 34.0, 86.0, 9.0, 1.8)
    mattress = Part("Mattress", bevel=4.5, segments=5, angle=30.0)
    cbox(mattress, 0.0, 0.0, 43.4, 87.0, 186.0, 17.0)
    parts += [paint, turned, slats, mattress]
    return parts


def nightstand():
    """A bedside table, 44 x 36 x 60: a drawer over an open shelf, on four tapered legs."""
    paint = Part("Paint", bevel=0.4)
    top = Part("Paint", bevel=0.9, segments=3)
    cbox(top, 0.0, 0.5, 58.5, 46.0, 38.0, 3.0)
    for sx in (-1, 1):
        cbox(paint, sx * 21.0, 0.0, 33.0, 2.0, 34.0, 48.0)
    cbox(paint, 0.0, -16.2, 33.0, 40.0, 1.6, 48.0)
    cbox(paint, 0.0, 0.5, 10.0, 40.0, 33.0, 2.0)
    cbox(paint, 0.0, 0.5, 40.5, 40.0, 33.0, 2.0)
    front = Part("Paint", bevel=0.5)
    cbox(front, 0.0, 17.6, 48.6, 39.0, 1.8, 14.2)
    legs = Part("Paint", bevel=0.3)
    for sx in (-1, 1):
        for sy in (-1, 1):
            tapered_leg(legs, sx * 20.5, sy * 15.5 + 0.5, 0.0, 10.0, 3.4, 2.4)
    inside = Part("Shadow")
    cbox(inside, 0.0, 0.5, 48.6, 39.6, 32.0, 15.0)
    brass = Part("Brass")
    knob(brass, 0.0, 18.4, 48.6, 1.3)
    return [paint, top, front, legs, inside, brass]


def wardrobe_door_shape(part, x0, x1, y_front, z0, z1, panel):
    """One door leaf with two raised panels, its face at y_front."""
    cbox(part, (x0 + x1) * 0.5, y_front - 1.0, (z0 + z1) * 0.5, x1 - x0, 2.0, z1 - z0)
    stile = 6.0
    split = z0 + (z1 - z0) * 0.42
    for a, b in ((z0 + stile, split - stile * 0.5), (split + stile * 0.5, z1 - stile)):
        cbox(panel, (x0 + x1) * 0.5, y_front + 0.35, (a + b) * 0.5, x1 - x0 - stile * 2.0, 0.7, b - a)
        cbox(panel, (x0 + x1) * 0.5, y_front + 0.85, (a + b) * 0.5, x1 - x0 - stile * 2.0 - 5.0, 0.6, b - a - 5.0)


def wardrobe():
    """A child's wardrobe, 100 x 52 x 192: two panelled doors over a drawer, a cornice and crest.
    The right-hand door is its own model (wardrobe_door) so it can stand open."""
    paint = Part("Paint", bevel=0.4)
    inside = Part("Inside")
    panel = Part("Paint", bevel=0.35, segments=2)
    trim = Part("Paint", bevel=0.8, segments=3)
    cbox(trim, 0.0, 0.0, 4.0, 99.0, 51.0, 8.0)
    for sx in (-1, 1):
        cbox(paint, sx * 49.0, -1.0, 94.0, 2.0, 50.0, 172.0)
    cbox(paint, 0.0, -25.2, 94.0, 96.0, 1.6, 172.0)
    cbox(paint, 0.0, -1.0, 9.0, 96.0, 50.0, 2.0)
    cbox(paint, 0.0, -1.0, 179.0, 96.0, 50.0, 2.0)
    # Inside, which the open door shows: lined in a darker paint, a hat shelf and a hanging rail.
    cbox(inside, 0.0, -24.3, 107.0, 95.6, 0.2, 140.0)
    cbox(inside, 0.0, -1.0, 37.2, 95.6, 48.0, 0.4)
    cbox(paint, 0.0, -2.0, 160.0, 96.0, 44.0, 1.8)
    brass = Part("Brass")
    tube(brass, [(-48.0, -3.0, 152.0), (48.0, -3.0, 152.0)], 1.0, sides=12)
    # The drawer and the rail over it.
    cbox(paint, 0.0, 24.6, 35.5, 96.0, 1.6, 3.0)
    front = Part("Paint", bevel=0.6, segments=2)
    cbox(front, 0.0, 25.0, 22.0, 95.0, 2.0, 23.0)
    cbox(panel, 0.0, 26.3, 22.0, 85.0, 0.6, 15.0)
    knob(brass, -24.0, 26.6, 22.0, 1.4)
    knob(brass, 24.0, 26.6, 22.0, 1.4)
    # The left door, shut.
    wardrobe_door_shape(front, -48.0, -0.2, 26.0, 37.4, 177.6, panel)
    knob(brass, -4.5, 26.6, 108.0, 1.5)
    # Cornice, and a little arched crest over it.
    cbox(trim, 0.0, 0.5, 183.0, 104.0, 54.0, 6.0)
    cbox(trim, 0.0, 1.0, 187.5, 108.0, 56.0, 3.0)
    crest = [(-30.0, 189.0)]
    for k in range(1, 20):
        x = -30.0 + 60.0 * k / 20
        crest.append((x, 189.0 + 9.0 * math.cos(math.pi * x / 60.0)))
    crest.append((30.0, 189.0))
    prism_xz(trim, crest[::-1], 25.0, 27.4)
    shadow = Part("Shadow")
    cbox(shadow, 0.0, -1.0, 22.0, 95.6, 48.0, 23.0)
    return [paint, inside, panel, trim, front, brass, shadow]


def wardrobe_door():
    """The right-hand door. Origin on the hinge at floor level, the leaf running to -X, face at y=0."""
    leaf = Part("Paint", bevel=0.6, segments=2)
    panel = Part("Paint", bevel=0.35, segments=2)
    wardrobe_door_shape(leaf, -47.8, 0.0, 0.0, 37.4, 177.6, panel)
    # Its inside face, which is what an open door shows.
    inside = Part("Inside")
    cbox(inside, -23.9, -2.1, 107.5, 46.0, 0.2, 138.0)
    brass = Part("Brass")
    knob(brass, -43.5, 0.6, 108.0, 1.5)
    for z in (50.0, 165.0):
        cbox(brass, -0.4, -1.0, z, 1.2, 2.4, 7.0)
    return [leaf, panel, inside, brass]


def desk():
    """A writing desk, 110 x 55 x 75: three drawers in a pedestal on the right, a pencil drawer in
    the apron, two tapered legs on the left and a low gallery along the back."""
    paint = Part("Paint", bevel=0.4)
    top = Part("Paint", bevel=0.9, segments=3)
    cbox(top, 0.0, 0.0, 73.5, 112.0, 56.0, 3.0)
    for x in (16.0, 52.0):
        cbox(paint, x, 0.0, 39.0, 2.0, 52.0, 70.0)
    cbox(paint, 34.0, -25.0, 39.0, 34.0, 1.6, 70.0)
    cbox(paint, 34.0, 0.0, 2.0, 38.0, 50.0, 4.0)
    fronts = Part("Paint", bevel=0.6, segments=2)
    brass = Part("Brass")
    for z0, z1 in ((4.6, 26.0), (26.8, 48.0), (48.8, 70.0)):
        cbox(fronts, 34.0, 26.2, (z0 + z1) * 0.5, 35.0, 1.8, z1 - z0)
        knob(brass, 34.0, 27.0, (z0 + z1) * 0.5 + 2.0, 1.3)
    legs = Part("Paint", bevel=0.3)
    for y in (-23.5, 23.5):
        tapered_leg(legs, -51.5, y, 0.0, 72.0, 4.2, 2.8)
    cbox(paint, -18.0, 24.0, 67.0, 66.0, 2.0, 10.0)
    cbox(paint, -18.0, -24.0, 67.0, 66.0, 2.0, 10.0)
    cbox(paint, -52.0, 0.0, 67.0, 2.0, 46.0, 10.0)
    cbox(fronts, -18.0, 25.4, 67.2, 40.0, 1.2, 7.6)
    knob(brass, -18.0, 26.0, 67.2, 1.1)
    gallery = Part("Paint", bevel=0.7, segments=3)
    profile = [(-55.0, 75.0), (55.0, 75.0), (55.0, 82.0)]
    for k in range(1, 12):
        x = 55.0 - 110.0 * k / 12
        profile.append((x, 82.0 + 4.0 * math.sin(math.pi * k / 12)))
    profile.append((-55.0, 82.0))
    prism_xz(gallery, profile, -27.5, -25.6)
    shadow = Part("Shadow")
    cbox(shadow, 34.0, 0.0, 37.0, 33.0, 48.0, 66.0)
    return [paint, top, fronts, legs, brass, gallery, shadow]


def chair():
    """A painted chair to go with the desk: seat at 45, turned legs, a heart cut through the top
    rail. Front at +Y."""
    seat = Part("Paint", bevel=0.8, segments=3)
    cbox(seat, 0.0, 0.0, 43.5, 41.0, 39.0, 3.0)
    turned = Part("Paint")
    leg = [(0.0, 0.0), (1.4, 0.0), (1.6, 2.0), (1.5, 12.0), (2.1, 14.0), (1.5, 16.0), (1.8, 30.0), (2.0, 40.0), (2.0, 42.0), (0.0, 42.0)]
    for sx in (-1, 1):
        lathe(turned, leg, at(sx * 17.0, 16.0, 0.0), sides=16)
        post = [(0.0, 0.0), (1.5, 0.0), (1.7, 2.0), (1.6, 14.0), (2.2, 16.0), (1.6, 18.0), (1.9, 44.0), (1.7, 78.0),
                (2.4, 80.0), (2.6, 83.0), (1.4, 85.0), (0.0, 85.5)]
        lathe(turned, post, at(sx * 17.0, -16.0, 0.0, roll=-4.0), sides=16)
        tube(turned, [(sx * 17.0, -15.0, 15.0), (sx * 17.0, 15.0, 15.0)], 0.9, sides=10)
    tube(turned, [(-17.0, 16.0, 13.0), (17.0, 16.0, 13.0)], 0.9, sides=10)
    rails = Part("Paint", bevel=0.5, segments=2)
    lean = at(0.0, -16.0, 0.0, roll=-4.0)
    cbox(rails, 0.0, 0.0, 74.0, 33.0, 2.4, 11.0, matrix=lean)
    cbox(rails, 0.0, 0.0, 53.0, 33.0, 2.0, 4.0, matrix=lean)
    for x in (-8.0, 0.0, 8.0):
        cbox(rails, x, 0.0, 61.0, 2.2, 1.6, 12.0, matrix=lean)
    cutter = Part("Paint")
    prism_xz(cutter, heart_outline(0.0, 74.8, 8.0), -4.0, 4.0, matrix=lean)
    rails.cutter = cutter.bm
    return [seat, turned, rails]


def bookcase():
    """A small open bookcase, 70 x 28 x 132, scalloped along its top."""
    paint = Part("Paint", bevel=0.4)
    for sx in (-1, 1):
        cbox(paint, sx * 34.0, 0.0, 62.0, 2.0, 28.0, 124.0)
    for z in (6.0, 38.0, 69.0, 99.0):
        cbox(paint, 0.0, 0.5, z, 66.0, 27.0, 2.0)
    cbox(paint, 0.0, 13.0, 3.0, 66.0, 2.0, 6.0)
    top = Part("Paint", bevel=0.9, segments=3)
    cbox(top, 0.0, 0.5, 125.5, 74.0, 31.0, 3.0)
    crest = [(-35.0, 127.0)]
    for k in range(1, 24):
        x = -35.0 + 70.0 * k / 24
        crest.append((x, 127.0 + 4.0 + 3.0 * math.cos(math.pi * x / 70.0) * abs(math.cos(math.pi * x / 14.0))))
    crest.append((35.0, 127.0))
    prism_xz(top, crest[::-1], -14.5, -12.7)
    inside = Part("Inside")
    cbox(inside, 0.0, -13.4, 62.0, 66.0, 1.2, 120.0)
    return [paint, top, inside]


def toyshelf():
    """A low shelf unit of six cubbies, 120 x 34 x 80, for the toys."""
    paint = Part("Paint", bevel=0.4)
    top = Part("Paint", bevel=0.9, segments=3)
    cbox(top, 0.0, 0.0, 78.5, 122.0, 35.0, 3.0)
    for sx in (-1, 1):
        cbox(paint, sx * 59.0, 0.0, 41.0, 2.0, 33.0, 74.0)
        cbox(paint, sx * 19.6, 0.0, 41.0, 1.8, 33.0, 74.0)
    for z in (5.0, 41.0):
        cbox(paint, 0.0, 0.0, z, 116.0, 33.0, 2.0)
    cbox(paint, 0.0, 15.5, 2.0, 118.0, 2.0, 4.0)
    inside = Part("Inside")
    cbox(inside, 0.0, -16.0, 41.0, 116.0, 1.0, 74.0)
    return [paint, top, inside]


def toychest():
    """A toy chest, 80 x 42 x 44, on bun feet. The lid is its own model (toychest_lid)."""
    paint = Part("Paint", bevel=0.5)
    for sy in (-1, 1):
        cbox(paint, 0.0, sy * 20.0, 24.0, 80.0, 2.0, 40.0)
    for sx in (-1, 1):
        cbox(paint, sx * 39.0, 0.0, 24.0, 2.0, 38.0, 40.0)
    cbox(paint, 0.0, 0.0, 5.0, 76.0, 38.0, 2.0)
    band = Part("Paint", bevel=0.6, segments=2)
    for z in (5.5, 42.0):
        cbox(band, 0.0, 0.0, z, 82.4, 44.4, 3.0)
    inside = Part("Inside")
    for sy in (-1, 1):
        cbox(inside, 0.0, sy * 18.9, 25.0, 75.8, 0.2, 38.0)
    for sx in (-1, 1):
        cbox(inside, sx * 37.9, 0.0, 25.0, 0.2, 37.6, 38.0)
    feet = Part("Paint")
    for sx in (-1, 1):
        for sy in (-1, 1):
            lathe(feet, [(0.0, 0.0), (2.0, 0.0), (3.2, 1.0), (3.4, 2.4), (2.6, 4.0), (0.0, 4.2)], at(sx * 35.0, sy * 17.0, 0.0), sides=16)
    brass = Part("Brass")
    for sx in (-1, 1):
        tube(brass, [(sx * 41.2, -6.0, 31.0), (sx * 43.6, -5.0, 33.0), (sx * 43.6, 5.0, 33.0), (sx * 41.2, 6.0, 31.0)], 0.7, sides=10)
    return [paint, band, inside, feet, brass]


def toychest_lid():
    """The chest's lid. Origin on the hinge line at the back top edge; the lid runs to +Y from it."""
    lid = Part("Paint", bevel=1.2, segments=3)
    cbox(lid, 0.0, 22.2, 1.8, 83.0, 44.4, 3.6)
    inside = Part("Inside")
    cbox(inside, 0.0, 22.2, -0.05, 76.0, 38.0, 0.1)
    brass = Part("Brass")
    for x in (-25.0, 25.0):
        cbox(brass, x, 1.5, -0.3, 6.0, 3.0, 0.6)
    return [lid, inside, brass]


def dollhouse():
    """A two-storey toy house, 56 x 30, 64 to the ridge, on a base board. Front at +Y."""
    walls = Part("Paint", bevel=0.3)
    cbox(walls, 0.0, 0.0, 22.0, 52.0, 28.0, 40.0)
    gable = Part("Paint", bevel=0.3)
    for sx in (-1, 1):
        prism_xz(gable, [(-14.0, 42.0), (14.0, 42.0), (0.0, 59.0)], -1.0, 1.0, matrix=at(sx * 25.0, 0.0, 0.0, yaw=90.0))
    base = Part("Trim", bevel=0.4)
    cbox(base, 0.0, 0.0, 1.0, 58.0, 33.0, 2.0)
    roof = Part("Roof", bevel=0.4)
    slope = math.degrees(math.atan2(18.0, 16.0))
    for sy in (-1, 1):
        cbox(roof, 0.0, 0.0, 0.0, 60.0, 25.6, 1.6, matrix=at(0.0, sy * 8.0, 51.6, roll=-sy * slope))
    cbox(roof, 14.0, -4.0, 60.0, 4.4, 4.4, 14.0)
    trim = Part("Trim", bevel=0.2)
    shadow = Part("Shadow")
    cbox(trim, 0.0, 14.3, 22.0, 52.4, 0.8, 1.4)
    for x in (-15.0, 15.0):
        for z in (10.0, 31.0):
            if x < 0 and z < 20:
                continue
            cbox(shadow, x, 14.05, z, 9.0, 0.3, 10.0)
            for dx, dz, sx, sz in ((0, 5.4, 10.8, 1.0), (0, -5.4, 10.8, 1.0), (-5.0, 0, 1.0, 11.8), (5.0, 0, 1.0, 11.8), (0, 0, 0.6, 10.0), (0, 0, 10.0, 0.6)):
                cbox(trim, x + dx, 14.4, z + dz, sx, 0.8, sz)
            cbox(trim, x, 14.9, z - 6.2, 12.0, 2.0, 1.0)
    cbox(shadow, -15.0, 14.05, 9.0, 8.0, 0.3, 13.0)
    cbox(trim, -15.0, 14.5, 9.0, 7.0, 0.8, 12.0)
    for dx, dz, sx, sz in ((0, 7.0, 10.0, 1.2), (-4.5, 0, 1.0, 14.0), (4.5, 0, 1.0, 14.0)):
        cbox(trim, -15.0 + dx, 14.6, 9.0 + dz, sx, 1.0, sz)
    brass = Part("Brass")
    ellipsoid(brass, (-12.6, 15.2, 9.0), (0.5, 0.5, 0.5))
    return [walls, gable, base, roof, trim, shadow, brass]


# ---------------------------------------------------------------------------------------------
# Soft toys: metaball skins, with the features as separate pieces sewn on.
# ---------------------------------------------------------------------------------------------

def teddy():
    """A sitting teddy bear, about 34cm, facing +Y."""
    fur = Part("Fur", smooth=True)
    elements = [
        ((0.0, 0.0, 11.0), (8.6, 7.8, 10.6), None),
        ((0.0, 1.5, 24.8), (7.2, 6.6, 6.6), None),
        ((0.0, 6.0, 22.8), (3.4, 2.6, 2.6), None),
    ]
    for sx in (-1, 1):
        elements.append(((sx * 6.0, 0.0, 30.8), (2.6, 1.5, 2.6), None))
        elements.append(((sx * 9.2, 4.6, 11.8), (2.9, 2.9, 6.4), to_axis((sx * 0.25, 0.55, -1.0))))
        elements.append(((sx * 5.4, 8.6, 4.4), (3.4, 6.4, 3.4), None))
    blob(fur, elements, resolution=0.55)
    pad = Part("Pad", smooth=True)
    ellipsoid(pad, (0.0, 7.2, 22.6), (3.0, 1.9, 2.3))
    for sx in (-1, 1):
        ellipsoid(pad, (sx * 6.0, 1.0, 30.8), (1.7, 0.6, 1.7))
        ellipsoid(pad, (sx * 5.4, 14.6, 4.6), (2.5, 0.7, 2.8))
    button = Part("Button", smooth=True)
    for sx in (-1, 1):
        ellipsoid(button, (sx * 2.7, 6.6, 26.6), (0.8, 0.5, 0.8))
    nose = Part("Nose", smooth=True)
    ellipsoid(nose, (0.0, 9.2, 23.8), (1.3, 0.8, 0.9))
    ribbon = Part("Ribbon", smooth=True)
    ring = [(math.cos(a) * 6.9, 0.8 + math.sin(a) * 6.6, 19.0) for a in [k * math.tau / 24 for k in range(25)]]
    tube(ribbon, ring, 0.7, sides=8)
    for sx in (-1, 1):
        ellipsoid(ribbon, (sx * 2.4, 7.4, 19.2), (2.4, 0.8, 1.6))
        tube(ribbon, [(sx * 0.6, 7.6, 18.8), (sx * 1.6, 8.0, 16.4), (sx * 2.2, 8.2, 14.6)], 0.5, sides=6)
    return [fur, pad, button, nose, ribbon]


def rabbit():
    """A sitting plush rabbit, about 40cm to the tips of its ears, one ear flopped. Facing +Y."""
    fur = Part("Fur", smooth=True)
    elements = [
        ((0.0, 0.0, 10.0), (7.8, 7.0, 9.6), None),
        ((0.0, 2.0, 21.6), (6.2, 5.6, 5.6), None),
        ((0.0, 6.0, 20.2), (3.2, 2.2, 2.2), None),
        # The upright ear, and the one that has flopped forward over the face.
        ((2.8, 0.4, 31.0), (2.1, 1.1, 7.4), to_axis((0.18, -0.05, 1.0))),
        ((-3.4, 0.6, 28.4), (2.1, 1.1, 3.2), to_axis((-0.4, 0.0, 1.0))),
        ((-6.0, 3.2, 28.6), (2.0, 1.1, 4.6), to_axis((-0.6, 1.0, -0.2))),
    ]
    for sx in (-1, 1):
        elements.append(((sx * 6.8, 4.0, 11.0), (2.3, 2.3, 5.2), to_axis((sx * 0.2, 0.5, -1.0))))
        elements.append(((sx * 4.4, 7.6, 3.0), (3.0, 6.0, 2.8), None))
    blob(fur, elements, resolution=0.55)
    pad = Part("Pad", smooth=True)
    ellipsoid(pad, (2.95, 1.45, 31.4), (1.2, 0.3, 5.4), matrix=to_axis((0.18, -0.05, 1.0)))
    ellipsoid(pad, (0.0, 4.4, 11.0), (4.2, 3.4, 5.6))
    button = Part("Button", smooth=True)
    for sx in (-1, 1):
        ellipsoid(button, (sx * 2.4, 6.6, 23.2), (0.7, 0.45, 0.8))
    nose = Part("Nose", smooth=True)
    ellipsoid(nose, (0.0, 8.1, 21.0), (0.9, 0.6, 0.6))
    return [fur, pad, button, nose]


def ragdoll():
    """A soft cloth doll, sitting with her legs out, about 38cm. Plaits, a dress, stitched eyes —
    a doll somebody made, not a porcelain one. Facing +Y."""
    skin = Part("Skin", smooth=True)
    ellipsoid(skin, (0.0, 0.0, 30.0), (6.2, 5.8, 6.4), rings=16, sides=24)
    tube(skin, [(0.0, 0.0, 22.0), (0.0, 0.0, 25.5)], 2.0, sides=12)
    for sx in (-1, 1):
        tube(skin, [(sx * 4.6, 0.2, 22.0), (sx * 6.8, 1.6, 17.0), (sx * 7.6, 3.2, 12.4)], 1.35, sides=10)
        ellipsoid(skin, (sx * 7.7, 3.5, 11.8), (1.6, 1.6, 1.8))
        tube(skin, [(sx * 2.6, 2.0, 5.0), (sx * 3.0, 9.0, 4.0), (sx * 3.4, 16.0, 3.2)], 1.6, sides=10)
    dress = Part("Dress", smooth=True)
    lathe(dress, [(0.0, 4.2), (12.6, 4.2), (12.2, 6.4), (9.4, 9.0), (6.4, 12.8), (4.8, 16.6), (4.5, 20.6),
                  (4.7, 22.6), (3.4, 24.2), (0.0, 24.6)], at(0.0, 0.0, 0.0), sides=32)
    for sx in (-1, 1):
        ellipsoid(dress, (sx * 4.9, 0.0, 22.2), (2.4, 2.2, 2.0))
    hair = Part("Hair", smooth=True)
    ellipsoid(hair, (0.0, -1.6, 32.0), (6.6, 6.0, 6.0), rings=14, sides=24)
    ellipsoid(hair, (0.0, 3.6, 35.0), (4.6, 1.8, 1.6))
    for sx in (-1, 1):
        tube(hair, [(sx * 5.2, -1.0, 30.0), (sx * 6.6, 0.0, 25.0), (sx * 7.0, 1.0, 20.0)], [1.4, 1.2, 1.0], sides=10)
        ellipsoid(hair, (sx * 7.0, 1.1, 19.0), (1.2, 1.2, 1.4))
    ribbon = Part("Ribbon", smooth=True)
    for sx in (-1, 1):
        ellipsoid(ribbon, (sx * 6.4, -0.4, 27.6), (1.6, 0.8, 1.0))
    button = Part("Button", smooth=True)
    for sx in (-1, 1):
        ellipsoid(button, (sx * 2.2, 5.5, 30.8), (0.65, 0.4, 0.65))
    tube(button, [(-1.4, 5.55, 27.6), (0.0, 5.75, 27.0), (1.4, 5.55, 27.6)], 0.22, sides=6)
    cheek = Part("Cheek", smooth=True)
    for sx in (-1, 1):
        ellipsoid(cheek, (sx * 3.4, 5.0, 28.6), (1.0, 0.4, 0.7))
    shoe = Part("Shoe", smooth=True)
    for sx in (-1, 1):
        ellipsoid(shoe, (sx * 3.4, 17.6, 3.4), (1.9, 2.6, 1.8))
    return [skin, dress, hair, ribbon, button, cheek, shoe]


def toy_block():
    """One wooden alphabet block, 5cm."""
    block = Part("Paint", bevel=0.45, segments=3)
    cbox(block, 0.0, 0.0, 2.5, 5.0, 5.0, 5.0)
    return [block]


# ---------------------------------------------------------------------------------------------
# Her things.
# ---------------------------------------------------------------------------------------------

def backpack():
    """A school backpack, 30 x 15 x 40, standing. Front pocket at +Y, straps at -Y."""
    body = Part("Fabric", bevel=5.5, segments=5, angle=30.0)
    cbox(body, 0.0, 0.0, 20.0, 29.0, 13.0, 39.0)
    pocket = Part("Fabric", bevel=3.0, segments=4, angle=30.0)
    cbox(pocket, 0.0, 7.6, 12.5, 23.0, 5.0, 17.0)
    trim = Part("Trim")
    # The zip round the top of the main compartment, and along the pocket's top edge.
    arc = []
    for k in range(25):
        a = math.pi * k / 24
        arc.append((-math.cos(a) * 12.0, 6.7, 30.0 + math.sin(a) * 8.0))
    tube(trim, [(-14.0, 6.6, 6.0)] + [(-13.8, 6.7, 22.0)] + arc + [(13.8, 6.7, 22.0), (14.0, 6.6, 6.0)], 0.45, sides=6)
    tube(trim, [(-10.5, 10.2, 20.6), (10.5, 10.2, 20.6)], 0.45, sides=6)
    straps = Part("Trim", bevel=0.4)
    for sx in (-1, 1):
        path = [(sx * 6.0, -6.6, 37.0), (sx * 7.0, -10.0, 31.0), (sx * 8.4, -11.0, 20.0), (sx * 9.6, -9.6, 8.0), (sx * 10.0, -7.0, 3.5)]
        for a, b in zip(path, path[1:]):
            a, b = Vector(a), Vector(b)
            d = b - a
            cbox(straps, 0.0, 0.0, 0.0, 4.4, 0.9, d.length + 0.6, matrix=Matrix.Translation((a + b) * 0.5) @ to_axis(d))
    tube(straps, [(-4.0, -3.0, 39.0), (-3.0, -3.0, 42.4), (0.0, -3.0, 43.4), (3.0, -3.0, 42.4), (4.0, -3.0, 39.0)], 0.7, sides=8)
    zip_ = Part("Zip", bevel=0.1)
    for x, y, z, yaw in ((-7.0, 7.3, 37.3, 0.0), (7.5, 10.8, 20.6, 0.0)):
        cbox(zip_, x, y, z - 1.4, 1.0, 0.4, 2.8)
    charm = Part("Charm", smooth=True)
    tube(charm, [(7.5, 11.0, 19.2), (7.6, 11.4, 16.0)], 0.15, sides=5)
    ellipsoid(charm, (7.6, 11.6, 14.0), (1.9, 1.9, 1.9))
    return [body, pocket, trim, straps, zip_, charm]


def headphones():
    """Over-ear wireless headphones, about 19cm across, as worn: band up, cups at +-X."""
    plastic = Part("Plastic")
    arc = [(math.cos(math.radians(a)) * 8.6, 0.0, 9.0 + math.sin(math.radians(a)) * 8.6) for a in range(-12, 193, 6)]
    tube(plastic, arc, 0.75, sides=10)
    cushion = Part("Cushion", smooth=True)
    pad = [(math.cos(math.radians(a)) * 7.8, 0.0, 9.0 + math.sin(math.radians(a)) * 7.8) for a in range(55, 126, 5)]
    tube(cushion, pad, 1.0, sides=10)
    for sx in (-1, 1):
        lathe(plastic, [(0.0, 0.0), (3.6, 0.0), (4.2, 0.6), (4.3, 2.0), (3.9, 2.8), (0.0, 3.0)],
              Matrix.Translation((sx * 9.6, 0.0, 0.0)) @ to_axis((sx, 0, 0)), sides=28)
        ellipsoid(cushion, (sx * 8.9, 0.0, 0.0), (1.3, 3.9, 4.3))
        tube(plastic, [(sx * 8.4, 0.0, 7.6), (sx * 9.8, 0.0, 5.0), (sx * 10.4, 0.0, 3.6)], 0.5, sides=8)
    return [plastic, cushion]


def tablet():
    """A tablet in a pink protective case, lying screen up. 25.6 x 18.4 x 1.4."""
    case = Part("Case", bevel=1.1, segments=4, angle=30.0)
    cbox(case, 0.0, 0.0, 0.65, 25.6, 18.4, 1.3)
    screen = Part("Screen", bevel=0.2, segments=1)
    cbox(screen, 0.0, 0.0, 1.32, 23.4, 16.2, 0.1)
    lens = Part("Lens", smooth=True)
    ellipsoid(lens, (10.6, 7.2, 0.0), (0.7, 0.7, 0.25))
    ellipsoid(lens, (0.0, 8.7, 1.37), (0.18, 0.18, 0.05))
    return [case, screen, lens]


def musicbox():
    """A music box, 18 x 12 x 7.5, its lid open on a mirror and a dancer on a spring."""
    box_ = Part("Box", bevel=0.4)
    for sy in (-1, 1):
        cbox(box_, 0.0, sy * 5.6, 4.2, 18.0, 0.8, 7.0)
    for sx in (-1, 1):
        cbox(box_, sx * 8.6, 0.0, 4.2, 0.8, 10.4, 7.0)
    cbox(box_, 0.0, 0.0, 0.4, 18.4, 12.4, 0.8)
    velvet = Part("Velvet")
    cbox(velvet, 0.0, 0.0, 4.5, 16.4, 10.4, 0.6)
    hinge = at(0.0, -6.0, 7.7, roll=104.0)
    lid = Part("Box", bevel=0.4)
    cbox(lid, 0.0, 6.0, 0.6, 18.0, 12.0, 1.2, matrix=hinge)
    mirror = Part("Mirror")
    cbox(mirror, 0.0, 6.0, -0.05, 14.0, 9.0, 0.1, matrix=hinge)
    figure = Part("Figure", smooth=True)
    lathe(figure, [(0.0, 4.8), (0.5, 4.8), (0.4, 6.0)], at(0.0, 0.0, 0.0), sides=8)
    lathe(figure, [(0.0, 6.0), (2.0, 6.6), (1.6, 7.1), (0.5, 7.3), (0.45, 8.6), (0.3, 8.9), (0.0, 9.0)], at(0.0, 0.0, 0.0), sides=20)
    ellipsoid(figure, (0.0, 0.0, 9.4), (0.45, 0.45, 0.5))
    tube(figure, [(0.0, 0.0, 8.5), (0.9, 0.0, 9.4), (0.4, 0.0, 10.3)], 0.12, sides=5)
    tube(figure, [(0.0, 0.0, 8.5), (-0.9, 0.0, 9.4), (-0.4, 0.0, 10.3)], 0.12, sides=5)
    brass = Part("Brass")
    for sx in (-1, 1):
        for sy in (-1, 1):
            ellipsoid(brass, (sx * 8.2, sy * 5.2, -0.1), (0.7, 0.7, 0.4))
    knob(brass, 9.2, 0.0, 3.0, 0.6, out=(1, 0, 0))
    return [box_, velvet, lid, mirror, figure, brass]


def sneaker():
    """One canvas trainer, a left, 23cm long with the toe at +Y."""
    outline = []
    for k in range(48):
        t = k / 48 * math.tau
        # A footprint: narrow heel at -Y, wider across the ball, a rounded toe.
        y = math.sin(t) * 11.5
        width = 3.4 + 1.0 * (y + 11.5) / 23.0 + 0.5 * math.exp(-((y - 4.0) / 4.0) ** 2)
        x = math.cos(t) * width - 0.4 * max(0.0, y - 3.0) / 8.5
        outline.append((x, y))
    sole = Part("Sole", bevel=0.5, segments=2)
    prism(sole, outline, 0.0, 2.4)
    canvas = Part("Canvas", smooth=True)
    blob(canvas, [((0.0, -7.6, 5.4), (3.6, 3.6, 3.8), None),
                  ((0.0, -1.0, 5.2), (4.0, 5.0, 3.6), None),
                  ((-0.3, 6.0, 3.6), (3.8, 4.8, 2.4), None)], resolution=0.45)
    shadow = Part("Shadow", smooth=True)
    ellipsoid(shadow, (0.0, -5.0, 8.6), (2.6, 3.8, 0.5))
    lace = Part("Lace", bevel=0.05)
    for k in range(5):
        y = -1.0 + k * 1.6
        z = 8.4 - k * 0.55
        cbox(lace, 0.0, y, z, 4.4, 0.35, 0.3, matrix=None)
    cbox(lace, 1.2, -1.8, 7.0, 0.4, 0.4, 4.0, matrix=None)
    return [sole, canvas, shadow, lace]


def desklamp():
    """A small dome desk lamp, 38cm, the shade turned down over the desk. Front at +Y."""
    metal = Part("Metal")
    lathe(metal, [(0.0, 0.0), (7.0, 0.0), (7.2, 0.6), (6.4, 2.0), (2.0, 2.6), (1.0, 3.4), (0.0, 3.4)], at(0.0, -2.0, 0.0), sides=32)
    tube(metal, [(0.0, -2.0, 3.0), (0.0, -2.4, 18.0), (0.0, -1.8, 28.0), (0.0, 1.0, 34.0), (0.0, 4.0, 36.0)], 0.6, sides=10)
    lathe(metal, [(0.0, 3.0), (6.4, 0.2), (6.9, 0.0), (6.3, 3.5), (4.6, 6.6), (2.0, 8.6), (0.0, 9.2)],
          at(0.0, 8.6, 28.0, roll=30.0), sides=36)
    bulb = Part("Bulb", smooth=True)
    lathe(bulb, [(0.0, 1.8), (1.0, 2.0), (1.7, 3.2), (1.5, 4.6), (0.0, 5.2)], at(0.0, 8.6, 28.0, roll=30.0), sides=16)
    tube(metal, [(5.5, -2.0, 1.6), (9.0, -6.0, 0.4), (18.0, -14.0, 0.4)], 0.25, sides=6)
    return [metal, bulb]


def pencilcase():
    """A zip pencil case, 21 x 7 x 5."""
    body = Part("Fabric", bevel=2.2, segments=4, angle=30.0)
    cbox(body, 0.0, 0.0, 2.5, 21.0, 7.0, 5.0)
    trim = Part("Trim")
    tube(trim, [(-9.5, 0.0, 5.02), (9.5, 0.0, 5.02)], 0.35, sides=6)
    zip_ = Part("Zip", bevel=0.1)
    cbox(zip_, 7.0, 1.6, 5.0, 0.8, 3.0, 0.3)
    return [body, trim, zip_]


def hairbrush():
    """A paddle hairbrush, 24cm, lying bristles down."""
    plastic = Part("Plastic", bevel=0.6, segments=3)
    prism(plastic, [(math.cos(k / 32 * math.tau) * 4.6, 5.0 + math.sin(k / 32 * math.tau) * 6.2) for k in range(32)], 1.6, 2.8)
    cbox(plastic, 0.0, -6.0, 2.2, 2.4, 13.0, 1.2)
    cushion = Part("Cushion", smooth=True)
    ellipsoid(cushion, (0.0, 5.0, 1.0), (3.9, 5.4, 1.0))
    return [plastic, cushion]


def hairbow():
    """A hair ribbon tied in a bow, about 11cm across, lying flat."""
    ribbon = Part("Ribbon", smooth=True)
    for sx in (-1, 1):
        loop = [(sx * (0.8 + 4.4 * (1 - math.cos(a)) * 0.5), 2.0 * math.sin(a) * (1.0 if sx > 0 else 1.1), 0.9 + 0.4 * math.sin(a * 2))
                for a in [k * math.tau / 20 for k in range(21)]]
        tube(ribbon, loop, 0.32, sides=6)
        tube(ribbon, [(sx * 0.4, -0.6, 0.5), (sx * 1.8, -3.6, 0.3), (sx * 2.6, -6.4, 0.25)], 0.3, sides=6)
    ellipsoid(ribbon, (0.0, 0.0, 0.9), (0.9, 1.1, 0.7))
    return [ribbon]


def pajamas():
    """Folded pyjamas: a top folded in thirds over the bottoms, 30 x 24, about 7cm high."""
    fabric = Part("Fabric", bevel=1.6, segments=4, angle=30.0)
    cbox(fabric, 0.0, 0.0, 1.6, 30.0, 24.0, 3.2)
    cbox(fabric, 0.4, -0.3, 4.6, 29.0, 23.0, 3.0, matrix=at(0.0, 0.0, 0.0, yaw=1.5))
    trim = Part("Fabric", smooth=True)
    tube(trim, [(-14.4, 9.2, 6.0), (14.4, 9.6, 6.0)], 0.4, sides=6)
    collar = Part("Ribbon", smooth=True)
    for k in range(3):
        ellipsoid(collar, (0.0, -6.0 + k * 3.0, 6.25), (0.5, 0.5, 0.25))
    return [fabric, trim, collar]


def pillow():
    """A pillow, 60 x 40, stuffed rather than boxed."""
    fabric = Part("Fabric", smooth=True)
    elements = []
    for sx in (-1, 0, 1):
        elements.append(((sx * 15.0, 0.0, 7.0), (14.0, 17.0, 5.6), None))
    blob(fabric, elements, resolution=1.0)
    return [fabric]


MODELS = [
    ("nursery_bed", bed), ("nursery_nightstand", nightstand), ("nursery_wardrobe", wardrobe),
    ("nursery_wardrobe_door", wardrobe_door), ("nursery_desk", desk), ("nursery_chair", chair),
    ("nursery_bookcase", bookcase), ("nursery_toyshelf", toyshelf), ("nursery_toychest", toychest),
    ("nursery_toychest_lid", toychest_lid), ("nursery_dollhouse", dollhouse),
    ("toy_teddy", teddy), ("toy_rabbit", rabbit), ("toy_ragdoll", ragdoll), ("toy_block", toy_block),
    ("girl_backpack", backpack), ("girl_headphones", headphones), ("girl_tablet", tablet),
    ("girl_musicbox", musicbox), ("girl_sneaker", sneaker), ("girl_desklamp", desklamp),
    ("girl_pencilcase", pencilcase), ("girl_hairbrush", hairbrush), ("girl_hairbow", hairbow),
    ("girl_pajamas", pajamas), ("girl_pillow", pillow),
]


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    only = [a.split("=", 1)[1] for a in sys.argv if a.startswith("--only=")]
    only = set(only[0].split(",")) if only else None
    baked = []
    for name, make in MODELS:
        if only and name not in only:
            continue
        obj = K.bake(make(), name, SLOTS)
        K.export(obj)
        open(os.path.join(K.MODEL_DIR, name, "GENERATED"), "w").close()
        baked.append(obj)
        print("NURSERY baked {} ({} verts)".format(name, len(obj.data.vertices)))

    # The previews: the furniture in a row, and the small things on a table-top grid.
    big = [o for o in baked if o.name.startswith("nursery_")]
    small = [o for o in baked if not o.name.startswith("nursery_")]
    x = 0.0
    for obj in big:
        width = obj.dimensions.x
        obj.location = (x + width * 0.5, 0.0, 0.0)
        x += width + 0.3
    for i, obj in enumerate(small):
        obj.location = (-1.2 - (i % 4) * 0.42, 3.0 + (i // 4) * 0.42, 0.0)
    K.preview(baked, PREVIEW_DIR, "models", [
        ("furniture", (x * 0.5, -11.5, 3.0), (x * 0.5, 0.0, 0.8)),
        ("small", (-1.8, 1.2, 1.4), (-1.8, 4.1, 0.1)),
    ])


main()
