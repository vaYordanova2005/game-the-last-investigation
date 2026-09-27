"""
Builds the living room's grand piano and its bench in Blender, headless, and exports them as FBX.

    blender --background --factory-startup --python Tools/make_piano.py

Nothing free on Poly Haven is a piano, and the version assembled from boxes and extrusions in C++
read as exactly that: every edge was a knife edge, every leg a pipe, and the keyboard a slab with
stripes on it. Blender (free, open source) can do what a room of primitives cannot — bevelled
edges that catch the lantern, turned legs, a lyre that curves, keys with the shape of keys — and
it can be driven from a script, so the piano stays in the repo as code like everything else.

Outputs:
    Art/Source/Models/grand_piano/grand_piano.fbx
    Art/Source/Models/piano_bench/piano_bench.fbx
    Saved/PianoPreview/*.png     a quick solid render, to judge the shape before any import

Every number is in centimetres in the game's own frame for the piano (see ALivingRoomActor):
origin on the floor under the front edge of the case, +X from the keyboard to the tail, +Y to the
right of the player sitting at it, so the straight bass side is on -Y. Unreal's FBX import negates
Y, so the final transform mirrors Y (and flips the faces back) along with the metre scale.

Material slots, overridden in C++: Lacquer, Ivory, IvoryWorn, Ebony, Brass, Felt, Cushion, Shadow.
"""

import math
import os

import bmesh
import bpy
from mathutils import Matrix, Vector

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODEL_DIR = os.path.join(ROOT, "Art", "Source", "Models")
PREVIEW_DIR = os.path.join(ROOT, "Saved", "PianoPreview")

# Slot name -> (preview colour, texel size in cm for the box-projected UVs). The lacquer's texel
# matches RoomSurfaces::PianoWood and the cushion's RoomSurfaces::Carpet: the instances that go on
# these slots have their tiling at one.
SLOTS = {
    "Lacquer": ((0.035, 0.022, 0.015, 1.0), 120.0),
    "Ivory": ((0.62, 0.56, 0.42, 1.0), 100.0),
    "IvoryWorn": ((0.22, 0.16, 0.10, 1.0), 100.0),
    "Ebony": ((0.012, 0.012, 0.012, 1.0), 100.0),
    "Brass": ((0.55, 0.40, 0.16, 1.0), 100.0),
    "Felt": ((0.30, 0.04, 0.04, 1.0), 100.0),
    "Cushion": ((0.10, 0.20, 0.11, 1.0), 90.0),
    "Shadow": ((0.0, 0.0, 0.0, 1.0), 100.0),
}

# Shared with ALivingRoomActor: the case top, and the music desk's place and lean.
CASE_BOTTOM = 60.0
CASE_TOP = 92.0
LID_TOP = CASE_TOP + 2.5
DESK_X = 16.0
DESK_LEAN = 14.0

KEYS = 52
KEY_PITCH = 2.35
KEY_HALF = KEYS * KEY_PITCH * 0.5
CASE_HALF = 74.0
WHITE_TOP = 73.3


# ---------------------------------------------------------------------------------------------
# Parts: each is a bmesh in the game frame with a slot and an optional bevel.
# ---------------------------------------------------------------------------------------------

class Part:
    def __init__(self, slot, bevel=0.0, segments=2, angle=35.0):
        self.bm = bmesh.new()
        self.slot = slot
        self.bevel = bevel
        self.segments = segments
        self.angle = angle


def area2(points):
    return sum(points[i][0] * points[(i + 1) % len(points)][1] - points[(i + 1) % len(points)][0] * points[i][1]
               for i in range(len(points))) * 0.5


def offset(points, distance, closed=True):
    """Moves a ccw outline outward by distance (negative is inward), mitred at the corners."""
    n = len(points)
    out = []
    for i in range(n):
        normals = []
        for a, b in ((i - 1, i), (i, i + 1)):
            if not closed and (a < 0 or b >= n):
                continue
            p, q = Vector(points[a % n]), Vector(points[b % n])
            e = (q - p).normalized()
            normals.append(Vector((e.y, -e.x)))
        m = sum(normals, Vector((0.0, 0.0))).normalized()
        cos = max(0.35, m.dot(normals[0]))
        out.append(tuple(Vector(points[i]) + m * (distance / cos)))
    return out


def clip(points, keep):
    """Sutherland-Hodgman against one half-plane: keep(p) is the signed distance, >= 0 is kept."""
    out = []
    for i in range(len(points)):
        a, b = Vector(points[i]), Vector(points[(i + 1) % len(points)])
        da, db = keep(a), keep(b)
        if da >= 0.0:
            out.append(tuple(a))
        if (da >= 0.0) != (db >= 0.0):
            t = da / (da - db)
            out.append(tuple(a + (b - a) * t))
    return out


def catmull(points, steps):
    out = []
    for i in range(len(points) - 1):
        p0 = Vector(points[max(i - 1, 0)])
        p1 = Vector(points[i])
        p2 = Vector(points[i + 1])
        p3 = Vector(points[min(i + 2, len(points) - 1)])
        for k in range(steps):
            t = k / steps
            t2, t3 = t * t, t * t * t
            p = 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3)
            out.append(tuple(p))
    out.append(tuple(points[-1]))
    return out


def outline():
    """The case in plan, ccw: the straight bass side, round the tail, the bentside's S to the treble."""
    curve = [(118.0, -CASE_HALF), (142.0, -73.6), (166.0, -70.0), (185.0, -60.0), (197.0, -43.0),
             (200.0, -24.0), (194.0, -8.0), (179.0, 2.0), (156.0, 9.0), (128.0, 19.0), (100.0, 35.0),
             (72.0, 54.0), (46.0, 67.0), (22.0, 73.2), (0.0, CASE_HALF)]
    return [(0.0, -CASE_HALF)] + catmull(curve, 10)


def box(part, x0, x1, y0, y1, z0, z1, matrix=None):
    verts = [part.bm.verts.new((x, y, z)) for z in (z0, z1) for y in (y0, y1) for x in (x0, x1)]
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    for f in faces:
        part.bm.faces.new([verts[i] for i in f])
    if matrix is not None:
        bmesh.ops.transform(part.bm, matrix=matrix, verts=verts)
    return verts


def prism(part, points, z0, z1):
    bottom = [part.bm.verts.new((x, y, z0)) for x, y in points]
    top = [part.bm.verts.new((x, y, z1)) for x, y in points]
    part.bm.faces.new(bottom[::-1])
    part.bm.faces.new(top)
    for i in range(len(points)):
        j = (i + 1) % len(points)
        part.bm.faces.new((bottom[i], bottom[j], top[j], top[i]))
    return bottom + top


def prism_xz(part, points, y0, y1):
    """A profile drawn in XZ, extruded across Y."""
    near = [part.bm.verts.new((x, y0, z)) for x, z in points]
    far = [part.bm.verts.new((x, y1, z)) for x, z in points]
    part.bm.faces.new(near)
    part.bm.faces.new(far[::-1])
    for i in range(len(points)):
        j = (i + 1) % len(points)
        part.bm.faces.new((near[j], near[i], far[i], far[j]))
    return near + far


def lathe(part, profile, matrix, sides=32):
    """A turned part: profile is (radius, z) from the bottom up; zero radius closes a pole."""
    rings = []
    for r, z in profile:
        if r < 1e-4:
            rings.append([part.bm.verts.new((0.0, 0.0, z))])
        else:
            rings.append([part.bm.verts.new((r * math.cos(2 * math.pi * k / sides), r * math.sin(2 * math.pi * k / sides), z))
                          for k in range(sides)])
    for a, b in zip(rings, rings[1:]):
        if len(a) == 1 and len(b) == 1:
            continue
        if len(a) == 1:
            for k in range(sides):
                part.bm.faces.new((a[0], b[k], b[(k + 1) % sides]))
        elif len(b) == 1:
            for k in range(sides):
                part.bm.faces.new((a[k], a[(k + 1) % sides], b[0]))
        else:
            for k in range(sides):
                part.bm.faces.new((a[k], a[(k + 1) % sides], b[(k + 1) % sides], b[k]))
    if len(rings[0]) > 1:
        part.bm.faces.new(rings[0][::-1])
    if len(rings[-1]) > 1:
        part.bm.faces.new(rings[-1])
    verts = [v for ring in rings for v in ring]
    bmesh.ops.transform(part.bm, matrix=matrix, verts=verts)
    return verts


def tube(part, path, radius, sides=14):
    """A round rod swept along a polyline, with parallel-transported rings and flat ends."""
    path = [Vector(p) for p in path]
    radii = radius if isinstance(radius, (list, tuple)) else [radius] * len(path)
    tangents = []
    for i in range(len(path)):
        a = path[max(i - 1, 0)]
        b = path[min(i + 1, len(path) - 1)]
        tangents.append((b - a).normalized())
    up = Vector((0, 0, 1)) if abs(tangents[0].z) < 0.9 else Vector((1, 0, 0))
    normal = tangents[0].cross(up).normalized()
    rings = []
    for i, p in enumerate(path):
        if i > 0:
            normal = (normal - tangents[i] * normal.dot(tangents[i])).normalized()
        binormal = tangents[i].cross(normal)
        rings.append([part.bm.verts.new(p + (normal * math.cos(2 * math.pi * k / sides) + binormal * math.sin(2 * math.pi * k / sides)) * radii[i])
                      for k in range(sides)])
    for a, b in zip(rings, rings[1:]):
        for k in range(sides):
            part.bm.faces.new((a[k], a[(k + 1) % sides], b[(k + 1) % sides], b[k]))
    part.bm.faces.new(rings[0][::-1])
    part.bm.faces.new(rings[-1])


def at(x, y, z, yaw=0.0, pitch=0.0, roll=0.0):
    """Translation and rotation in degrees: roll about X, then pitch about Y, then yaw about Z."""
    return (Matrix.Translation((x, y, z)) @ Matrix.Rotation(math.radians(yaw), 4, "Z")
            @ Matrix.Rotation(math.radians(pitch), 4, "Y") @ Matrix.Rotation(math.radians(roll), 4, "X"))


# ---------------------------------------------------------------------------------------------
# The piano.
# ---------------------------------------------------------------------------------------------

def build_case(parts):
    plan = outline()
    chain = plan  # open at the front: the keyboard and the fallboard fill it between the cheeks
    inner = offset(chain, -3.0, closed=False)

    rim = Part("Lacquer", bevel=0.9, segments=3)
    n = len(chain)
    ob = [rim.bm.verts.new((x, y, CASE_BOTTOM)) for x, y in chain]
    ot = [rim.bm.verts.new((x, y, CASE_TOP)) for x, y in chain]
    ib = [rim.bm.verts.new((x, y, CASE_BOTTOM)) for x, y in inner]
    it = [rim.bm.verts.new((x, y, CASE_TOP)) for x, y in inner]
    for i in range(n - 1):
        j = i + 1
        rim.bm.faces.new((ob[i], ob[j], ot[j], ot[i]))
        rim.bm.faces.new((ib[j], ib[i], it[i], it[j]))
        rim.bm.faces.new((ot[i], ot[j], it[j], it[i]))
        rim.bm.faces.new((ob[j], ob[i], ib[i], ib[j]))
    rim.bm.faces.new((ob[0], ot[0], it[0], ib[0]))
    rim.bm.faces.new((ob[-1], ib[-1], it[-1], ot[-1]))
    parts.append(rim)

    # The bottom board, and a moulding round the foot of the rim that stands a centimetre proud.
    bottom = Part("Lacquer", bevel=0.4)
    prism(bottom, plan, CASE_BOTTOM, CASE_BOTTOM + 1.5)
    parts.append(bottom)
    moulding = Part("Lacquer", bevel=0.8, segments=3)
    prism(moulding, offset(plan, 1.0), CASE_BOTTOM - 0.5, CASE_BOTTOM + 3.5)
    parts.append(moulding)

    # Inside the front of the case, seen with the flap folded back: the action cover, low down,
    # and the rail the music desk stands on, level with the rim.
    inside = clip(clip(offset(plan, -3.0), lambda p: p.x - 3.0), lambda p: 30.0 - p.x)
    cover = Part("Lacquer", bevel=0.3)
    prism(cover, inside, 83.5, 85.0)
    parts.append(cover)
    rail = Part("Lacquer", bevel=0.5, segments=2)
    box(rail, DESK_X - 4.5, DESK_X + 4.5, -CASE_HALF + 3.0, CASE_HALF - 3.0, CASE_TOP - 3.0, CASE_TOP + 0.4)
    parts.append(rail)

    # The lid over everything behind the flap, standing half a centimetre proud of the rim, and the
    # front flap folded back over it on its hinges.
    hinge = 30.0
    lid_plan = offset(plan, 0.5)
    lid = Part("Lacquer", bevel=0.8, segments=3)
    prism(lid, clip(lid_plan, lambda p: p.x - hinge), CASE_TOP, LID_TOP)
    parts.append(lid)
    flap_plan = clip(clip(lid_plan, lambda p: hinge - p.x), lambda p: p.x + 0.5)
    folded = [(2.0 * hinge - x, y) for x, y in flap_plan][::-1]
    flap = Part("Lacquer", bevel=0.8, segments=3)
    prism(flap, folded, LID_TOP + 0.1, LID_TOP + 2.2)
    parts.append(flap)
    brass = Part("Brass", bevel=0.15)
    for y in (-52.0, 0.0, 52.0):
        lathe(brass, [(0.0, -4.0), (0.75, -4.0), (0.75, 4.0), (0.0, 4.0)], at(hinge, y, LID_TOP + 0.6, roll=90.0), sides=12)
        box(brass, hinge - 3.2, hinge, y - 3.8, y + 3.8, LID_TOP + 0.0, LID_TOP + 0.25)
        box(brass, hinge, hinge + 3.2, y - 3.8, y + 3.8, LID_TOP + 2.2, LID_TOP + 2.45)
    parts.append(brass)


def build_keyboard(parts):
    # Cheek blocks either end of the keys: a scroll at the front, flush with the case sides.
    cheek = Part("Lacquer", bevel=0.6, segments=3)
    profile = [(-21.0, 62.0), (4.0, 62.0), (4.0, 84.0), (-10.0, 84.0)]
    for k in range(1, 9):
        a = math.radians(90.0 + 90.0 * k / 8)
        profile.append((-12.0 + 9.0 * math.cos(a), 75.0 + 9.0 * math.sin(a)))
    profile.append((-21.0, 70.0))
    for side in (-1.0, 1.0):
        y0, y1 = (KEY_HALF, CASE_HALF) if side > 0 else (-CASE_HALF, -KEY_HALF)
        prism_xz(cheek, profile, y0, y1)
    parts.append(cheek)

    # The key bed, and the key slip along its front.
    bed = Part("Lacquer", bevel=0.4)
    box(bed, -20.0, 1.0, -KEY_HALF, KEY_HALF, 62.0, 71.0)
    box(bed, -21.0, -19.4, -KEY_HALF, KEY_HALF, 63.0, 72.4)
    parts.append(bed)
    gap = Part("Shadow")
    box(gap, -18.0, 0.0, -KEY_HALF, KEY_HALF, 70.9, 71.1)
    parts.append(gap)

    # The keys. Counting naturals from the bottom A, which have a sharp above: A, C, D, F and G.
    # Two naturals are gone, three have gone down and stayed there, four have lost their ivory.
    has_sharp = [True, False, True, True, False, True, True]
    ivory = Part("Ivory", bevel=0.12, segments=2)
    worn = Part("IvoryWorn", bevel=0.12, segments=2)
    ebony = Part("Ebony", bevel=0.18, segments=2)
    for i in range(KEYS):
        y = -KEY_HALF + KEY_PITCH * (i + 0.5)
        if i in (17, 18):
            continue
        down = 0.8 if i in (9, 33, 34) else 0.0
        target = worn if i in (5, 22, 40, 47) else ivory
        box(target, -17.5, -0.5, y - KEY_PITCH * 0.5 + 0.07, y + KEY_PITCH * 0.5 - 0.07, 71.2 - down, WHITE_TOP - down)
    for i in range(KEYS - 1):
        if not has_sharp[i % 7] or i == 25:
            continue
        y = -KEY_HALF + KEY_PITCH * (i + 1)
        # Narrower at the top than the bottom, and sloped at the front.
        section = [(-9.6, WHITE_TOP - 0.4), (-0.5, WHITE_TOP - 0.4), (-0.5, WHITE_TOP + 1.25), (-8.6, WHITE_TOP + 1.25)]
        verts = prism_xz(ebony, section, -0.62, 0.62)
        for v in verts:
            if v.co.z > WHITE_TOP:
                v.co.y *= 0.72
            v.co.y += y
    parts.extend([ivory, worn, ebony])

    # The strip of red felt behind the keys, and the fallboard folded up over the back of them,
    # with the maker's plate in brass.
    felt = Part("Felt", bevel=0.1)
    box(felt, -0.6, 0.6, -KEY_HALF, KEY_HALF, 72.4, 74.4)
    parts.append(felt)
    fall = Part("Lacquer", bevel=0.7, segments=3)
    box(fall, -1.2, 1.4, -KEY_HALF, KEY_HALF, 0.0, 11.5, matrix=at(1.0, 0.0, 73.4, pitch=12.0) @ Matrix.Translation((-1.0, 0.0, 0.0)))
    parts.append(fall)
    plate = Part("Brass", bevel=0.1)
    box(plate, -1.25, -1.05, -6.0, 6.0, 5.2, 6.6, matrix=at(1.0, 0.0, 73.4, pitch=12.0) @ Matrix.Translation((-1.0, 0.0, 0.0)))
    parts.append(plate)


def build_desk(parts):
    # The music desk, up, leaning back, pierced with a row of slots the way a Victorian desk is.
    # The slots are cut with a boolean and the bevel runs round the cut edges.
    lean = at(DESK_X, 0.0, CASE_TOP + 2.4, pitch=DESK_LEAN)
    desk = Part("Lacquer", bevel=0.5, segments=3)
    box(desk, -0.75, 0.75, -39.0, 39.0, 0.0, 32.0, matrix=lean)
    parts.append(desk)
    cutter = bmesh.new()
    for k in range(-5, 6):
        if k == 0:
            continue
        y = k * 6.2
        tall = 14.0 - abs(k) * 0.9
        centre = 20.0
        m = lean @ at(0.0, y, centre) @ Matrix.Diagonal((1.0, 1.0, 1.0, 1.0))
        bmesh.ops.create_cone(cutter, cap_ends=True, segments=16, radius1=1.3, radius2=1.3, depth=4.0,
                              matrix=m @ Matrix.Rotation(math.radians(90.0), 4, "Y"))
        for dz in (-tall * 0.5, tall * 0.5):
            bmesh.ops.create_uvsphere(cutter, u_segments=16, v_segments=8, radius=1.3, matrix=m @ Matrix.Translation((0.0, 0.0, dz)))
        verts = bmesh.ops.create_cube(cutter, size=1.0)["verts"]
        bmesh.ops.transform(cutter, matrix=m @ Matrix.Diagonal((4.0, 2.6, tall, 1.0)), verts=verts)
    desk.cutter = cutter

    # The ledge the music stands on, along the foot of the desk, and two struts behind it.
    ledge = Part("Lacquer", bevel=0.35, segments=2)
    box(ledge, -4.0, 0.9, -38.0, 38.0, -0.4, 1.4, matrix=lean)
    box(ledge, -4.4, -3.2, -38.0, 38.0, 1.4, 2.6, matrix=lean)
    for y in (-24.0, 24.0):
        box(ledge, 0.7, 2.3, y - 0.8, y + 0.8, 1.0, 16.0, matrix=lean @ at(0.0, 0.0, 0.0, pitch=-24.0))
    parts.append(ledge)


def build_legs(parts):
    leg = Part("Lacquer", bevel=0.0)
    blocks = Part("Lacquer", bevel=0.5, segments=2)
    brass = Part("Brass", bevel=0.0)
    profile = [(0.0, 5.6), (3.4, 5.6), (3.9, 6.4), (3.5, 7.4), (3.5, 8.8), (4.4, 9.8), (4.4, 11.8), (3.3, 13.2),
               (3.0, 15.5), (3.3, 24.0), (3.8, 34.0), (4.3, 42.0), (4.8, 45.5), (5.8, 46.5), (5.8, 48.6),
               (4.9, 49.6), (5.3, 51.2), (6.7, 53.0), (6.7, 55.4), (0.0, 55.4)]
    for x, y in ((20.0, -62.0), (20.0, 62.0), (176.0, -44.0)):
        lathe(leg, profile, at(x, y, 0.0))
        box(blocks, x - 7.0, x + 7.0, y - 7.0, y + 7.0, 55.2, CASE_BOTTOM + 0.3)
        # The castor: a cup under the leg, a fork, and the wheel on its axle, trailing.
        lathe(brass, [(0.0, 3.6), (3.0, 3.6), (3.6, 5.0), (3.5, 5.8), (0.0, 5.8)], at(x, y, 0.0))
        box(brass, x - 0.2, x + 2.2, y - 1.6, y - 1.1, 1.2, 3.8)
        box(brass, x - 0.2, x + 2.2, y + 1.1, y + 1.6, 1.2, 3.8)
        lathe(brass, [(0.0, -0.95), (2.1, -0.95), (2.4, -0.5), (2.4, 0.5), (2.1, 0.95), (0.0, 0.95)], at(x + 1.2, y, 2.4, roll=90.0), sides=20)
    parts.extend([leg, blocks, brass])


def build_lyre(parts):
    wood = Part("Lacquer", bevel=0.4, segments=2)
    box(wood, 12.0, 25.0, -13.0, 13.0, 55.0, CASE_BOTTOM + 0.2)
    box(wood, 11.0, 25.0, -16.0, 16.0, 3.0, 11.0)
    parts.append(wood)
    arms = Part("Lacquer")
    for side in (-1.0, 1.0):
        path = []
        for k in range(17):
            t = k / 16.0
            z = 11.0 + (55.0 - 11.0) * t
            y = side * (7.0 + 6.5 * math.sin(math.pi * t) ** 0.8 - 1.5 * t)
            path.append((18.5, y, z))
        tube(arms, path, [1.5 - 0.4 * math.sin(math.pi * k / 16.0) for k in range(17)])
    # The pedal rods between the arms, and the braces back to the case.
    for y in (-3.5, 0.0, 3.5):
        tube(arms, [(18.5, y, 11.0), (18.5, y, 55.0)], 0.35, sides=8)
    for side in (-1.0, 1.0):
        tube(arms, [(24.0, side * 12.0, 8.0), (46.0, side * 20.0, 34.0), (70.0, side * 28.0, CASE_BOTTOM + 0.5)], 1.0, sides=10)
    parts.append(arms)
    pedals = Part("Brass", bevel=0.45, segments=3)
    for y, dip in ((-5.2, 0.0), (0.0, -2.5), (5.2, 0.0)):
        box(pedals, -1.0, 12.0, y - 1.5, y + 1.5, 0.0, 1.1, matrix=at(12.0, 0.0, 6.2, pitch=dip) @ Matrix.Translation((-12.0, 0.0, 0.0)))
    parts.append(pedals)


def build_bench(parts):
    frame = Part("Lacquer", bevel=0.45, segments=2)
    box(frame, -19.0, 19.0, -44.0, 44.0, 40.0, 46.5)
    parts.append(frame)
    cushion = Part("Cushion", bevel=2.6, segments=5, angle=30.0)
    box(cushion, -18.2, 18.2, -43.0, 43.0, 46.3, 53.5)
    parts.append(cushion)
    legs = Part("Lacquer")
    profile = [(0.0, 0.0), (1.7, 0.0), (2.0, 1.4), (1.6, 2.8), (1.7, 11.0), (2.3, 24.0), (2.7, 32.0), (3.5, 33.4),
               (3.5, 35.4), (2.9, 36.4), (2.9, 40.2), (0.0, 40.2)]
    for x in (-15.5, 15.5):
        for y in (-40.0, 40.0):
            lathe(legs, profile, at(x, y, 0.0), sides=24)
    for x in (-15.5, 15.5):
        tube(legs, [(x, -40.0, 12.0), (x, 40.0, 12.0)], 0.85, sides=10)
    tube(legs, [(-15.5, 0.0, 12.0), (15.5, 0.0, 12.0)], 0.85, sides=10)
    parts.append(legs)


# ---------------------------------------------------------------------------------------------
# Baking: bevel each part, merge by slot, UV, mirror into Blender's frame, export.
# ---------------------------------------------------------------------------------------------

TO_BLENDER = Matrix.Diagonal((0.01, -0.01, 0.01, 1.0))


def box_uvs(bm, texel):
    """Box-projected UVs in repeats of the slot's texel size, measured in centimetres."""
    uv = bm.loops.layers.uv.verify()
    for face in bm.faces:
        axis = max(range(3), key=lambda a: abs(face.normal[a]))
        for loop in face.loops:
            c = loop.vert.co * 100.0
            if axis == 2:
                loop[uv].uv = (c.x / texel, c.y / texel)
            elif axis == 0:
                loop[uv].uv = (c.y / texel, c.z / texel)
            else:
                loop[uv].uv = (c.x / texel, c.z / texel)


def bake(parts, name):
    """One object per part with its modifiers applied, then joined.

    Into Blender's frame first — centimetres to metres, and Y mirrored because the FBX import
    mirrors it back — with the normals recalculated after the mirror, since every part is closed.
    The bevels harden their normals, so a big flat face stays flat right up to its rounded edge:
    averaged across the bevel, the top of the bench cushion and the long keys shaded in diagonal
    streaks. Joined with the operator rather than through a bmesh, which would drop those custom
    normals; the FBX carries them and the import is told to use them.
    """
    scene = bpy.context.scene
    objects = []
    cutters = []
    for index, part in enumerate(parts):
        bmesh.ops.transform(part.bm, matrix=TO_BLENDER, verts=part.bm.verts)
        bmesh.ops.recalc_face_normals(part.bm, faces=part.bm.faces)
        box_uvs(part.bm, SLOTS[part.slot][1])
        mesh = bpy.data.meshes.new("{}_part{}".format(name, index))
        part.bm.to_mesh(mesh)
        mesh.shade_smooth()
        material = bpy.data.materials.get(part.slot) or bpy.data.materials.new(part.slot)
        material.diffuse_color = SLOTS[part.slot][0]
        mesh.materials.append(material)
        obj = bpy.data.objects.new(mesh.name, mesh)
        scene.collection.objects.link(obj)
        if getattr(part, "cutter", None) is not None:
            bmesh.ops.transform(part.cutter, matrix=TO_BLENDER, verts=part.cutter.verts)
            bmesh.ops.recalc_face_normals(part.cutter, faces=part.cutter.faces)
            cutter_mesh = bpy.data.meshes.new(mesh.name + "_cut")
            part.cutter.to_mesh(cutter_mesh)
            cutter = bpy.data.objects.new(cutter_mesh.name, cutter_mesh)
            scene.collection.objects.link(cutter)
            cutters.append(cutter)
            boolean = obj.modifiers.new("cut", "BOOLEAN")
            boolean.operation = "DIFFERENCE"
            boolean.object = cutter
            # The cutter is a heap of overlapping pieces (a bar and two rounded ends per slot).
            boolean.solver = "EXACT"
            boolean.use_self = True
        if part.bevel > 0.0:
            bevel = obj.modifiers.new("bevel", "BEVEL")
            bevel.width = part.bevel * 0.01
            bevel.segments = part.segments
            bevel.limit_method = "ANGLE"
            bevel.angle_limit = math.radians(part.angle)
            bevel.use_clamp_overlap = True
            bevel.harden_normals = True
        else:
            # Turned and swept parts are smooth all round; only their flat ends are sharp.
            obj.modifiers.new("smooth", "WEIGHTED_NORMAL").keep_sharp = True
            mesh.set_sharp_from_angle(angle=math.radians(50.0))
        obj.modifiers.new("tris", "TRIANGULATE").keep_custom_normals = True
        objects.append(obj)

    bpy.context.view_layer.update()
    for obj in objects:
        bpy.context.view_layer.objects.active = obj
        for modifier in list(obj.modifiers):
            bpy.ops.object.modifier_apply(modifier=modifier.name)
    for cutter in cutters:
        bpy.data.objects.remove(cutter)

    for other in scene.objects:
        other.select_set(other in objects)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    joined = bpy.context.view_layer.objects.active
    joined.name = name
    joined.data.name = name
    return joined


def export(obj):
    folder = os.path.join(MODEL_DIR, obj.name)
    os.makedirs(folder, exist_ok=True)
    for other in bpy.context.scene.objects:
        other.select_set(other == obj)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=os.path.join(folder, obj.name + ".fbx"), use_selection=True,
                             object_types={"MESH"}, mesh_smooth_type="FACE", add_leaf_bones=False,
                             bake_anim=False, apply_unit_scale=True)


def preview(objects):
    os.makedirs(PREVIEW_DIR, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_cavity = True
    scene.display.shading.show_shadows = True
    scene.render.resolution_x = 1400
    scene.render.resolution_y = 900
    world = bpy.data.worlds.new("preview")
    scene.world = world
    target = bpy.data.objects.new("target", None)
    scene.collection.objects.link(target)
    target.location = (0.9, 0.0, 0.6)
    camera_data = bpy.data.cameras.new("camera")
    camera_data.lens = 40.0
    camera = bpy.data.objects.new("camera", camera_data)
    scene.collection.objects.link(camera)
    track = camera.constraints.new("TRACK_TO")
    track.target = target
    scene.camera = camera
    # Blender's frame: the game's +Y is Blender's -Y.
    for label, location in (("front", (-2.6, 1.3, 1.6)), ("side", (0.8, -3.4, 1.5)), ("keys", (-1.1, 0.35, 1.2))):
        camera.location = location
        target.location = (0.9, 0.0, 0.6) if label != "keys" else (0.0, 0.0, 0.75)
        scene.render.filepath = os.path.join(PREVIEW_DIR, "piano_{}.png".format(label))
        bpy.ops.render.render(write_still=True)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    piano_parts = []
    build_case(piano_parts)
    build_keyboard(piano_parts)
    build_desk(piano_parts)
    build_legs(piano_parts)
    build_lyre(piano_parts)
    piano = bake(piano_parts, "grand_piano")

    bench_parts = []
    build_bench(bench_parts)
    bench = bake(bench_parts, "piano_bench")

    export(piano)
    export(bench)
    # Stand the bench where the game puts it, for the preview only.
    bench.location = (-0.46, -0.03, 0.0)
    preview([piano, bench])


main()
