"""
Builds the upstairs bathroom's fittings and belongings in Blender, headless, and exports each as an
FBX the art pipeline imports.

    blender --background --factory-startup --python Tools/make_bathroom.py [-- --only=a,b]

The bathroom is the room off the upstairs corridor between the girl's room and the stairs. It was
done over when the master bedroom was, and it is the same idea carried further: the Victorian
bones kept and made much of — white bevelled tiles with a black liner, a hexagon mosaic floor, a
cast-iron roll-top bath on claw feet with a floor-standing brass filler, a high-level cistern with
its chain, a brass towel rail — and modern things put in among them: a walk-in shower behind a
black steel-framed screen (the brass riser and the rose are Victorian again), a floating vanity in
deep green under a marble top, and a big mirror with globe bulbs down both sides. No window. The
bulbs round the mirror are half of them smashed, the pendant's globe has gone and its pieces are on
the floor, one of the sconces over the bath has lost its shade.

Outputs Art/Source/Models/<name>/<name>.fbx with an empty GENERATED file beside it (re-imported
every run with its own normals), and Workbench previews in Saved/BathroomPreview/.

Frame, as every prop in the house: centimetres, origin on the floor under the middle of the
footprint, the front facing local +Y. Wall-hung things have their origin on the wall plane (the
mirror, the shower set, the sconces, the roll holder, the mouldings); the pendant hangs from z = 0
(the ceiling); a bulb's origin is its socket's mouth with the glass along +Y. The numbers
ABathroomActor also needs are the constants below, and must stay in step with BathroomActor.h.

Material slots are set by name in C++ (ABathroomActor). Texel sizes match the surface each is given:
    Lacquer             bedroom_paint (RoomSurfaces::Paint, 120cm), tinted the vanity's green
    Marble              marble_01 (RoomSurfaces::Marble, 150cm)
    Towel, Towel2, Mat  rough_linen (Drapery, 34cm)
Everything else is a flat colour or glass, where the UVs do not matter.
"""

import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blender_kit as K  # noqa: E402
from blender_kit import Part, at, blob, box, cbox, ellipsoid, lathe, prism, tube  # noqa: E402
from make_bedroom import cushion, solid_grid  # noqa: E402

PREVIEW_DIR = os.path.join(K.ROOT, "Saved", "BathroomPreview")

PAINT = 120.0
MARBLE = 150.0
CLOTH = 34.0
FLAT = 100.0

SLOTS = {
    "Enamel": ((0.92, 0.91, 0.88, 1.0), FLAT),
    "BathPaint": ((0.05, 0.05, 0.055, 1.0), FLAT),
    "Ceramic": ((0.90, 0.89, 0.86, 1.0), FLAT),
    "Glaze": ((0.90, 0.90, 0.88, 1.0), FLAT),
    "GlazeBlack": ((0.04, 0.04, 0.045, 1.0), FLAT),
    "Brass": ((0.70, 0.55, 0.28, 1.0), FLAT),
    "Chrome": ((0.70, 0.70, 0.72, 1.0), FLAT),
    "BlackMetal": ((0.05, 0.05, 0.05, 1.0), FLAT),
    "CastIron": ((0.07, 0.07, 0.075, 1.0), FLAT),
    "Lacquer": ((0.10, 0.25, 0.20, 1.0), PAINT),
    "Marble": ((0.85, 0.84, 0.82, 1.0), MARBLE),
    "Mirror": ((0.35, 0.37, 0.40, 1.0), FLAT),
    "Glass": ((0.70, 0.78, 0.80, 1.0), FLAT),
    "BulbGlass": ((0.80, 0.82, 0.80, 1.0), FLAT),
    "Opal": ((0.92, 0.91, 0.88, 1.0), FLAT),
    "Filament": ((0.20, 0.18, 0.15, 1.0), FLAT),
    "Water": ((0.06, 0.05, 0.03, 1.0), FLAT),
    "Wood": ((0.20, 0.10, 0.05, 1.0), FLAT),
    "Bentwood": ((0.12, 0.07, 0.04, 1.0), FLAT),
    "Cane": ((0.55, 0.42, 0.25, 1.0), FLAT),
    "Towel": ((0.85, 0.83, 0.78, 1.0), CLOTH),
    "Towel2": ((0.40, 0.55, 0.50, 1.0), CLOTH),
    "Paper": ((0.88, 0.86, 0.80, 1.0), FLAT),
    "Sponge": ((0.75, 0.60, 0.35, 1.0), FLAT),
    "Soap": ((0.80, 0.72, 0.60, 1.0), FLAT),
    "Wax": ((0.85, 0.82, 0.72, 1.0), FLAT),
    "Duck": ((0.90, 0.75, 0.10, 1.0), FLAT),
    "Beak": ((0.90, 0.40, 0.05, 1.0), FLAT),
    "Black": ((0.03, 0.03, 0.03, 1.0), FLAT),
    "BrushA": ((0.15, 0.30, 0.65, 1.0), FLAT),
    "BrushB": ((0.15, 0.50, 0.35, 1.0), FLAT),
    "BrushC": ((0.85, 0.35, 0.60, 1.0), FLAT),
    "Bristle": ((0.92, 0.92, 0.90, 1.0), FLAT),
    "Badger": ((0.45, 0.40, 0.33, 1.0), FLAT),
    "BottleA": ((0.85, 0.85, 0.82, 1.0), FLAT),
    "BottleB": ((0.20, 0.25, 0.30, 1.0), FLAT),
    "BottleC": ((0.55, 0.40, 0.30, 1.0), FLAT),
    "Liquid": ((0.60, 0.40, 0.15, 1.0), FLAT),
    "Cap": ((0.10, 0.10, 0.10, 1.0), FLAT),
    "Pot": ((0.55, 0.30, 0.18, 1.0), FLAT),
    "Soil": ((0.12, 0.08, 0.05, 1.0), FLAT),
    "Frond": ((0.42, 0.30, 0.16, 1.0), FLAT),
    "Stem": ((0.30, 0.22, 0.14, 1.0), FLAT),
    "Shadow": ((0.03, 0.03, 0.03, 1.0), FLAT),
    "Biscuit": ((0.62, 0.50, 0.40, 1.0), FLAT),
}

# ---------------------------------------------------------------------------------------------
# Shared with BathroomActor.h.
# ---------------------------------------------------------------------------------------------

BATH_LENGTH = 172.0        # along local X
BATH_WIDTH = 78.0          # along local Y
BATH_RIM = 65.3            # top of the roll
BATH_A = BATH_LENGTH * 0.5
BATH_B = BATH_WIDTH * 0.5
BATH_EXP = 3.2             # the plan's superellipse exponent: an oblong with round ends
FILLER_REACH = 34.0        # how far the spout reaches out over +Y from the standpipes
VANITY_W = 120.0
VANITY_D = 52.0
VANITY_BOTTOM = 40.0
VANITY_TOP = 86.0          # the marble's top face
MIRROR_W = 76.0            # the glass
MIRROR_H = 100.0
MIRROR_SIDE = 12.0         # the bulb strip each side of the glass
MIRROR_DEPTH = 4.0
BULB_X = MIRROR_W * 0.5 + MIRROR_SIDE * 0.5
BULB_ZS = (-40.0, -20.0, 0.0, 20.0, 40.0)
SOCKET_Y = MIRROR_DEPTH + 2.4  # the socket's mouth, where a bulb's origin goes
SCREEN_W = 100.0
SCREEN_H = 200.0
WC_BACK = 34.0             # from the pan's origin back to the wall
WC_SEAT = 43.0
LID_UP = 90.4              # the lid's turn up from flat, leaning on the flush pipe (see toilet)
RAIL_W = 64.0
RAIL_BARS = (44.0, 66.0, 88.0)
RAIL_TUBE = 1.4
CHAIR_SEAT = 46.0
PENDANT_DROP = 66.0        # the gallery the globe was held in, under the ceiling


# ---------------------------------------------------------------------------------------------
# Helpers.
# ---------------------------------------------------------------------------------------------

def lathe_y(part, profile, centre, sides=32):
    """A turned part whose axis is local +Y (the profile's height runs out of the front)."""
    lathe(part, profile, Matrix.Translation(centre) @ Matrix.Rotation(math.radians(-90.0), 4, "X"), sides=sides)


def cutter_of(part):
    """A second bmesh on the part, subtracted from it at the bake (blender_kit.bake)."""
    if part.cutter is None:
        part.cutter = bmesh.new()
    holder = Part("Shadow")
    holder.bm = part.cutter
    return holder


def superellipse(a, b, n, t):
    c, s = math.cos(t), math.sin(t)
    return (a * math.copysign(abs(c) ** (2.0 / n), c), b * math.copysign(abs(s) ** (2.0 / n), s))


def plan_loft(part, profile, a, b, n, sides=72, cy=0.0, matrix=None):
    """
    A surface swept through rings of one oblong plan (a superellipse a x b, exponent n), each ring
    the plan offset by d: profile is (d, z) from the bottom pole round to the top pole, a pole
    written as (None, z). The bath, the pan: closed solids whose section is a loop touching the
    axis at both ends, the way blender_kit.lathe's is.
    """
    bm = part.bm
    rings = []
    for entry in profile:
        d, z = entry[0], entry[1]
        ccy = entry[2] if len(entry) > 2 else cy
        if d is None:
            rings.append([bm.verts.new((0.0, ccy, z))])
            continue
        ring = []
        for k in range(sides):
            x, y = superellipse(max(a + d, 0.5), max(b + d, 0.5), n, 2 * math.pi * k / sides)
            ring.append(bm.verts.new((x, y + ccy, z)))
        rings.append(ring)
    for r0, r1 in zip(rings, rings[1:]):
        if len(r0) == 1 and len(r1) == 1:
            continue
        if len(r0) == 1:
            for k in range(sides):
                bm.faces.new((r0[0], r1[(k + 1) % sides], r1[k]))
        elif len(r1) == 1:
            for k in range(sides):
                bm.faces.new((r0[k], r0[(k + 1) % sides], r1[0]))
        else:
            for k in range(sides):
                j = (k + 1) % sides
                bm.faces.new((r0[k], r0[j], r1[j], r1[k]))
    verts = [v for ring in rings for v in ring]
    if matrix is not None:
        bmesh.ops.transform(bm, matrix=matrix, verts=verts)
    return verts


def plan_slab(part, a, b, n, z0, z1, sides=64, cy=0.0):
    """A flat slab of the oblong plan: a disc of water, a seat."""
    pts = [superellipse(a, b, n, 2 * math.pi * k / sides) for k in range(sides)]
    prism(part, [(x, y + cy) for x, y in pts], z0, z1)


def plan_ring(part, a, b, n, width, z0, z1, sides=64, cy=0.0):
    """A flat ring of the oblong plan, width wide inward: a toilet seat."""
    bm = part.bm
    outer = [superellipse(a, b, n, 2 * math.pi * k / sides) for k in range(sides)]
    inner = [superellipse(a - width, b - width, n, 2 * math.pi * k / sides) for k in range(sides)]
    o0 = [bm.verts.new((x, y + cy, z0)) for x, y in outer]
    o1 = [bm.verts.new((x, y + cy, z1)) for x, y in outer]
    i0 = [bm.verts.new((x, y + cy, z0)) for x, y in inner]
    i1 = [bm.verts.new((x, y + cy, z1)) for x, y in inner]
    for k in range(sides):
        j = (k + 1) % sides
        bm.faces.new((o0[k], o0[j], o1[j], o1[k]))
        bm.faces.new((i0[j], i0[k], i1[k], i1[j]))
        bm.faces.new((o1[k], o1[j], i1[j], i1[k]))
        bm.faces.new((o0[j], o0[k], i0[k], i0[j]))
    return o0 + o1 + i0 + i1


def jagged_globe(part, matrix, radius, thick, theta0, rim, sides=40, rings=10):
    """
    What is left of a glass globe that has been smashed: the sphere from its neck (theta0 from the
    -Z pole, where the socket is) out to a ragged rim, rim(phi) being the polar angle the glass
    still reaches at each longitude. A closed thin shell, so it lights as glass (the 10-08 rule:
    one winding, closed). The local frame has the neck down -Z and the sphere's centre at the origin.
    """
    bm = part.bm
    outer, inner = [], []
    for i in range(rings + 1):
        t = i / rings
        row_o, row_i = [], []
        for k in range(sides):
            phi = 2 * math.pi * k / sides
            th = theta0 + t * (rim(phi) - theta0)
            d = Vector((math.sin(th) * math.cos(phi), math.sin(th) * math.sin(phi), -math.cos(th)))
            row_o.append(bm.verts.new(matrix @ (d * radius)))
            row_i.append(bm.verts.new(matrix @ (d * (radius - thick))))
        outer.append(row_o)
        inner.append(row_i)
    for i in range(rings):
        for k in range(sides):
            j = (k + 1) % sides
            bm.faces.new((outer[i][k], outer[i][j], outer[i + 1][j], outer[i + 1][k]))
            bm.faces.new((inner[i + 1][k], inner[i + 1][j], inner[i][j], inner[i][k]))
    for k in range(sides):
        j = (k + 1) % sides
        bm.faces.new((outer[rings][k], outer[rings][j], inner[rings][j], inner[rings][k]))
        bm.faces.new((inner[0][k], inner[0][j], outer[0][j], outer[0][k]))


def ragged(seed, base, depth, lobes=5):
    """A rim angle that wanders round the globe: a few harmonics and a hard notch or two, so the
    break has points and bites rather than a wave."""
    rng = random.Random(seed)
    terms = [(rng.uniform(0, 2 * math.pi), k, depth * rng.uniform(0.4, 1.0) / k) for k in range(1, lobes + 1)]
    notches = [(rng.uniform(0, 2 * math.pi), rng.uniform(0.12, 0.3), rng.uniform(0.25, 0.5) * depth * 2.0) for _ in range(3)]

    def rim(phi):
        v = base + sum(a * math.sin(k * phi + p) for p, k, a in terms)
        for c, w, d in notches:
            dist = abs((phi - c + math.pi) % (2 * math.pi) - math.pi)
            if dist < w:
                v -= d * (1.0 - dist / w)
        return v
    return rim


def curved_shard(part, centre, size, curvature, thick, seed, flip=False, yaw=0.0):
    """
    A piece of a broken globe lying on a surface: an irregular outline of five to seven corners,
    bowed as the sphere it came off was (z = curvature * r^2), given its thickness, closed, and
    laid so its lowest point rests on centre's z.
    """
    rng = random.Random(seed)
    corners = rng.randint(5, 7)
    angles = sorted(rng.uniform(0, 2 * math.pi) for _ in range(corners))
    outline = [(math.cos(a) * size * rng.uniform(0.45, 1.0), math.sin(a) * size * rng.uniform(0.35, 0.9)) for a in angles]
    sgn = -1.0 if flip else 1.0

    def lift(x, y):
        return sgn * curvature * (x * x + y * y)

    low = min(min(lift(x, y) for x, y in outline), 0.0) - thick * 0.5
    rot = Matrix.Rotation(math.radians(yaw), 4, "Z")
    base = Vector(centre)
    bm = part.bm
    top = [bm.verts.new(base + rot @ Vector((x, y, lift(x, y) - low + thick * 0.5))) for x, y in outline]
    bot = [bm.verts.new(base + rot @ Vector((x, y, lift(x, y) - low - thick * 0.5))) for x, y in outline]
    ct = bm.verts.new(base + rot @ Vector((0.0, 0.0, -low + thick * 0.5)))
    cb = bm.verts.new(base + rot @ Vector((0.0, 0.0, -low - thick * 0.5)))
    n = len(outline)
    for k in range(n):
        j = (k + 1) % n
        bm.faces.new((ct, top[k], top[j]))
        bm.faces.new((cb, bot[j], bot[k]))
        bm.faces.new((top[k], bot[k], bot[j], top[j]))


def drape(part, width, path, thick, seed, wave=0.6, wave_len=9.0, nx=26):
    """
    A piece of cloth hung over something (make_laundry.py's drape, without the fray): a grid along
    X by a polyline in YZ from one hem over the support to the other, solidified into one closed
    shell. The folds are sines along X whose phase drifts down the drop and that die out at the
    support, where the weight holds the cloth flat.
    """
    rng = random.Random(seed)
    pts = [Vector((0.0, y, z)) for y, z in path]
    lengths = [0.0]
    for a, b in zip(pts, pts[1:]):
        lengths.append(lengths[-1] + (b - a).length)
    normals = []
    for i in range(len(pts)):
        a = pts[max(i - 1, 0)]
        b = pts[min(i + 1, len(pts) - 1)]
        t = (b - a).normalized()
        normals.append(Vector((0.0, t.z, -t.y)))
    top = max(range(len(pts)), key=lambda i: pts[i].z)
    phases = [rng.uniform(0, 2 * math.pi) for _ in range(2)]
    grid = []
    for ix in range(nx + 1):
        x = -width * 0.5 + width * ix / nx
        col = []
        for i, p in enumerate(pts):
            s = abs(lengths[i] - lengths[top])
            fade = min(1.0, s / 14.0)
            w = wave * fade * (math.sin(2 * math.pi * x / wave_len + phases[0] + s * 0.06)
                               + 0.5 * math.sin(2 * math.pi * x / (wave_len * 0.47) + phases[1] - s * 0.04))
            col.append(Vector((x, p.y, p.z)) + normals[i] * w)
        grid.append(col)
    solid_grid(part, grid, thick)


def over_rail(radius, front, back, step=2.0, splay=0.0):
    """A path in YZ over a rail of this radius at the origin: down the front (+Y) to -front, over,
    and down the back to -back. Splay leans the hems out, as a heavy cloth does."""
    path = []
    n = max(2, int(front / step))
    for k in range(n, 0, -1):
        z = -front * k / n
        path.append((radius + splay * (-z / front) ** 1.5 * front * 0.1, z))
    for k in range(13):
        a = math.pi * k / 12
        path.append((radius * math.cos(a), radius * math.sin(a)))
    n = max(2, int(back / step))
    for k in range(1, n + 1):
        z = -back * k / n
        path.append((-radius - splay * (-z / back) ** 1.5 * back * 0.1, z))
    return path


def crosshead(part, centre, radius=3.2, turn=0.0, button=None, matrix=None):
    """A Victorian cross-head handle on a short shank: four arms ending in balls, a porcelain
    button in the hub. Its axis is +Z from centre, turned by matrix if given."""
    m = Matrix.Translation(centre) @ (matrix if matrix is not None else Matrix.Identity(4))
    start = len(part.bm.verts)
    lathe(part, [(0.0, 0.0), (1.4, 0.0), (1.2, 2.0), (0.9, 2.6), (0.0, 2.8)], Matrix.Identity(4), sides=16)
    for k in range(4):
        a = math.radians(45.0 + 90.0 * k + turn)
        tip = (math.cos(a) * radius, math.sin(a) * radius, 2.2)
        tube(part, [(0.0, 0.0, 2.2), tip], 0.42, sides=6)
        ellipsoid(part, tip, (0.62, 0.62, 0.62), rings=4, sides=8)
    part.bm.verts.ensure_lookup_table()
    bmesh.ops.transform(part.bm, matrix=m, verts=part.bm.verts[start:])
    if button is not None:
        lathe(button, [(0.0, 2.6), (0.75, 2.6), (0.75, 3.1), (0.0, 3.3)], m, sides=12)


# ---------------------------------------------------------------------------------------------
# The bath and its filler.
# ---------------------------------------------------------------------------------------------

BATH_OUTER = [(-14.0, 16.0), (-11.5, 17.2), (-8.5, 20.0), (-5.5, 26.0), (-3.0, 36.0), (-1.4, 48.0), (-0.4, 57.0), (0.0, 60.5)]
BATH_ROLL = [(0.9, 61.8), (1.5, 63.2), (1.2, 64.5), (0.3, 65.2), (-0.9, 65.3), (-2.0, 64.8), (-2.8, 63.6), (-3.1, 62.0)]
BATH_INNER = [(-3.3, 58.0), (-4.2, 48.0), (-5.6, 38.0), (-7.6, 30.0), (-10.5, 25.2), (-14.0, 23.0), (-17.0, 22.4)]


def bath():
    """
    A cast-iron double-ended roll-top bath on four claw feet, 172 x 78, the rim 65 up: white
    enamel inside and over the roll, the outside painted black, the feet brass. An inch of brown
    water has stood in the bottom since the plug was last put in, and the plug's chain hangs from
    the overflow at the head. The long axis is local X.
    """
    enamel = Part("Enamel")
    paint = Part("BathPaint")
    brass = Part("Brass")
    water = Part("Water")
    a, b, n = BATH_A, BATH_B, BATH_EXP
    plan_loft(enamel, [(None, 16.0)] + BATH_OUTER + BATH_ROLL + BATH_INNER + [(None, 22.3)], a, b, n)
    # The paint: a skin a hair outside the iron, up to just under the roll, and back down inside the
    # wall to close it.
    skin_out = [(d + 0.25, z) for d, z in BATH_OUTER[:-1]] + [(0.3, 59.0)]
    skin_in = [(d - 0.4, z) for d, z in reversed(BATH_OUTER[:-1])]
    plan_loft(paint, [(None, 15.7)] + skin_out + skin_in + [(None, 16.3)], a, b, n)
    # The water: an inch of it, its edge just inside the enamel.
    plan_slab(water, a - 10.9, b - 10.9, n, 25.6, 26.1)
    # The waste at the head, its plug lying in the water on its chain, the overflow over it.
    lathe(brass, [(0.0, 22.25), (3.0, 22.25), (3.2, 22.6), (0.0, 22.7)], at(-a + 24.0, 0.0, 0.0), sides=20)
    lathe(brass, [(0.0, 0.0), (2.6, 0.0), (2.6, 0.6), (0.0, 0.8)], at(-a + 3.9, 0.0, 50.0, pitch=90.0), sides=20)
    lathe(brass, [(0.0, 26.1), (2.4, 26.1), (2.2, 27.6), (0.0, 27.8)], at(-a + 30.0, 9.0, 0.0), sides=16)
    chain = [(-a + 4.6, 0.0, 49.5)]
    for k in range(1, 12):
        t = k / 11
        chain.append((-a + 4.6 + 25.4 * t, 9.0 * t, 49.5 - 23.0 * math.sin(t * math.pi * 0.5) - 2.0 * math.sin(t * math.pi)))
    for p in chain:
        ellipsoid(brass, p, (0.45, 0.45, 0.45), rings=3, sides=6)
    tube(brass, chain, 0.18, sides=5)

    # Claw feet: a leg out of the iron, a knee of leaf, a ball held in three claws.
    for sx in (-1, 1):
        for sy in (-1, 1):
            x, y = sx * (a - 22.0), sy * (b - 11.0)
            out = Vector((sx * 0.55, sy * 0.83, 0.0)).normalized()
            ox, oy = out.x, out.y
            ellipsoid(brass, (x, y, 24.0), (5.4, 4.6, 4.2), rings=6, sides=12)
            tube(brass, [(x, y, 27.0), (x + ox * 1.0, y + oy * 1.0, 18.0), (x + ox * 2.6, y + oy * 2.6, 10.0), (x + ox * 3.2, y + oy * 3.2, 6.0)],
                 [3.6, 3.1, 2.6, 2.4], sides=14)
            bx, by = x + ox * 3.6, y + oy * 3.6
            ellipsoid(brass, (bx, by, 3.8), (3.7, 3.7, 3.5), rings=8, sides=14)
            for spread in (-55.0, 0.0, 55.0):
                d = Matrix.Rotation(math.radians(spread), 3, "Z") @ Vector((ox, oy, 0.0))
                path = [(bx + d.x * 0.5, by + d.y * 0.5, 7.0), (bx + d.x * 3.6, by + d.y * 3.6, 5.6), (bx + d.x * 4.6, by + d.y * 4.6, 2.6), (bx + d.x * 4.9, by + d.y * 4.9, 0.4)]
                tube(brass, path, [1.15, 1.05, 0.85, 0.5], sides=8)
    return [enamel, paint, brass, water]


def bath_filler():
    """
    A floor-standing bath mixer in brass: two standpipes out of the floor on flanges, a cross-bar
    with a crosshead on each, a swan-neck spout reaching FILLER_REACH out over +Y and down, and a
    telephone handset in a cradle on top with its hose looped down behind. Origin on the floor
    between the pipes.
    """
    brass = Part("Brass", bevel=0.15)
    ceramic = Part("Ceramic")
    hose = Part("Chrome")

    for x in (-6.0, 6.0):
        lathe(brass, [(0.0, 0.0), (4.2, 0.0), (4.2, 0.6), (2.2, 1.6), (1.7, 3.0), (0.0, 3.0)], at(x, 0.0, 0.0), sides=24)
        tube(brass, [(x, 0.0, 2.0), (x, 0.0, 84.0)], 1.6, sides=16)
        lathe(brass, [(0.0, 84.0), (2.3, 84.0), (2.3, 90.0), (0.0, 90.0)], at(x, 0.0, 0.0), sides=18)
        crosshead(brass, (x, 0.0, 90.0), radius=3.4, turn=10.0 if x < 0 else -8.0, button=ceramic)
    tube(brass, [(-6.0, 0.0, 86.5), (6.0, 0.0, 86.5)], 1.5, sides=14)
    lathe(brass, [(0.0, 84.5), (2.6, 84.5), (2.6, 89.0), (0.0, 89.0)], at(0.0, 0.0, 0.0), sides=18)
    # The swan neck: up out of the middle and over.
    neck = [(0.0, 0.0, 88.0), (0.0, 0.0, 98.0)]
    for k in range(1, 13):
        t = k / 12
        ang = math.pi * t
        neck.append((0.0, (FILLER_REACH - 8.0) * 0.5 * (1.0 - math.cos(ang)), 98.0 + 9.0 * math.sin(ang)))
    neck += [(0.0, FILLER_REACH - 4.0, 93.0), (0.0, FILLER_REACH, 84.0), (0.0, FILLER_REACH, 79.0)]
    tube(brass, neck, 1.25, sides=14)
    lathe(brass, [(0.0, 77.5), (1.7, 77.5), (1.5, 80.0), (0.0, 80.0)], at(0.0, FILLER_REACH, 0.0), sides=14)
    # The handset cradle on the cross-bar, and the handset in it, lying along X.
    for x in (-4.0, 4.0):
        tube(brass, [(x, 0.0, 88.0), (x, -3.0, 93.0), (x, -3.6, 95.5)], 0.5, sides=8)
    tube(brass, [(-9.0, -3.6, 96.4), (9.0, -3.6, 96.4)], 1.0, sides=12)
    lathe(brass, [(0.0, -1.2), (2.2, -0.5), (3.4, 2.0), (3.6, 3.0), (0.0, 3.2)], at(-9.0, -3.6, 96.4, pitch=-90.0), sides=18)
    lathe(brass, [(0.0, 0.0), (1.6, 0.0), (1.6, 2.5), (0.0, 2.5)], at(9.0, -3.6, 96.4, pitch=90.0), sides=12)
    # Its hose: out of the handset's end, down behind the pipes in a loop and back up into the body.
    path = [(11.5, -3.6, 96.4), (14.0, -5.0, 90.0), (13.0, -8.0, 70.0), (8.0, -9.0, 52.0), (0.0, -8.0, 46.0), (-6.0, -6.0, 52.0), (-4.0, -3.0, 70.0), (-2.0, -2.0, 84.0)]
    tube(hose, K.catmull(path, 5), 0.75, sides=8)
    return [brass, ceramic, hose]


# ---------------------------------------------------------------------------------------------
# The shower.
# ---------------------------------------------------------------------------------------------

def shower_screen():
    """
    The walk-in's fixed screen: a black steel frame of narrow bars, two lights across and four up,
    glazed with clear glass gone milky with limescale. SCREEN_W along X, SCREEN_H tall, standing on
    its bottom bar at z = 0; the glass in the plane y = 0.
    """
    steel = Part("BlackMetal", bevel=0.2)
    glass = Part("Glass")
    w, h = SCREEN_W, SCREEN_H
    bar, depth = 3.0, 3.6
    xs = [-w * 0.5 + bar * 0.5, 0.0, w * 0.5 - bar * 0.5]
    zs = [bar * 0.5, 50.0, 100.0, 150.0, h - bar * 0.5]
    for x in xs:
        cbox(steel, x, 0.0, h * 0.5, bar, depth, h)
    for z in zs:
        cbox(steel, 0.0, 0.0, z, w, depth, bar)
    for i in range(2):
        for j in range(4):
            x0, x1 = xs[i] + bar * 0.5 - 0.6, xs[i + 1] - bar * 0.5 + 0.6
            z0, z1 = zs[j] + bar * 0.5 - 0.6, zs[j + 1] - bar * 0.5 + 0.6
            box(glass, x0, x1, -0.4, 0.4, z0, z1)
    return [steel, glass]


def shower_set():
    """
    The shower's fittings, exposed and in brass, as a Victorian shower was plumbed: a thermostatic
    valve with two crossheads and a dial on the wall, a riser pipe up from it on two clips, and a
    rose thirty across on an arm out from the top. Origin on the wall plane at the floor under the
    riser; out of the wall is +Y.
    """
    brass = Part("Brass", bevel=0.15)
    ceramic = Part("Ceramic")
    face = Part("Shadow")
    # The valve: a horizontal body on two wall unions.
    for x in (-10.0, 10.0):
        lathe_y(brass, [(0.0, 0.0), (3.2, 0.0), (3.2, 1.0), (1.8, 1.6), (1.8, 4.4), (0.0, 4.4)], (x, 0.0, 105.0), sides=18)
    tube(brass, [(-12.0, 4.4, 105.0), (12.0, 4.4, 105.0)], 2.4, sides=18)
    for x in (-12.0, 12.0):
        lathe(brass, [(0.0, -1.0), (2.6, -1.0), (2.6, 1.0), (0.0, 1.0)], at(x, 4.4, 105.0, pitch=90.0), sides=16)
    for x in (-8.0, 8.0):
        tube(brass, [(x, 6.6, 105.0), (x, 9.0, 105.0)], 1.3, sides=12)
        crosshead(brass, (x, 9.0, 105.0), radius=3.0, turn=8.0 if x < 0 else -15.0, button=ceramic,
                  matrix=Matrix.Rotation(math.radians(-90.0), 4, "X"))
    # The dial between them.
    lathe_y(brass, [(0.0, 0.0), (3.8, 0.0), (3.8, 1.6), (0.0, 1.6)], (0.0, 6.4, 105.0), sides=24)
    lathe_y(ceramic, [(0.0, 0.0), (3.2, 0.0), (3.2, 0.3), (0.0, 0.3)], (0.0, 8.0, 105.0), sides=24)
    cbox(face, 0.0, 8.35, 106.4, 0.25, 0.1, 2.6)
    # The riser, up from the body to the arm, on two clips.
    tube(brass, [(0.0, 4.4, 107.0), (0.0, 4.4, 204.0)], 1.25, sides=14)
    for z in (140.0, 185.0):
        lathe_y(brass, [(0.0, 0.0), (1.6, 0.0), (1.6, 4.4), (0.0, 4.4)], (0.0, 0.0, z), sides=12)
        tube(brass, [(-1.8, 4.4, z), (1.8, 4.4, z)], 1.6, sides=12)
    # The arm out over the head, and the rose.
    tube(brass, K.catmull([(0.0, 4.4, 204.0), (0.0, 5.4, 209.0), (0.0, 10.0, 211.0), (0.0, 38.0, 211.0)], 4), 1.1, sides=12)
    lathe(brass, [(0.0, 198.5), (15.0, 198.5), (15.4, 199.6), (14.6, 201.0), (6.0, 205.0), (2.0, 209.0), (1.3, 211.0), (0.0, 211.0)],
          at(0.0, 38.0, 0.0), sides=40)
    lathe(face, [(0.0, 198.4), (13.8, 198.4), (13.8, 198.55), (0.0, 198.55)], at(0.0, 38.0, 0.0), sides=40)
    return [brass, ceramic, face]


def shower_drain():
    """A long drain along the wall at the shower's foot: a black steel grate in slots. 70 x 7."""
    steel = Part("BlackMetal", bevel=0.1)
    shadow = Part("Shadow")
    box(shadow, -35.0, 35.0, -3.5, 3.5, -0.2, 0.05)
    box(steel, -35.0, 35.0, -3.5, -3.0, 0.0, 0.4)
    box(steel, -35.0, 35.0, 3.0, 3.5, 0.0, 0.4)
    for k in range(-33, 34, 2):
        box(steel, k - 0.35, k + 0.35, -3.0, 3.0, 0.0, 0.35)
    return [steel, shadow]


def shower_bottles():
    """Three plastic bottles left in the shower's corner: a shampoo, a wash, a conditioner on its
    cap. Origin on the floor at the middle of the group."""
    a, b, c = Part("BottleA", bevel=0.8, segments=3), Part("BottleB", bevel=0.8, segments=3), Part("BottleC", bevel=0.8, segments=3)
    cap = Part("Cap", bevel=0.3)
    box(a, -9.0, -2.0, -2.2, 2.2, 0.0, 21.0)
    lathe(cap, [(0.0, 21.0), (1.6, 21.0), (1.6, 23.4), (0.0, 23.4)], at(-5.5, 0.0, 0.0), sides=14)
    lathe(b, [(0.0, 0.0), (3.2, 0.0), (3.4, 2.0), (3.4, 17.0), (2.0, 19.0), (1.2, 20.0), (0.0, 20.0)], at(2.5, 1.0, 0.0), sides=24)
    lathe(cap, [(0.0, 20.0), (1.3, 20.0), (1.3, 22.4), (0.0, 22.4)], at(2.5, 1.0, 0.0), sides=14)
    # The conditioner stands on its cap, upside down.
    lathe(cap, [(0.0, 0.0), (2.4, 0.0), (2.4, 2.4), (0.0, 2.4)], at(9.0, -1.5, 0.0), sides=16)
    box(c, 6.4, 11.6, -3.4, 0.4, 2.4, 19.0)
    return [a, b, c, cap]


# ---------------------------------------------------------------------------------------------
# The vanity and the mirror.
# ---------------------------------------------------------------------------------------------

def vanity():
    """
    A floating vanity, VANITY_W wide and VANITY_D deep, hung on the wall: a cabinet of two drawers
    in deep green lacquer with brass cup pulls, a slab of veined marble on it, a rectangular
    ceramic basin standing on the marble, and a brass bridge mixer behind the basin, two
    crossheads and a swan neck. Origin on the floor under the middle; the wall is at -Y.
    """
    lacquer = Part("Lacquer", bevel=0.5)
    shadow = Part("Shadow")
    marble = Part("Marble", bevel=0.4)
    ceramic = Part("Ceramic", bevel=1.2, segments=3, angle=40.0)
    button = Part("Ceramic")
    brass = Part("Brass", bevel=0.12)
    w, d = VANITY_W, VANITY_D
    face = d * 0.5 - 1.5
    box(lacquer, -w * 0.5, w * 0.5, -d * 0.5, face - 2.0, VANITY_BOTTOM, VANITY_TOP - 4.0)
    for x0, x1 in ((-w * 0.5 + 1.0, -0.4), (0.4, w * 0.5 - 1.0)):
        cbox(shadow, (x0 + x1) * 0.5, face - 2.4, (VANITY_BOTTOM + VANITY_TOP - 4.0) * 0.5, x1 - x0, 0.8, VANITY_TOP - 4.0 - VANITY_BOTTOM - 2.0)
        box(lacquer, x0 + 0.3, x1 - 0.3, face - 2.2, face, VANITY_BOTTOM + 1.0, VANITY_TOP - 5.0)
        # A raised field on each front, its moulding in the same paint.
        cx = (x0 + x1) * 0.5
        box(lacquer, cx - (x1 - x0) * 0.5 + 6.0, cx + (x1 - x0) * 0.5 - 6.0, face, face + 0.6, VANITY_BOTTOM + 7.0, VANITY_TOP - 11.0)
        # The cup pull, a half-moon of brass under a plate.
        cbox(brass, cx, face + 0.9, VANITY_TOP - 14.0, 9.0, 0.6, 2.2)
        tube(brass, [(cx - 4.2 + 8.4 * k / 10, face + 0.6 + 2.6 * math.sin(math.pi * k / 10), VANITY_TOP - 15.2) for k in range(11)], 0.5, sides=6)
    box(marble, -w * 0.5 - 1.5, w * 0.5 + 1.5, -d * 0.5, d * 0.5, VANITY_TOP - 4.0, VANITY_TOP)
    # The basin: a glazed rectangle with rounded corners and a dished inside, standing on the slab.
    bw, bd, bh = 52.0, 36.0, 13.0
    cbox(ceramic, 0.0, 3.0, VANITY_TOP + bh * 0.5, bw, bd, bh)
    cut = cutter_of(ceramic)
    cbox(cut, 0.0, 3.0, VANITY_TOP + bh * 0.5 + 2.0, bw - 4.0, bd - 4.0, bh)
    lathe(shadow, [(0.0, VANITY_TOP + 2.02), (1.8, VANITY_TOP + 2.02), (1.8, VANITY_TOP + 2.15), (0.0, VANITY_TOP + 2.15)], at(0.0, 3.0, 0.0), sides=16)
    lathe(brass, [(1.8, VANITY_TOP + 2.0), (2.5, VANITY_TOP + 2.0), (2.5, VANITY_TOP + 2.3), (1.8, VANITY_TOP + 2.3)], at(0.0, 3.0, 0.0), sides=16)
    # The bridge mixer on the marble behind it.
    my = -d * 0.5 + 6.5
    for x in (-10.0, 10.0):
        lathe(brass, [(0.0, VANITY_TOP), (2.6, VANITY_TOP), (2.6, VANITY_TOP + 0.8), (1.5, VANITY_TOP + 1.6), (1.4, VANITY_TOP + 12.0), (0.0, VANITY_TOP + 12.0)],
              at(x, my, 0.0), sides=18)
        lathe(brass, [(0.0, VANITY_TOP + 12.0), (2.0, VANITY_TOP + 12.0), (2.0, VANITY_TOP + 16.0), (0.0, VANITY_TOP + 16.0)], at(x, my, 0.0), sides=16)
        crosshead(brass, (x, my, VANITY_TOP + 16.0), radius=3.0, turn=12.0 if x < 0 else -6.0, button=button)
    tube(brass, [(-10.0, my, VANITY_TOP + 13.5), (10.0, my, VANITY_TOP + 13.5)], 1.2, sides=12)
    neck = [(0.0, my, VANITY_TOP + 13.5), (0.0, my, VANITY_TOP + 24.0)]
    for k in range(1, 11):
        t = k / 10
        ang = math.pi * t
        neck.append((0.0, my + 9.0 * (1.0 - math.cos(ang)), VANITY_TOP + 24.0 + 6.0 * math.sin(ang)))
    neck += [(0.0, my + 19.0, VANITY_TOP + 21.0)]
    tube(brass, neck, 1.0, sides=12)
    return [lacquer, shadow, marble, ceramic, button, brass]


def mirror():
    """
    The big mirror over the vanity: MIRROR_W x MIRROR_H of glass in a black frame, the frame's
    sides broadened into strips that each carry five brass bulb sockets (the bulbs are their own
    models, so the actor can say which are smashed), a thin brass bead round the glass. Origin on
    the wall plane at the middle of the glass; it faces +Y.
    """
    frame = Part("BlackMetal", bevel=0.4)
    brass = Part("Brass", bevel=0.1)
    glass = Part("Mirror")
    back = Part("Shadow")
    w, h, side, depth = MIRROR_W, MIRROR_H, MIRROR_SIDE, MIRROR_DEPTH
    rail = 4.0
    ow = w + side * 2.0
    box(frame, -ow * 0.5, -w * 0.5, 0.0, depth, -h * 0.5 - rail, h * 0.5 + rail)
    box(frame, w * 0.5, ow * 0.5, 0.0, depth, -h * 0.5 - rail, h * 0.5 + rail)
    box(frame, -w * 0.5, w * 0.5, 0.0, depth, -h * 0.5 - rail, -h * 0.5)
    box(frame, -w * 0.5, w * 0.5, 0.0, depth, h * 0.5, h * 0.5 + rail)
    box(back, -w * 0.5, w * 0.5, 0.2, depth - 1.6, -h * 0.5, h * 0.5)
    box(glass, -w * 0.5, w * 0.5, depth - 1.6, depth - 1.0, -h * 0.5, h * 0.5)
    bead = [(-w * 0.5, -h * 0.5), (w * 0.5, -h * 0.5), (w * 0.5, h * 0.5), (-w * 0.5, h * 0.5), (-w * 0.5, -h * 0.5)]
    tube(brass, [(x, depth + 0.1, z) for x, z in bead], 0.45, sides=8)
    for sx in (-1, 1):
        for z in BULB_ZS:
            lathe_y(brass, [(0.0, 0.0), (2.4, 0.0), (2.4, 0.4), (1.8, 0.8), (1.8, 2.4), (0.0, 2.4)], (sx * BULB_X, depth, z), sides=18)
    return [frame, brass, glass, back]


def bulb_cap(brass):
    """A bulb's screw cap, out of the socket's mouth along +Y."""
    lathe_y(brass, [(0.0, -0.4), (1.45, -0.4), (1.45, 2.4), (1.2, 2.8), (0.0, 2.8)], (0.0, 0.0, 0.0), sides=16)


def bulb_innards(glass, wire, length=7.5):
    """The glass stem up the middle and the filament on its two support wires."""
    lathe_y(glass, [(0.0, 2.6), (0.55, 2.6), (0.35, length - 2.4), (0.6, length - 2.0), (0.0, length - 1.8)], (0.0, 0.0, 0.0), sides=10)
    for sx in (-1, 1):
        tube(wire, [(sx * 0.3, length - 2.2, 0.0), (sx * 1.6, length + 0.6, 0.0), (sx * 1.7, length + 1.4, 0.0)], 0.05, sides=4)
    coil = [(-1.7 + 3.4 * k / 40, length + 1.4 + 0.18 * math.sin(k * 1.7), 0.18 * math.cos(k * 1.7)) for k in range(41)]
    tube(wire, coil, 0.05, sides=4)


GLOBE_R = 4.75
GLOBE_Y = 8.4


def bulb():
    """A globe bulb, G95, dead and dusty: the brass cap, a clear globe, the filament inside."""
    brass = Part("Brass")
    glass = Part("BulbGlass")
    wire = Part("Filament")
    bulb_cap(brass)
    prof = [(0.0, 2.7), (1.5, 2.7), (1.7, 3.4)]
    for k in range(1, 14):
        th = math.radians(18.0 + 162.0 * k / 14)
        prof.append((GLOBE_R * math.sin(th), GLOBE_Y - GLOBE_R * math.cos(th)))
    prof.append((0.0, GLOBE_Y + GLOBE_R))
    lathe_y(glass, prof, (0.0, 0.0, 0.0), sides=28)
    bulb_innards(glass, wire)
    return [brass, glass, wire]


def bulb_broken():
    """A globe bulb with its globe smashed off: the cap, the collar of glass round the neck with a
    ragged edge, the stem standing up out of it with one of its wires bent and the filament gone."""
    brass = Part("Brass")
    glass = Part("BulbGlass")
    wire = Part("Filament")
    bulb_cap(brass)
    m = Matrix.Translation((0.0, GLOBE_Y, 0.0)) @ Matrix.Rotation(math.radians(-90.0), 4, "X")
    jagged_globe(glass, m, GLOBE_R, 0.09, math.radians(19.0), ragged(77, math.radians(62.0), math.radians(16.0)))
    lathe_y(glass, [(0.0, 2.7), (1.5, 2.7), (1.72, 3.6), (1.62, 3.6), (1.4, 2.8), (0.0, 2.8)], (0.0, 0.0, 0.0), sides=20)
    lathe_y(glass, [(0.0, 2.6), (0.55, 2.6), (0.35, 5.1), (0.6, 5.5), (0.0, 5.7)], (0.0, 0.0, 0.0), sides=10)
    tube(wire, [(0.3, 5.3, 0.0), (1.6, 8.1, 0.0), (1.7, 8.9, 0.0)], 0.05, sides=4)
    tube(wire, [(-0.3, 5.3, 0.0), (-1.2, 7.0, 0.3), (-2.6, 7.4, 1.4), (-3.0, 6.6, 2.0)], 0.05, sides=4)
    return [brass, glass, wire]


def bulb_shards():
    """The pieces of the smashed globes, lying where they fell: a dozen curved flakes and two or
    three bigger pieces of a globe's shoulder, scattered across about sixty centimetres."""
    glass = Part("BulbGlass")
    rng = random.Random(81)
    for k in range(14):
        r = rng.uniform(0.0, 30.0) ** 1.0
        a = rng.uniform(0, 2 * math.pi)
        size = rng.uniform(0.6, 1.6) if k > 2 else rng.uniform(2.0, 2.8)
        curved_shard(glass, (math.cos(a) * r, math.sin(a) * r * 0.6, 0.0), size, 1.0 / (2.0 * GLOBE_R), 0.09, 900 + k,
                     flip=rng.random() < 0.5, yaw=rng.uniform(0, 360))
    return [glass]


# ---------------------------------------------------------------------------------------------
# The lamps: the pendant and the sconces over the bath.
# ---------------------------------------------------------------------------------------------

def pendant_fitting(brass, glass, broken):
    lathe(brass, [(0.0, 0.0), (7.0, 0.0), (6.8, -1.2), (4.0, -3.4), (0.0, -3.6)], Matrix.Identity(4), sides=32)
    tube(brass, [(0.0, 0.0, -3.0), (0.0, 0.0, -PENDANT_DROP + 6.0)], 0.7, sides=12)
    # The gallery: a cup the globe's neck was held in by three thumbscrews.
    g = -PENDANT_DROP
    lathe(brass, [(0.0, g + 6.0), (2.2, g + 6.0), (2.4, g + 4.0), (6.4, g + 2.0), (6.6, g), (6.0, g), (5.9, g + 1.8), (2.0, g + 3.6), (0.0, g + 3.6)],
          Matrix.Identity(4), sides=32)
    for k in range(3):
        a = 2 * math.pi * k / 3 + 0.3
        tube(brass, [(math.cos(a) * 5.6, math.sin(a) * 5.6, g + 1.0), (math.cos(a) * 8.0, math.sin(a) * 8.0, g + 1.0)], 0.35, sides=6)
        ellipsoid(brass, (math.cos(a) * 8.2, math.sin(a) * 8.2, g + 1.0), (0.7, 0.7, 0.5), rings=3, sides=8)


def pendant_broken():
    """
    The ceiling light: a brass rod from a rose, a gallery at the bottom, and in it what is left of
    a schoolhouse globe in opal glass — the neck and a ragged collar of the shoulder; the rest is on
    the floor (pendant_shards). Hangs from z = 0.
    """
    brass = Part("Brass")
    opal = Part("Opal")
    pendant_fitting(brass, opal, True)
    # The globe would have been 14 in radius about a point 15 under the gallery.
    g = -PENDANT_DROP
    m = Matrix.Translation((0.0, 0.0, g - 13.0)) @ Matrix.Rotation(math.radians(180.0), 4, "X")
    jagged_globe(opal, m, 13.0, 0.35, math.radians(24.0), ragged(55, math.radians(56.0), math.radians(13.0)), sides=56, rings=12)
    return [brass, opal]


def pendant_shards():
    """The globe's pieces under the light: big curved pieces of opal glass and many small ones,
    the way a globe hits a tiled floor. Scattered across a metre."""
    opal = Part("Opal")
    rng = random.Random(62)
    for k in range(26):
        r = rng.uniform(0.0, 1.0) ** 0.7 * 55.0
        a = rng.uniform(0, 2 * math.pi)
        size = rng.uniform(5.0, 9.0) if k < 5 else rng.uniform(0.8, 3.0)
        curved_shard(opal, (math.cos(a) * r, math.sin(a) * r * 0.75, 0.0), size, 1.0 / 26.0, 0.35, 700 + k,
                     flip=rng.random() < 0.6, yaw=rng.uniform(0, 360))
    return [opal]


def sconce_parts(broken):
    """A wall light over the bath: a brass backplate, a swan arm, a gallery, a tulip shade in opal
    glass with its bulb in it; or the shade gone to a collar and the bulb smashed. Origin on the
    wall plane at the backplate's middle; out of the wall is +Y."""
    brass = Part("Brass", bevel=0.1)
    opal = Part("Opal")
    glass = Part("BulbGlass")
    wire = Part("Filament")
    lathe_y(brass, [(0.0, 0.0), (5.5, 0.0), (5.4, 0.8), (3.0, 1.8), (1.4, 2.4), (0.0, 2.4)], (0.0, 0.0, 0.0), sides=28)
    arm = [(0.0, 2.0, 0.0), (0.0, 8.0, -1.0), (0.0, 13.0, 2.0), (0.0, 15.0, 7.0), (0.0, 15.0, 9.0)]
    tube(brass, K.catmull(arm, 5), 0.6, sides=10)
    lathe(brass, [(0.0, 9.0), (1.8, 9.0), (2.2, 11.0), (3.6, 12.0), (3.6, 13.0), (0.0, 13.0)], at(0.0, 15.0, 0.0), sides=20)
    # The tulip: opens upward from the gallery.
    m = Matrix.Translation((0.0, 15.0, 22.0))
    if broken:
        jagged_globe(opal, m, 9.0, 0.25, math.radians(28.0), ragged(33, math.radians(58.0), math.radians(12.0)), sides=40, rings=8)
    else:
        jagged_globe(opal, m, 9.0, 0.25, math.radians(28.0), lambda phi: math.radians(118.0 + 2.0 * math.sin(phi * 6.0)), sides=40, rings=10)
    # Its bulb, upright in the gallery.
    up = Matrix.Translation((0.0, 15.0, 13.0)) @ Matrix.Rotation(math.radians(90.0), 4, "X")
    start_b = len(brass.bm.verts)
    bulb_cap(brass)
    brass.bm.verts.ensure_lookup_table()
    bmesh.ops.transform(brass.bm, matrix=up, verts=brass.bm.verts[start_b:])
    start_g = len(glass.bm.verts)
    start_w = len(wire.bm.verts)
    if broken:
        jagged_globe(glass, Matrix.Translation((0.0, 0.0, GLOBE_Y)), 3.2, 0.08, math.radians(22.0), ragged(41, math.radians(70.0), math.radians(18.0)), sides=28, rings=6)
        lathe(glass, [(0.0, 2.6), (0.5, 2.6), (0.35, 5.0), (0.55, 5.4), (0.0, 5.6)], Matrix.Identity(4), sides=10)
        tube(wire, [(0.3, 0.0, 5.2), (1.4, 0.0, 7.6), (1.0, 0.6, 8.4)], 0.05, sides=4)
    else:
        prof = [(0.0, 2.7), (1.3, 2.7)]
        for k in range(1, 12):
            th = math.radians(24.0 + 156.0 * k / 12)
            prof.append((3.2 * math.sin(th), GLOBE_Y - 3.2 * math.cos(th)))
        prof.append((0.0, GLOBE_Y + 3.2))
        lathe(glass, prof, Matrix.Identity(4), sides=20)
    glass.bm.verts.ensure_lookup_table()
    bmesh.ops.transform(glass.bm, matrix=Matrix.Translation((0.0, 15.0, 13.0)), verts=glass.bm.verts[start_g:])
    wire.bm.verts.ensure_lookup_table()
    bmesh.ops.transform(wire.bm, matrix=Matrix.Translation((0.0, 15.0, 13.0)), verts=wire.bm.verts[start_w:])
    return [brass, opal, glass, wire]


def sconce():
    return sconce_parts(False)


def sconce_broken():
    return sconce_parts(True)


# ---------------------------------------------------------------------------------------------
# The WC.
# ---------------------------------------------------------------------------------------------

def toilet():
    """
    A Victorian closet: a glazed pan on its pedestal, a mahogany seat with the lid up, the flush pipe
    from the back of the pan up the wall to a cast-iron cistern on two scrolled brackets, and its
    chain down from the lever with a porcelain pull. The wall is WC_BACK behind the origin (-Y).
    """
    ceramic = Part("Ceramic")
    wood = Part("Wood", bevel=0.4)
    brass = Part("Brass")
    iron = Part("CastIron", bevel=0.3)
    water = Part("Water")
    a, b, n = 19.0, 27.0, 2.4
    prof = [(None, 0.0, 0.0), (-8.0, 0.0, 0.0), (-7.0, 1.6, 0.0), (-8.5, 3.0, 0.0), (-11.0, 8.0, -1.0), (-11.5, 16.0, -1.0), (-9.0, 24.0, 2.0),
            (-4.0, 31.0, 4.0), (-1.0, 37.0, 5.0), (0.0, 40.0, 5.0), (-1.0, 41.0, 5.0), (-3.2, 40.5, 5.0), (-5.0, 35.0, 5.5),
            (-9.0, 28.0, 7.0), (-14.0, 22.0, 9.0), (None, 20.0, 10.0)]
    plan_loft(ceramic, prof, a, b, n, sides=56)
    # The back of the pan, where the flush pipe goes in, and the water left in the trap.
    tube(ceramic, [(0.0, -18.0, 34.0), (0.0, -24.0, 36.5)], 3.2, sides=16)
    plan_slab(water, a - 13.5, b - 13.0, n, 23.0, 23.3, sides=40, cy=9.5)
    # The seat, the hinge bar, and the lid standing up against the pipe: turned up from flat by
    # LID_UP, just past square, so its back face at the top touches the pipe's front (wall + 4 - 2)
    # and goes no further. At 100 degrees it leaned back through the pipe.
    plan_ring(wood, a + 0.5, b + 0.5, n, 6.5, 41.2, WC_SEAT, sides=56, cy=5.0)
    for x in (-8.0, 8.0):
        tube(brass, [(x, -21.5, 42.0), (x, -24.0, 43.0)], 0.9, sides=10)
    tube(brass, [(-9.0, -23.6, 43.0), (9.0, -23.6, 43.0)], 0.6, sides=8)
    lid = Part("Wood", bevel=0.4)
    plan_slab(lid, a + 0.2, b, n, 0.0, 1.8, sides=56)
    lid.bm.verts.ensure_lookup_table()
    bmesh.ops.transform(lid.bm, matrix=at(0.0, -25.5, 43.0) @ Matrix.Rotation(math.radians(LID_UP), 4, "X") @ Matrix.Translation((0.0, b, 0.0)),
                        verts=lid.bm.verts)
    # The flush pipe: up the wall a hand off it into the cistern's bottom.
    wall = -WC_BACK
    tube(brass, K.catmull([(0.0, -24.0, 36.5), (0.0, -28.0, 38.0), (0.0, wall + 4.0, 44.0), (0.0, wall + 4.0, 60.0)], 4) + [(0.0, wall + 4.0, 188.0)], 2.0, sides=14)
    for z in (90.0, 150.0):
        lathe_y(brass, [(0.0, 0.0), (1.4, 0.0), (1.4, 4.0), (0.0, 4.0)], (0.0, wall, z), sides=12)
        tube(brass, [(-2.4, wall + 4.0, z), (2.4, wall + 4.0, z)], 2.4, sides=12)
    # The cistern: a cast box with a moulded front, on two brackets.
    cw, cd, cz0, cz1 = 46.0, 21.0, 190.0, 216.0
    box(iron, -cw * 0.5, cw * 0.5, wall, wall + cd, cz0, cz1)
    box(iron, -cw * 0.5 - 1.2, cw * 0.5 + 1.2, wall, wall + cd + 1.2, cz1, cz1 + 2.2)
    box(iron, -cw * 0.5 + 4.0, cw * 0.5 - 4.0, wall + cd, wall + cd + 0.8, cz0 + 4.0, cz1 - 4.0)
    for k in range(5):
        x = -cw * 0.5 + 8.0 + k * (cw - 16.0) / 4
        cbox(iron, x, wall + cd + 1.1, (cz0 + cz1) * 0.5, 2.4, 0.8, cz1 - cz0 - 10.0)
    for sx in (-1, 1):
        x = sx * (cw * 0.5 - 7.0)
        pts = [(wall, cz0), (wall + cd - 2.0, cz0), (wall + cd - 2.0, cz0 - 2.5)]
        for k in range(1, 9):
            ang = math.pi * 0.5 * k / 8
            pts.append((wall + cd - 2.0 - (cd - 2.0) * (1.0 - math.cos(ang)), cz0 - 2.5 - 16.0 * math.sin(ang)))
        bm = iron.bm
        near = [bm.verts.new((x - 1.0, y, z)) for y, z in pts]
        far = [bm.verts.new((x + 1.0, y, z)) for y, z in pts]
        bm.faces.new(near[::-1])
        bm.faces.new(far)
        for i in range(len(pts)):
            j = (i + 1) % len(pts)
            bm.faces.new((near[i], near[j], far[j], far[i]))
        ellipsoid(iron, (x, wall + cd - 4.0, cz0 - 6.0), (1.4, 2.2, 2.2), rings=4, sides=8)
    # The lever off the right-hand end and the chain down from it, the pull on its end.
    tube(iron, [(cw * 0.5, wall + 10.0, 210.0), (cw * 0.5 + 6.0, wall + 16.0, 208.0), (cw * 0.5 + 9.0, wall + 26.0, 206.0)], 0.9, sides=8)
    top = Vector((cw * 0.5 + 9.0, wall + 26.0, 205.0))
    links = [top + Vector((0.0, 0.0, -k * 1.6)) for k in range(52)]
    for p in links:
        ellipsoid(brass, tuple(p), (0.35, 0.35, 0.55), rings=3, sides=6)
    tube(brass, [tuple(p) for p in links], 0.12, sides=4)
    end = links[-1]
    lathe(ceramic, [(0.0, -8.0), (1.6, -7.8), (2.2, -6.0), (2.0, -3.0), (1.0, -0.6), (0.4, 0.0), (0.0, 0.2)], at(end.x, end.y, end.z), sides=16)
    return [ceramic, wood, lid, brass, iron, water]


def roll_holder():
    """A brass toilet-roll holder: a round plate on the wall, an arm, the roll on it, its end hanging
    down a hand's length. Origin on the wall plane; out of the wall is +Y."""
    brass = Part("Brass", bevel=0.1)
    paper = Part("Paper", smooth=True)
    lathe_y(brass, [(0.0, 0.0), (3.6, 0.0), (3.4, 0.6), (1.4, 1.4), (0.0, 1.4)], (0.0, 0.0, 0.0), sides=24)
    tube(brass, [(0.0, 1.0, 0.0), (0.0, 7.0, 0.0), (0.0, 8.0, -1.0), (-14.0, 8.0, -1.0)], 0.5, sides=10)
    lathe(paper, [(0.0, -5.4), (2.2, -5.4), (5.4, -5.4), (5.6, -5.0), (5.6, 5.0), (5.4, 5.4), (2.2, 5.4), (0.0, 5.4)], at(-7.4, 8.0, -1.0, pitch=90.0), sides=32)
    path = [(8.0 + 5.4 * math.cos(math.radians(a)), -1.0 + 5.4 * math.sin(math.radians(a))) for a in range(90, -1, -15)]
    path += [(13.6, -6.0), (13.7, -14.0), (13.5, -20.0)]
    strip = Part("Paper")
    drape(strip, 10.0, path[::-1], 0.12, 5, wave=0.15, wave_len=6.0, nx=8)
    strip.bm.verts.ensure_lookup_table()
    bmesh.ops.transform(strip.bm, matrix=Matrix.Translation((-7.4, 0.0, 0.0)), verts=strip.bm.verts)
    return [brass, paper, strip]


# ---------------------------------------------------------------------------------------------
# The towel rail, the towels, the chair, the fern.
# ---------------------------------------------------------------------------------------------

def towel_rail():
    """
    A floor-standing brass towel rail of the kind that ran off the hot water: two posts on scrolled
    feet, three bars between them, a ball on each post, the pipes into the floor under one end.
    RAIL_W between the posts' middles; the bars along X.
    """
    brass = Part("Brass", bevel=0.1)
    for sx in (-1, 1):
        x = sx * RAIL_W * 0.5
        tube(brass, [(x, 0.0, 6.0), (x, 0.0, 96.0)], 1.5, sides=16)
        ellipsoid(brass, (x, 0.0, 97.6), (2.4, 2.4, 2.4), rings=8, sides=16)
        lathe(brass, [(0.0, 93.0), (2.0, 93.0), (2.0, 95.0), (0.0, 95.0)], at(x, 0.0, 0.0), sides=16)
        for sy in (-1, 1):
            foot = K.catmull([(x, 0.0, 9.0), (x, sy * 6.0, 4.0), (x, sy * 12.0, 1.4), (x, sy * 15.0, 2.4), (x, sy * 14.0, 3.6)], 4)
            tube(brass, foot, 1.0, sides=10)
        lathe(brass, [(0.0, 6.0), (2.4, 6.0), (2.4, 10.0), (0.0, 10.0)], at(x, 0.0, 0.0), sides=16)
    for z in RAIL_BARS:
        tube(brass, [(-RAIL_W * 0.5, 0.0, z), (RAIL_W * 0.5, 0.0, z)], RAIL_TUBE, sides=14)
    # The flow and return into the floor under the left post.
    tube(brass, [(-RAIL_W * 0.5, 0.0, 7.0), (-RAIL_W * 0.5 - 4.0, 0.0, 5.0), (-RAIL_W * 0.5 - 5.0, 0.0, 0.0), (-RAIL_W * 0.5 - 5.0, 0.0, -2.0)], 0.9, sides=10)
    return [brass]


def towel_big():
    """A bath towel thrown over the top bar of the rail, longer at the front, the hems uneven."""
    towel = Part("Towel")
    drape(towel, 58.0, over_rail(RAIL_TUBE + 0.6, 62.0, 48.0, splay=0.5), 1.2, 31, wave=1.4, wave_len=15.0, nx=30)
    return [towel]


def towel_hand():
    """A hand towel folded in three over the middle bar."""
    towel = Part("Towel2")
    drape(towel, 26.0, over_rail(RAIL_TUBE + 0.5, 24.0, 22.0, splay=0.2), 1.0, 33, wave=0.7, wave_len=9.0, nx=16)
    return [towel]


def towel_on_bath():
    """A towel left over the side of the bath: one end in the bath and dragging in the water, the
    rest down the outside nearly to the floor. Hangs over a roll of BATH_RIM's radius at the origin
    along X; +Y is the outside."""
    towel = Part("Towel")
    path = over_rail(1.6, 44.0, 32.0, splay=0.9)
    # Inside, the end lies along the curve of the enamel and into the water: bend the back hem in.
    bent = []
    for y, z in path:
        if y < 0.0 and z < -10.0:
            t = (-10.0 - z) / 22.0
            y = y - 6.0 * t * t
        bent.append((y, z))
    drape(towel, 54.0, bent, 1.1, 35, wave=1.1, wave_len=14.0, nx=28)
    return [towel]


def towel_folded():
    """A towel folded in four on the chair's seat."""
    towel = Part("Towel2", smooth=True)
    cushion(towel, (0.0, 0.0, 3.0), (32.0, 24.0, 6.0), round_v=0.35, round_h=0.15, pinch=0.08)
    cushion(towel, (0.0, 11.6, 3.0), (32.5, 3.6, 6.2), round_v=0.8, round_h=0.2)
    return [towel]


def chair():
    """
    A bentwood chair, the cheap one every café had: a round cane seat in a bent hoop, four legs
    splayed a little and braced by a ring, the back legs running on up into the back's loop with a
    smaller loop inside it. Faces +Y.
    """
    wood = Part("Bentwood")
    cane = Part("Cane")
    r = 21.0
    seat = [(math.cos(2 * math.pi * k / 48) * r, math.sin(2 * math.pi * k / 48) * r, CHAIR_SEAT) for k in range(49)]
    tube(wood, seat, 1.6, sides=10)
    lathe(cane, [(0.0, CHAIR_SEAT - 0.6), (r - 0.8, CHAIR_SEAT - 0.6), (r - 0.8, CHAIR_SEAT + 0.4), (0.0, CHAIR_SEAT + 0.2)], Matrix.Identity(4), sides=40)
    ring = [(math.cos(2 * math.pi * k / 40) * 17.5, math.sin(2 * math.pi * k / 40) * 17.5, 24.0) for k in range(41)]
    tube(wood, ring, 0.9, sides=8)
    for ang in (60.0, 120.0):
        a = math.radians(ang)
        x, y = math.cos(a) * (r - 3.0), math.sin(a) * (r - 3.0)
        tube(wood, [(x * 1.12, y * 1.12, 0.0), (x, y, CHAIR_SEAT - 1.0)], [1.2, 1.6], sides=10)
    # The back legs: up from the floor behind, through the seat's hoop, on up and round into the loop.
    for sx in (-1, 1):
        a = math.radians(-90.0 + sx * 34.0)
        x, y = math.cos(a) * (r - 1.0), math.sin(a) * (r - 1.0)
        path = [(x * 1.14, y * 1.10, 0.0), (x, y, CHAIR_SEAT), (x * 0.92, y - 3.0, 70.0), (x * 0.70, y - 4.5, 86.0)]
        tube(wood, K.catmull(path, 6), [1.3] * len(K.catmull(path, 6)), sides=10)
    top = []
    for k in range(25):
        t = math.pi * k / 24
        top.append((-math.cos(t) * 11.6, -r - 2.5 - 1.5 * math.sin(t), 86.0 + 9.0 * math.sin(t)))
    tube(wood, top, 1.3, sides=10)
    inner = []
    for k in range(33):
        t = 2 * math.pi * k / 32
        inner.append((math.cos(t) * 7.0, -r - 2.0, 74.0 + math.sin(t) * 9.0))
    tube(wood, inner, 0.8, sides=8)
    return [wood, cane]


def leaflet(part, origin, direction, length, width, droop, seed):
    """One leaflet of a fern frond: a narrow blade, curled, a thin solid on a 4 x 3 grid."""
    rng = random.Random(seed)
    d = Vector(direction).normalized()
    side = d.cross(Vector((0, 0, 1)))
    if side.length < 1e-3:
        side = Vector((1, 0, 0))
    side.normalize()
    up = side.cross(d).normalized()
    grid = []
    for i in range(4):
        t = i / 3
        half = width * 0.5 * (1.0 - t * 0.85)
        col = []
        for j in range(3):
            s = -1.0 + j
            p = Vector(origin) + d * (t * length) + side * (s * half) + up * (0.4 * s * s * half - droop * t * t * length + rng.uniform(-0.05, 0.05))
            col.append(p)
        grid.append(col)
    solid_grid(part, grid, 0.06)


def fern():
    """
    A Boston fern that died on its stand: a tall plant stand in dark wood (a round top on a turned
    column and three splayed feet), a glazed pot on it, and the fronds gone brown and hanging down
    round the pot in long arcs, half of their leaflets dropped and lying on the floor. Origin on
    the floor under the stand.
    """
    wood = Part("Bentwood", bevel=0.3)
    pot = Part("Pot")
    soil = Part("Soil")
    stem = Part("Stem")
    leaf = Part("Frond")
    top = 82.0
    lathe(wood, [(0.0, top - 2.5), (16.0, top - 2.5), (16.6, top - 1.2), (16.0, top), (0.0, top)], Matrix.Identity(4), sides=40)
    lathe(wood, [(0.0, 12.0), (3.8, 12.0), (3.0, 20.0), (2.2, 40.0), (3.4, 52.0), (2.0, 64.0), (2.6, 74.0), (4.4, top - 2.5), (0.0, top - 2.5)],
          Matrix.Identity(4), sides=20)
    for k in range(3):
        a = 2 * math.pi * k / 3
        tube(wood, K.catmull([(0.0, 0.0, 16.0), (math.cos(a) * 9.0, math.sin(a) * 9.0, 9.0), (math.cos(a) * 16.0, math.sin(a) * 16.0, 1.4),
                               (math.cos(a) * 18.0, math.sin(a) * 18.0, 1.4)], 4), 1.4, sides=10)
    lathe(pot, [(0.0, top), (10.0, top), (10.4, top + 0.8), (14.0, top + 20.0), (14.8, top + 21.0), (14.8, top + 22.0), (13.6, top + 22.0), (12.9, top + 20.4),
                (9.4, top + 2.0), (0.0, top + 2.0)], Matrix.Identity(4), sides=36)
    lathe(soil, [(0.0, top + 17.0), (13.2, top + 17.0), (13.3, top + 17.6), (0.0, top + 18.0)], Matrix.Identity(4), sides=28)
    rng = random.Random(91)
    base_z = top + 18.0
    for f in range(16):
        a = 2 * math.pi * f / 16 + rng.uniform(-0.2, 0.2)
        reach = rng.uniform(30.0, 46.0)
        rise = rng.uniform(9.0, 17.0)
        fall = rng.uniform(30.0, 70.0)
        out = Vector((math.cos(a), math.sin(a), 0.0))
        # Up out of the soil until clear of the rim (radius CLEAR), over it in a short arc, then down
        # outside the pot. The first version bent over at once and every frond went out through the
        # side of the pot under its rim.
        clear = 19.0
        path = []
        for k in range(12):
            r = 3.0 + reach * k / 11
            if r < clear:
                h = rise * math.sqrt((r - 3.0) / (clear - 3.0))
            else:
                u = (r - clear) / (reach + 3.0 - clear)
                h = rise + 3.0 * math.sin(min(u * 3.0, 1.0) * math.pi * 0.5) - fall * u ** 2.2
            p = Vector((0.0, 0.0, base_z)) + out * r + Vector((0.0, 0.0, h))
            path.append(tuple(p))
        tube(stem, path, [0.32 - 0.22 * k / 11 for k in range(12)], sides=5)
        # Leaflets from the fourth point on, past the rim: nearer in they drooped into the pot's wall.
        for k in range(3, 11):
            if rng.random() < 0.45:
                continue
            p0, p1 = Vector(path[k]), Vector(path[min(k + 1, 11)])
            along = (p1 - p0).normalized()
            side = along.cross(Vector((0, 0, 1))).normalized()
            for s in (-1, 1):
                if rng.random() < 0.3:
                    continue
                length = 6.0 * (1.0 - k / 13)
                leaflet(leaf, tuple(p0), tuple(side * s + along * 0.35 + Vector((0, 0, -0.5))), length, 1.4, rng.uniform(0.1, 0.3), f * 40 + k * 2 + (s > 0))
    for k in range(40):
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(14.0, 60.0)
        leaflet(leaf, (math.cos(a) * r, math.sin(a) * r, 0.2), (math.cos(a + 1.4), math.sin(a + 1.4), 0.0), rng.uniform(2.5, 5.0), 1.2, -0.01, 2000 + k)
    return [wood, pot, soil, stem, leaf]


# ---------------------------------------------------------------------------------------------
# Small things: on the bath, on the vanity, on the floor.
# ---------------------------------------------------------------------------------------------

def bath_caddy():
    """
    A brass bath rack across the bath: two rails with hooked ends over the rolls, a grid of slats,
    a soap well; on it a natural sponge gone hard, a bar of soap cracked dry, a nail brush and a
    candle burnt to a stub in its saucer. Origin at the middle of its length at the top of the roll;
    the length is local X (BATH_WIDTH across, and a little more for the hooks).
    """
    brass = Part("Brass")
    sponge = Part("Sponge", smooth=True)
    soap = Part("Soap", bevel=0.8, segments=3)
    wood = Part("Wood", bevel=0.3)
    bristle = Part("Bristle")
    wax = Part("Wax")
    ceramic = Part("Ceramic")
    half = BATH_B + 1.5
    for y in (-6.5, 6.5):
        path = [(-half - 0.6, y, -5.0), (-half - 0.8, y, -1.0), (-half + 0.6, y, 0.6), (half - 0.6, y, 0.6), (half + 0.8, y, -1.0), (half + 0.6, y, -5.0)]
        tube(brass, K.catmull(path, 4), 0.45, sides=8)
    for k in range(-14, 15):
        x = k * 2.4
        tube(brass, [(x, -6.5, 0.6), (x, 6.5, 0.6)], 0.28, sides=6)
    # The soap well: a dished ring of wire at one end.
    for z, r in ((0.6, 5.6), (-1.4, 4.4)):
        tube(brass, [(-22.0 + math.cos(2 * math.pi * k / 24) * r, math.sin(2 * math.pi * k / 24) * r, z) for k in range(25)], 0.3, sides=6)
    cbox(soap, -22.0, 0.0, 0.6, 8.0, 5.0, 2.2, matrix=Matrix.Rotation(math.radians(14.0), 4, "Z"))
    blob(sponge, [((10.0, 0.5, 4.0), (5.6, 4.6, 3.6), None), ((13.5, 1.8, 4.6), (3.2, 2.8, 2.6), None), ((7.0, -1.5, 3.4), (3.0, 2.6, 2.2), None)], resolution=0.8)
    box(wood, 22.0, 32.0, -3.0, 0.4, 1.0, 2.6)
    for i in range(6):
        for j in range(2):
            cbox(bristle, 23.0 + i * 1.6, -2.2 + j * 1.6, 3.4, 0.9, 0.9, 1.8)
    lathe(ceramic, [(0.0, 0.9), (3.6, 0.9), (4.2, 1.8), (3.8, 2.0), (3.2, 1.4), (0.0, 1.4)], at(-6.0, 0.0, 0.0), sides=24)
    lathe(wax, [(0.0, 1.4), (2.0, 1.4), (2.6, 1.8), (2.1, 2.4), (1.8, 4.6), (1.4, 5.0), (0.0, 4.9)], at(-6.0, 0.0, 0.0), sides=20)
    return [brass, sponge, soap, wood, bristle, wax, ceramic]


def duck():
    """The girl's rubber duck, yellow gone to the colour of old teeth, sitting on the rim. 9 long."""
    body = Part("Duck", smooth=True)
    beak = Part("Beak", smooth=True)
    eye = Part("Black")
    blob(body, [((0.0, 0.0, 2.6), (4.6, 3.6, 2.6), None), ((-3.4, 0.0, 3.6), (1.8, 2.2, 1.8), None), ((2.3, 0.0, 5.6), (2.3, 2.2, 2.3), None)], resolution=0.4)
    ellipsoid(beak, (4.7, 0.0, 5.2), (1.4, 1.1, 0.55), rings=6, sides=12)
    for sy in (-1, 1):
        ellipsoid(eye, (3.7, sy * 1.25, 6.4), (0.35, 0.3, 0.35), rings=4, sides=8)
    return [body, beak, eye]


def tooth_glass():
    """A glass tumbler on the vanity with three toothbrushes in it: his, hers, and a small one in
    pink with a cartoon handle worn smooth. Origin on the glass's foot."""
    glass = Part("Glass")
    a, b, c = Part("BrushA"), Part("BrushB"), Part("BrushC")
    bristle = Part("Bristle")
    prof = [(0.0, 0.0), (3.0, 0.0), (3.6, 10.5), (3.45, 10.5), (2.85, 0.9), (0.0, 0.9)]
    lathe(glass, prof, Matrix.Identity(4), sides=32)
    for part, lean, turn, length, head in ((a, 14.0, 20.0, 19.0, 2.6), (b, 12.0, 150.0, 19.0, 2.6), (c, 16.0, 265.0, 14.0, 2.0)):
        m = Matrix.Rotation(math.radians(turn), 4, "Z") @ Matrix.Rotation(math.radians(lean), 4, "Y")
        foot = Vector((0.0, 0.0, 1.0))
        tip = foot + m @ Vector((0.0, 0.0, length))
        tube(part, [tuple(foot), tuple(foot + (tip - foot) * 0.75), tuple(tip)], [0.45, 0.35, 0.4], sides=8)
        start = len(bristle.bm.verts)
        cbox(bristle, 0.0, 0.55, length - head * 0.5, 1.0, 1.0, head)
        bristle.bm.verts.ensure_lookup_table()
        bmesh.ops.transform(bristle.bm, matrix=Matrix.Translation(foot) @ m @ Matrix.Translation((0.0, 0.0, -1.0)) @ Matrix.Translation((0.0, 0.0, 1.0)),
                            verts=bristle.bm.verts[start:])
    return [glass, a, b, c, bristle]


def shaving_set():
    """His shaving things on a chrome stand: a safety razor hanging from one arm, a badger brush
    upside down in the other, the stand's foot a dish. Origin on the vanity."""
    chrome = Part("Chrome", bevel=0.1)
    handle = Part("Black", bevel=0.2)
    badger = Part("Badger", smooth=True)
    lathe(chrome, [(0.0, 0.0), (4.4, 0.0), (4.6, 0.6), (4.2, 1.0), (0.6, 1.0), (0.6, 14.0), (0.0, 14.0)], Matrix.Identity(4), sides=28)
    tube(chrome, [(0.0, 0.0, 13.0), (0.0, 3.2, 13.0)], 0.3, sides=6)
    tube(chrome, [(-2.0, 3.2, 13.0), (2.0, 3.2, 13.0)], 0.3, sides=6)
    tube(chrome, [(0.0, 0.0, 12.0), (0.0, -3.6, 12.0)], 0.3, sides=6)
    tube(chrome, [(-1.4, -3.6, 12.0), (1.4, -3.6, 12.0)], 0.3, sides=6)
    # The razor hangs head up between its arm's prongs.
    tube(chrome, [(0.0, 3.2, 13.5), (0.0, 3.2, 3.0)], 0.5, sides=10)
    cbox(chrome, 0.0, 3.2, 14.2, 4.2, 1.4, 0.8)
    # The brush hangs bristles down.
    lathe(handle, [(0.0, 12.6), (1.3, 12.6), (1.6, 10.6), (1.3, 8.0), (1.5, 7.0), (0.0, 7.0)], at(0.0, -3.6, 0.0), sides=18)
    blob(badger, [((0.0, -3.6, 5.2), (1.9, 1.9, 2.6), None), ((0.0, -3.6, 3.6), (1.5, 1.5, 1.6), None)], resolution=0.3)
    return [chrome, handle, badger]


def soap_dish():
    """A ceramic soap dish with a cake of soap gone to cracks in it. 13 x 9."""
    ceramic = Part("Ceramic", bevel=0.4)
    soap = Part("Soap", bevel=0.7, segments=3)
    plan_loft(ceramic, [(None, 0.0), (-1.6, 0.0), (-0.6, 0.6), (0.0, 1.8), (0.0, 2.2), (-0.5, 2.2), (-1.0, 1.4), (-1.6, 1.0), (None, 0.9)], 6.5, 4.5, 2.4, sides=40)
    cbox(soap, 0.0, 0.0, 2.0, 8.0, 5.0, 2.0, matrix=Matrix.Rotation(math.radians(-8.0), 4, "Z"))
    return [ceramic, soap]


def bottles():
    """Her things on the vanity: a cut-glass scent bottle with a ball stopper, a cream jar with a
    brass lid, an apothecary bottle of something brown. Origin on the vanity at the group's middle."""
    glass = Part("Glass")
    liquid = Part("Liquid")
    brass = Part("Brass")
    ceramic = Part("Ceramic")
    lathe(glass, [(0.0, 0.0), (3.2, 0.0), (3.6, 1.0), (3.6, 7.0), (2.4, 8.4), (0.8, 9.0), (0.8, 10.4), (0.0, 10.4)], at(-6.0, 0.0, 0.0), sides=8)
    lathe(liquid, [(0.0, 0.6), (3.0, 0.6), (3.2, 2.6), (0.0, 2.6)], at(-6.0, 0.0, 0.0), sides=8)
    ellipsoid(glass, (-6.0, 0.0, 12.2), (1.8, 1.8, 1.8), rings=6, sides=10)
    lathe(ceramic, [(0.0, 0.0), (3.0, 0.0), (3.2, 0.8), (3.2, 4.4), (0.0, 4.4)], at(2.0, 3.0, 0.0), sides=28)
    lathe(brass, [(0.0, 4.2), (3.4, 4.2), (3.4, 5.4), (2.4, 5.8), (0.0, 5.9)], at(2.0, 3.0, 0.0), sides=28)
    lathe(glass, [(0.0, 0.0), (2.4, 0.0), (2.6, 0.8), (2.6, 10.0), (1.4, 12.0), (0.9, 13.4), (1.1, 14.0), (0.0, 14.0)], at(6.5, -2.0, 0.0), sides=20)
    lathe(liquid, [(0.0, 0.4), (2.3, 0.4), (2.4, 5.0), (0.0, 5.0)], at(6.5, -2.0, 0.0), sides=16)
    lathe(brass, [(0.0, 13.6), (1.0, 13.6), (1.0, 15.0), (0.0, 15.0)], at(6.5, -2.0, 0.0), sides=12)
    return [glass, liquid, brass, ceramic]


def tile_pieces():
    """Wall tiles that came off the wall and broke on the floor: three whole, four halves and
    corners, every one bevelled and glazed, a couple lying face down on their unglazed biscuit
    (black backs read as dominoes, the cellar's 10-05 note). Spread over about forty centimetres."""
    glaze = Part("Glaze", bevel=0.25)
    back = Part("Biscuit")
    rng = random.Random(23)
    for k in range(7):
        w = 15.0 if k < 3 else rng.uniform(5.0, 10.0)
        h = 7.5 if k < 3 else rng.uniform(4.0, 7.5)
        x, y = rng.uniform(-20.0, 20.0), rng.uniform(-14.0, 14.0)
        yaw = rng.uniform(0.0, 180.0)
        flip = rng.random() < 0.3
        m = at(x, y, 0.45, yaw=yaw)
        cbox(glaze, 0.0, 0.0, 0.0, w, h, 0.9, matrix=m)
        if flip:
            cbox(back, 0.0, 0.0, 0.46, w - 0.3, h - 0.3, 0.02, matrix=m)
    return [glaze, back]


# ---------------------------------------------------------------------------------------------
# The tiled walls' trim.
# ---------------------------------------------------------------------------------------------

def moulding_run(slot, profile):
    """A straight run 100 long along X, its profile (out, up) in YZ from the wall (y = 0) into the
    room; the actor lays it end to end and scales X to the length (as the bedroom's mouldings)."""
    part = Part(slot)
    points = [(-50.0, y, z) for y, z in profile]
    bm = part.bm
    left = [bm.verts.new(p) for p in points]
    right = [bm.verts.new((50.0, p[1], p[2])) for p in points]
    n = len(points)
    for k in range(n):
        j = (k + 1) % n
        bm.faces.new((left[k], right[k], right[j], left[j]))
    bm.faces.new(left[::-1])
    bm.faces.new(right)
    return [part]


def tile_cap():
    """The glazed capping along the top of the tiles: 5 tall, 2.2 proud, a round over a step."""
    return moulding_run("Glaze", [(0.0, 0.0), (0.0, 5.0), (0.6, 5.0), (1.5, 4.6), (2.1, 3.7), (2.2, 2.6), (1.9, 1.6), (1.6, 1.3), (1.6, 0.0)])


def tile_pencil():
    """The black liner across the tiles, a round-faced pencil 2.4 tall, 1.6 proud."""
    return moulding_run("GlazeBlack", [(0.0, 0.0), (0.0, 2.4), (0.8, 2.4), (1.4, 2.0), (1.6, 1.2), (1.4, 0.4), (0.8, 0.0)])


def tile_skirt():
    """The black skirting tile at the foot of the wall: 13 tall with a cove into the floor."""
    return moulding_run("GlazeBlack", [(0.0, 0.0), (0.0, 13.0), (0.9, 13.0), (1.4, 12.4), (1.6, 11.2), (1.6, 3.0), (2.0, 1.4), (3.0, 0.4), (3.6, 0.0)])


MODELS = [
    ("bathroom_bath", bath),
    ("bathroom_bath_filler", bath_filler),
    ("bathroom_shower_screen", shower_screen),
    ("bathroom_shower_set", shower_set),
    ("bathroom_shower_drain", shower_drain),
    ("bathroom_shower_bottles", shower_bottles),
    ("bathroom_vanity", vanity),
    ("bathroom_mirror", mirror),
    ("bathroom_bulb", bulb),
    ("bathroom_bulb_broken", bulb_broken),
    ("bathroom_bulb_shards", bulb_shards),
    ("bathroom_pendant", pendant_broken),
    ("bathroom_pendant_shards", pendant_shards),
    ("bathroom_sconce", sconce),
    ("bathroom_sconce_broken", sconce_broken),
    ("bathroom_toilet", toilet),
    ("bathroom_roll_holder", roll_holder),
    ("bathroom_towel_rail", towel_rail),
    ("bathroom_towel_big", towel_big),
    ("bathroom_towel_hand", towel_hand),
    ("bathroom_towel_bath", towel_on_bath),
    ("bathroom_towel_folded", towel_folded),
    ("bathroom_chair", chair),
    ("bathroom_fern", fern),
    ("bathroom_caddy", bath_caddy),
    ("bathroom_duck", duck),
    ("bathroom_tooth_glass", tooth_glass),
    ("bathroom_shaving_set", shaving_set),
    ("bathroom_soap_dish", soap_dish),
    ("bathroom_bottles", bottles),
    ("bathroom_tile_pieces", tile_pieces),
    ("bathroom_tile_cap", tile_cap),
    ("bathroom_tile_pencil", tile_pencil),
    ("bathroom_tile_skirt", tile_skirt),
]


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
        print("BATHROOM baked {} ({} verts, {:.1f} x {:.1f} x {:.1f} cm)".format(name, len(obj.data.vertices), dims.x * 100, dims.y * 100, dims.z * 100))

    big = [o for o in baked if max(o.dimensions) > 0.6]
    small = [o for o in baked if o not in big]
    x = 0.0
    for obj in big:
        width = obj.dimensions.x
        obj.location = (x + width * 0.5, 0.0, 2.2 if obj.name == "bathroom_pendant" else 0.0)
        x += width + 0.3
    for i, obj in enumerate(small):
        obj.location = (-0.6 - (i % 6) * 0.4, 1.5 + (i // 6) * 0.4, 0.0)
    K.preview(baked, PREVIEW_DIR, "models", [
        ("all", (x * 0.5, -14.0, 4.0), (x * 0.5, 0.0, 0.8)),
        ("left", (x * 0.15, -4.0, 2.0), (x * 0.15, 0.0, 0.6)),
        ("midleft", (x * 0.38, -4.0, 2.0), (x * 0.38, 0.0, 0.6)),
        ("midright", (x * 0.62, -4.0, 2.0), (x * 0.62, 0.0, 0.6)),
        ("right", (x * 0.85, -4.0, 2.0), (x * 0.85, 0.0, 0.6)),
        ("small", (-1.6, -1.0, 1.2), (-1.6, 2.0, 0.0)),
    ])


if __name__ == "__main__":
    main()
