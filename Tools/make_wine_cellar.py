"""
Builds the wine cellar's architecture, racks, bottles, glassware and furniture in Blender,
headless, and exports each as an FBX the art pipeline imports.

    blender --background --factory-startup --python Tools/make_wine_cellar.py [-- --only=a,b]

Poly Haven has a wine barrel, a rusted shelf unit, a crate and a lantern, and nothing else a wine
cellar is made of: no rack, no bottle that is not a catalogue line-up, no stemware, no club chair.
The rest of the house has shown what primitives do to things like these (a bowl built from a
cylinder is a drum with a lid), so they are modelled here: turned glass with an inside and a lip,
racks joined from slats so every cell is a cell, a barrel vault laid in courses along its curve.

Outputs Art/Source/Models/<name>/<name>.fbx with an empty GENERATED file beside it (re-imported
every run with its own normals) and, for the heavy repeated meshes, an empty NANITE file (the
import turns Nanite on: the racks and the bottles go in by the thousand). Workbench previews go to
Saved/CellarPreview/.

Frame, as every prop in the house: centimetres, origin on the floor under the middle of the
footprint, front facing local +Y. Exceptions are noted on the model: wall-hung things have their
origin on the wall plane, small things lying about have theirs at their own base.

The numbers the C++ also needs (AWineCellarActor) are the constants below, and must stay in step
with WineCellarActor.h: the vault, the rack grid, the bottle.

Material slots are set by name in C++. Their texel sizes here match the surface each is given
there, whose instances have their tiling at one:
    Oak        black_oak_veneer (100cm)          Leather   brown_leather (40cm)
    Vault      medieval_red_brick (200cm)        Stone     medieval_blocks_03 (170cm)
    Wood       weathered_brown_planks (150cm)    Marble    marble_01 (150cm)
    Label      the wine_paper atlas, with its own UVs
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
from blender_kit import Part, at, blob, box, cbox, ellipsoid, lathe, prism_xz, tube  # noqa: E402

PREVIEW_DIR = os.path.join(K.ROOT, "Saved", "CellarPreview")

FLAT = 100.0
SLOTS = {
    "Oak": ((0.16, 0.10, 0.06, 1.0), 100.0),
    "Leather": ((0.30, 0.13, 0.05, 1.0), 40.0),
    "Vault": ((0.45, 0.22, 0.15, 1.0), 200.0),
    "Rib": ((0.50, 0.46, 0.40, 1.0), 170.0),
    "Stone": ((0.50, 0.46, 0.40, 1.0), 170.0),
    "Wood": ((0.40, 0.30, 0.20, 1.0), 150.0),
    "Marble": ((0.75, 0.73, 0.70, 1.0), 150.0),
    "Glass": ((0.03, 0.08, 0.04, 1.0), FLAT),
    "Capsule": ((0.45, 0.06, 0.06, 1.0), FLAT),
    "Label": ((0.85, 0.80, 0.68, 1.0), FLAT),
    "Crystal": ((0.70, 0.75, 0.78, 1.0), FLAT),
    "Residue": ((0.20, 0.03, 0.04, 1.0), FLAT),
    "Brass": ((0.60, 0.45, 0.20, 1.0), FLAT),
    "Iron": ((0.15, 0.12, 0.10, 1.0), FLAT),
    "Wax": ((0.85, 0.80, 0.68, 1.0), FLAT),
    "Wick": ((0.02, 0.02, 0.02, 1.0), FLAT),
    "Cork": ((0.55, 0.40, 0.25, 1.0), FLAT),
    "Stain": ((0.25, 0.04, 0.06, 1.0), FLAT),
    "Felt": ((0.08, 0.14, 0.10, 1.0), FLAT),
    "Dial": ((0.85, 0.80, 0.70, 1.0), FLAT),
    "Ink": ((0.03, 0.03, 0.03, 1.0), FLAT),
    "Tag": ((0.80, 0.74, 0.60, 1.0), FLAT),
    "Shadow": ((0.02, 0.02, 0.02, 1.0), FLAT),
    "Gilt": ((0.75, 0.60, 0.25, 1.0), FLAT),
    "BookRed": ((0.40, 0.08, 0.06, 1.0), FLAT),
    "BookGreen": ((0.10, 0.25, 0.14, 1.0), FLAT),
    "BookBrown": ((0.35, 0.20, 0.10, 1.0), FLAT),
    "BookBlack": ((0.06, 0.05, 0.05, 1.0), FLAT),
    "BookTan": ((0.55, 0.42, 0.28, 1.0), FLAT),
    "Pages": ((0.80, 0.75, 0.62, 1.0), FLAT),
}

# ---------------------------------------------------------------------------------------------
# Shared with WineCellarActor.h.
# ---------------------------------------------------------------------------------------------

# The nave's barrel vault, in the room's frame: it runs the length of the room along X, springs
# from the arcade beams at +-VAULT_HALF and rises to VAULT_CROWN. A segmental arc.
ROOM_HALF_X = 610.0
VAULT_HALF = 185.0
VAULT_SPRING = 304.0
VAULT_CROWN = 415.0
VAULT_THICK = 16.0
VAULT_R = (VAULT_HALF ** 2 + (VAULT_CROWN - VAULT_SPRING) ** 2) / (2.0 * (VAULT_CROWN - VAULT_SPRING))
VAULT_ZC = VAULT_CROWN - VAULT_R
# Transverse ribs under the vault, at the pier lines that are not the alcove's arch.
RIB_X = (-120.0, 120.0, 360.0)

# The racks: a lattice of cells on a 12.5cm pitch, 14 across and 22 up, on an 8cm plinth.
RACK_PITCH = 12.5
RACK_COLS = 14
RACK_ROWS = 22
RACK_PLINTH = 8.0
RACK_STILE = 2.5
RACK_SLAT = 1.2
RACK_WIDTH = RACK_COLS * RACK_PITCH + 2 * RACK_STILE          # 180
RACK_TOP = RACK_PLINTH + RACK_ROWS * RACK_PITCH                # 283
RACK_HEIGHT = RACK_TOP + 7.0                                   # 290 with the cornice
RACK_BACK_DEPTH = 30.0
RACK_SPINE_DEPTH = 60.0

# The bottle: 7.4cm across, 30 tall.
BOTTLE_R = 3.7
LABEL_CELLS = 4
LABEL_V = 410.0 / 2048.0


def to_axis(direction):
    return Vector(direction).normalized().to_track_quat("Z", "Y").to_matrix().to_4x4()


def prism_yz(part, points, x0, x1):
    """A profile drawn in YZ, extruded along X from x0 to x1. The bake recalculates the normals."""
    near = [part.bm.verts.new((x0, y, z)) for y, z in points]
    far = [part.bm.verts.new((x1, y, z)) for y, z in points]
    part.bm.faces.new(near)
    part.bm.faces.new(far[::-1])
    for i in range(len(points)):
        j = (i + 1) % len(points)
        part.bm.faces.new((near[i], near[j], far[j], far[i]))


def band(part, r_in, r_out, z0, z1, a0, a1, segments):
    """A closed sector of a thick-walled tube about Z: a label on a bottle, a ring of brass."""
    bm = part.bm
    rings = []
    for k in range(segments + 1):
        a = a0 + (a1 - a0) * k / segments
        s, c = math.sin(a), math.cos(a)
        rings.append([bm.verts.new((r * s, r * c, z)) for r, z in ((r_out, z0), (r_out, z1), (r_in, z1), (r_in, z0))])
    for a, b in zip(rings, rings[1:]):
        for i in range(4):
            j = (i + 1) % 4
            bm.faces.new((a[i], b[i], b[j], a[j]))
    bm.faces.new(rings[0])
    bm.faces.new(rings[-1][::-1])


def segmental(half, spring, crown, steps):
    """Points (y, z) on a segmental arc from -half to +half, through (0, crown)."""
    rise = crown - spring
    r = (half ** 2 + rise ** 2) / (2.0 * rise)
    zc = crown - r
    a = math.asin(half / r)
    return [(r * math.sin(-a + 2 * a * k / steps), zc + r * math.cos(-a + 2 * a * k / steps)) for k in range(steps + 1)]


def vault_z(y):
    return VAULT_ZC + math.sqrt(max(VAULT_R ** 2 - y * y, 0.0))


# ---------------------------------------------------------------------------------------------
# The vault and the alcove arch. In the room's frame: origin at the room's centre on the floor.
# ---------------------------------------------------------------------------------------------

def arc_uvs(texel, along_x):
    """UVs for a part curved round the vault's axis: courses along X, the arc length across."""
    def apply(bm):
        uv = bm.loops.layers.uv.verify()
        for face in bm.faces:
            for loop in face.loops:
                co = loop.vert.co
                s = VAULT_R * math.atan2(co.y, co.z - VAULT_ZC)
                if along_x:
                    loop[uv].uv = (co.x / texel, s / texel)
                else:
                    loop[uv].uv = (s / texel, co.x / texel)
    return apply


def curved_sheet(part, x0, x1, r_in, r_out, a_half, steps):
    """A thick sheet of a cylinder about X through (0, VAULT_ZC), from -a_half to +a_half."""
    bm = part.bm
    rows = []
    for k in range(steps + 1):
        a = -a_half + 2 * a_half * k / steps
        s, c = math.sin(a), math.cos(a)
        rows.append([bm.verts.new((x, r * s, VAULT_ZC + r * c)) for x, r in ((x0, r_in), (x1, r_in), (x1, r_out), (x0, r_out))])
    for a, b in zip(rows, rows[1:]):
        for i in range(4):
            j = (i + 1) % 4
            bm.faces.new((a[i], a[j], b[j], b[i]))
    bm.faces.new(rows[0][::-1])
    bm.faces.new(rows[-1])


def vault():
    """The brick barrel vault over the nave, and the stone ribs that stand under it at the piers."""
    a_half = math.asin(VAULT_HALF / VAULT_R)
    brick = Part("Vault")
    curved_sheet(brick, -ROOM_HALF_X - 10.0, ROOM_HALF_X + 10.0, VAULT_R, VAULT_R + VAULT_THICK, a_half, 48)
    brick.uv_fn = arc_uvs(200.0, True)
    ribs = Part("Rib", bevel=0.8)
    for x in RIB_X:
        # A rib springs a little lower than the vault, off the capital, so it reads as carrying it.
        curved_sheet(ribs, x - 18.0, x + 18.0, VAULT_R - 13.0, VAULT_R + 1.0, a_half + 0.06, 40)
    ribs.uv_fn = arc_uvs(170.0, False)
    return [brick, ribs]


def alcove_arch():
    """The stone arch between the nave and the tasting alcove: a wall of masonry 50 thick, filling
    the vault down to a segmental opening that springs off the two west piers. In its own frame:
    origin at its centre on the floor, the opening in the YZ plane, its thickness along X."""
    stone = Part("Stone", bevel=0.8)
    opening = segmental(175.0, 246.0, 352.0, 28)
    top = [(y, vault_z(y) + 1.0) for y in [185.0 - 370.0 * k / 40 for k in range(41)]]
    outline = [(-185.0, 246.0)] + opening + [(185.0, 246.0)] + top
    prism_yz(stone, outline, -25.0, 25.0)
    # The keystone and the voussoirs either side of it stand a finger proud of the face.
    trim = Part("Stone", bevel=0.6)
    for side in (-1.0, 1.0):
        x = side * 25.6
        for k in range(1, 28, 3):
            y0, z0 = opening[k]
            y1, z1 = opening[k + 1]
            ang = math.degrees(math.atan2(z1 - z0, y1 - y0))
            mid = ((y0 + y1) * 0.5, (z0 + z1) * 0.5)
            # Each voussoir: a block whose bottom sits on the intrados, its long side radial.
            normal = Vector((0.0, -(z1 - z0), y1 - y0)).normalized()
            centre = Vector((x, mid[0], mid[1])) + normal * 15.0
            cbox(trim, 0, 0, 0, 1.4, 13.0, 30.0, matrix=Matrix.Translation(centre) @ Matrix.Rotation(math.radians(ang), 4, "X"))
        cbox(trim, x, 0.0, 362.0, 1.6, 22.0, 26.0)
    # Impost blocks the arch springs from, on top of the piers' capitals.
    for side in (-1.0, 1.0):
        cbox(stone, 0.0, side * 192.0, 238.0, 58.0, 34.0, 16.0)
    return [stone, trim]


def pier():
    """A dressed stone pier, 50 square, PIER_TOP tall: a moulded base, a shaft with its arrises
    chamfered, a capital that steps out under the beam. Built as a model rather than boxes because
    the engine cube's faces do not share one UV orientation, and two sides of every box pier came
    out with the stone courses stretched across them."""
    stone = Part("Stone", bevel=0.6)
    cbox(stone, 0.0, 0.0, 5.0, 64.0, 64.0, 10.0)
    cbox(stone, 0.0, 0.0, 13.0, 58.0, 58.0, 6.0)
    shaft = Part("Stone", bevel=2.2, segments=1, angle=60.0)
    cbox(shaft, 0.0, 0.0, 16.0 + 118.0, 50.0, 50.0, 236.0)
    cap = Part("Stone", bevel=0.6)
    cbox(cap, 0.0, 0.0, 255.0, 56.0, 56.0, 6.0)
    cbox(cap, 0.0, 0.0, 264.0, 64.0, 64.0, 12.0)
    return [stone, shaft, cap]


# ---------------------------------------------------------------------------------------------
# Racks.
# ---------------------------------------------------------------------------------------------

def rack(depth, double):
    """The lattice: stiles at the ends, slats across and up, a plinth and a cornice. Single-sided
    racks stand against a wall with a back board; a spine is open both sides with a board down
    its middle. Front at +Y; a single-sided rack's back is at -depth/2."""
    oak = Part("Oak", bevel=0.25)
    hw = RACK_WIDTH * 0.5
    inner = hw - RACK_STILE
    hd = depth * 0.5
    for sx in (-1, 1):
        cbox(oak, sx * (hw - RACK_STILE * 0.5), 0.0, RACK_TOP * 0.5, RACK_STILE, depth, RACK_TOP)
    for k in range(RACK_ROWS + 1):
        z = RACK_PLINTH + k * RACK_PITCH
        cbox(oak, 0.0, 0.0, z + RACK_SLAT * 0.5 - (RACK_SLAT if k == RACK_ROWS else 0.0), inner * 2, depth - 0.4, RACK_SLAT)
    for j in range(1, RACK_COLS):
        x = -inner + j * RACK_PITCH
        cbox(oak, x, 0.0, (RACK_PLINTH + RACK_TOP) * 0.5, RACK_SLAT, depth - 0.4, RACK_TOP - RACK_PLINTH)
    back = Part("Oak")
    if double:
        cbox(back, 0.0, 0.0, (RACK_PLINTH + RACK_TOP) * 0.5, inner * 2, 1.0, RACK_TOP - RACK_PLINTH)
    else:
        cbox(back, 0.0, -hd + 0.5, (RACK_PLINTH + RACK_TOP) * 0.5, inner * 2, 1.0, RACK_TOP - RACK_PLINTH)
    plinth = Part("Oak", bevel=0.4)
    cbox(plinth, 0.0, 0.0, RACK_PLINTH * 0.5, RACK_WIDTH, depth - (3.0 if double else 1.5), RACK_PLINTH)
    cornice = Part("Oak", bevel=0.8, segments=3)
    cbox(cornice, 0.0, 0.0 if double else 1.0, RACK_TOP + 3.5, RACK_WIDTH + 3.0, depth + (3.0 if double else 2.0), 7.0)
    # Cast iron bin numbers on the end of each stile were what a cellar had; a plain plate here.
    plate = Part("Brass")
    for sy in ((-1, 1) if double else (1,)):
        cbox(plate, 0.0, sy * (hd + 0.6 if double else hd + 1.6), RACK_TOP + 3.5, 6.0, 0.4, 3.6)
    return [oak, back, plinth, cornice, plate]


def rack_back():
    return rack(RACK_BACK_DEPTH, False)


def rack_spine():
    return rack(RACK_SPINE_DEPTH, True)


# ---------------------------------------------------------------------------------------------
# Bottles. Origin at the middle of the base, axis up local Z.
# ---------------------------------------------------------------------------------------------

BORDEAUX = [(0.0, 2.2), (1.6, 1.1), (2.9, 0.2), (3.45, 0.0), (3.66, 0.3), (3.7, 1.0), (3.7, 20.5), (3.6, 21.6),
            (3.2, 22.8), (2.4, 24.0), (1.6, 25.1), (1.48, 26.0), (1.45, 28.6), (1.62, 28.8), (1.62, 29.6), (1.5, 30.0)]
BURGUNDY = [(0.0, 2.0), (1.6, 1.0), (3.0, 0.2), (3.6, 0.0), (3.85, 0.3), (3.9, 1.0), (3.9, 16.0), (3.75, 18.0),
            (3.3, 20.4), (2.6, 22.6), (1.9, 24.4), (1.55, 25.8), (1.5, 28.6), (1.66, 28.8), (1.66, 29.6), (1.52, 30.0)]


def label_uvs(cell, z0, z1, a0, a1):
    col, row = cell % 4, cell // 4

    def apply(bm):
        uv = bm.loops.layers.uv.verify()
        for face in bm.faces:
            for loop in face.loops:
                co = loop.vert.co
                a = math.atan2(co.x, co.y)
                tu = min(max((a - a0) / (a1 - a0), 0.0), 1.0)
                tv = min(max((co.z - z0) / (z1 - z0), 0.0), 1.0)
                u = col * 0.25 + tu * 0.25
                v_img = row * 0.25 + (1.0 - tv) * LABEL_V
                loop[uv].uv = (u, 1.0 - v_img)
    return apply


def bottle(shape, cell, capsule=True, open_mouth=False, label=True):
    glass = Part("Glass")
    profile = list(shape)
    radius = profile[6][0]
    if open_mouth:
        # The mouth is open: down the inside of the neck and back to the axis below the shoulder.
        profile += [(1.08, 30.0), (0.95, 29.4), (0.92, 26.0), (0.0, 25.0)]
    else:
        profile += [(0.0, 30.0)]
    lathe(glass, profile, Matrix.Identity(4), sides=24)
    parts = [glass]
    if capsule:
        foil = Part("Capsule")
        if open_mouth:
            # The cut capsule of an opened bottle keeps its lower half, ragged where the knife went.
            band(foil, 1.49, 1.62, 25.6, 27.5, 0.0, 2 * math.pi, 24)
        else:
            lathe(foil, [(0.0, 25.6), (1.58, 25.6), (1.6, 26.0), (1.6, 28.6), (1.76, 28.8), (1.76, 29.62),
                         (1.62, 30.05), (1.25, 30.12), (0.0, 30.12)], Matrix.Identity(4), sides=24)
        parts.append(foil)
    if label:
        paper = Part("Label")
        span = math.radians(150.0) * (BOTTLE_R / radius)
        height = 2 * span * radius / 2.0 / 1.25
        z0 = 6.0
        band(paper, radius - 0.2, radius + 0.035, z0, z0 + height, -span / 2, span / 2, 24)
        paper.uv_fn = label_uvs(cell, z0, z0 + height, -span / 2, span / 2)
        parts.append(paper)
    return parts


def jagged_collar(part, r_out, r_in, z0, heights):
    bm = part.bm
    n = len(heights)
    rings = []
    for k in range(n):
        a = 2 * math.pi * k / n
        s, c = math.sin(a), math.cos(a)
        top = z0 + heights[k]
        rings.append([bm.verts.new((r * s, r * c, z)) for r, z in ((r_out, z0), (r_out, top), (r_in, top - 0.15), (r_in, z0))])
    for k in range(n):
        a, b = rings[k], rings[(k + 1) % n]
        for i in range(4):
            j = (i + 1) % 4
            bm.faces.new((a[i], b[i], b[j], a[j]))


def bottle_broken():
    """The bottom half of a bottle that hit the flags: the punt, a stub of wall, a jagged edge."""
    glass = Part("Glass")
    lathe(glass, [(0.0, 2.2), (1.6, 1.1), (2.9, 0.2), (3.45, 0.0), (3.66, 0.3), (3.7, 1.0), (3.7, 7.6), (3.45, 7.6),
                  (3.45, 2.8), (0.0, 3.2)], Matrix.Identity(4), sides=24)
    rng = random.Random(7)
    heights = []
    for k in range(24):
        heights.append(max(0.4, 2.5 + 3.5 * math.sin(k * 0.7) + rng.uniform(-2.0, 2.0) + (5.0 if 5 <= k <= 7 else 0.0)))
    jagged_collar(glass, 3.7, 3.45, 7.4, heights)
    paper = Part("Label")
    band(paper, 3.5, 3.735, 6.0, 7.5, -0.9, 0.9, 12)
    paper.uv_fn = label_uvs(2, 6.0, 13.8, -1.3, 1.3)
    return [glass, paper]


def bottle_neck():
    """The neck and shoulder that broke off it, capsule still on: lying where it rolled."""
    glass = Part("Glass")
    lathe(glass, [(0.0, 21.0), (3.5, 21.0), (3.6, 21.6), (3.2, 22.8), (2.4, 24.0), (1.6, 25.1), (1.48, 26.0), (1.45, 28.6),
                  (1.62, 28.8), (1.62, 29.6), (1.5, 30.0), (0.0, 30.0)], Matrix.Identity(4), sides=24)
    rng = random.Random(9)
    jagged_collar(glass, 3.62, 3.38, 21.0 - 2.0, [max(0.3, 2.0 + rng.uniform(-1.6, 1.4)) for _ in range(24)])
    foil = Part("Capsule")
    lathe(foil, [(0.0, 25.6), (1.58, 25.6), (1.6, 26.0), (1.6, 28.6), (1.76, 28.8), (1.76, 29.62), (1.62, 30.05),
                 (1.25, 30.12), (0.0, 30.12)], Matrix.Identity(4), sides=24)
    return [glass, foil]


def shards():
    """Broken glass: a dozen curved flakes of a bottle wall, lying flat, spread over half a metre."""
    glass = Part("Glass")
    rng = random.Random(11)
    for _ in range(16):
        cx, cy = rng.uniform(-24, 24), rng.uniform(-18, 18)
        n = rng.choice([3, 3, 4])
        size = rng.choice([1.0, 1.5, 2.0, 3.0, 4.5])
        angles = sorted(rng.uniform(0, 2 * math.pi) for _ in range(n))
        pts = [(cx + math.cos(a) * size * rng.uniform(0.5, 1.2), cy + math.sin(a) * size * rng.uniform(0.5, 1.2)) for a in angles]
        bm = glass.bm
        tilt = rng.uniform(0, 0.25)
        low = [bm.verts.new((x, y, 0.05 + tilt * (x - cx) * 0.1)) for x, y in pts]
        high = [bm.verts.new((x, y, 0.32 + tilt * (x - cx) * 0.1)) for x, y in pts]
        bm.faces.new(low[::-1])
        bm.faces.new(high)
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((low[i], low[j], high[j], high[i]))
    return [glass]


def cork():
    """A cork, its bottom end stained with wine. Origin at the middle of its dry end, axis up Z."""
    c = Part("Cork", bevel=0.25, segments=2)
    lathe(c, [(0.0, 0.0), (1.2, 0.0), (1.2, 4.4), (0.0, 4.4)], Matrix.Identity(4), sides=16)
    stain = Part("Stain")
    lathe(stain, [(0.0, 4.38), (1.12, 4.38), (1.12, 4.45), (0.0, 4.45)], Matrix.Identity(4), sides=16)
    return [c, stain]


# ---------------------------------------------------------------------------------------------
# Glassware and candles.
# ---------------------------------------------------------------------------------------------

def wine_glass():
    """A tulip wine glass: foot, stem, a bowl with an inside and a rim. One closed shell, one
    winding, so the two-sided glass material lights it as glass and not as two sheets fighting.
    The Residue slot is what dried in the bottom; C++ gives it the glass where there is none."""
    crystal = Part("Crystal")
    lathe(crystal, [(0.0, 0.0), (3.6, 0.0), (3.65, 0.15), (3.5, 0.32), (0.55, 0.7), (0.38, 1.4), (0.33, 7.8), (0.42, 8.6),
                    (0.9, 9.2), (2.3, 10.2), (3.4, 12.0), (3.85, 14.6), (3.65, 18.0), (3.2, 20.6), (3.14, 20.8),
                    (3.04, 20.7), (3.1, 20.4), (3.54, 17.9), (3.73, 14.6), (3.3, 12.1), (2.2, 10.45), (0.8, 9.75),
                    (0.0, 9.65)], Matrix.Identity(4), sides=32)
    residue = Part("Residue")
    lathe(residue, [(0.0, 9.68), (1.2, 9.82), (1.9, 10.2), (1.85, 10.32), (0.0, 10.0)], Matrix.Identity(4), sides=24)
    return [crystal, residue]


def decanter():
    """A ship's decanter: wide and low so it does not go over, a long neck, a flared lip, and the
    sediment dried in a crust on its floor. The stopper is a separate model."""
    crystal = Part("Crystal")
    outer = [(0.0, 0.0), (11.5, 0.0), (12.2, 0.6), (12.4, 2.0), (11.6, 5.0), (8.6, 9.2), (5.0, 13.0), (2.6, 16.5),
             (2.1, 19.0), (2.05, 27.0), (2.6, 28.4), (3.0, 29.0), (2.9, 29.3)]
    inner = [(2.6, 29.0), (2.4, 28.5), (1.75, 27.0), (1.8, 19.0), (2.3, 16.6), (4.7, 13.0), (8.3, 9.0), (11.2, 4.9),
             (11.9, 2.0), (11.2, 0.9), (0.0, 1.2)]
    lathe(crystal, outer + inner, Matrix.Identity(4), sides=40)
    residue = Part("Residue")
    lathe(residue, [(0.0, 1.22), (10.6, 1.25), (11.4, 1.9), (11.0, 2.05), (0.0, 1.5)], Matrix.Identity(4), sides=32)
    return [crystal, residue]


def decanter_stopper():
    crystal = Part("Crystal")
    lathe(crystal, [(0.0, 0.0), (1.6, 0.0), (1.7, 3.5), (2.6, 4.2), (3.4, 6.0), (3.0, 8.2), (1.6, 9.2), (0.0, 9.4)],
          Matrix.Identity(4), sides=24)
    return [crystal]


def wax_runs(part, elements_extra, resolution=0.35):
    blob(part, elements_extra, resolution=resolution)


def candlestick():
    """A brass candlestick with the candle burned right down into its socket: a stub, the wax that
    ran over the drip pan and down the stem, frozen where it stopped."""
    brass = Part("Brass")
    lathe(brass, [(0.0, 0.0), (6.4, 0.0), (6.5, 0.6), (5.6, 1.4), (4.0, 2.0), (2.0, 2.6), (1.4, 3.8), (1.9, 5.0), (1.2, 6.2),
                  (1.0, 14.0), (1.5, 15.2), (1.0, 16.2), (1.0, 19.4), (4.2, 19.8), (4.4, 20.4), (3.9, 20.6), (1.6, 20.6),
                  (1.6, 24.0), (1.9, 24.3), (1.5, 24.4), (1.3, 21.2), (0.0, 21.2)], Matrix.Identity(4), sides=28)
    wax = Part("Wax", smooth=True)
    rng = random.Random(3)
    elements = [((0.0, 0.0, 24.6), (1.35, 1.35, 1.0), None),
                ((0.6, 0.3, 21.0), (3.0, 2.6, 0.45), None),
                ((-1.2, -0.8, 20.95), (2.4, 2.0, 0.4), None)]
    for k in range(5):
        a = rng.uniform(0, 2 * math.pi)
        r = 1.75
        length = rng.uniform(1.0, 3.4)
        elements.append(((math.cos(a) * r, math.sin(a) * r, 24.0 - length * 0.5), (0.45, 0.45, length * 0.55), None))
    for k in range(3):
        a = rng.uniform(0, 2 * math.pi)
        elements.append(((math.cos(a) * 4.2, math.sin(a) * 4.2, 20.2), (0.55, 0.55, 0.8), None))
    blob(wax, elements, resolution=0.3)
    wick = Part("Wick")
    tube(wick, [(0.0, 0.0, 25.2), (0.1, 0.05, 25.9), (0.4, 0.1, 26.3)], 0.08, sides=6)
    return [brass, wax, wick]


def chamberstick():
    """A brass chamberstick: a saucer with a ring handle and a short socket, the candle gone to a
    puddle that filled the saucer and spilled over its rim."""
    brass = Part("Brass")
    lathe(brass, [(0.0, 0.0), (7.0, 0.0), (7.3, 0.4), (7.4, 1.6), (7.0, 1.7), (6.8, 0.7), (1.8, 0.8), (1.6, 1.4), (1.7, 5.4),
                  (2.1, 5.7), (1.6, 5.8), (1.4, 1.6), (0.0, 1.6)], Matrix.Identity(4), sides=28)
    tube(brass, [(7.2, -1.2, 1.2), (9.6, -1.6, 1.6), (10.6, 0.0, 2.6), (9.6, 1.6, 3.4), (7.4, 1.0, 3.2)], 0.35, sides=10)
    wax = Part("Wax", smooth=True)
    blob(wax, [((0.0, 0.0, 6.0), (1.3, 1.3, 0.7), None), ((0.4, 0.2, 1.2), (5.6, 5.2, 0.4), None),
               ((2.6, -2.0, 1.15), (3.0, 2.4, 0.35), None), ((5.9, 3.2, 1.0), (1.4, 1.0, 0.8), None),
               ((7.3, 3.9, 0.4), (0.7, 0.6, 1.1), None), ((1.7, 0.1, 4.0), (0.4, 0.4, 1.6), None),
               ((-1.6, 0.6, 3.4), (0.4, 0.4, 2.0), None)], resolution=0.28)
    wick = Part("Wick")
    tube(wick, [(0.0, 0.0, 6.4), (0.2, 0.0, 6.9), (0.5, 0.1, 7.1)], 0.08, sides=6)
    return [brass, wax, wick]


def corkscrew():
    """A T-handled corkscrew with the cork still on its worm. Standing on the worm's tip; C++ lays
    it on its side."""
    wood = Part("Wood", bevel=0.4)
    lathe(wood, [(0.0, -5.2), (0.9, -5.0), (1.15, -3.8), (1.0, -1.0), (1.25, 0.0), (1.0, 1.0), (1.15, 3.8), (0.9, 5.0), (0.0, 5.2)],
          at(0.0, 0.0, 11.2, pitch=90.0), sides=16)
    iron = Part("Iron")
    lathe(iron, [(0.0, 6.0), (0.32, 6.0), (0.32, 10.4), (0.7, 10.6), (0.0, 10.8)], Matrix.Identity(4), sides=10)
    path = []
    for k in range(80):
        t = k / 79
        a = t * 5.2 * 2 * math.pi
        r = 0.55 * min(1.0, t * 6.0 + 0.15)
        path.append((r * math.cos(a), r * math.sin(a), 0.2 + t * 5.9))
    tube(iron, path, 0.13, sides=6)
    c = Part("Cork", bevel=0.2)
    lathe(c, [(0.0, 2.6), (1.2, 2.6), (1.2, 6.9), (0.0, 6.9)], Matrix.Identity(4), sides=16)
    stain = Part("Stain")
    lathe(stain, [(0.0, 2.57), (1.12, 2.57), (1.12, 2.62), (0.0, 2.62)], Matrix.Identity(4), sides=16)
    return [wood, iron, c, stain]


# ---------------------------------------------------------------------------------------------
# Furniture.
# ---------------------------------------------------------------------------------------------

def tasting_table():
    """A refectory table in solid oak, 300 x 100: a top three fingers thick, aprons, six turned legs
    and stretchers low down, worn smooth on top where feet rested."""
    top = Part("Oak", bevel=1.2, segments=3)
    cbox(top, 0.0, 0.0, 74.5, 300.0, 100.0, 7.0)
    frame = Part("Oak", bevel=0.5)
    for sy in (-1, 1):
        cbox(frame, 0.0, sy * 39.0, 66.0, 268.0, 3.0, 10.0)
        cbox(frame, 0.0, sy * 38.0, 10.0, 268.0, 6.0, 6.0)
    for sx in (-1, 0, 1):
        cbox(frame, sx * 130.0, 0.0, 66.0, 3.0, 76.0, 10.0)
        cbox(frame, sx * 130.0, 0.0, 10.0, 6.0, 76.0, 6.0)
    legs = Part("Oak")
    profile = [(0.0, 0.0), (4.2, 0.0), (4.4, 2.0), (4.0, 3.0), (4.0, 14.0), (3.4, 16.0), (4.6, 22.0), (6.2, 30.0), (6.4, 34.0),
               (5.4, 40.0), (3.6, 45.0), (3.2, 48.0), (4.0, 52.0), (4.0, 61.0), (4.4, 62.0), (4.4, 71.0), (0.0, 71.0)]
    for sx in (-1, 0, 1):
        for sy in (-1, 1):
            lathe(legs, profile, at(sx * 130.0, sy * 38.0, 0.0), sides=20)
    return [top, frame, legs]


def writing_table():
    """A plain writing table, 96 x 52, with a drawer: where the cellar book was kept."""
    oak = Part("Oak", bevel=0.6)
    cbox(oak, 0.0, 0.0, 74.5, 96.0, 52.0, 3.0)
    for sy in (-1, 1):
        cbox(oak, 0.0, sy * 22.0, 66.0, 82.0, 2.0, 12.0)
    for sx in (-1, 1):
        cbox(oak, sx * 40.0, 0.0, 66.0, 2.0, 42.0, 12.0)
    legs = Part("Oak", bevel=0.3)
    for sx in (-1, 1):
        for sy in (-1, 1):
            bm = legs.bm
            x, y = sx * 42.0, sy * 21.0
            lo = [bm.verts.new((x + dx * 1.4, y + dy * 1.4, 0.0)) for dx, dy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
            hi = [bm.verts.new((x + dx * 2.3, y + dy * 2.3, 73.0)) for dx, dy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
            bm.faces.new(lo[::-1])
            bm.faces.new(hi)
            for i in range(4):
                j = (i + 1) % 4
                bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    drawer = Part("Oak", bevel=0.4)
    cbox(drawer, 0.0, 23.3, 66.0, 50.0, 1.2, 9.0)
    brass = Part("Brass")
    lathe(brass, [(0.0, 0.0), (0.6, 0.0), (1.1, 0.8), (0.8, 1.6), (0.0, 1.7)], at(0.0, 23.8, 66.0, roll=-90.0), sides=12)
    return [oak, legs, drawer, brass]


def armchair():
    """A leather club chair, 88 x 86: a deep seat, rolled arms with a row of brass nails round their
    fronts, a buttoned back that curls over at the top, and short turned feet. Back at -Y."""
    leather = Part("Leather", bevel=4.0, segments=4, angle=30.0)
    cbox(leather, 0.0, 1.0, 26.0, 62.0, 80.0, 28.0)
    for sx in (-1, 1):
        cbox(leather, sx * 36.0, 0.0, 36.0, 16.0, 84.0, 48.0)
    cbox(leather, 0.0, -36.0, 62.0, 74.0, 16.0, 56.0)
    cushion = Part("Leather", bevel=5.0, segments=5, angle=30.0)
    cbox(cushion, 0.0, 4.0, 44.5, 58.0, 70.0, 12.0)
    cbox(cushion, 0.0, -25.5, 64.0, 58.0, 9.0, 38.0, matrix=None)
    rolls = Part("Leather")
    for sx in (-1, 1):
        lathe(rolls, [(0.0, -42.0), (7.2, -42.0), (8.6, -40.0), (8.6, 40.0), (7.6, 42.6), (0.0, 43.2)], at(sx * 37.0, 0.0, 60.0, roll=-90.0), sides=24)
    lathe(rolls, [(0.0, -38.0), (6.6, -38.0), (8.0, -36.0), (8.0, 36.0), (6.6, 38.0), (0.0, 38.0)], at(0.0, -36.5, 89.0, pitch=90.0), sides=24)
    brass = Part("Brass")
    for sx in (-1, 1):
        # Nails round the face of each arm: up its sides and over the scroll.
        for k in range(15):
            t = k / 14
            if t < 0.35:
                x, z = sx * 37.0 + 6.8, 14.0 + t / 0.35 * 44.0
            elif t < 0.65:
                a = math.pi * (t - 0.35) / 0.3
                x, z = sx * 37.0 + 6.8 * math.cos(a), 60.0 + 6.8 * math.sin(a)
            else:
                x, z = sx * 37.0 - 6.8, 58.0 - (t - 0.65) / 0.35 * 44.0
            ellipsoid(brass, (x, 43.4, z), (0.55, 0.3, 0.55), rings=6, sides=8)
    buttons = Part("Leather")
    for row, (z, n) in enumerate(((54.0, 3), (66.0, 4), (78.0, 3))):
        for k in range(n):
            x = (k - (n - 1) / 2) * 13.0
            ellipsoid(buttons, (x, -20.8, z), (0.9, 0.5, 0.9), rings=6, sides=10)
    feet = Part("Wood", bevel=0.2)
    for sx in (-1, 1):
        for sy in (-1, 1):
            lathe(feet, [(0.0, 0.0), (1.8, 0.0), (2.2, 2.0), (2.8, 6.0), (2.6, 10.0), (0.0, 12.0)], at(sx * 37.0, sy * 36.0, 0.0), sides=14)
    return [leather, cushion, rolls, brass, buttons, feet]


def pedestal_table():
    """A small round table: a marble top on a turned oak column and three splayed feet."""
    marble = Part("Marble")
    lathe(marble, [(0.0, 59.0), (30.5, 59.0), (31.6, 59.6), (32.0, 60.8), (31.6, 62.0), (30.8, 62.6), (0.0, 62.6)], Matrix.Identity(4), sides=48)
    oak = Part("Oak")
    lathe(oak, [(0.0, 14.0), (5.0, 14.0), (5.2, 16.0), (3.4, 19.0), (2.6, 24.0), (3.6, 32.0), (4.2, 36.0), (3.2, 40.0), (2.4, 46.0),
                (2.6, 52.0), (3.6, 55.0), (6.0, 57.0), (6.0, 59.0), (0.0, 59.0)], Matrix.Identity(4), sides=24)
    for k in range(3):
        a = 2 * math.pi * k / 3 + math.pi / 2
        c, s = math.cos(a), math.sin(a)
        path = [(c * r, s * r, z) for r, z in ((3.0, 18.0), (9.0, 13.0), (16.0, 6.0), (23.0, 2.4), (27.0, 1.2))]
        tube(oak, path, [2.4, 2.2, 1.9, 1.6, 1.4], sides=12)
        ellipsoid(oak, (c * 27.5, s * 27.5, 0.9), (1.8, 1.8, 0.9), rings=6, sides=12)
    return [marble, oak]


def bookshelf():
    """A small open bookcase, 92 x 30 x 112, full of old wine books and the tasting journals."""
    oak = Part("Oak", bevel=0.5)
    for sx in (-1, 1):
        cbox(oak, sx * 44.5, 0.0, 55.0, 3.0, 30.0, 110.0)
    cbox(oak, 0.0, 0.5, 111.0, 96.0, 32.0, 3.0)
    cbox(oak, 0.0, 0.0, 4.0, 86.0, 28.0, 8.0)
    for z in (40.0, 76.0):
        cbox(oak, 0.0, 0.0, z, 86.0, 28.0, 2.2)
    cbox(oak, 0.0, -14.5, 55.0, 86.0, 1.0, 110.0)
    rng = random.Random(5)
    colours = ["BookRed", "BookGreen", "BookBrown", "BookBlack", "BookTan"]
    parts = {name: Part(name, bevel=0.35) for name in colours}
    gilt = Part("Gilt")
    pages = Part("Pages")
    for floor, ceiling in ((8.0, 38.9), (41.1, 74.9), (77.1, 109.5)):
        x = -42.5
        lean = 0.0
        while x < 42.0:
            if rng.random() < 0.08 and x < 30.0:
                # A gap: a book taken out and not put back.
                x += rng.uniform(4.0, 9.0)
                continue
            if rng.random() < 0.06 and x < 20.0:
                # A few lying flat in a pile.
                for k in range(rng.randint(2, 3)):
                    w, d, h = rng.uniform(16, 22), rng.uniform(13, 19), rng.uniform(2.4, 4.0)
                    z = floor + k * 3.6 + h * 0.5
                    yaw = rng.uniform(-6, 6)
                    cbox(parts[rng.choice(colours)], 0, 0, 0, w, d, h, matrix=at(x + w * 0.5, 1.0, z, yaw=yaw))
                    cbox(pages, 0, 0, 0, w - 0.8, d - 0.4, h - 0.6, matrix=at(x + w * 0.5 + 0.5, 1.0, z, yaw=yaw))
                x += 23.0
                continue
            journal = 0.0 < x < 22.0 and floor > 70.0
            w = 2.2 if journal else rng.uniform(2.0, 5.4)
            h = min(ceiling - floor - 1.0, 27.0 if journal else rng.uniform(17.0, 29.0))
            d = 17.0 if journal else rng.uniform(13.0, 21.0)
            colour = "BookBlack" if journal else rng.choice(colours)
            front = 13.0 - rng.uniform(0.0, 3.0)
            if x > 36.0 and rng.random() < 0.6:
                lean = rng.uniform(10.0, 22.0)
            m = at(x + w * 0.5, front - d * 0.5, floor, pitch=-lean) @ Matrix.Translation((0.0, 0.0, h * 0.5))
            cbox(parts[colour], 0, 0, 0, w, d, h, matrix=m)
            for gz in (0.18, 0.82) if not journal else (0.12, 0.3, 0.88):
                cbox(gilt, 0, 0, 0, w * 0.86, 0.12, 0.5, matrix=m @ Matrix.Translation((0.0, d * 0.5 + 0.02, (gz - 0.5) * h)))
            x += w + (0.05 if rng.random() < 0.8 else rng.uniform(0.3, 1.0))
            if lean:
                break
    return [oak, gilt, pages] + list(parts.values())


def wine_cabinet():
    """A decorative wine cabinet: a cupboard on a plinth, and over it a glazed case with three shelves
    for the bottles that were kept to be looked at. Carved crest over the cornice."""
    oak = Part("Oak", bevel=0.6)
    hw, hd = 56.0, 23.0
    cbox(oak, 0.0, 0.0, 4.0, 2 * hw + 2.0, 2 * hd + 1.0, 8.0)
    for sx in (-1, 1):
        cbox(oak, sx * (hw - 1.5), 0.0, 47.0, 3.0, 2 * hd, 78.0)
    cbox(oak, 0.0, -hd + 0.6, 47.0, 2 * hw - 3.0, 1.2, 78.0)
    top = Part("Oak", bevel=1.0, segments=3)
    cbox(top, 0.0, 1.0, 88.0, 2 * hw + 4.0, 2 * hd + 3.0, 4.0)
    doors = Part("Oak", bevel=0.5)
    panels = Part("Oak", bevel=0.8, segments=2)
    for sx in (-1, 1):
        cx = sx * hw * 0.5
        cbox(doors, cx, hd - 1.0, 47.0, hw - 4.0, 2.0, 74.0)
        cbox(panels, cx, hd + 0.4, 47.0, hw - 16.0, 1.2, 60.0)
    brass = Part("Brass")
    for sx in (-1, 1):
        lathe(brass, [(0.0, 0.0), (0.7, 0.0), (1.3, 0.9), (1.0, 1.9), (0.0, 2.0)], at(sx * 4.5, hd + 0.2, 52.0, roll=-90.0), sides=12)
    # The glazed case, set back over the cupboard.
    ud = 17.0
    for sx in (-1, 1):
        cbox(oak, sx * (hw - 3.0), -6.0, 141.0, 3.0, 2 * ud, 102.0)
    cbox(oak, 0.0, -6.0 - ud + 0.6, 141.0, 2 * hw - 6.0, 1.2, 102.0)
    inside = Part("Shadow")
    cbox(inside, 0.0, -6.0 - ud + 1.3, 141.0, 2 * hw - 8.0, 0.4, 100.0)
    for z in (91.0, 123.0, 157.0, 191.0):
        cbox(oak, 0.0, -6.0, z, 2 * hw - 6.0, 2 * ud, 2.0)
    frames = Part("Oak", bevel=0.4)
    glass = Part("Crystal")
    for sx in (-1, 1):
        x0, x1 = (0.4, hw - 4.5) if sx > 0 else (-(hw - 4.5), -0.4)
        y = -6.0 + ud + 0.6
        z0, z1 = 92.0, 190.0
        cbox(frames, (x0 + x1) * 0.5, y, z0 + 2.5, x1 - x0, 1.6, 5.0)
        cbox(frames, (x0 + x1) * 0.5, y, z1 - 2.5, x1 - x0, 1.6, 5.0)
        cbox(frames, x0 + 2.5, y, (z0 + z1) * 0.5, 5.0, 1.6, z1 - z0)
        cbox(frames, x1 - 2.5, y, (z0 + z1) * 0.5, 5.0, 1.6, z1 - z0)
        cbox(frames, (x0 + x1) * 0.5, y, (z0 + z1) * 0.5, 1.4, 1.4, z1 - z0 - 10.0)
        for k in (1, 2):
            cbox(frames, (x0 + x1) * 0.5, y, z0 + 5.0 + k * (z1 - z0 - 10.0) / 3, x1 - x0 - 10.0, 1.4, 1.4)
        cbox(glass, (x0 + x1) * 0.5, y - 0.3, (z0 + z1) * 0.5, x1 - x0 - 6.0, 0.3, z1 - z0 - 6.0)
        lathe(brass, [(0.0, 0.0), (0.5, 0.0), (0.9, 0.7), (0.7, 1.4), (0.0, 1.5)], at(sx * 3.5, y + 0.8, 140.0, roll=-90.0), sides=12)
    cornice = Part("Oak", bevel=0.8, segments=3)
    cbox(cornice, 0.0, -4.0, 195.0, 2 * hw + 2.0, 2 * ud + 6.0, 6.0)
    # The crest: a low arched pediment board over the cornice, a turned finial at its middle.
    crest = Part("Oak", bevel=0.4)
    arc = [(x, 198.0 + 12.0 * math.cos(math.pi * x / 100.0)) for x in [50.0 - 100.0 * k / 24 for k in range(25)]]
    prism_xz(crest, [(-50.0, 198.0), (50.0, 198.0)] + arc, 10.0, 12.5)
    lathe(crest, [(0.0, 209.0), (2.4, 209.0), (2.8, 211.0), (3.4, 213.5), (2.4, 216.0), (1.0, 217.0), (1.4, 218.4), (0.0, 219.6)],
          at(0.0, 11.0, 0.0), sides=16)
    return [oak, top, doors, panels, brass, inside, frames, glass, cornice, crest]


def cradle():
    """A barrel stand: two saddles cut to the barrel's curve and two rails between them. The barrel
    lies on it along local Y, its head to the front; its centre is CRADLE_AXIS_Z up."""
    wood = Part("Wood", bevel=0.8)
    for sy in (-1, 1):
        pts = [(-36.0, 0.0), (36.0, 0.0), (36.0, 24.0)]
        for k in range(17):
            a = math.radians(48.0 - 96.0 * k / 16)
            pts.append((math.sin(a) * 38.5, 46.0 - math.cos(a) * 38.5))
        pts.append((-36.0, 24.0))
        prism_xz(wood, pts, sy * 28.0 - 4.0, sy * 28.0 + 4.0)
    for sx in (-1, 1):
        cbox(wood, sx * 26.0, 0.0, 4.0, 7.0, 72.0, 8.0)
    return [wood]


def crate(lid):
    """A twelve-bottle wine crate in deal, 50 x 33 x 19: thick end boards with hand-holds, thin
    side and bottom boards with a gap between, and a nailed lid. The vineyard's brand is burned
    into the +X end board (a masked plane in C++)."""
    wood = Part("Wood", bevel=0.25)
    for sx in (-1, 1):
        cbox(wood, sx * 24.0, 0.0, 9.5, 2.0, 33.0, 19.0)
    for sy in (-1, 1):
        for z0, z1 in ((0.0, 8.8), (9.8, 19.0)):
            cbox(wood, 0.0, sy * 16.0, (z0 + z1) * 0.5, 46.0, 1.0, z1 - z0)
    for y0, y1 in ((-15.5, -5.6), (-4.6, 4.6), (5.6, 15.5)):
        cbox(wood, 0.0, (y0 + y1) * 0.5, 0.6, 46.0, y1 - y0, 1.2)
    if lid:
        for y0, y1 in ((-16.5, -5.8), (-5.0, 5.0), (5.8, 16.5)):
            cbox(wood, 0.0, (y0 + y1) * 0.5, 19.6, 50.0, y1 - y0, 1.2)
    cutter = bmesh.new()
    for sx in (-1, 1):
        verts = [cutter.verts.new((x, y, z)) for z in (13.0, 16.0) for y in (-6.0, 6.0) for x in (sx * 22.0, sx * 26.0)]
        for f in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)):
            cutter.faces.new([verts[i] for i in f])
    wood.cutter = cutter
    iron = Part("Iron")
    for sx in (-1, 1):
        for sy in (-1, 1):
            for z in (4.0, 15.0):
                cbox(iron, sx * 24.0, sy * 16.55, z, 0.7, 0.15, 0.7)
            if lid:
                for y in (-11.0, 0.0, 11.0):
                    cbox(iron, sx * 24.0, y, 20.25, 0.7, 0.7, 0.12)
    return [wood, iron]


def crate_closed():
    return crate(True)


def crate_open():
    return crate(False)


def tray():
    """A wooden serving tray, 52 x 34: a thin board in a low gallery, the ends raised with a hand-hold
    cut through each."""
    oak = Part("Oak", bevel=0.3)
    cbox(oak, 0.0, 0.0, 0.6, 52.0, 34.0, 1.2)
    for sy in (-1, 1):
        cbox(oak, 0.0, sy * 16.5, 2.6, 50.0, 1.0, 4.0)
    ends = Part("Oak", bevel=0.4)
    for sx in (-1, 1):
        prism_yz(ends, [(-17.0, 0.0), (17.0, 0.0), (17.0, 4.6), (10.0, 7.4), (0.0, 8.2), (-10.0, 7.4), (-17.0, 4.6)], sx * 26.0 - 0.6, sx * 26.0 + 0.6)
    cutter = bmesh.new()
    for sx in (-1, 1):
        ring = []
        for k in range(16):
            a = 2 * math.pi * k / 16
            ring.append((math.cos(a) * 5.5, math.sin(a) * 1.2 + 5.0))
        near = [cutter.verts.new((sx * 26.0 - 3.0, y, z)) for y, z in ring]
        far = [cutter.verts.new((sx * 26.0 + 3.0, y, z)) for y, z in ring]
        cutter.faces.new(near)
        cutter.faces.new(far[::-1])
        for i in range(16):
            j = (i + 1) % 16
            cutter.faces.new((near[i], near[j], far[j], far[i]))
    ends.cutter = cutter
    return [oak, ends]


# ---------------------------------------------------------------------------------------------
# Wall things: origin on the wall plane, front +Y.
# ---------------------------------------------------------------------------------------------

def key_rack():
    """A board of hooks by the door, three big iron keys on it, a ring of small ones, a brass tag,
    and one hook empty."""
    wood = Part("Wood", bevel=0.6)
    cbox(wood, 0.0, 1.1, 0.0, 46.0, 2.2, 13.0)
    iron = Part("Iron")
    hooks = [-18.0, -9.0, 0.0, 9.0, 18.0]
    for x in hooks:
        tube(iron, [(x, 2.0, 1.0), (x, 4.6, 1.0), (x, 5.6, 2.2), (x, 5.6, 3.4)], 0.32, sides=8)
        ellipsoid(iron, (x, 2.3, 1.0), (0.7, 0.3, 0.7), rings=4, sides=8)

    def key(x, z_top, length, yaw):
        m = at(x, 4.4, z_top, yaw=yaw)
        bow = [(m @ Vector((math.cos(a) * 1.9, 0.0, -1.9 + math.sin(a) * 1.9))) for a in [2 * math.pi * k / 18 for k in range(19)]]
        tube(iron, [tuple(p) for p in bow], 0.35, sides=8)
        tube(iron, [tuple(m @ Vector((0.0, 0.0, -3.8))), tuple(m @ Vector((0.0, 0.0, -3.8 - length)))], 0.38, sides=8)
        cbox(iron, 0, 0, 0, 0.6, 1.9, 1.6, matrix=m @ Matrix.Translation((0.0, 0.9, -3.8 - length + 1.0)))
        cbox(iron, 0, 0, 0, 0.6, 1.0, 0.7, matrix=m @ Matrix.Translation((0.0, 1.9, -3.8 - length + 0.5)))
    key(-18.0, 2.4, 9.0, 4.0)
    key(-9.0, 2.4, 11.5, -3.0)
    key(18.0, 2.4, 8.0, 0.0)
    # The ring of small keys on the middle hook.
    ring = [(math.cos(a) * 2.4, 5.0, 0.4 + math.sin(a) * 2.4) for a in [2 * math.pi * k / 20 for k in range(21)]]
    tube(iron, ring, 0.15, sides=6)
    brass = Part("Brass")
    for k, ang in enumerate((-30.0, 5.0, 35.0)):
        m = at(0.0, 5.0 + k * 0.25, -2.0, pitch=ang)
        cbox(brass, 0, 0, 0, 0.3, 0.25, 4.5, matrix=m @ Matrix.Translation((0.0, 0.0, -2.0)))
        ellipsoid(brass, tuple(m @ Vector((0.0, 0.0, -0.3))), (0.6, 0.15, 0.6), rings=4, sides=8)
    tag = Part("Tag")
    cbox(tag, 0, 0, 0, 3.0, 0.15, 4.6, matrix=at(9.0, 4.6, -1.6, pitch=8.0))
    tube(iron, [(9.0, 4.6, 0.7), (9.0, 5.4, 2.0), (9.0, 5.6, 3.0)], 0.06, sides=4)
    return [wood, iron, brass, tag]


def wall_clock():
    """A round wall clock in a turned oak case with a short drop trunk: the glass smashed out but for
    three pieces caught in the bezel, the minute hand gone, the hour hand hanging off its arbor.
    Origin on the wall plane at the dial's centre."""
    oak = Part("Oak")
    lathe(oak, [(0.0, 0.0), (21.0, 0.0), (21.5, 1.5), (21.2, 6.0), (20.0, 7.6), (18.2, 8.2), (16.0, 8.4), (15.6, 6.6), (0.0, 6.6)],
          at(0.0, 0.0, 0.0, roll=-90.0), sides=48)
    trunk = Part("Oak", bevel=0.6)
    bm = trunk.bm
    pts = [(-12.0, -16.0), (12.0, -16.0), (14.0, -36.0), (0.0, -42.0), (-14.0, -36.0)]
    prism_xz(trunk, [(x, z) for x, z in pts], 0.0, 6.0)
    window = Part("Shadow")
    cbox(window, 0.0, 6.05, -30.0, 9.0, 0.2, 7.0)
    dial = Part("Dial")
    lathe(dial, [(0.0, 6.4), (15.6, 6.4), (15.6, 6.8), (0.0, 6.8)], at(0.0, 0.0, 0.0, roll=-90.0), sides=48)
    brass = Part("Brass")
    lathe(brass, [(15.4, 8.0), (16.6, 8.0), (16.9, 9.0), (16.4, 9.8), (15.4, 9.6)], at(0.0, 0.0, 0.0, roll=-90.0), sides=48)
    lathe(brass, [(0.0, 6.8), (0.8, 6.8), (0.6, 7.6), (0.0, 7.8)], at(0.0, 0.0, 0.0, roll=-90.0), sides=12)
    ellipsoid(brass, (2.0, 6.5, -30.0), (2.4, 0.5, 2.4), rings=6, sides=14)
    ink = Part("Ink")
    for k in range(12):
        a = 2 * math.pi * k / 12
        long = 2.6 if k % 3 == 0 else 1.6
        cbox(ink, 0, 0, 0, 0.6 if k % 3 == 0 else 0.4, 0.1, long, matrix=at(math.sin(a) * (13.4 - long * 0.5), 6.85, math.cos(a) * (13.4 - long * 0.5), pitch=math.degrees(a)))
    for k in range(60):
        if k % 5 == 0:
            continue
        a = 2 * math.pi * k / 60
        ellipsoid(ink, (math.sin(a) * 13.9, 6.85, math.cos(a) * 13.9), (0.15, 0.05, 0.15), rings=3, sides=4)
    # The hour hand, loose on its arbor, hanging nearly straight down; the minute hand is gone.
    hand = [(0.0, 7.3, 0.0), (0.4, 7.3, -3.0), (0.6, 7.3, -6.0), (0.2, 7.3, -8.6)]
    tube(ink, hand, [0.35, 0.3, 0.5, 0.15], sides=6)
    glass = Part("Crystal")
    for a0, a1, depth in ((0.1, 0.9, 4.0), (2.4, 2.9, 2.5), (4.4, 5.6, 5.5)):
        pts = [(math.sin(a0) * 15.8, math.cos(a0) * 15.8), (math.sin(a1) * 15.8, math.cos(a1) * 15.8),
               (math.sin((a0 + a1) * 0.5) * (15.8 - depth), math.cos((a0 + a1) * 0.5) * (15.8 - depth))]
        bm = glass.bm
        low = [bm.verts.new((x, 8.6, z)) for x, z in pts]
        high = [bm.verts.new((x, 8.9, z)) for x, z in pts]
        bm.faces.new(low)
        bm.faces.new(high[::-1])
        for i in range(3):
            j = (i + 1) % 3
            bm.faces.new((low[i], high[i], high[j], low[j]))
    return [oak, trunk, window, dial, brass, ink, glass]


def cork_board():
    """A collection of corks behind a moulded frame: rows of them laid on their sides in a deep box
    lined with green baize, a gap at the bottom where some have dropped out. Origin on the wall
    plane at the middle of the bottom of the frame."""
    oak = Part("Oak", bevel=0.6)
    w, h, d = 52.0, 72.0, 7.0
    for sx in (-1, 1):
        cbox(oak, sx * (w * 0.5 - 2.5), d * 0.5, h * 0.5, 5.0, d, h)
    for z in (2.5, h - 2.5):
        cbox(oak, 0.0, d * 0.5, z, w, d, 5.0)
    felt = Part("Felt")
    cbox(felt, 0.0, 0.6, h * 0.5, w - 9.0, 1.2, h - 9.0)
    corks = Part("Cork")
    stains = Part("Stain")
    rng = random.Random(21)
    rows = 24
    for r in range(rows):
        z = 6.0 + r * 2.55 + 1.2
        offset = (r % 2) * 2.3
        x = -w * 0.5 + 5.0 + offset
        k = 0
        while x + 4.6 < w * 0.5 - 4.6:
            missing = r < 3 and rng.random() < 0.55 or rng.random() < 0.04
            if not missing:
                m = at(x + 2.3, 2.6, z, pitch=90.0 + rng.uniform(-6, 6), yaw=rng.uniform(-4, 4))
                lathe(corks, [(0.0, -2.2), (1.15, -2.2), (1.15, 2.2), (0.0, 2.2)], m, sides=10)
                if rng.random() < 0.5:
                    lathe(stains, [(0.0, 2.19), (1.05, 2.19), (1.05, 2.25), (0.0, 2.25)], m if rng.random() < 0.5 else m @ Matrix.Rotation(math.pi, 4, "Y"), sides=10)
            x += 4.7
            k += 1
    return [oak, felt, corks, stains]


def framed_print():
    """A small framed engraving for the alcove wall, 34 x 40. The print is a plane in C++ (the atlas)."""
    oak = Part("Oak", bevel=0.4)
    w, h, d = 34.0, 40.0, 2.4
    for sx in (-1, 1):
        cbox(oak, sx * (w * 0.5 - 1.75), d * 0.5, h * 0.5, 3.5, d, h)
    for z in (1.75, h - 1.75):
        cbox(oak, 0.0, d * 0.5, z, w, d, 3.5)
    gilt = Part("Gilt")
    for sx in (-1, 1):
        cbox(gilt, sx * (w * 0.5 - 3.7), d - 0.4, h * 0.5, 0.4, 0.5, h - 7.0)
    for z in (3.7, h - 3.7):
        cbox(gilt, 0.0, d - 0.4, z, w - 7.0, 0.5, 0.4)
    back = Part("Shadow")
    cbox(back, 0.0, 0.3, h * 0.5, w - 6.0, 0.6, h - 6.0)
    return [oak, gilt, back]


# ---------------------------------------------------------------------------------------------

MODELS = [
    ("wine_vault", vault), ("wine_arch", alcove_arch), ("wine_pier", pier),
    ("wine_rack_back", rack_back), ("wine_rack_spine", rack_spine),
    ("wine_bottle_a", lambda: bottle(BORDEAUX, 0)), ("wine_bottle_b", lambda: bottle(BURGUNDY, 1)),
    ("wine_bottle_c", lambda: bottle(BORDEAUX, 2)), ("wine_bottle_d", lambda: bottle(BORDEAUX, 3)),
    ("wine_bottle_open", lambda: bottle(BORDEAUX, 7, open_mouth=True)),
    ("wine_bottle_open_b", lambda: bottle(BURGUNDY, 4, open_mouth=True)),
    ("wine_bottle_full", lambda: bottle(BORDEAUX, 6)),
    ("wine_bottle_broken", bottle_broken), ("wine_bottle_neck", bottle_neck), ("wine_shards", shards),
    ("wine_cork", cork), ("wine_glass", wine_glass), ("wine_decanter", decanter), ("wine_decanter_stopper", decanter_stopper),
    ("cellar_candlestick", candlestick), ("cellar_chamberstick", chamberstick), ("cellar_corkscrew", corkscrew),
    ("cellar_table", tasting_table), ("cellar_writing_table", writing_table), ("cellar_armchair", armchair),
    ("cellar_pedestal_table", pedestal_table), ("cellar_bookshelf", bookshelf), ("cellar_wine_cabinet", wine_cabinet),
    ("cellar_cradle", cradle), ("wine_crate", crate_closed), ("wine_crate_open", crate_open), ("cellar_tray", tray),
    ("cellar_key_rack", key_rack), ("cellar_clock", wall_clock), ("cellar_cork_board", cork_board), ("cellar_print_frame", framed_print),
]

# Repeated by the hundred or the thousand: imported with Nanite on.
NANITE = {"wine_rack_back", "wine_rack_spine", "wine_bottle_a", "wine_bottle_b", "wine_bottle_c", "wine_bottle_d", "wine_cork", "wine_vault"}


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
        marker = os.path.join(folder, "NANITE")
        if name in NANITE:
            open(marker, "w").close()
        elif os.path.exists(marker):
            os.remove(marker)
        baked.append(obj)
        print("CELLAR baked {} ({} verts)".format(name, len(obj.data.vertices)))

    # Previews: the architecture alone, the furniture in a row, and the small things on a grid.
    arch = [o for o in baked if o.name in ("wine_vault", "wine_arch")]
    big = [o for o in baked if o.name.startswith("cellar_") and o.dimensions.z > 0.5 or o.name.startswith("wine_rack") or o.name.startswith("wine_crate")]
    small = [o for o in baked if o not in arch and o not in big]
    for o in arch:
        o.location = (0.0, 30.0, 0.0)
    if any(o.name == "wine_arch" for o in arch):
        next(o for o in arch if o.name == "wine_arch").location = (-3.6, 30.0, 0.0)
    x = 0.0
    for obj in big:
        width = obj.dimensions.x
        obj.location = (x + width * 0.5, 0.0, 0.0)
        x += width + 0.3
    for i, obj in enumerate(small):
        obj.location = (-1.0 - (i % 6) * 0.3, 3.0 + (i // 6) * 0.3, 0.0)
    K.preview(baked, PREVIEW_DIR, "models", [
        ("furniture", (x * 0.5, -10.0, 2.6), (x * 0.5, 0.0, 0.8)),
        ("small", (-1.75, 2.0, 1.0), (-1.75, 3.7, 0.05)),
        ("vault", (6.0, 30.0 - 3.0, 1.6), (-2.0, 30.0, 3.0)),
    ])


main()
