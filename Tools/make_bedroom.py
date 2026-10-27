"""
Builds the master bedroom's furniture and belongings in Blender, headless, and exports each as an
FBX the art pipeline imports.

    blender --background --factory-startup --python Tools/make_bedroom.py [-- --only=a,b]

The master bedroom is Room01, the room the detective wakes in and the room the story ends in. It
was done over ten or twenty years before the house was shut: the Victorian shell kept (the window,
the door, the plaster cornice), and modern things put into it — a king-size bed with a tall
upholstered headboard, built-in wardrobes in matte white with long brushed handles, a white
dresser, a floating cabinet under a big white television, a white fabric armchair by the window.
Nothing on Poly Haven is any of those, so they are generated here, as the other rooms' furniture
is: bevelled edges that catch the lantern, cushions as rounded solids rather than boxes, the
bedding as one continuous cloth that falls over the sides of the mattress.

Outputs Art/Source/Models/<name>/<name>.fbx with an empty GENERATED file beside it (re-imported
every run with its own normals), and Workbench previews in Saved/BedroomPreview/.

Frame, as every prop in the house: centimetres, origin on the floor under the middle of the
footprint, the front facing local +Y. Exceptions are noted on the model: wall-hung things have
their origin on the wall plane (the television, the clock, the canvas, the mouldings), the ceiling
light hangs from z = 0, the wardrobe's loose door has its origin on its hinge, small things stand
at their own base. The numbers ABedroomActor also needs are the constants below, and must stay in
step with MasterBedroomActor.h.

Material slots are set by name in C++ (AMasterBedroomActor). Their texel sizes here match the
surface each is given there, whose instances have their tiling at one:
    Paint, Inside       bedroom_paint (RoomSurfaces::Paint, 120cm): white paint, or the door's black
    Upholstery, Linen, Mattress, Throw, Shade, Cloth1-3, Lining   rough_linen (Drapery, 34cm)
    Oak                 wood_floor (170cm), the floor's oak
    Pictures            the baked bedroom_pictures atlas (Tools/make_bedroom_art.py), own UVs
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
from blender_kit import Part, at, box, cbox, lathe, prism_xz, tube  # noqa: E402

PREVIEW_DIR = os.path.join(K.ROOT, "Saved", "BedroomPreview")

PAINT = 120.0
CLOTH = 34.0
OAK = 170.0
FLAT = 100.0

SLOTS = {
    "Paint": ((0.86, 0.85, 0.82, 1.0), PAINT),
    "Inside": ((0.74, 0.73, 0.70, 1.0), PAINT),
    "Upholstery": ((0.82, 0.80, 0.75, 1.0), CLOTH),
    "Linen": ((0.92, 0.92, 0.90, 1.0), CLOTH),
    "Mattress": ((0.88, 0.87, 0.84, 1.0), CLOTH),
    "Throw": ((0.72, 0.62, 0.48, 1.0), CLOTH),
    "Shade": ((0.85, 0.82, 0.74, 1.0), CLOTH),
    "Cloth1": ((0.30, 0.36, 0.48, 1.0), CLOTH),
    "Cloth2": ((0.80, 0.78, 0.72, 1.0), CLOTH),
    "Cloth3": ((0.45, 0.40, 0.38, 1.0), CLOTH),
    "Lining": ((0.30, 0.30, 0.34, 1.0), CLOTH),
    "Oak": ((0.36, 0.22, 0.12, 1.0), OAK),
    "Metal": ((0.62, 0.62, 0.62, 1.0), FLAT),
    "Brass": ((0.70, 0.55, 0.28, 1.0), FLAT),
    "Shadow": ((0.03, 0.03, 0.03, 1.0), FLAT),
    "Plastic": ((0.90, 0.90, 0.90, 1.0), FLAT),
    "Black": ((0.04, 0.04, 0.045, 1.0), FLAT),
    "Screen": ((0.01, 0.01, 0.012, 1.0), FLAT),
    "Mirror": ((0.35, 0.37, 0.40, 1.0), FLAT),
    "Frame": ((0.03, 0.03, 0.03, 1.0), FLAT),
    "Glass": ((0.70, 0.78, 0.80, 1.0), FLAT),
    "Liquid": ((0.60, 0.40, 0.15, 1.0), FLAT),
    "Ceramic": ((0.90, 0.89, 0.86, 1.0), FLAT),
    "Stone": ((0.30, 0.29, 0.28, 1.0), FLAT),
    "Soil": ((0.12, 0.08, 0.05, 1.0), FLAT),
    "Stem": ((0.30, 0.22, 0.14, 1.0), FLAT),
    "Leaf": ((0.42, 0.30, 0.16, 1.0), FLAT),
    "Leather": ((0.18, 0.10, 0.06, 1.0), FLAT),
    "Velvet": ((0.30, 0.10, 0.14, 1.0), FLAT),
    "Diffuser": ((0.95, 0.95, 0.93, 1.0), FLAT),
    "Face": ((0.08, 0.08, 0.09, 1.0), FLAT),
    "Cover": ((0.20, 0.25, 0.35, 1.0), FLAT),
    "Pages": ((0.90, 0.86, 0.76, 1.0), FLAT),
    "Pictures": ((0.80, 0.78, 0.72, 1.0), FLAT),
}

# The baked atlas: name -> (x, y, w, h) in pixels of a 1024 sheet. Must stay in step with
# make_bedroom_art.py: PICTURES.
ATLAS = 1024.0
PICTURES = {
    "artwork": (0, 0, 1024, 658),
    "wedding": (0, 680, 256, 320),
    "family": (280, 680, 320, 256),
}

# Shared with MasterBedroomActor.h.
BED_WIDTH = 206.0          # the frame, across
BED_LENGTH = 216.0         # the frame, headboard to foot
MATTRESS_TOP = 58.0
NIGHTSTAND_TOP = 58.0
BENCH_TOP = 46.0
DRESSER_TOP = 84.0
MEDIA_HEIGHT = 36.0
WARDROBE_MODULE = 100.0
WARDROBE_DEPTH = 62.0
WARDROBE_HEIGHT = 290.0
SIDE_TABLE_TOP = 56.0
TRAY_TOP = 1.4


# ---------------------------------------------------------------------------------------------
# Helpers.
# ---------------------------------------------------------------------------------------------

def no_uvs(bm):
    """For parts whose UVs were laid while they were built: keeps box_uvs off them."""
    return None


def spow(value, exponent):
    return math.copysign(abs(value) ** exponent, value)


def cushion(part, centre, size, matrix=None, round_v=0.45, round_h=0.25, pinch=0.0, rings=14, sides=32):
    """
    An upholstered block: a superellipsoid, square in plan with rounded corners (round_h) and
    domed top and bottom (round_v), optionally pinched thin towards its edges, as a stuffed
    pillow is. Every cushion in here is one of these: a box with a bevel is a slab of wood with a
    fabric texture on it, and what reads as stuffing is that there is no flat face anywhere.
    """
    a, b, c = size[0] * 0.5, size[1] * 0.5, size[2] * 0.5
    bm = part.bm
    m = Matrix.Translation(centre) @ (matrix or Matrix.Identity(4))
    rows = []
    bottom = bm.verts.new(m @ Vector((0.0, 0.0, -c * (1.0 - pinch * 0.0))))
    for i in range(1, rings):
        lat = -math.pi * 0.5 + math.pi * i / rings
        ring = []
        for k in range(sides):
            lon = -math.pi + 2 * math.pi * k / sides
            x = a * spow(math.cos(lat), round_v) * spow(math.cos(lon), round_h)
            y = b * spow(math.cos(lat), round_v) * spow(math.sin(lon), round_h)
            z = c * spow(math.sin(lat), round_v)
            edge = max(abs(x) / a, abs(y) / b)
            z *= 1.0 - pinch * edge ** 3
            ring.append(bm.verts.new(m @ Vector((x, y, z))))
        rows.append(ring)
    top = bm.verts.new(m @ Vector((0.0, 0.0, c)))
    for k in range(sides):
        j = (k + 1) % sides
        bm.faces.new((bottom, rows[0][j], rows[0][k]))
        bm.faces.new((top, rows[-1][k], rows[-1][j]))
        for r in range(len(rows) - 1):
            bm.faces.new((rows[r][k], rows[r][j], rows[r + 1][j], rows[r + 1][k]))


def solid_grid(part, grid, thick):
    """
    A cloth surface given as a grid of points (rows of columns), made a closed solid of this
    thickness: both faces, the edges closed. Closed, so the bake's normals have an inside.
    """
    bm = part.bm
    nu, nv = len(grid), len(grid[0])
    outer, inner = [], []
    for i in range(nu):
        row_o, row_i = [], []
        for j in range(nv):
            du = grid[min(i + 1, nu - 1)][j] - grid[max(i - 1, 0)][j]
            dv = grid[i][min(j + 1, nv - 1)] - grid[i][max(j - 1, 0)]
            n = du.cross(dv)
            n = n.normalized() if n.length > 1e-6 else Vector((0, 0, 1))
            row_o.append(bm.verts.new(grid[i][j] + n * thick * 0.5))
            row_i.append(bm.verts.new(grid[i][j] - n * thick * 0.5))
        outer.append(row_o)
        inner.append(row_i)
    for i in range(nu - 1):
        for j in range(nv - 1):
            bm.faces.new((outer[i][j], outer[i + 1][j], outer[i + 1][j + 1], outer[i][j + 1]))
            bm.faces.new((inner[i][j + 1], inner[i + 1][j + 1], inner[i + 1][j], inner[i][j]))
    for i in range(nu - 1):
        bm.faces.new((outer[i][0], inner[i][0], inner[i + 1][0], outer[i + 1][0]))
        bm.faces.new((outer[i + 1][nv - 1], inner[i + 1][nv - 1], inner[i][nv - 1], outer[i][nv - 1]))
    for j in range(nv - 1):
        bm.faces.new((outer[0][j + 1], inner[0][j + 1], inner[0][j], outer[0][j]))
        bm.faces.new((outer[nu - 1][j], inner[nu - 1][j], inner[nu - 1][j + 1], outer[nu - 1][j + 1]))


def cover(part, x0, x1, y0, y1, top, side_fall, foot_fall, thick, seed, head_fall=0.0, step=3.0, rumple=0.8, floor=1.0):
    """
    Cloth laid over a rectangle [x0, x1] x [y0, y1] at height top, falling side_fall over both
    long sides, foot_fall over the +Y end and head_fall over the -Y end: one flat sheet, unfolded
    onto the block. A point of the sheet that lies over the block stays on top of it; a point past
    an edge goes round the edge on a short radius and then down, flaring a little, with folds that
    open the further it falls. A duvet is exactly that, and a heap of boxes is not.
    """
    rng = random.Random(seed)
    waves = [(rng.uniform(0, 2 * math.pi), rng.uniform(14.0, 26.0)) for _ in range(3)]
    bumps = [(rng.uniform(0, 2 * math.pi), rng.uniform(0, 2 * math.pi), rng.uniform(18.0, 40.0)) for _ in range(4)]
    radius = 4.0
    us = [x0 - side_fall + k * step for k in range(int((x1 - x0 + 2 * side_fall) / step) + 1)]
    vs = [y0 - head_fall + k * step for k in range(int((y1 - y0 + foot_fall + head_fall) / step) + 1)]
    grid = []
    for u in us:
        col = []
        for v in vs:
            cu = min(max(u, x0), x1)
            cv = min(max(v, y0), y1)
            dx, dy = u - cu, v - cv
            d = math.hypot(dx, dy)
            if d < 1e-6:
                z = top + rumple * sum(math.sin(u / s + p) * math.cos(v / s + q) for p, q, s in bumps) / len(bumps)
                col.append(Vector((u, v, z)))
                continue
            out = Vector((dx / d, dy / d, 0.0))
            along = (v if abs(dx) > abs(dy) else u)
            if d < radius * math.pi * 0.5:
                angle = d / radius
                h = radius * math.sin(angle)
                z = top - radius * (1.0 - math.cos(angle))
            else:
                hang = d - radius * math.pi * 0.5
                h = radius + 0.07 * hang
                z = top - radius - hang
            fold = min(1.0, d / 18.0) * sum(math.sin(along / wl + ph) for ph, wl in waves) / len(waves)
            h += 1.6 * fold
            col.append(Vector((cu, cv, max(z, floor))) + out * h)
        grid.append(col)
    solid_grid(part, grid, thick)


def picture_quad(part, corners, name, thickness=0.05):
    """A print or a canvas face: a closed thin box whose front shows one rectangle of the atlas,
    corners (bl, br, tr, tl) of the face as seen from the front. The back goes the way (right x up)
    points, which in the game's left-handed frame is away from whoever reads it (the 10-22 note)."""
    x, y, w, h = PICTURES[name]
    bm = part.bm
    uv = bm.loops.layers.uv.verify()
    bl, br, tr, tl = (Vector(c) for c in corners)
    n = (br - bl).cross(tl - bl).normalized() * thickness
    front = [bm.verts.new(v) for v in (bl, br, tr, tl)]
    back = [bm.verts.new(v + n) for v in (bl, br, tr, tl)]
    face = bm.faces.new(front)
    for loop, (s, t) in zip(face.loops, ((0, 1), (1, 1), (1, 0), (0, 0))):
        loop[uv].uv = ((x + s * w) / ATLAS, 1.0 - (y + t * h) / ATLAS)
    edge = ((x + 2) / ATLAS, 1.0 - (y + 2) / ATLAS)
    for f in ([back[3], back[2], back[1], back[0]], [front[0], back[0], back[1], front[1]], [front[1], back[1], back[2], front[2]],
              [front[2], back[2], back[3], front[3]], [front[3], back[3], back[0], front[0]]):
        face = bm.faces.new(f)
        for loop in face.loops:
            loop[uv].uv = edge


def bar_handle(part, x0, x1, z, y_face, proud=2.4, radius=0.6, vertical=False):
    """A long brushed-steel bar pull on two stand-off posts: the modern wardrobe's handle."""
    if vertical:
        tube(part, [(x0, y_face + proud, z), (x0, y_face + proud, x1)], radius, sides=12)
        for zz in (z + 2.0, x1 - 2.0):
            tube(part, [(x0, y_face, zz), (x0, y_face + proud, zz)], radius * 0.8, sides=10)
    else:
        tube(part, [(x0, y_face + proud, z), (x1, y_face + proud, z)], radius, sides=12)
        for xx in (x0 + 2.0, x1 - 2.0):
            tube(part, [(xx, y_face, z), (xx, y_face + proud, z)], radius * 0.8, sides=10)


def tapered_round_leg(part, x, y, z0, z1, r_top, r_bottom, splay=(0.0, 0.0)):
    """A turned leg, thinner at its foot, splayed out by (dx, dy) at the floor."""
    tube(part, [(x + splay[0], y + splay[1], z0), (x, y, z1)], [r_bottom, r_top], sides=16)


def drawer_front(paint, shadow, x0, x1, z0, z1, y_face, gap=0.35):
    """A flush drawer front: the face and a dark reveal round it, which is all a flush drawer is."""
    cbox(shadow, (x0 + x1) * 0.5, y_face - 0.4, (z0 + z1) * 0.5, x1 - x0, 0.9, z1 - z0)
    box(paint, x0 + gap, x1 - gap, y_face - 0.3, y_face + 1.8, z0 + gap, z1 - gap)


# ---------------------------------------------------------------------------------------------
# The bed.
# ---------------------------------------------------------------------------------------------

def bed():
    """A king-size bed: a white platform frame, a tall upholstered headboard in channels, the
    mattress, the duvet turned down, pillows and cushions against the headboard, and a beige throw
    across the foot. Head at -Y, foot at +Y."""
    paint = Part("Paint", bevel=0.8)
    shadow = Part("Shadow")
    upholstery = Part("Upholstery", smooth=True)
    linen = Part("Linen", smooth=True)
    mattress = Part("Mattress", bevel=3.0, segments=3)
    throw = Part("Throw", smooth=True)
    hw, hl = BED_WIDTH * 0.5, BED_LENGTH * 0.5

    # The platform: a frame of rails standing on a recessed plinth, so it seems to float.
    cbox(shadow, 0, 4, 4.5, BED_WIDTH - 24, BED_LENGTH - 30, 9)
    box(paint, -hw, hw, -hl + 10, hl, 9, 34)
    # The headboard: a painted board behind nine stuffed channels, wider than the bed.
    head_y = -hl + 5
    box(paint, -hw - 10, hw + 10, head_y - 5, head_y + 3, 0, 150)
    channels = 9
    cw = (BED_WIDTH + 16) / channels
    for k in range(channels):
        x = -hw - 8 + cw * (k + 0.5)
        cushion(upholstery, (x, head_y + 5.5, 90), (cw + 0.6, 7.5, 114), round_v=0.18, round_h=0.4, rings=16, sides=20)
    # A capping roll along the top.
    cushion(upholstery, (0, head_y + 3, 149), (BED_WIDTH + 22, 11, 9), round_v=0.6, round_h=0.2)

    # Mattress, in its fitted sheet.
    mw, my0, my1 = 194.0, head_y + 10, hl - 3
    box(mattress, -mw * 0.5, mw * 0.5, my0, my1, 34, MATTRESS_TOP)

    # The duvet, turned down a third of the way and falling over the sides and the foot.
    dy0 = my0 + 62
    cover(linen, -mw * 0.5, mw * 0.5, dy0, my1, MATTRESS_TOP + 3.5, 30, 28, 5.0, 31, step=2.5, rumple=1.4, floor=6.0)
    # The turned-down band: the top of the duvet folded back over itself, a soft roll across.
    cushion(linen, (0, dy0 + 11, MATTRESS_TOP + 7.5), (mw + 6, 30, 10), round_v=0.55, round_h=0.18, pinch=0.25)

    # Pillows: two sleeping pillows standing against the headboard, two square shams in front,
    # two small cushions in front of those. Each leans back a little and sits a little off true.
    def pillow(x, y, z, sx, sy, sz, lean, yaw, part, pinch=0.45):
        m = Matrix.Rotation(math.radians(yaw), 4, "Z") @ Matrix.Rotation(math.radians(-lean), 4, "X")
        cushion(part, (x, y, z), (sx, sz, sy), m, round_v=0.55, round_h=0.32, pinch=pinch)

    pillow(-48, my0 + 12, MATTRESS_TOP + 24, 74, 50, 16, 18, 3, linen)
    pillow(48, my0 + 12, MATTRESS_TOP + 24, 74, 50, 16, 15, -4, linen)
    pillow(-44, my0 + 27, MATTRESS_TOP + 26, 62, 58, 17, 24, -6, upholstery)
    pillow(44, my0 + 27, MATTRESS_TOP + 26, 62, 58, 17, 21, 5, upholstery)
    pillow(-26, my0 + 42, MATTRESS_TOP + 16, 46, 32, 13, 32, 10, throw, 0.35)
    pillow(30, my0 + 41, MATTRESS_TOP + 15, 40, 40, 13, 34, -12, throw, 0.35)

    # The throw across the foot, over the duvet, its ends hanging down both sides.
    cover(throw, -mw * 0.5 - 3, mw * 0.5 + 3, my1 - 58, my1 - 12, MATTRESS_TOP + 7.5, 34, 0, 2.2, 37, step=2.5, rumple=0.9, floor=8.0)
    return [paint, shadow, upholstery, linen, mattress, throw]


def nightstand():
    """A white two-drawer nightstand on four oak legs, a long steel pull on each drawer. 52 x 40."""
    paint = Part("Paint", bevel=0.6)
    shadow = Part("Shadow")
    oak = Part("Oak")
    metal = Part("Metal")
    w, d = 52.0, 40.0
    box(paint, -w * 0.5, w * 0.5, -d * 0.5, d * 0.5, 16, NIGHTSTAND_TOP)
    for sx in (-1, 1):
        for sy in (-1, 1):
            tapered_round_leg(oak, sx * (w * 0.5 - 5), sy * (d * 0.5 - 5), 0, 16.5, 1.5, 0.9, (sx * 1.2, sy * 1.2))
    face = d * 0.5
    for z0, z1 in ((18.5, 36.5), (37.5, 55.5)):
        drawer_front(paint, shadow, -w * 0.5 + 2, w * 0.5 - 2, z0, z1, face)
        bar_handle(metal, -12, 12, z1 - 4.5, face + 1.8, proud=1.8, radius=0.45)
    return [paint, shadow, oak, metal]


def bench():
    """The storage bench at the foot of the bed: a white box on a recessed plinth under a deep
    upholstered lid. 140 x 44."""
    paint = Part("Paint", bevel=0.6)
    shadow = Part("Shadow")
    upholstery = Part("Upholstery", smooth=True)
    w, d = 140.0, 44.0
    cbox(shadow, 0, 0, 3, w - 10, d - 10, 6)
    box(paint, -w * 0.5, w * 0.5, -d * 0.5, d * 0.5, 6, 35)
    cbox(shadow, 0, d * 0.5, 34.8, w - 1, 0.6, 0.6)
    cushion(upholstery, (0, 0, 40.5), (w + 1, d + 1, 11), round_v=0.4, round_h=0.16, pinch=0.15)
    return [paint, shadow, upholstery]


# ---------------------------------------------------------------------------------------------
# The wardrobes: one module, closed; one with its right door gone (hung separately); the door.
# ---------------------------------------------------------------------------------------------

def wardrobe_carcass(right_door):
    """A built-in module 100 wide, 62 deep, 290 tall: the carcass, the inside, and its doors.
    The front is at +Y. The left door is always shut; the right one is left out when it is hung
    separately, open."""
    paint = Part("Paint", bevel=0.4)
    inside = Part("Inside")
    shadow = Part("Shadow")
    metal = Part("Metal")
    w, d, h = WARDROBE_MODULE, WARDROBE_DEPTH, WARDROBE_HEIGHT
    face = d * 0.5
    t = 1.8
    # Plinth, recessed, and the carcass over it.
    cbox(shadow, 0, -2, 5, w, d - 8, 10)
    box(paint, -w * 0.5, -w * 0.5 + t, -face, face - 2.2, 10, h)
    box(paint, w * 0.5 - t, w * 0.5, -face, face - 2.2, 10, h)
    box(paint, -w * 0.5, w * 0.5, -face, face - 2.2, h - t, h)
    box(inside, -w * 0.5 + t, w * 0.5 - t, -face, face - 2.2, 10, 10 + t)
    box(inside, -w * 0.5 + t, w * 0.5 - t, -face, -face + 1.2, 10, h - t)
    # The inside: a shelf at 225 and the rail under it, a shelf low down for shoes.
    box(inside, -w * 0.5 + t, w * 0.5 - t, -face + 1.2, face - 4, 224, 226)
    box(inside, -w * 0.5 + t, w * 0.5 - t, -face + 1.2, face - 4, 34, 36)
    tube(metal, [(-w * 0.5 + t, -2, 212), (w * 0.5 - t, -2, 212)], 1.2, sides=14)
    # Doors: two leaves 49.6 wide, a long vertical bar on each beside the meeting stiles.
    leaves = [(-w * 0.5 + 0.2, -0.2)] + ([(0.2, w * 0.5 - 0.2)] if right_door else [])
    for x0, x1 in leaves:
        box(paint, x0, x1, face - 2.2, face, 11, h - 0.5)
        hx = x1 - 5 if x1 < 0 else x0 + 5
        tube(metal, [(hx, face + 2.6, 96), (hx, face + 2.6, 166)], 0.65, sides=12)
        for zz in (99, 163):
            tube(metal, [(hx, face, zz), (hx, face + 2.6, zz)], 0.5, sides=10)
    cbox(shadow, 0, face - 1.1, h * 0.5 + 5, 0.5, 2.0, h - 12)
    return [paint, inside, shadow, metal]


def wardrobe():
    return wardrobe_carcass(True)


def wardrobe_open():
    return wardrobe_carcass(False)


def wardrobe_door():
    """The right-hand leaf of a module, alone, origin on its hinge (the module's right edge, at the
    back of the leaf): it runs off along -X, its face at +Y. Opened by a negative yaw."""
    paint = Part("Paint", bevel=0.4)
    metal = Part("Metal")
    w = WARDROBE_MODULE * 0.5 - 0.4
    box(paint, -w, 0, 0, 2.2, 11, WARDROBE_HEIGHT - 0.5)
    hx = -w + 5
    tube(metal, [(hx, 2.2 + 2.6, 96), (hx, 2.2 + 2.6, 166)], 0.65, sides=12)
    for zz in (99, 163):
        tube(metal, [(hx, 2.2, zz), (hx, 2.2 + 2.6, zz)], 0.5, sides=10)
    return [paint, metal]


# ---------------------------------------------------------------------------------------------
# The dresser, the media cabinet and the television.
# ---------------------------------------------------------------------------------------------

def dresser():
    """A long white dresser, six drawers in two columns, steel pulls, on a recessed plinth. 160 x 48."""
    paint = Part("Paint", bevel=0.6)
    shadow = Part("Shadow")
    metal = Part("Metal")
    w, d = 160.0, 48.0
    cbox(shadow, 0, -1, 4, w - 8, d - 8, 8)
    box(paint, -w * 0.5, w * 0.5, -d * 0.5, d * 0.5 - 2, 8, DRESSER_TOP)
    face = d * 0.5 - 2
    rows = ((10, 32), (33, 57), (58, DRESSER_TOP - 2))
    for cx in (-w * 0.25, w * 0.25):
        for z0, z1 in rows:
            drawer_front(paint, shadow, cx - w * 0.25 + 1.5, cx + w * 0.25 - 1.5, z0, z1, face)
            bar_handle(metal, cx - 16, cx + 16, z1 - 5, face + 1.8, proud=2.0, radius=0.5)
    return [paint, shadow, metal]


def media_cabinet():
    """The floating cabinet under the television: 190 wide, 40 deep, 36 tall, three push-to-open
    fronts with no handles. Hung on the wall; the origin is under the middle of its footprint, at
    its own underside."""
    paint = Part("Paint", bevel=0.6)
    shadow = Part("Shadow")
    w, d = 190.0, 40.0
    box(paint, -w * 0.5, w * 0.5, -d * 0.5, d * 0.5 - 2, 0, MEDIA_HEIGHT)
    face = d * 0.5 - 2
    for k in range(3):
        x0 = -w * 0.5 + 1.5 + k * (w - 3) / 3
        drawer_front(paint, shadow, x0, x0 + (w - 3) / 3, 1.5, MEDIA_HEIGHT - 1.5, face)
    return [paint, shadow]


def television():
    """A 75-inch set in white: a thin white bezel and back, black glass, on a slim wall bracket.
    Origin on the wall plane under the middle of the set; the screen faces +Y."""
    plastic = Part("Plastic", bevel=0.4)
    screen = Part("Screen")
    metal = Part("Metal")
    w, h = 168.0, 97.0
    y0 = 4.5
    box(metal, -20, 20, 0, y0, 30, 60)
    box(plastic, -w * 0.5, w * 0.5, y0, y0 + 2.8, 0, h)
    box(screen, -w * 0.5 + 0.9, w * 0.5 - 0.9, y0 + 2.8, y0 + 3.0, 2.2, h - 0.9)
    # A strip under the glass for the speaker grille and the dead standby light.
    cbox(metal, 0, y0 + 2.9, 1.1, w - 4, 0.3, 1.2)
    return [plastic, screen, metal]


# ---------------------------------------------------------------------------------------------
# The sitting corner by the window.
# ---------------------------------------------------------------------------------------------

def armchair():
    """A large modern armchair in white fabric on short oak legs: deep seat, low broad arms, a
    loose back cushion and a pillow. 94 wide, 90 deep, facing +Y."""
    up = Part("Upholstery", smooth=True)
    oak = Part("Oak")
    w, d = 94.0, 90.0
    for sx in (-1, 1):
        for sy in (-1, 1):
            tapered_round_leg(oak, sx * (w * 0.5 - 8), sy * (d * 0.5 - 8), 0, 13, 1.8, 1.1, (sx * 1.0, sy * 1.0))
    # The base, the arms, the back.
    cushion(up, (0, 0, 25), (w, d, 26), round_v=0.35, round_h=0.18)
    for sx in (-1, 1):
        cushion(up, (sx * (w * 0.5 - 10), 2, 46), (20, d - 2, 40), round_v=0.4, round_h=0.25)
    cushion(up, (0, -d * 0.5 + 10, 56), (w - 4, 20, 58), round_v=0.4, round_h=0.25)
    # Seat cushion and the loose back cushion leaning on the back.
    cushion(up, (0, 6, 43), (w - 38, d - 22, 13), round_v=0.5, round_h=0.2, pinch=0.2)
    m = Matrix.Rotation(math.radians(-16), 4, "X")
    cushion(up, (0, -d * 0.5 + 25, 62), (w - 40, 14, 44), m, round_v=0.5, round_h=0.25, pinch=0.25)
    m = Matrix.Rotation(math.radians(8), 4, "Z") @ Matrix.Rotation(math.radians(-24), 4, "X")
    cushion(up, (-8, -d * 0.5 + 34, 58), (42, 12, 34), m, round_v=0.55, round_h=0.3, pinch=0.4)
    return [up, oak]


def ottoman():
    """The footstool in the chair's fabric, on the same legs. 64 x 48."""
    up = Part("Upholstery", smooth=True)
    oak = Part("Oak")
    w, d = 64.0, 48.0
    for sx in (-1, 1):
        for sy in (-1, 1):
            tapered_round_leg(oak, sx * (w * 0.5 - 7), sy * (d * 0.5 - 7), 0, 13, 1.6, 1.0, (sx * 0.8, sy * 0.8))
    cushion(up, (0, 0, 28), (w, d, 30), round_v=0.38, round_h=0.2, pinch=0.08)
    return [up, oak]


def side_table():
    """A small round oak table on three splayed legs, the top with a softened edge. 46 across."""
    oak = Part("Oak", bevel=0.6)
    lathe(oak, [(0.0, SIDE_TABLE_TOP - 3.0), (22.6, SIDE_TABLE_TOP - 3.0), (23.0, SIDE_TABLE_TOP - 1.5), (22.6, SIDE_TABLE_TOP), (0.0, SIDE_TABLE_TOP)],
          Matrix.Identity(4), sides=48)
    lathe(oak, [(0.0, SIDE_TABLE_TOP - 6.0), (14.0, SIDE_TABLE_TOP - 6.0), (14.0, SIDE_TABLE_TOP - 3.0), (0.0, SIDE_TABLE_TOP - 3.0)], Matrix.Identity(4), sides=32)
    for k in range(3):
        a = math.radians(90 + 120 * k)
        top = (math.cos(a) * 11.0, math.sin(a) * 11.0, SIDE_TABLE_TOP - 4.5)
        foot = (math.cos(a) * 18.0, math.sin(a) * 18.0, 0.0)
        tube(oak, [foot, top], [0.9, 1.6], sides=14)
    return [oak]


def floor_lamp():
    """A tripod floor lamp: three oak legs to a brass collar, a brass stem, a linen drum shade."""
    oak = Part("Oak")
    brass = Part("Brass")
    shade = Part("Shade")
    for k in range(3):
        a = math.radians(30 + 120 * k)
        tube(oak, [(math.cos(a) * 30, math.sin(a) * 30, 0), (math.cos(a) * 3, math.sin(a) * 3, 112)], [1.3, 1.7], sides=14)
    lathe(brass, [(0.0, 108), (3.6, 108), (3.6, 116), (0.0, 116)], Matrix.Identity(4), sides=24)
    tube(brass, [(0, 0, 116), (0, 0, 140)], 0.9, sides=14)
    lathe(brass, [(0.0, 138), (2.2, 138), (2.2, 144), (0.0, 144)], Matrix.Identity(4), sides=16)
    # The drum: a shell open top and bottom, given a thickness.
    lathe(shade, [(22.0, 132), (22.6, 132), (22.6, 164), (22.0, 164), (21.4, 164), (21.4, 132)], Matrix.Identity(4), sides=48)
    for k in range(3):
        a = math.radians(90 + 120 * k)
        tube(brass, [(0, 0, 142), (math.cos(a) * 21.4, math.sin(a) * 21.4, 158)], 0.25, sides=6)
    return [oak, brass, shade]


# ---------------------------------------------------------------------------------------------
# The black mirror by the door.
# ---------------------------------------------------------------------------------------------

def mirror():
    """A tall floor mirror with an arched head in a black frame: 92 wide, 204 tall, meant to lean
    on the wall. Origin at the bottom of its back face; the glass faces +Y. The lean is the
    actor's (a pitch about the floor line)."""
    frame = Part("Frame", bevel=0.5)
    glass = Part("Mirror")
    back = Part("Shadow")
    w, h = 92.0, 204.0
    r = w * 0.5
    spring = h - r
    fw, depth = 6.5, 4.2

    def outline(inset, steps=40):
        pts = [(-r + inset, 0.0 + inset), (r - inset, 0.0 + inset)]
        for k in range(steps + 1):
            a = math.pi * k / steps
            pts.append(((r - inset) * math.cos(a), spring + (r - inset) * math.sin(a)))
        return pts

    outer = outline(0.0)
    inner = outline(fw)
    # The frame: the band between the two outlines, extruded through its depth (in XZ, across Y).
    n = len(outer)
    bm = frame.bm
    rings = []
    for y in (0.0, depth):
        rings.append(([bm.verts.new((x, y, z)) for x, z in outer], [bm.verts.new((x, y, z)) for x, z in inner]))
    (o0, i0), (o1, i1) = rings
    for k in range(n):
        j = (k + 1) % n
        bm.faces.new((o0[k], o0[j], i0[j], i0[k]))
        bm.faces.new((o1[j], o1[k], i1[k], i1[j]))
        bm.faces.new((o0[j], o0[k], o1[k], o1[j]))
        bm.faces.new((i0[k], i0[j], i1[j], i1[k]))
    # The glass in its rebate, a little back from the frame's face, and the board behind it.
    prism_xz(glass, inner[::-1], depth - 1.6, depth - 1.0)
    prism_xz(back, outline(fw - 1.0)[::-1], 0.3, depth - 1.6)
    return [frame, glass, back]


# ---------------------------------------------------------------------------------------------
# Things left out.
# ---------------------------------------------------------------------------------------------

def shell_box(part, x0, x1, y0, y1, z0, z1, t):
    """An open box: a floor and four walls of thickness t, open at the top."""
    box(part, x0, x1, y0, y1, z0, z0 + t)
    box(part, x0, x1, y0, y0 + t, z0 + t, z1)
    box(part, x0, x1, y1 - t, y1, z0 + t, z1)
    box(part, x0, x0 + t, y0 + t, y1 - t, z0 + t, z1)
    box(part, x1 - t, x1, y0 + t, y1 - t, z0 + t, z1)


def suitcase_open():
    """A hard-shell case lying open on the floor, packed: the base shell (+Y half) full of folded
    clothes, the lid shell (-Y half) lying back flat with its lining strap across. Hinge along
    y = 0. 72 x 94 open."""
    shell = Part("Black", bevel=1.6, segments=3)
    lining = Part("Lining")
    metal = Part("Metal")
    c1, c2, c3 = Part("Cloth1", smooth=True), Part("Cloth2", smooth=True), Part("Cloth3", smooth=True)
    w, d = 72.0, 46.0
    shell_box(shell, -w * 0.5, w * 0.5, 0.5, d, 0, 24, 1.4)
    shell_box(shell, -w * 0.5, w * 0.5, -d, -0.5, 0, 9, 1.4)
    box(lining, -w * 0.5 + 1.4, w * 0.5 - 1.4, 1.9, d - 1.4, 1.4, 1.8)
    box(lining, -w * 0.5 + 1.4, w * 0.5 - 1.4, -d + 1.4, -1.9, 1.4, 1.8)
    # The zip round both rims, the wheels at the base's far end, the handle housing.
    for y0, y1, z in ((0.5, d, 24), (-d, -0.5, 9)):
        box(metal, -w * 0.5 - 0.2, w * 0.5 + 0.2, y0 - 0.2, y0 + 0.6, z - 0.4, z + 0.2)
        box(metal, -w * 0.5 - 0.2, w * 0.5 + 0.2, y1 - 0.6, y1 + 0.2, z - 0.4, z + 0.2)
    for sx in (-1, 1):
        lathe(metal, [(0.0, -1.0), (3.0, -1.0), (3.0, 1.0), (0.0, 1.0)], at(sx * (w * 0.5 - 6), d + 1.0, 4.0, 0, 90, 0) @ Matrix.Identity(4), sides=16)
    # The lid's elastic strap and the clothes: three folded garments stacked, a fourth half
    # unfolded and hanging over the rim, as if somebody had stopped halfway through.
    box(lining, -w * 0.5 + 1.4, w * 0.5 - 1.4, -d * 0.5 - 2, -d * 0.5 + 2, 1.8, 2.6)
    cushion(c1, (-12, d * 0.5, 7), (38, 32, 9), round_v=0.35, round_h=0.18, pinch=0.1)
    cushion(c2, (-11, d * 0.5 + 1, 14.5), (36, 30, 7), Matrix.Rotation(math.radians(4), 4, "Z"), round_v=0.35, round_h=0.18, pinch=0.1)
    cushion(c3, (18, d * 0.5 - 1, 8), (26, 36, 12), round_v=0.4, round_h=0.2, pinch=0.1)
    cushion(c1, (17, d * 0.5, 17), (24, 32, 6), Matrix.Rotation(math.radians(-6), 4, "Z"), round_v=0.4, round_h=0.2, pinch=0.1)
    cover(c2, 4, 30, d - 14, d - 2, 21.5, 0, 26, 1.0, 51, step=1.5, rumple=0.4, floor=0.6)
    return [shell, lining, metal, c1, c2, c3]


def leaf_sheet(part, origin, direction, length, width, curl, droop, seed):
    """A dead leaf: an oval blade along direction, curled across its width and drooping along its
    length, made a thin solid."""
    rng = random.Random(seed)
    d = Vector(direction).normalized()
    side = d.cross(Vector((0, 0, 1)))
    if side.length < 1e-3:
        side = Vector((1, 0, 0))
    side.normalize()
    up = side.cross(d).normalized()
    grid = []
    nu, nv = 9, 5
    for i in range(nu):
        t = i / (nu - 1)
        half = width * 0.5 * math.sin(math.pi * min(0.98, max(0.02, t))) ** 0.8
        col = []
        for j in range(nv):
            s = -1.0 + 2.0 * j / (nv - 1)
            p = Vector(origin) + d * (t * length) + side * (s * half)
            p += up * (curl * (s * s) * half * 0.6 - droop * t * t * length + rng.uniform(-0.15, 0.15))
            col.append(p)
        grid.append(col)
    solid_grid(part, grid, 0.12)


def plant():
    """A tall plant that died in its pot by the window: a white glazed planter, the soil gone to
    dust, three bare stems with a few brown leaves curled and hanging off them, and the leaves that
    dropped lying round the pot."""
    pot = Part("Ceramic", bevel=0.3)
    soil = Part("Soil")
    stem = Part("Stem")
    leaf = Part("Leaf")
    lathe(pot, [(0.0, 0.0), (15.0, 0.0), (15.5, 1.0), (19.5, 40.0), (20.2, 41.0), (20.2, 42.0), (18.8, 42.0), (18.4, 41.0), (14.2, 4.0), (0.0, 4.0)],
          Matrix.Identity(4), sides=40)
    lathe(soil, [(0.0, 36.0), (18.6, 36.0), (18.7, 37.0), (0.0, 37.4)], Matrix.Identity(4), sides=32)
    rng = random.Random(61)
    stems = [((-3, 2), (-10, 8), 132, 3), ((4, -2), (12, -6), 150, 4), ((1, 4), (2, 16), 104, 3)]
    for (bx, by), (tx, ty), height, leaves in stems:
        path = [(bx, by, 36.5)]
        for k in range(1, 7):
            t = k / 6
            path.append((bx + (tx - bx) * t + math.sin(t * 3) * 2, by + (ty - by) * t, 36.5 + (height - 36.5) * t))
        tube(stem, path, [1.1 - 0.7 * k / 6 for k in range(7)], sides=8)
        for k in range(leaves):
            t = 0.55 + 0.45 * k / max(1, leaves - 1)
            i = min(5, int(t * 6))
            p = Vector(path[i])
            a = rng.uniform(0, 2 * math.pi)
            leaf_sheet(leaf, p, (math.cos(a), math.sin(a), -0.9 - rng.uniform(0, 0.6)), rng.uniform(16, 24), rng.uniform(9, 13),
                       rng.uniform(0.5, 1.2), rng.uniform(0.05, 0.2), 70 + k)
    for k in range(7):
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(26, 48)
        leaf_sheet(leaf, (math.cos(a) * r, math.sin(a) * r, 0.4), (math.cos(a + 1.3), math.sin(a + 1.3), 0.0), rng.uniform(14, 22),
                   rng.uniform(8, 12), rng.uniform(0.6, 1.4), -0.02, 90 + k)
    return [pot, soil, stem, leaf]


def wall_clock():
    """A modern wall clock, 46 across: a thin brass ring, a charcoal face with no numerals at all,
    two fine brass hands and a centre cap. Origin on the wall plane at its middle; the face is +Y.
    The hands stand at a little past eleven, and without a numeral on the dial and hung crooked as
    it is, nobody could say to the minute what time it stopped at."""
    brass = Part("Brass")
    face = Part("Face")
    r = 23.0
    m = at(0, 0, 0, 0, 0, -90)
    lathe(face, [(0.0, 0.0), (r - 0.8, 0.0), (r - 0.8, 2.6), (0.0, 2.6)], m, sides=64)
    ring = [(math.cos(2 * math.pi * k / 72) * r, 3.0, math.sin(2 * math.pi * k / 72) * r) for k in range(73)]
    tube(brass, ring, 0.9, sides=10)
    # Twelve fine brass bars where the hours would be: an index, not a numeral.
    for k in range(12):
        a = 2 * math.pi * k / 12
        x, z = math.sin(a), math.cos(a)
        tube(brass, [(x * (r - 3.4), 2.8, z * (r - 3.4)), (x * (r - (6.0 if k % 3 == 0 else 4.6)), 2.8, z * (r - (6.0 if k % 3 == 0 else 4.6)))], 0.28, sides=6)
    # Seen from in front (from +Y, looking along -Y) the viewer's right is +X in the game's
    # left-handed frame, so clockwise from twelve is (sin, cos). The bedroom clock on the old door
    # wall faced the other way, which is where the 09-21 note's (-sin, cos) comes from.
    for hand_angle, length, width in (((11 + 4 / 60) * 30.0, 11.5, 0.7), ((4 / 60) * 360.0, 17.0, 0.45)):
        a = math.radians(hand_angle)
        x, z = math.sin(a), math.cos(a)
        tube(brass, [(-x * 2.0, 3.2, -z * 2.0), (x * length, 3.2, z * length)], width, sides=6)
    lathe(brass, [(0.0, 2.6), (1.2, 2.6), (1.2, 4.0), (0.0, 4.2)], m, sides=20)
    return [brass, face]


def ceiling_light():
    """A white flush-mount ceiling light: a shallow steel base on the ceiling and an opal dome.
    Hangs from z = 0 (the ceiling), down -Z."""
    plastic = Part("Plastic", bevel=0.3)
    diffuser = Part("Diffuser")
    lathe(plastic, [(0.0, -2.6), (26.0, -2.6), (26.5, -1.5), (26.0, 0.0), (0.0, 0.0)], Matrix.Identity(4), sides=64)
    profile = [(0.0, -11.5)]
    for k in range(1, 11):
        a = math.pi * 0.5 * k / 10
        profile.append((23.0 * math.sin(a), -2.6 - 8.9 * math.cos(a)))
    profile.append((0.0, -2.6))
    lathe(diffuser, profile, Matrix.Identity(4), sides=64)
    return [plastic, diffuser]


def artwork():
    """The canvas over the bed, 140 x 90, a gallery wrap in a thin oak tray frame with a shadow gap.
    Origin on the wall plane at its middle; the picture faces +Y."""
    pictures = Part("Pictures")
    pictures.uv_fn = no_uvs
    oak = Part("Oak")
    shadow = Part("Shadow")
    w, h, d = 140.0, 90.0, 3.8
    picture_quad(pictures, [(-w / 2, d, -h / 2), (w / 2, d, -h / 2), (w / 2, d, h / 2), (-w / 2, d, h / 2)], "artwork", thickness=d - 0.2)
    gap, fw = 1.0, 1.2
    ow, oh = w + 2 * (gap + fw), h + 2 * (gap + fw)
    box(oak, -ow / 2, ow / 2, 0, d + 0.6, -oh / 2, -oh / 2 + fw)
    box(oak, -ow / 2, ow / 2, 0, d + 0.6, oh / 2 - fw, oh / 2)
    box(oak, -ow / 2, -ow / 2 + fw, 0, d + 0.6, -oh / 2 + fw, oh / 2 - fw)
    box(oak, ow / 2 - fw, ow / 2, 0, d + 0.6, -oh / 2 + fw, oh / 2 - fw)
    box(shadow, -ow / 2 + fw, ow / 2 - fw, 0, 0.6, -oh / 2 + fw, oh / 2 - fw)
    return [pictures, oak, shadow]


def standing_frame(name, slot, portrait):
    """A photograph in a slim frame on its easel strut: the frame leans back 12 degrees on the
    strut, its bottom edge on the table. Origin at the middle of the bottom edge; faces +Y."""
    frame = Part(slot, bevel=0.15)
    glass = Part("Glass")
    pictures = Part("Pictures")
    pictures.uv_fn = no_uvs
    back = Part("Black")
    w, h = (20.0, 25.0) if portrait else (25.0, 20.0)
    fw, d = 1.6, 1.4
    lean = Matrix.Rotation(math.radians(-12), 4, "X")

    def leaned(points):
        return [tuple(lean @ Vector(p)) for p in points]

    start = len(frame.bm.verts)
    box(frame, -w / 2, w / 2, -d, 0, 0, fw)
    box(frame, -w / 2, w / 2, -d, 0, h - fw, h)
    box(frame, -w / 2, -w / 2 + fw, -d, 0, fw, h - fw)
    box(frame, w / 2 - fw, w / 2, -d, 0, fw, h - fw)
    frame.bm.verts.ensure_lookup_table()
    bmesh.ops.transform(frame.bm, matrix=lean, verts=frame.bm.verts[start:])
    iw, ih = w - 2 * fw + 0.4, h - 2 * fw + 0.4
    picture_quad(pictures, leaned([(-iw / 2, -0.7, fw - 0.2), (iw / 2, -0.7, fw - 0.2), (iw / 2, -0.7, fw - 0.2 + ih), (-iw / 2, -0.7, fw - 0.2 + ih)]),
                 name, thickness=0.05)
    start = len(glass.bm.verts)
    box(glass, -iw / 2, iw / 2, -0.4, -0.25, fw - 0.2, fw - 0.2 + ih)
    glass.bm.verts.ensure_lookup_table()
    bmesh.ops.transform(glass.bm, matrix=lean, verts=glass.bm.verts[start:])
    start = len(back.bm.verts)
    box(back, -w / 2 + 0.5, w / 2 - 0.5, -d - 0.3, -d, 0.5, h - 0.5)
    back.bm.verts.ensure_lookup_table()
    bmesh.ops.transform(back.bm, matrix=lean, verts=back.bm.verts[start:])
    # The strut from the back to the table.
    top = lean @ Vector((0, -d - 0.3, h * 0.62))
    tube(back, [(0, top.y - 0.2, top.z), (0, top.y - (top.z * math.tan(math.radians(22))), 0.25)], 0.5, sides=6)
    return [frame, glass, pictures, back]


def frame_wedding():
    return standing_frame("wedding", "Metal", True)


def frame_family():
    return standing_frame("family", "Frame", False)


def remote():
    """The television's remote: black, rounded, a few rubber buttons. 4.6 x 17."""
    body = Part("Black", bevel=0.7, segments=3)
    keys = Part("Shadow")
    box(body, -2.3, 2.3, -8.5, 8.5, 0, 1.9)
    for row in range(5):
        for col in (-1, 0, 1):
            cbox(keys, col * 1.2, 4.5 - row * 1.6, 1.95, 0.8, 0.7, 0.2)
    lathe(keys, [(0.0, 1.9), (0.9, 1.9), (0.9, 2.1), (0.0, 2.1)], at(0, 6.8, 0), sides=12)
    return [body, keys]


def phone():
    """A smartphone lying face up, its black glass the one surface in the room the dust shows on
    most. 7.2 x 15."""
    frame = Part("Metal", bevel=0.35, segments=3)
    screen = Part("Screen")
    box(frame, -3.6, 3.6, -7.5, 7.5, 0, 0.8)
    box(screen, -3.4, 3.4, -7.3, 7.3, 0.8, 0.86)
    return [frame, screen]


def glasses():
    """Reading glasses, folded, lying on their arms: two lens rims, the bridge, the temples folded
    behind. 13 wide. Origin at the middle of their footprint on the table."""
    rim = Part("Black")
    lens = Part("Glass")
    for sx in (-1, 1):
        cx = sx * 3.4
        pts = [(cx + math.cos(2 * math.pi * k / 24) * 2.7, 0.0, 2.2 + math.sin(2 * math.pi * k / 24) * 1.9) for k in range(25)]
        tube(rim, pts, 0.18, sides=6)
        lathe(lens, [(0.0, -0.05), (2.55, -0.05), (2.55, 0.05), (0.0, 0.05)],
              Matrix.Translation((cx, 0.0, 2.2)) @ Matrix.Rotation(math.radians(90), 4, "X") @ Matrix.Diagonal((1.0, 0.74, 1.0, 1.0)), sides=24)
        tube(rim, [(sx * 6.0, 0.0, 2.8), (sx * 6.2, -0.8, 2.8), (sx * 1.0, -1.6, 0.6)], 0.16, sides=6)
    tube(rim, [(-0.8, 0.0, 2.9), (0.0, 0.1, 3.2), (0.8, 0.0, 2.9)], 0.17, sides=6)
    return [rim, lens]


def mug():
    """A coffee mug left on the side table: glazed stoneware, the coffee dried to a ring at the
    bottom. 8.4 across, 9.5 tall."""
    ceramic = Part("Ceramic")
    dregs = Part("Soil")
    lathe(ceramic, [(0.0, 0.0), (3.8, 0.0), (4.1, 0.6), (4.2, 9.4), (4.0, 9.6), (3.7, 9.4), (3.6, 1.0), (0.0, 1.0)], Matrix.Identity(4), sides=40)
    lathe(dregs, [(0.0, 1.0), (3.55, 1.0), (3.55, 1.3), (0.0, 1.3)], Matrix.Identity(4), sides=32)
    tube(ceramic, [(4.0, 0.0, 7.6), (6.4, 0.0, 7.4), (6.9, 0.0, 5.0), (6.2, 0.0, 2.8), (4.1, 0.0, 2.6)], 0.55, sides=10)
    return [ceramic, dregs]


def book():
    """A closed hardback, 15 x 23 x 3, lying flat: two boards, a rounded spine, the block of pages.
    Origin at the middle of its underside; the spine along -X."""
    cover_p = Part("Cover", bevel=0.15)
    pages = Part("Pages")
    w, l, t = 15.0, 23.0, 3.0
    box(cover_p, -w / 2, w / 2, -l / 2, l / 2, 0, 0.25)
    box(cover_p, -w / 2, w / 2, -l / 2, l / 2, t - 0.25, t)
    lathe(cover_p, [(0.0, -l / 2), (t / 2, -l / 2), (t / 2, l / 2), (0.0, l / 2)],
          Matrix.Translation((-w / 2 + 0.4, 0, t / 2)) @ Matrix.Rotation(math.radians(-90), 4, "X") @ Matrix.Diagonal((0.5, 1.0, 1.0, 1.0)), sides=20)
    box(pages, -w / 2 + 0.3, w / 2 - 0.3, -l / 2 + 0.3, l / 2 - 0.3, 0.25, t - 0.25)
    return [cover_p, pages]


def novel_open():
    """A paperback left open face down, as somebody puts a book down to come back to it: two
    halves sloping from the spine, the pages showing at the ends. 26 x 19."""
    cover_p = Part("Cover")
    pages = Part("Pages")
    half, l = 12.5, 19.0
    for sx in (-1, 1):
        m = Matrix.Translation((0, 0, 2.8)) @ Matrix.Rotation(math.radians(-sx * 9), 4, "Y")
        start = len(cover_p.bm.verts)
        box(cover_p, min(0, sx * half), max(0, sx * half), -l / 2, l / 2, 0.0, 0.2)
        cover_p.bm.verts.ensure_lookup_table()
        bmesh.ops.transform(cover_p.bm, matrix=m, verts=cover_p.bm.verts[start:])
        start = len(pages.bm.verts)
        box(pages, min(0.3, sx * (half - 0.3)), max(-0.3, sx * (half - 0.3)), -l / 2 + 0.3, l / 2 - 0.3, -1.2, 0.0)
        pages.bm.verts.ensure_lookup_table()
        bmesh.ops.transform(pages.bm, matrix=m, verts=pages.bm.verts[start:])
    return [cover_p, pages]


def sculptures():
    """The two small sculptures on the cabinet: a glazed white ring stood on its edge on a foot,
    and a stack of three smooth dark stones. Side by side along X, 30 apart."""
    ceramic = Part("Ceramic")
    stone = Part("Stone")
    ring = [(-15 + math.cos(2 * math.pi * k / 48) * 9.0, 0.0, 12.0 + math.sin(2 * math.pi * k / 48) * 9.0) for k in range(49)]
    tube(ceramic, ring, 2.6, sides=20)
    cushion(ceramic, (-15, 0, 1.2), (12, 7, 2.4), round_v=0.5, round_h=0.4)
    for k, (r, h) in enumerate(((7.0, 4.2), (5.4, 3.4), (3.8, 2.8))):
        z = sum(hh for _, hh in ((7.0, 4.2), (5.4, 3.4), (3.8, 2.8))[:k]) * 0.92
        cushion(stone, (15 + k * 0.6, k * 0.4, z + h * 0.5), (r * 2, r * 1.7, h), Matrix.Rotation(math.radians(k * 37), 4, "Z"), round_v=0.6, round_h=0.7)
    return [ceramic, stone]


def jewelry_box():
    """A jewellery box in dark leather, its lid a little open on the velvet inside. 22 x 14 x 8."""
    leather = Part("Leather", bevel=0.4)
    velvet = Part("Velvet")
    brass = Part("Brass")
    w, d = 22.0, 14.0
    shell_box(leather, -w / 2, w / 2, -d / 2, d / 2, 0, 6.0, 0.8)
    box(velvet, -w / 2 + 0.8, w / 2 - 0.8, -d / 2 + 0.8, d / 2 - 0.8, 0.8, 4.8)
    for k in range(3):
        box(velvet, -w / 2 + 0.8, w / 2 - 0.8, -d / 2 + 2.5 + k * 3.5, -d / 2 + 3.0 + k * 3.5, 4.8, 5.6)
    m = Matrix.Translation((0, -d / 2, 6.0)) @ Matrix.Rotation(math.radians(-14), 4, "X") @ Matrix.Translation((0, d / 2, -6.0))
    start = len(leather.bm.verts)
    box(leather, -w / 2, w / 2, -d / 2, d / 2, 6.0, 8.0)
    leather.bm.verts.ensure_lookup_table()
    bmesh.ops.transform(leather.bm, matrix=m, verts=leather.bm.verts[start:])
    cbox(brass, 0, d / 2 + 0.15, 5.6, 2.4, 0.4, 1.6)
    return [leather, velvet, brass]


def perfume():
    """Two perfume bottles: a tall square one with a steel cap and a squat round one with a black
    cap, each with what is left in it. Side by side along X, 9 apart. Glass is a closed shell."""
    glass = Part("Glass")
    liquid = Part("Liquid")
    metal = Part("Metal")
    black = Part("Black")
    box(glass, -7.6, -1.4, -2.2, 2.2, 0, 10.5)
    box(liquid, -7.0, -2.0, -1.6, 1.6, 0.6, 3.6)
    lathe(metal, [(0.0, 10.5), (1.0, 10.5), (1.0, 11.5), (1.8, 11.5), (1.8, 14.5), (0.0, 14.5)], at(-4.5, 0, 0), sides=20)
    lathe(glass, [(0.0, 0.0), (3.4, 0.0), (3.9, 1.0), (3.9, 5.0), (3.2, 6.6), (1.0, 7.0), (0.0, 7.0)], at(4.5, 0, 0), sides=32)
    lathe(liquid, [(0.0, 0.6), (3.2, 0.6), (3.3, 2.0), (0.0, 2.0)], at(4.5, 0, 0), sides=24)
    lathe(black, [(0.0, 7.0), (1.5, 7.0), (1.5, 9.4), (0.0, 9.6)], at(4.5, 0, 0), sides=20)
    return [glass, liquid, metal, black]


def hairbrush():
    """A paddle hairbrush lying on its back: the handle, the oval pad, the pins in it. 24 long."""
    body = Part("Black", bevel=0.4)
    pad = Part("Shadow")
    cushion(body, (0, 4, 1.0), (9.0, 11.0, 2.0), round_v=0.5, round_h=0.6)
    tube(body, [(0, -1, 1.0), (0, -12, 0.9)], [1.0, 0.9], sides=12)
    cushion(pad, (0, 4, 2.0), (7.6, 9.6, 0.6), round_v=0.5, round_h=0.6)
    rng = random.Random(5)
    for k in range(40):
        a, r = rng.uniform(0, 2 * math.pi), math.sqrt(rng.random())
        x, y = math.cos(a) * r * 3.3, 4 + math.sin(a) * r * 4.2
        tube(pad, [(x, y, 2.2), (x, y, 3.4)], 0.12, sides=4)
    return [body, pad]


def watch():
    """A man's wristwatch lying on its back, the strap spread open either side. 4 across the case."""
    metal = Part("Metal")
    face = Part("Face")
    strap = Part("Leather")
    lathe(metal, [(0.0, 0.0), (1.9, 0.0), (2.1, 0.5), (2.1, 1.0), (1.9, 1.15), (0.0, 1.15)], Matrix.Identity(4), sides=40)
    lathe(face, [(0.0, 1.0), (1.75, 1.0), (1.75, 1.18), (0.0, 1.18)], Matrix.Identity(4), sides=40)
    lathe(metal, [(0.0, 0.2), (0.4, 0.2), (0.4, 0.6), (0.0, 0.6)], at(2.4, 0, 0.4, 0, 90, 0), sides=10)
    for sy in (-1, 1):
        path = [(0, sy * 2.0, 0.5), (0, sy * 4.0, 0.25), (0, sy * 7.0, 0.15), (0.4, sy * 9.5, 0.15)]
        for p0, p1 in zip(path, path[1:]):
            mid = ((p0[0] + p1[0]) * 0.5, (p0[1] + p1[1]) * 0.5)
            cbox(strap, mid[0], mid[1], 0.25, 1.9, abs(p1[1] - p0[1]) + 0.2, 0.4)
    return [metal, face, strap]


def tray():
    """A small decorative tray: a marble-white base in a thin brass rim. 34 x 22."""
    brass = Part("Brass", bevel=0.2)
    base = Part("Ceramic", bevel=0.4)
    w, d = 34.0, 22.0
    box(base, -w / 2, w / 2, -d / 2, d / 2, 0, 1.0)
    shell_box(brass, -w / 2 - 0.3, w / 2 + 0.3, -d / 2 - 0.3, d / 2 + 0.3, 0, 2.6, 0.3)
    return [brass, base]


def folded_clothes():
    """Clothes ironed and folded, waiting to be put away: three garments in a stack, the top one a
    shirt with its collar showing. 32 x 26."""
    c1, c2, c3 = Part("Cloth1", smooth=True), Part("Cloth2", smooth=True), Part("Cloth3", smooth=True)
    cushion(c3, (0, 0, 2.8), (32, 26, 5.6), round_v=0.35, round_h=0.15, pinch=0.08)
    cushion(c1, (0.6, -0.4, 7.6), (31, 25, 4.6), Matrix.Rotation(math.radians(3), 4, "Z"), round_v=0.35, round_h=0.15, pinch=0.08)
    cushion(c2, (-0.4, 0.3, 11.6), (30, 25, 4.0), Matrix.Rotation(math.radians(-2), 4, "Z"), round_v=0.35, round_h=0.15, pinch=0.08)
    cushion(c2, (0, 9.5, 14.0), (12, 4, 1.6), round_v=0.5, round_h=0.4)
    return [c1, c2, c3]


def folded_blanket():
    """The blanket folded on the ottoman, its fold to the front. 50 x 36 x 9."""
    throw = Part("Throw", smooth=True)
    cushion(throw, (0, 0, 4.5), (50, 36, 9), round_v=0.3, round_h=0.12, pinch=0.05)
    cushion(throw, (0, 17.5, 4.5), (51, 4, 9.4), round_v=0.8, round_h=0.2)
    return [throw]


# ---------------------------------------------------------------------------------------------
# The room's joinery: mouldings for the wall panels, the dado rail, the skirting.
# ---------------------------------------------------------------------------------------------

def moulding_run(name, profile):
    """A straight run of moulding 100 long along X, its profile (out, up) drawn in YZ from the wall
    (y = 0) into the room (+Y). The actor lays it end to end and scales X to the length."""
    paint = Part("Paint")
    points = [(-50.0, y, z) for y, z in profile]
    bm = paint.bm
    left = [bm.verts.new(p) for p in points]
    right = [bm.verts.new((50.0, p[1], p[2])) for p in points]
    n = len(points)
    for k in range(n):
        j = (k + 1) % n
        bm.faces.new((left[k], right[k], right[j], left[j]))
    bm.faces.new(left[::-1])
    bm.faces.new(right)
    return [paint]


def panel_moulding():
    """The bolection of the wall panels: 3.6 tall, 1.6 proud, an ogee rising to a bead."""
    return moulding_run("panel", [(0.0, 0.0), (0.0, 3.6), (0.5, 3.6), (0.9, 3.2), (1.3, 3.0), (1.6, 2.4), (1.5, 1.6), (1.0, 1.0), (0.6, 0.5), (0.3, 0.0)])


def dado_rail():
    """The dado rail: 7 tall, 2.8 proud, a rounded nosing over a cove."""
    return moulding_run("dado", [(0.0, 0.0), (0.0, 7.0), (0.6, 7.0), (1.6, 6.6), (2.5, 5.6), (2.8, 4.4), (2.5, 3.4), (1.6, 2.8), (1.4, 1.8), (0.9, 0.6), (0.6, 0.0)])


def skirting():
    """The skirting: 22 tall, 2.2 proud, a torus and a fillet along the top."""
    return moulding_run("skirting", [(0.0, 0.0), (0.0, 22.0), (0.8, 22.0), (1.4, 21.6), (1.8, 20.8), (1.9, 19.8), (1.6, 19.0), (1.6, 18.2), (2.2, 17.4), (2.2, 0.0)])


MODELS = [
    ("bedroom_bed", bed),
    ("bedroom_nightstand", nightstand),
    ("bedroom_bench", bench),
    ("bedroom_wardrobe", wardrobe),
    ("bedroom_wardrobe_open", wardrobe_open),
    ("bedroom_wardrobe_door", wardrobe_door),
    ("bedroom_dresser", dresser),
    ("bedroom_media", media_cabinet),
    ("bedroom_tv", television),
    ("bedroom_armchair", armchair),
    ("bedroom_ottoman", ottoman),
    ("bedroom_side_table", side_table),
    ("bedroom_floor_lamp", floor_lamp),
    ("bedroom_mirror", mirror),
    ("bedroom_suitcase", suitcase_open),
    ("bedroom_plant", plant),
    ("bedroom_clock", wall_clock),
    ("bedroom_ceiling_light", ceiling_light),
    ("bedroom_artwork", artwork),
    ("bedroom_frame_wedding", frame_wedding),
    ("bedroom_frame_family", frame_family),
    ("bedroom_remote", remote),
    ("bedroom_phone", phone),
    ("bedroom_glasses", glasses),
    ("bedroom_mug", mug),
    ("bedroom_book", book),
    ("bedroom_novel", novel_open),
    ("bedroom_sculptures", sculptures),
    ("bedroom_jewelry_box", jewelry_box),
    ("bedroom_perfume", perfume),
    ("bedroom_hairbrush", hairbrush),
    ("bedroom_watch", watch),
    ("bedroom_tray", tray),
    ("bedroom_folded_clothes", folded_clothes),
    ("bedroom_folded_blanket", folded_blanket),
    ("bedroom_panel_moulding", panel_moulding),
    ("bedroom_dado", dado_rail),
    ("bedroom_skirting", skirting),
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
        print("BEDROOM baked {} ({} verts, {:.1f} x {:.1f} x {:.1f} cm)".format(name, len(obj.data.vertices), dims.x * 100, dims.y * 100, dims.z * 100))

    big = [o for o in baked if max(o.dimensions) > 0.6]
    small = [o for o in baked if o not in big]
    x = 0.0
    for obj in big:
        width = obj.dimensions.x
        obj.location = (x + width * 0.5, 0.0, 2.2 if obj.name == "bedroom_ceiling_light" else 0.0)
        x += width + 0.3
    for i, obj in enumerate(small):
        obj.location = (-0.6 - (i % 6) * 0.4, 1.5 + (i // 6) * 0.4, 0.0)
    K.preview(baked, PREVIEW_DIR, "models", [
        ("all", (x * 0.5, -14.0, 4.0), (x * 0.5, 0.0, 0.8)),
        ("left", (x * 0.18, -5.0, 2.2), (x * 0.18, 0.0, 0.7)),
        ("mid", (x * 0.5, -5.0, 2.2), (x * 0.5, 0.0, 0.7)),
        ("right", (x * 0.82, -5.0, 2.2), (x * 0.82, 0.0, 0.7)),
        ("small", (-1.6, -1.2, 1.6), (-1.6, 2.0, 0.0)),
    ])


if __name__ == "__main__":
    main()
