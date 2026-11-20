"""
Builds the house's outside fittings in Blender, headless, and exports each as an FBX the art
pipeline imports.

    blender --background --factory-startup --python Tools/make_exterior.py [-- --only=a,b]

The walls, roofs, dressings and bargeboards are generated in C++ (AMansionExteriorActor), because
every one of them is sized off the rooms behind it. What is here is what has a shape a box cannot
give: the front door, the porch's posts and brackets, the gable finials, the turret's spire, the
chimney pots and the louvred shutters.

The front door is the house's one new thing: a black double door, panelled, with an arched upper
field on each leaf, a fanlight over it, and brass on the outer face (a ring knocker, a letter plate,
two knobs). The inner face has its own knobs, the letter plate's cowl, two barrel bolts on the
standing leaf and the hinges. The same model stands in the stair hall's east wall, seen from inside.

Outputs Art/Source/Models/<name>/<name>.fbx with an empty GENERATED file beside it (re-imported
every run with its own normals), and Workbench previews in Saved/ExteriorPreview/.

Frame: centimetres. Things that stand on the ground (the post) have their origin on the ground
under their middle. The door has its origin at the foot of the opening's middle in the wall's
middle plane, +Y is OUTSIDE, +X is on the right of somebody standing outside looking at it. A
shutter leaf has its origin at the foot of its hinge, the leaf along +X, its outer face +Y. The
finial and the spire have their origin where they stand on the roof. The numbers AMansionExteriorActor
needs are the constants below, and must stay in step with MansionExteriorActor.h.

Material slots are set by name in C++. Texel sizes match the surface each is given:
    Paint      bedroom_paint (RoomSurfaces::Paint, 120cm), tinted to black gloss paint
    Trim       weathered_brown_planks (RoomSurfaces::RoughWood, 150cm), tinted to old dark paint
    Shutter    weathered_brown_planks (150cm), tinted to faded green paint
Everything else is a flat colour or glass, where the UVs do not matter.
"""

import math
import os
import random
import sys

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blender_kit as K  # noqa: E402
from blender_kit import Part, at, box, cbox, ellipsoid, lathe, prism_xz, tube  # noqa: E402

PREVIEW_DIR = os.path.join(K.ROOT, "Saved", "ExteriorPreview")

PAINT = 120.0
TRIM = 150.0
FLAT = 100.0

SLOTS = {
    "Paint": ((0.02, 0.02, 0.022, 1.0), PAINT),
    "Trim": ((0.10, 0.10, 0.09, 1.0), TRIM),
    "Shutter": ((0.12, 0.16, 0.12, 1.0), TRIM),
    "Brass": ((0.70, 0.52, 0.22, 1.0), FLAT),
    "Iron": ((0.05, 0.05, 0.05, 1.0), FLAT),
    "Glass": ((0.20, 0.24, 0.26, 1.0), FLAT),
    "Shadow": ((0.01, 0.01, 0.01, 1.0), FLAT),
    "Pot": ((0.30, 0.12, 0.07, 1.0), FLAT),
}

# Shared with MansionExteriorActor.h: the front door's opening, and the wall it stands in.
WALL_T = 20.0
DOOR_UNIT_W = 176.0     # the opening the frame fills
DOOR_UNIT_H = 262.0
FRAME = 8.0             # the lining's width
LEAF_GAP = 0.25
LEAF_H = 222.0
LEAF_T = 5.5
LEAF_Y = 2.0            # the leaves' middle plane, towards the outside
TRANSOM_Z0 = 2.5 + LEAF_H
FANLIGHT_Z0 = TRANSOM_Z0 + 6.5

# Shutter leaves (each one half of an opening).
SHUTTERS = {
    "exterior_shutter_tall": (72.0, 222.0),
    "exterior_shutter_low": (72.0, 192.0),
    "exterior_shutter_narrow": (46.0, 172.0),
}

# The porch: its posts stand on the deck and carry the beam under the roof.
PORCH_POST_H = 270.0


def semicircle(cx, cz, r, steps, a0=0.0, a1=math.pi):
    return [(cx + r * math.cos(a0 + (a1 - a0) * k / steps), cz + r * math.sin(a0 + (a1 - a0) * k / steps)) for k in range(steps + 1)]


def outline_loop(part, points, y, radius, sides=8):
    """A rounded bead run round a closed outline in XZ at depth y."""
    pts = [(x, y, z) for x, z in points]
    pts.append(pts[0])
    tube(part, pts, radius, sides=sides)


# ---------------------------------------------------------------------------------------------
# The front door.
# ---------------------------------------------------------------------------------------------

def panel_outlines(x0, x1):
    """The two fields of a leaf between its stiles at x0..x1: an arched upper field and a plain lower
    one, as closed outlines in XZ (counter-clockwise seen from -Y)."""
    w = x1 - x0
    upper_z0, upper_z1 = 114.0, TRANSOM_Z0 - 15.0
    r = w * 0.5
    spring = upper_z1 - r
    arc = semicircle((x0 + x1) * 0.5, spring, r, 20)
    upper = [(x0, upper_z0), (x1, upper_z0)] + arc
    lower = [(x0, 31.0), (x1, 31.0), (x1, 92.0), (x0, 92.0)]
    return upper, lower


def inset(outline, d):
    """Shrinks an outline towards its own centre (good enough for a rectangle with a round head)."""
    cx = sum(p[0] for p in outline) / len(outline)
    xs = [p[0] for p in outline]
    zs = [p[1] for p in outline]
    cz = (min(zs) + max(zs)) * 0.5
    hw = (max(xs) - min(xs)) * 0.5
    hh = (max(zs) - min(zs)) * 0.5
    out = []
    for x, z in outline:
        sx = (hw - d) / hw
        sz = (hh - d) / hh
        out.append((cx + (x - cx) * sx, cz + (z - cz) * sz))
    return out


def leaf(parts, x0, x1, outer_hardware, inner_hardware):
    """One leaf from x0 to x1: a core, stiles and rails standing proud of it on both faces, a raised
    field in each panel with a bolection bead round it."""
    paint, fields, bead, brass, shadow = parts
    z0, z1 = 2.5, 2.5 + LEAF_H
    core = 3.4
    box(paint, x0, x1, LEAF_Y - core / 2, LEAF_Y + core / 2, z0, z1)
    stile = 13.0
    upper, lower = panel_outlines(x0 + stile, x1 - stile)
    for side in (1.0, -1.0):
        face = LEAF_Y + side * core / 2
        proud = LEAF_Y + side * LEAF_T / 2
        lo, hi = min(face, proud), max(face, proud)
        # Stiles and rails: the frame of the leaf, the full thickness.
        box(paint, x0, x0 + stile, lo, hi, z0, z1)
        box(paint, x1 - stile, x1, lo, hi, z0, z1)
        box(paint, x0 + stile, x1 - stile, lo, hi, z0, 31.0)
        box(paint, x0 + stile, x1 - stile, lo, hi, 92.0, 114.0)
        box(paint, x0 + stile, x1 - stile, lo, hi, TRANSOM_Z0 - 15.0, z1)
        # The spandrels over the arched field, at the frame's thickness.
        r = (x1 - x0 - 2 * stile) * 0.5
        spring = TRANSOM_Z0 - 15.0 - r
        for sx in (-1.0, 1.0):
            pts = []
            cx = (x0 + x1) * 0.5
            steps = 10
            for k in range(steps + 1):
                a = (math.pi * 0.5) * k / steps
                pts.append((cx + sx * r * math.sin(a), spring + r * math.cos(a)))
            corner = (cx + sx * r, TRANSOM_Z0 - 15.0)
            outline = pts + [corner]
            if sx < 0:
                outline = outline[::-1]
            prism_xz(paint, outline, lo, hi)
        # The fields: raised from the core, bevelled down to it.
        for outline in (upper, lower):
            field = inset(outline, 3.5)
            prism_xz(fields, field, min(face, face + side * 1.0), max(face, face + side * 1.0))
            # The bolection: a bead standing proud of the frame round the field's opening.
            outline_loop(bead, outline, proud + side * 0.6, 1.1, sides=8)
            outline_loop(bead, inset(outline, 1.6), proud + side * 0.1, 0.55, sides=6)
    outer_hardware(x0, x1)
    inner_hardware(x0, x1)


def knob(part, x, z, y_face, side):
    """A turned knob on a rose, standing off the face at y_face towards side."""
    lathe(part, [(0.0, 0.0), (3.4, 0.0), (3.4, 0.5), (2.6, 1.0), (0.9, 1.4), (0.9, 4.2), (2.4, 4.8), (3.0, 6.2), (2.6, 7.4), (0.0, 7.7)],
          Matrix.Translation((x, y_face, z)) @ Matrix.Rotation(math.radians(-90.0 * side), 4, "X"), sides=20)


def front_door():
    """The black double door in its frame, with the fanlight over it. See the module docstring."""
    paint = Part("Paint", bevel=0.25)
    fields = Part("Paint", bevel=0.9, segments=2, angle=30.0)
    bead = Part("Paint")
    brass = Part("Brass")
    shadow = Part("Shadow")
    glass = Part("Glass")
    iron = Part("Iron")
    half = DOOR_UNIT_W * 0.5
    inner = half - FRAME
    t = WALL_T * 0.5

    # The frame: jambs and head lining the opening through the wall, the transom between the leaves
    # and the fanlight, and stops the leaves shut against.
    for sx in (-1.0, 1.0):
        box(paint, min(sx * inner, sx * half), max(sx * inner, sx * half), -t, t, 0.0, DOOR_UNIT_H)
        box(paint, min(sx * (inner - 1.6), sx * inner), max(sx * (inner - 1.6), sx * inner), LEAF_Y - LEAF_T / 2 - 3.0, LEAF_Y - LEAF_T / 2, 0.0, TRANSOM_Z0)
    box(paint, -inner, inner, -t, t, DOOR_UNIT_H - FRAME, DOOR_UNIT_H)
    box(paint, -inner, inner, LEAF_Y - 4.0, LEAF_Y + 5.5, TRANSOM_Z0, FANLIGHT_Z0)
    # A drip moulding on the transom's outer face.
    box(paint, -inner, inner, LEAF_Y + 5.5, LEAF_Y + 8.0, TRANSOM_Z0 + 2.0, FANLIGHT_Z0 - 0.5)
    # The threshold: a brass weather bar on an oak sill.
    box(paint, -inner, inner, -t, t + 2.0, 0.0, 2.5)
    box(brass, -inner, inner, LEAF_Y + 3.4, LEAF_Y + 4.6, 2.5, 3.6)

    # The fanlight: three lights in a frame, the middle one with a fan of glazing bars.
    glass_z1 = DOOR_UNIT_H - FRAME
    box(glass, -inner, inner, LEAF_Y - 0.3, LEAF_Y + 0.3, FANLIGHT_Z0, glass_z1)
    for sx in (-1.0, 1.0):
        cbox(paint, sx * inner / 3.0, LEAF_Y, (FANLIGHT_Z0 + glass_z1) * 0.5, 2.4, 4.4, glass_z1 - FANLIGHT_Z0)
    for k in range(5):
        a = math.pi * (0.15 + 0.7 * k / 4)
        r = (glass_z1 - FANLIGHT_Z0) * 0.95
        tube(paint, [(0.0, LEAF_Y + 1.0, FANLIGHT_Z0 + 0.5), (r * math.cos(a) * 1.15, LEAF_Y + 1.0, FANLIGHT_Z0 + r * math.sin(a))], 0.7, sides=6)
    lathe(paint, [(0.0, -1.2), (6.0, -1.2), (6.0, 1.2), (0.0, 1.2)], at(0.0, LEAF_Y + 1.2, FANLIGHT_Z0 + 0.5, roll=90.0), sides=16)
    # Behind the glass, the dark of the hall the door shuts on: a board, so nothing behind is seen.
    box(shadow, -inner, inner, -t + 0.5, -t + 1.0, FANLIGHT_Z0, glass_z1)

    knob_z = 104.0

    def right_outer(x0, x1):
        face = LEAF_Y + LEAF_T / 2
        mid = (x0 + x1) * 0.5
        # The ring knocker on the upper field: a backplate, a boss, the ring hanging from it, a stud.
        kz = 162.0
        lathe(brass, [(0.0, 0.0), (6.0, 0.0), (6.0, 0.6), (4.8, 1.3), (2.0, 1.6), (0.0, 1.6)], at(mid, face + 1.0, kz, roll=-90.0), sides=24)
        lathe(brass, [(0.0, 0.0), (1.6, 0.0), (1.6, 2.6), (0.0, 3.0)], at(mid, face + 2.4, kz, roll=-90.0), sides=12)
        ring = []
        for k in range(29):
            a = 2.0 * math.pi * k / 28
            ring.append((mid + 7.2 * math.sin(a), face + 4.0, kz - 7.6 + 7.6 * math.cos(a)))
        tube(brass, ring, 0.85, sides=10)
        lathe(brass, [(0.0, 0.0), (2.2, 0.0), (2.0, 1.4), (0.0, 2.0)], at(mid, face + 1.4, kz - 16.0, roll=-90.0), sides=14)
        # The letter plate on the lock rail, its flap shut.
        cbox(brass, mid, face + 1.4, knob_z - 1.0, 28.0, 1.0, 8.5)
        cbox(brass, mid, face + 2.1, knob_z - 1.0, 23.0, 0.6, 4.2)
        tube(brass, [(mid - 4.0, face + 2.6, knob_z + 1.6), (mid - 4.0, face + 4.0, knob_z + 1.8), (mid + 4.0, face + 4.0, knob_z + 1.8), (mid + 4.0, face + 2.6, knob_z + 1.6)], 0.45, sides=8)
        # The knob near the meeting stile, and the keyhole's escutcheon under it.
        knob(brass, x0 + 6.5, knob_z + 30.0, face + 0.8, 1.0)
        lathe(brass, [(0.0, 0.0), (2.2, 0.0), (2.2, 0.5), (0.0, 0.7)], at(x0 + 6.5, face + 0.8, knob_z + 14.0, roll=-90.0), sides=16)
        cbox(shadow, x0 + 6.5, face + 1.35, knob_z + 14.0, 0.5, 0.2, 1.8)
        # The astragal over the meeting stiles, on the active leaf.
        cbox(paint, x0 - 0.6, face + 0.7, 2.5 + LEAF_H * 0.5, 3.2, 1.4, LEAF_H)

    def right_inner(x0, x1):
        face = LEAF_Y - LEAF_T / 2
        mid = (x0 + x1) * 0.5
        knob(brass, x0 + 6.5, knob_z + 30.0, face - 0.8, -1.0)
        # The letter plate's cowl.
        cbox(paint, mid, face - 3.0, knob_z - 1.0, 30.0, 6.0, 11.0)
        cbox(paint, mid, face - 3.0, knob_z - 6.0, 30.0, 6.0, 1.2)
        cbox(paint, x0 - 0.6, face - 0.7, 2.5 + LEAF_H * 0.5, 3.2, 1.4, LEAF_H)

    def left_outer(x0, x1):
        face = LEAF_Y + LEAF_T / 2
        knob(brass, x1 - 6.5, knob_z + 30.0, face + 0.8, 1.0)

    def left_inner(x0, x1):
        face = LEAF_Y - LEAF_T / 2
        # The standing leaf is held by barrel bolts into the head and the sill.
        for z, d in ((2.5 + LEAF_H - 14.0, 1.0), (16.0, -1.0)):
            cbox(iron, x1 - 6.0, face - 0.4, z, 3.6, 0.8, 18.0)
            tube(iron, [(x1 - 6.0, face - 1.8, z - 9.0 * d), (x1 - 6.0, face - 1.8, z + 12.0 * d)], 0.8, sides=10)
            tube(iron, [(x1 - 6.0, face - 1.8, z + 2.0 * d), (x1 - 9.5, face - 1.8, z + 2.0 * d)], 0.5, sides=8)

    lx0, lx1 = -inner + LEAF_GAP, -LEAF_GAP
    rx0, rx1 = LEAF_GAP, inner - LEAF_GAP
    parts = (paint, fields, bead, brass, shadow)
    leaf(parts, lx0, lx1, lambda a, b: left_outer(a, b), lambda a, b: left_inner(a, b))
    leaf(parts, rx0, rx1, lambda a, b: right_outer(a, b), lambda a, b: right_inner(a, b))

    # Hinges, three a leaf, knuckles on the inner face at the outer stiles.
    for sx in (-1.0, 1.0):
        for z in (22.0, 2.5 + LEAF_H * 0.5, LEAF_H - 18.0):
            lathe(brass, [(0.0, -6.5), (1.2, -6.5), (1.2, 6.5), (0.0, 6.5)], at(sx * (inner - 0.6), LEAF_Y - LEAF_T / 2 - 0.9, z), sides=12)
    return [paint, fields, bead, brass, shadow, glass, iron]


# ---------------------------------------------------------------------------------------------
# The roof's ornaments.
# ---------------------------------------------------------------------------------------------

def finial():
    """A gable finial: a square post where the two bargeboards meet, a turned spike over it and an
    acorn drop under it. Origin at the apex, on the bargeboards' face plane."""
    trim = Part("Trim", bevel=0.4)
    turned = Part("Trim")
    box(trim, -5.0, 5.0, -5.0, 5.0, -62.0, 12.0)
    # Collars on the post, top and bottom.
    for z in (-58.0, 4.0):
        box(trim, -6.5, 6.5, -6.5, 6.5, z, z + 4.0)
    lathe(turned, [(0.0, 12.0), (4.0, 12.0), (4.0, 15.0), (6.4, 20.0), (6.8, 26.0), (4.2, 32.0), (2.6, 36.0), (3.6, 40.0), (2.0, 44.0),
                   (1.6, 62.0), (2.8, 66.0), (1.2, 72.0), (0.6, 96.0), (0.0, 100.0)], at(0.0, 0.0, 0.0), sides=16)
    lathe(turned, [(0.0, -92.0), (0.8, -90.0), (3.6, -84.0), (4.6, -76.0), (3.8, -70.0), (4.6, -66.0), (4.6, -62.0), (0.0, -62.0)], at(0.0, 0.0, 0.0), sides=16)
    return [trim, turned]


def spire():
    """The turret's iron spire: a collar on the cone's tip, two balls on a rod, a weathervane's arrow
    and its compass arms. Origin where it stands on the cone."""
    iron = Part("Iron")
    lathe(iron, [(0.0, 0.0), (9.0, 0.0), (7.0, 6.0), (3.0, 10.0), (2.2, 12.0), (0.0, 12.0)], at(0.0, 0.0, 0.0), sides=20)
    tube(iron, [(0.0, 0.0, 10.0), (0.0, 0.0, 150.0)], 1.2, sides=10)
    ellipsoid(iron, (0.0, 0.0, 30.0), (7.0, 7.0, 7.0), rings=10, sides=18)
    ellipsoid(iron, (0.0, 0.0, 70.0), (4.0, 4.0, 4.0), rings=8, sides=14)
    for a in range(4):
        ang = math.pi * 0.5 * a
        tube(iron, [(0.0, 0.0, 88.0), (16.0 * math.cos(ang), 16.0 * math.sin(ang), 88.0)], 0.5, sides=6)
        ellipsoid(iron, (16.0 * math.cos(ang), 16.0 * math.sin(ang), 88.0), (1.4, 1.4, 1.4), rings=5, sides=8)
    # The vane, pointing towards the moon, as a vane does in no wind at all. A flat arrow in XZ.
    pts = [(-30.0, 118.0), (12.0, 118.0), (12.0, 115.0), (26.0, 120.5), (12.0, 126.0), (12.0, 123.0), (-30.0, 123.0)]
    prism_xz(iron, pts, -0.4, 0.4)
    pts = [(-36.0, 113.0), (-24.0, 113.0), (-28.0, 120.5), (-24.0, 128.0), (-36.0, 128.0), (-32.0, 120.5)]
    prism_xz(iron, pts, -0.4, 0.4)
    return [iron]


def chimney_pot():
    """A tall clay pot: a square-ish foot, a waist, a crown, and its inside, so the top is a hole."""
    pot = Part("Pot")
    soot = Part("Shadow")
    lathe(pot, [(0.0, 0.0), (15.0, 0.0), (15.0, 9.0), (12.0, 14.0), (10.5, 40.0), (12.0, 58.0), (14.5, 63.0), (14.5, 68.0),
                (13.0, 70.0), (10.0, 70.0), (9.0, 66.0), (8.5, 40.0), (0.0, 40.0)], at(0.0, 0.0, 0.0), sides=20)
    lathe(soot, [(0.0, 40.5), (8.3, 40.5), (8.3, 42.0), (0.0, 42.0)], at(0.0, 0.0, 0.0), sides=16)
    return [pot, soot]


def porch_post():
    """A chamfered square post with stop chamfers, on a plinth, under a moulded capital."""
    trim = Part("Trim", bevel=0.5)
    h = PORCH_POST_H
    box(trim, -10.0, 10.0, -10.0, 10.0, 0.0, 22.0)
    box(trim, -8.5, 8.5, -8.5, 8.5, 22.0, 30.0)
    # The shaft: an octagon between square blocks at each end.
    c = 2.6
    s = 7.0
    octa = [(s - c, -s), (s, -s + c), (s, s - c), (s - c, s), (-s + c, s), (-s, s - c), (-s, -s + c), (-s + c, -s)]
    bottom = [trim.bm.verts.new((x, y, 44.0)) for x, y in octa]
    top = [trim.bm.verts.new((x, y, h - 40.0)) for x, y in octa]
    trim.bm.faces.new(bottom[::-1])
    trim.bm.faces.new(top)
    for i in range(8):
        j = (i + 1) % 8
        trim.bm.faces.new((bottom[i], bottom[j], top[j], top[i]))
    box(trim, -s, s, -s, s, 30.0, 44.0)
    box(trim, -s, s, -s, s, h - 40.0, h - 22.0)
    # The capital.
    box(trim, -9.0, 9.0, -9.0, 9.0, h - 22.0, h - 16.0)
    box(trim, -10.5, 10.5, -10.5, 10.5, h - 16.0, h - 6.0)
    box(trim, -12.0, 12.0, -12.0, 12.0, h - 6.0, h)
    return [trim]


def porch_bracket():
    """A curved bracket between a post and the beam, in XZ: origin at the corner where the post's
    face meets the beam's underside, the bracket running +X along the beam and -Z down the post, a
    quatrefoil cut through it."""
    trim = Part("Trim", bevel=0.3)
    reach = 46.0
    steps = 16
    # Along the beam, then the inner edge (a quarter circle about the far corner, so it is concave),
    # then up the post.
    pts = [(0.0, 0.0), (reach, 0.0), (reach, -3.0)]
    for k in range(1, steps + 1):
        a = (math.pi * 0.5) * k / steps
        pts.append((reach - (reach - 3.0) * math.sin(a), -reach + (reach - 3.0) * math.cos(a)))
    pts.append((0.0, -reach))
    # pts runs along the beam, down the curve, up the post: clockwise seen from -Y; prism_xz wants ccw.
    prism_xz(trim, pts[::-1], -2.5, 2.5)
    # The quatrefoil, cut through.
    cutter = __import__("bmesh").new()
    trim.cutter = cutter
    cx, cz = 8.5, -8.5
    for k in range(4):
        a = math.pi * 0.5 * k + math.pi * 0.25
        ring = []
        for j in range(16):
            b = 2.0 * math.pi * j / 16
            ring.append((cx + 2.0 * math.cos(a) + 1.8 * math.cos(b), cz + 2.0 * math.sin(a) + 1.8 * math.sin(b)))
        near = [cutter.verts.new((x, -4.0, z)) for x, z in ring]
        far = [cutter.verts.new((x, 4.0, z)) for x, z in ring]
        cutter.faces.new(near)
        cutter.faces.new(far[::-1])
        for i in range(16):
            j = (i + 1) % 16
            cutter.faces.new((near[j], near[i], far[i], far[j]))
    return [trim]


def shutter(width, height, seed):
    """A louvred shutter leaf: stiles and three rails, angled slats between them, a few of them gone,
    and two iron straps on its outer face. Hinge at the origin, the leaf along +X, outer face +Y."""
    rng = random.Random(seed)
    wood = Part("Shutter", bevel=0.2)
    slats = Part("Shutter")
    iron = Part("Iron")
    shadow = Part("Shadow")
    t = 3.6
    stile = 6.5
    box(wood, 0.0, stile, -t / 2, t / 2, 0.0, height)
    box(wood, width - stile, width, -t / 2, t / 2, 0.0, height)
    mid = height * 0.48
    for z0, z1 in ((0.0, 9.0), (mid - 4.0, mid + 4.0), (height - 8.0, height)):
        box(wood, stile, width - stile, -t / 2, t / 2, z0, z1)
    # Behind the slats, the dark of the closed window.
    box(shadow, stile, width - stile, -t / 2 - 0.6, -t / 2 - 0.3, 0.0, height)
    gone = set(rng.sample(range(10, 30), 2))
    k = 0
    for z0, z1 in ((9.0, mid - 4.0), (mid + 4.0, height - 8.0)):
        z = z0 + 2.5
        while z < z1 - 2.0:
            k += 1
            if k not in gone:
                m = at((width) * 0.5, 0.0, z, roll=-38.0)
                cbox(slats, 0.0, 0.0, 0.0, width - 2 * stile + 1.2, 0.8, 4.6, matrix=m)
            z += 3.8
    for z in (22.0, height - 26.0):
        box(iron, 0.0, width * 0.62, t / 2, t / 2 + 0.4, z - 1.8, z + 1.8)
        ellipsoid(iron, (width * 0.62, t / 2 + 0.3, z), (1.9, 0.5, 1.9), rings=4, sides=10)
        lathe(iron, [(0.0, -5.0), (1.1, -5.0), (1.1, 5.0), (0.0, 5.0)], at(-0.8, 0.0, z), sides=10)
    return [wood, slats, iron, shadow]


MODELS = [
    ("exterior_front_door", front_door),
    ("exterior_finial", finial),
    ("exterior_spire", spire),
    ("exterior_chimney_pot", chimney_pot),
    ("exterior_porch_post", porch_post),
    ("exterior_porch_bracket", porch_bracket),
] + [(name, (lambda w=w, h=h, s=i: shutter(w, h, 31 + s))) for i, (name, (w, h)) in enumerate(SHUTTERS.items())]


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    only = [a.split("=", 1)[1] for a in args if a.startswith("--only=")]
    only = set(only[0].split(",")) if only else None
    baked = []
    for name, make in MODELS:
        if only and name not in only:
            continue
        obj = K.bake(make(), name, SLOTS)
        K.export(obj)
        folder = os.path.join(K.MODEL_DIR, name)
        open(os.path.join(folder, "GENERATED"), "w").close()
        baked.append(obj)
        dims = obj.dimensions
        print("EXTERIOR baked {} ({} verts, {:.1f} x {:.1f} x {:.1f} cm)".format(name, len(obj.data.vertices), dims.x * 100, dims.y * 100, dims.z * 100))

    x = 0.0
    for obj in baked:
        width = obj.dimensions.x
        obj.location = (x + width * 0.5, 0.0, 0.0)
        x += width + 0.4
    door = bpy.data.objects.get("exterior_front_door")
    views = [("all", (x * 0.5, -9.0, 2.0), (x * 0.5, 0.0, 1.0))]
    if door:
        d = door.location
        views += [
            ("door_out", (d.x + 0.5, -3.2, 1.4), (d.x, 0.0, 1.25)),
            ("door_in", (d.x - 0.5, 3.2, 1.4), (d.x, 0.0, 1.25)),
            ("door_close", (d.x + 0.6, -1.0, 1.6), (d.x + 0.4, 0.0, 1.45)),
        ]
    K.preview(baked, PREVIEW_DIR, "models", views)


if __name__ == "__main__":
    main()
