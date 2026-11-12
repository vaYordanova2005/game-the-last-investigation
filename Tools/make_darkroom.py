"""
Builds the darkroom's benches, enlarger, safelight and photographic things in Blender, headless,
and exports each as an FBX the art pipeline imports.

    blender --background --factory-startup --python Tools/make_darkroom.py [-- --only=a,b]

The father's darkroom in the small cellar room across from the wine cellar's corridor: a wet bench
with a lead-lined sink and the three trays in it, a dry bench with the enlarger, the red safelight
over the sink. Nothing free is a 1960s enlarger or a safelight, and primitives make a bellows a
stack of slabs, so they are modelled here.

Outputs Art/Source/Models/<name>/<name>.fbx with an empty GENERATED file beside it, and Workbench
previews in Saved/DarkroomPreview/.

Frame, as every prop in the house: centimetres, origin on the floor under the middle of the
footprint, front facing local +Y. The safelight's origin is on the wall plane at its bracket.

The numbers the C++ also needs (ADarkroomActor) are the constants below, and must stay in step
with DarkroomActor.h.

Material slots are set by name in C++:
    Wood   weathered_brown_planks (150cm)      Lead, Black, Chrome, Enamel, Rim, Liquid, Lens,
    Paper, Card, Yellow, Bottle, Cap, Rubber, Bellows, SafeGlass, Cord   flat colours
    Label  the darkroom_paper atlas on the model's own UVs
"""

import math
import os
import random
import sys

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blender_kit as K  # noqa: E402
from blender_kit import Part, at, cbox, ellipsoid, lathe, tube  # noqa: E402

PREVIEW_DIR = os.path.join(K.ROOT, "Saved", "DarkroomPreview")

FLAT = 100.0
SLOTS = {
    "Wood": ((0.45, 0.33, 0.22, 1.0), 150.0),
    "Lead": ((0.30, 0.31, 0.32, 1.0), FLAT),
    "Black": ((0.05, 0.05, 0.05, 1.0), FLAT),
    "Chrome": ((0.70, 0.70, 0.72, 1.0), FLAT),
    "Enamel": ((0.85, 0.83, 0.76, 1.0), FLAT),
    "Rim": ((0.12, 0.16, 0.30, 1.0), FLAT),
    "Liquid": ((0.30, 0.25, 0.12, 1.0), FLAT),
    "Lens": ((0.10, 0.12, 0.14, 1.0), FLAT),
    "Paper": ((0.85, 0.82, 0.74, 1.0), FLAT),
    "Card": ((0.55, 0.45, 0.32, 1.0), FLAT),
    "Yellow": ((0.85, 0.65, 0.10, 1.0), FLAT),
    "Bottle": ((0.25, 0.12, 0.05, 1.0), FLAT),
    "Cap": ((0.08, 0.08, 0.08, 1.0), FLAT),
    "Rubber": ((0.30, 0.10, 0.08, 1.0), FLAT),
    "Bellows": ((0.08, 0.07, 0.06, 1.0), FLAT),
    "SafeGlass": ((0.80, 0.05, 0.03, 1.0), FLAT),
    "Cord": ((0.06, 0.05, 0.05, 1.0), FLAT),
    "Label": ((0.85, 0.80, 0.68, 1.0), FLAT),
    "Shadow": ((0.02, 0.02, 0.02, 1.0), FLAT),
    "Leather": ((0.06, 0.05, 0.05, 1.0), 40.0),
    "Plastic": ((0.04, 0.04, 0.045, 1.0), FLAT),
    "Silver": ((0.55, 0.55, 0.57, 1.0), FLAT),
    "Glass": ((0.60, 0.70, 0.75, 1.0), FLAT),
    "Film": ((0.25, 0.12, 0.05, 1.0), FLAT),
}

# ---------------------------------------------------------------------------------------------
# Shared with DarkroomActor.h.
# ---------------------------------------------------------------------------------------------

WET_L = 240.0       # the wet bench along the north wall
WET_D = 62.0
WET_TOP = 90.0
SINK_FLOOR = 80.0   # the trays stand on the sink's floor
DRY_L = 150.0       # the dry bench along the east wall
DRY_D = 65.0
DRY_TOP = 90.0
TRAY_W = 32.0
TRAY_D = 26.0
TRAY_H = 6.0
SAFE_Z = 0.0        # the safelight's glass centre is SAFE_GLASS_Z below its bracket's origin
SAFE_GLASS_Z = -12.0
SAFE_PROUD = 14.0

# The paper atlas (Tools/make_darkroom_art.py: RECTS).
ATLAS = {
    "dev": (864, 480, 256, 320),
    "stop": (1136, 480, 256, 320),
    "fix": (1408, 480, 256, 320),
    "timer": (1680, 480, 256, 256),
    "filmbox": (0, 992, 256, 384),
    "canister": (272, 992, 256, 96),
}

# The camera cabinet west of the door (DarkroomActor.h: Cabinet*): a glazed upper case on a closed
# cupboard. Shelves' tops in its own frame, front +Y, origin on the floor under its middle.
CAB_W = 110.0
CAB_D = 42.0
CAB_H = 200.0
CAB_BASE = 86.0
CAB_SHELVES = (86.0, 124.0, 162.0)
CAB_DOOR_W = CAB_W / 2 - 3.0
# The pendant safelight: its origin at the ceiling rose, the red bowl's rim this far below it.
PENDANT_DROP = 52.0


def atlas_uv(name, tu, tv):
    x, y, w, h = ATLAS[name]
    return ((x + tu * w) / 2048.0, 1.0 - (y + (1.0 - tv) * h) / 2048.0)


def face_label_uvs(name, x0, x1, z0, z1):
    def apply(bm):
        uv = bm.loops.layers.uv.verify()
        for face in bm.faces:
            for loop in face.loops:
                co = loop.vert.co
                loop[uv].uv = atlas_uv(name, min(max((co.x - x0) / (x1 - x0), 0.0), 1.0), min(max((co.z - z0) / (z1 - z0), 0.0), 1.0))
    return apply


def wrap_label_uvs(name, z0, z1, a0, a1):
    def apply(bm):
        uv = bm.loops.layers.uv.verify()
        for face in bm.faces:
            for loop in face.loops:
                co = loop.vert.co
                a = math.atan2(co.x, co.y)
                loop[uv].uv = atlas_uv(name, min(max((a - a0) / (a1 - a0), 0.0), 1.0), min(max((co.z - z0) / (z1 - z0), 0.0), 1.0))
    return apply


def band(part, r_in, r_out, z0, z1, a0, a1, segments):
    """A closed sector of a thick-walled tube about Z, its middle at +Y: a label on a bottle."""
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


def box_frame(part, x0, x1, y0, y1, z0, z1, wall):
    """An open-topped box: a floor and four walls (a tray, a sink, a shoebox)."""
    cbox(part, (x0 + x1) / 2, (y0 + y1) / 2, z0 + wall / 2, x1 - x0, y1 - y0, wall)
    for y in (y0 + wall / 2, y1 - wall / 2):
        cbox(part, (x0 + x1) / 2, y, (z0 + z1) / 2, x1 - x0, wall, z1 - z0)
    for x in (x0 + wall / 2, x1 - wall / 2):
        cbox(part, x, (y0 + y1) / 2, (z0 + z1) / 2, wall, y1 - y0 - 2 * wall, z1 - z0)


# ---------------------------------------------------------------------------------------------
# Benches.
# ---------------------------------------------------------------------------------------------

def wet_bench():
    """A deal bench the length of the wall with a sink sunk in it, lined in lead: the trays stand
    on the sink's floor. A splashback, a rack of slats under, legs braced at the foot."""
    wood = Part("Wood", bevel=0.6)
    hl, hd = WET_L / 2, WET_D / 2
    for sx in (-1, 1):
        for sy in (-1, 1):
            cbox(wood, sx * (hl - 4), sy * (hd - 4), (SINK_FLOOR - 4) / 2, 7, 7, SINK_FLOOR - 4)
        cbox(wood, sx * (hl - 4), 0, 14, 5, WET_D - 8, 5)
    for sy in (-1, 1):
        cbox(wood, 0, sy * (hd - 4), 14, WET_L - 8, 5, 5)
    # The slatted shelf under it, for the carboys.
    for k in range(7):
        cbox(wood, 0, -hd + 8 + k * 7.6, 17.5, WET_L - 14, 5.6, 2)
    # The sink: a box of boards with its top edges as the bench's top rail.
    box_frame(wood, -hl, hl, -hd, hd, SINK_FLOOR - 6, WET_TOP, 4.0)
    cbox(wood, 0, -hd - 1.0, WET_TOP + 15, WET_L, 2.0, 30)
    lead = Part("Lead", bevel=0.3)
    box_frame(lead, -hl + 4, hl - 4, -hd + 4, hd - 4, SINK_FLOOR - 2, WET_TOP - 1, 0.6)
    # The waste, and the rail along the front edge worn by forty years of forearms.
    shadow = Part("Shadow")
    lathe(shadow, [(0.0, SINK_FLOOR - 1.9), (3.0, SINK_FLOOR - 1.9), (3.0, SINK_FLOOR - 1.5), (0.0, SINK_FLOOR - 1.5)],
          Matrix.Translation((hl - 20, 0, 0)), sides=16)
    chrome = Part("Chrome")
    # A swan-neck tap over the sink's east end, rising from the bench top behind it.
    tube(chrome, [(hl - 30, -hd + 2, WET_TOP), (hl - 30, -hd + 2, WET_TOP + 26), (hl - 30, -hd + 10, WET_TOP + 32),
                  (hl - 30, -hd + 18, WET_TOP + 24)], 1.1, sides=10)
    cbox(chrome, hl - 30, -hd + 2, WET_TOP + 20, 6, 1.5, 1.5)
    return [wood, lead, shadow, chrome]


def dry_bench():
    """A plain bench with two drawers and a shelf under: where the enlarger stands."""
    wood = Part("Wood", bevel=0.6)
    hl, hd = DRY_L / 2, DRY_D / 2
    cbox(wood, 0, 0, DRY_TOP - 1.6, DRY_L, DRY_D, 3.2)
    for sx in (-1, 1):
        for sy in (-1, 1):
            cbox(wood, sx * (hl - 4), sy * (hd - 4), (DRY_TOP - 3) / 2, 6, 6, DRY_TOP - 3)
    cbox(wood, 0, 0, 16, DRY_L - 4, DRY_D - 4, 2.4)
    cbox(wood, 0, -hd + 3.5, DRY_TOP - 10, DRY_L - 8, 2, 14)
    for sx in (-1, 1):
        cbox(wood, sx * DRY_L * 0.25, hd - 3, DRY_TOP - 10, DRY_L * 0.5 - 10, 2.4, 13)
    iron = Part("Chrome")
    for sx in (-1, 1):
        tube(iron, [(sx * DRY_L * 0.25 - 5, hd - 1.6, DRY_TOP - 10), (sx * DRY_L * 0.25 - 5, hd + 0.8, DRY_TOP - 10),
                    (sx * DRY_L * 0.25 + 5, hd + 0.8, DRY_TOP - 10), (sx * DRY_L * 0.25 + 5, hd - 1.6, DRY_TOP - 10)], 0.4, sides=6)
    return [wood, iron]


# ---------------------------------------------------------------------------------------------
# The enlarger and the safelight.
# ---------------------------------------------------------------------------------------------

def enlarger():
    """A 1960s condenser enlarger on its baseboard: a column at the back, the carriage on it, the
    lamphouse, the bellows down to the lens, a masking easel on the board. Origin under the board's
    middle; the board is 2.5 thick, so it stands on the bench at DRY_TOP."""
    wood = Part("Wood", bevel=0.4)
    cbox(wood, 0, 0, 1.25, 46, 56, 2.5)
    black = Part("Black", bevel=0.3)
    # The column and its foot.
    cbox(black, 0, -22, 3.5, 16, 9, 2)
    lathe(black, [(0.0, 2.5), (2.4, 2.5), (2.4, 108), (2.8, 109), (0.0, 110)], Matrix.Translation((0, -22, 0)), sides=20)
    # The carriage clamped on it, the arm forward.
    cbox(black, 0, -22, 72, 8, 8, 14)
    cbox(black, 0, -13, 72, 6, 14, 6)
    # The lamphouse: a drum on its side, vents on top, the condenser housing under it.
    lathe(black, [(0.0, 0.0), (9.0, 0.0), (9.0, 13.0), (0.0, 13.0)], Matrix.Translation((0, -1, 78)) @ Matrix.Rotation(math.radians(90), 4, "Y")
          @ Matrix.Translation((0, 0, -6.5)), sides=24)
    cbox(black, 0, -1, 66, 15, 15, 6)
    for k in range(4):
        cbox(black, -4.5 + k * 3, -1, 87.6, 1.2, 9, 1.2)
    # The bellows: pleats stepping out and in, square, from the condenser to the lens board.
    bellows = Part("Bellows")
    z = 63.0
    for k in range(10):
        s = 13.0 if k % 2 == 0 else 11.4
        cbox(bellows, 0, -1, z - 1.0, s, s, 2.0)
        z -= 2.0
    cbox(black, 0, -1, z - 0.6, 12, 12, 1.2)
    chrome = Part("Chrome", bevel=0.15)
    lathe(chrome, [(0.0, 0.0), (3.2, 0.0), (3.2, 4.0), (2.6, 4.6), (0.0, 4.6)], Matrix.Translation((0, -1, z - 5.6)), sides=20)
    lens = Part("Lens")
    lathe(lens, [(0.0, 0.0), (2.4, 0.0), (0.0, 0.2)], Matrix.Translation((0, -1, z - 5.7)), sides=16)
    # The focusing knob and the height crank on the carriage.
    lathe(chrome, [(0.0, 0.0), (2.2, 0.0), (2.2, 1.2), (0.0, 1.2)], Matrix.Translation((4.6, -22, 72)) @ Matrix.Rotation(math.radians(90), 4, "Y"), sides=14)
    tube(chrome, [(-4.6, -22, 72), (-8.0, -22, 72), (-8.0, -22, 66)], 0.5, sides=6)
    # The cable from the lamphouse down the back of the column to the board.
    cord = Part("Cord")
    tube(cord, [(0, -9, 80), (0, -18, 82), (2, -26, 70), (2, -26, 6), (6, -30, 2.6), (20, -30, 2.6)], 0.4, sides=6)
    # The masking easel: a white base, two black blades at a right angle.
    enamel = Part("Enamel", bevel=0.2)
    cbox(enamel, 0, 6, 3.1, 30, 26, 1.2)
    cbox(black, 0, 6 + 12, 3.9, 30, 2.2, 0.5)
    cbox(black, -14, 6, 3.9, 2.2, 26, 0.5)
    return [wood, black, bellows, chrome, lens, cord, enamel]


def safelight():
    """The safelight: a black sheet-steel box on a wall bracket, its front a pane of deep red glass,
    a hood over it, the flex looping down the wall. Origin on the wall plane at the bracket; the
    glass faces +Y, its centre SAFE_GLASS_Z below the origin and SAFE_PROUD out."""
    black = Part("Black", bevel=0.2)
    cbox(black, 0, 1.0, 0, 6, 2, 8)
    cbox(black, 0, 5.0, 0, 2.5, 8, 2.5)
    cbox(black, 0, SAFE_PROUD - 5.5, SAFE_GLASS_Z, 22, 11, 18)
    cbox(black, 0, SAFE_PROUD + 1.2, SAFE_GLASS_Z + 9.6, 24, 6, 1.2)
    for sx in (-1, 1):
        cbox(black, sx * 10.6, SAFE_PROUD + 0.2, SAFE_GLASS_Z, 1.6, 1.6, 18)
    cbox(black, 0, SAFE_PROUD + 0.2, SAFE_GLASS_Z - 8.2, 22, 1.6, 1.6)
    cbox(black, 0, SAFE_PROUD + 0.2, SAFE_GLASS_Z + 8.2, 22, 1.6, 1.6)
    glass = Part("SafeGlass")
    cbox(glass, 0, SAFE_PROUD - 0.2, SAFE_GLASS_Z, 19.6, 0.6, 14.8)
    cord = Part("Cord")
    tube(cord, [(9, SAFE_PROUD - 9, SAFE_GLASS_Z - 6), (14, 3, SAFE_GLASS_Z - 10), (16, 1.2, SAFE_GLASS_Z - 40),
                (14, 1.2, SAFE_GLASS_Z - 70)], 0.35, sides=6)
    return [black, glass, cord]


# ---------------------------------------------------------------------------------------------
# The small things.
# ---------------------------------------------------------------------------------------------

def tray(rim=True):
    """An enamelled steel developing tray: white inside, a blue rim, a pouring lip at one corner,
    an inch of liquid in it. Origin under its middle."""
    enamel = Part("Enamel", bevel=0.4)
    hw, hd = TRAY_W / 2, TRAY_D / 2
    box_frame(enamel, -hw, hw, -hd, hd, 0.0, TRAY_H, 0.5)
    for y in (-hd + 4, hd - 4):
        cbox(enamel, 0, y, 0.35, TRAY_W - 4, 1.2, 0.5)
    trim = Part("Rim")
    for y in (-hd, hd):
        tube(trim, [(-hw, y, TRAY_H), (hw, y, TRAY_H)], 0.45, sides=8)
    for x in (-hw, hw):
        tube(trim, [(x, -hd, TRAY_H), (x, hd, TRAY_H)], 0.45, sides=8)
    liquid = Part("Liquid")
    cbox(liquid, 0, 0, 2.0, TRAY_W - 1.2, TRAY_D - 1.2, 0.6)
    return [enamel, trim, liquid]


def bottle(label):
    """A brown glass chemical bottle, a black screw cap, a paper label round the front."""
    glass = Part("Bottle")
    lathe(glass, [(0.0, 0.0), (5.0, 0.0), (5.4, 0.6), (5.4, 17.0), (4.8, 19.4), (2.4, 21.6), (2.0, 22.4), (2.0, 24.0), (0.0, 24.0)],
          Matrix.Identity(4), sides=24)
    cap = Part("Cap", bevel=0.2)
    lathe(cap, [(0.0, 23.4), (2.4, 23.4), (2.4, 26.4), (0.0, 26.4)], Matrix.Identity(4), sides=16)
    paper = Part("Label")
    span = math.radians(120.0)
    band(paper, 5.35, 5.55, 3.0, 16.0, -span / 2, span / 2, 18)
    paper.uv_fn = wrap_label_uvs(label, 3.0, 16.0, -span / 2, span / 2)
    return [glass, cap, paper]


def carboy():
    """A big brown glass jar of stock solution, ten litres, in a wicker-less cradle of nothing."""
    glass = Part("Bottle")
    lathe(glass, [(0.0, 0.0), (11.0, 0.0), (12.5, 2.0), (13.0, 14.0), (12.0, 26.0), (8.0, 31.0), (3.5, 33.0), (3.2, 37.0), (0.0, 37.0)],
          Matrix.Identity(4), sides=28)
    cap = Part("Cap", bevel=0.2)
    lathe(cap, [(0.0, 36.4), (3.8, 36.4), (3.8, 39.6), (0.0, 39.6)], Matrix.Identity(4), sides=16)
    return [glass, cap]


def jug():
    """A stainless measuring jug: a tapered body, a pouring lip, a handle."""
    chrome = Part("Chrome")
    lathe(chrome, [(0.0, 0.0), (5.6, 0.0), (5.8, 0.4), (4.8, 14.0), (5.1, 14.4), (4.6, 14.4), (5.3, 0.9), (0.0, 0.9)], Matrix.Identity(4), sides=28)
    tube(chrome, [(0, -5.4, 12.0), (0, -9.0, 11.0), (0, -9.4, 5.0), (0, -5.6, 3.0)], 0.6, sides=8)
    liquid = Part("Liquid")
    lathe(liquid, [(0.0, 4.0), (5.0, 4.0), (5.0, 4.5), (0.0, 4.5)], Matrix.Identity(4), sides=24)
    return [chrome, liquid]


def timer():
    """A darkroom timer: a black box with a big sixty-second dial, two hands, a lever on top."""
    black = Part("Black", bevel=0.6)
    cbox(black, 0, 0, 8, 16, 8, 16)
    cbox(black, 0, -1, 0.5, 18, 10, 1)
    face = Part("Label")
    cbox(face, 0, 4.03, 8, 13.4, 0.06, 13.4)
    face.uv_fn = face_label_uvs("timer", -6.7, 6.7, 1.3, 14.7)
    hands = Part("Rubber")
    cbox(hands, 0, 4.3, 8 + 2.6, 0.5, 0.2, 5.2)
    cbox(hands, 1.6, 4.4, 8 - 1.0, 0.4, 0.2, 4.0, matrix=None)
    chrome = Part("Chrome")
    cbox(chrome, 5, 0, 17, 1.2, 1.2, 3)
    lathe(chrome, [(0.0, 0.0), (0.9, 0.0), (0.9, 0.3), (0.0, 0.3)], Matrix.Translation((0, 4.4, 8)) @ Matrix.Rotation(math.radians(-90), 4, "X"), sides=10)
    return [black, face, hands, chrome]


def tongs():
    """Two pairs of bamboo print tongs lying crossed, rubber on their tips. Origin at their middle."""
    wood = Part("Wood", bevel=0.1)
    rubber = Part("Rubber")
    for k, (yaw, off) in enumerate(((8.0, -2.0), (-14.0, 3.0))):
        m = at(0.0, off, 0.5 + k * 1.0, yaw=yaw)
        for sy in (-1, 1):
            cbox(wood, 0, sy * 0.7, 0, 20, 0.6, 1.0, matrix=m @ Matrix.Rotation(math.radians(sy * 2.5), 4, "Z"))
        cbox(wood, -9.5, 0, 0, 1.5, 2.2, 1.2, matrix=m)
        for sy in (-1, 1):
            cbox(rubber, 9.0, sy * 1.1, 0, 2.6, 0.9, 1.2, matrix=m)
    return [wood, rubber]


def neg_box():
    """A shoebox of negatives in their glassine sleeves, filed upright in years, its lid leant
    against it. Origin under the box's middle."""
    card = Part("Card", bevel=0.15)
    box_frame(card, -16, 16, -10, 10, 0.0, 11.0, 0.4)
    cbox(card, 0, -12.5, 6.0, 33, 0.5, 12, matrix=Matrix.Translation((0, -12.5, 0)) @ Matrix.Rotation(math.radians(-14), 4, "X")
         @ Matrix.Translation((0, 12.5, 0)))
    paper = Part("Paper")
    rng = random.Random(29)
    x = -15.0
    while x < 14.0:
        lean = rng.uniform(-8, 8)
        h = rng.uniform(9.5, 12.5)
        cbox(paper, 0, 0, h / 2, 0.25, 18.6, h, matrix=at(x, 0, 0.4, roll=lean))
        x += rng.uniform(0.6, 1.4)
    # Dividers between the years: card standing taller than the sleeves.
    for xd in (-8.0, 1.0, 9.0):
        cbox(card, xd, 0, 7.5, 0.3, 18.6, 14.0)
    return [card, paper]


def paper_box():
    """A box of enlarging paper, the lid off and askew on it: yellow and black card."""
    yellow = Part("Yellow", bevel=0.15)
    cbox(yellow, 0, 0, 1.6, 26.0, 21.0, 3.2)
    black = Part("Black", bevel=0.15)
    cbox(black, 0, 0, 3.4, 25.0, 20.0, 0.3)
    cbox(yellow, 3.0, 2.0, 4.1, 26.6, 21.6, 0.8, matrix=at(0, 0, 0, yaw=11.0))
    cbox(black, 3.0, 2.0, 4.55, 18.0, 4.0, 0.1, matrix=at(0, 0, 0, yaw=11.0))
    return [yellow, black]


def bin_():
    """A tin waste bin with prints balled up in it and two on the floor beside it."""
    tin = Part("Lead")
    lathe(tin, [(0.0, 0.0), (12.0, 0.0), (14.0, 32.0), (14.4, 32.6), (13.8, 32.6), (11.6, 1.0), (0.0, 1.0)], Matrix.Identity(4), sides=28)
    paper = Part("Paper")
    rng = random.Random(41)
    for k, centre in enumerate(((0, 0, 29), (-6, 4, 31), (6, -3, 30.5), (2, 6, 32), (24, 6, 3.2), (20, -14, 2.8))):
        verts = ellipsoid(paper, centre, (4.0, 3.6, 3.4), rings=6, sides=8)
        for v in verts:
            v.co += Vector((rng.uniform(-1.1, 1.1), rng.uniform(-1.1, 1.1), rng.uniform(-1.0, 1.0)))
    return [tin, paper]


# ---------------------------------------------------------------------------------------------
# The camera cabinet and its door.
# ---------------------------------------------------------------------------------------------

def glass_pane(part, cx, cy, cz, sx, sz):
    """A pane as a closed single-winding shell (the 10-08 glass rule), 3mm thick."""
    cbox(part, cx, cy, cz, sx, 0.3, sz)


def camera_cabinet():
    """A tall oak cabinet: a cupboard with two panelled doors below, a glazed case above with three
    shelves, a cornice. The left glazed door (as the viewer sees it, -X) is in the model, shut; the
    right one is a separate model (darkroom_cabinet_door), standing open."""
    wood = Part("Wood", bevel=0.4)
    hw, hd = CAB_W / 2, CAB_D / 2
    for sx in (-1, 1):
        cbox(wood, sx * (hw - 1.0), 0, CAB_H / 2, 2.0, CAB_D, CAB_H)
    cbox(wood, 0, -hd + 0.8, CAB_H / 2, CAB_W - 4, 1.6, CAB_H)
    cbox(wood, 0, 0, 5.0, CAB_W, CAB_D, 10.0)
    cbox(wood, 0, 0, CAB_BASE - 1.5, CAB_W + 2, CAB_D + 2, 3.0)
    cbox(wood, 0, 0, CAB_H - 1.5, CAB_W - 4, CAB_D - 2, 3.0)
    cbox(wood, 0, 1.0, CAB_H + 2.5, CAB_W + 6, CAB_D + 4, 5.0)
    # The cupboard's two doors, a panel raised in each, knobs.
    for sx in (-1, 1):
        cbox(wood, sx * hw * 0.5, hd - 1.0, 10 + (CAB_BASE - 13) / 2, hw - 1.6, 2.0, CAB_BASE - 13)
        cbox(wood, sx * hw * 0.5, hd + 0.3, 10 + (CAB_BASE - 13) / 2, hw - 14, 1.0, CAB_BASE - 27)
    for z in CAB_SHELVES[1:]:
        cbox(wood, 0, -1.0, z - 1.0, CAB_W - 4, CAB_D - 6, 2.0)
    # The left glazed door, shut: stiles and rails round a pane, a glazing bar across.
    top = CAB_H - 3.0
    x0, x1 = -hw + 2.0, -1.0
    cx = (x0 + x1) / 2
    cbox(wood, x0 + 2.5, hd - 1.0, (CAB_BASE + top) / 2, 5.0, 2.2, top - CAB_BASE)
    cbox(wood, x1 - 2.5, hd - 1.0, (CAB_BASE + top) / 2, 5.0, 2.2, top - CAB_BASE)
    cbox(wood, cx, hd - 1.0, CAB_BASE + 3.0, x1 - x0, 2.2, 6.0)
    cbox(wood, cx, hd - 1.0, top - 3.0, x1 - x0, 2.2, 6.0)
    cbox(wood, cx, hd - 1.0, (CAB_BASE + top) / 2, x1 - x0 - 8, 1.6, 2.0)
    glass = Part("Glass")
    glass_pane(glass, cx, hd - 1.2, (CAB_BASE + top) / 2, x1 - x0 - 9, top - CAB_BASE - 11)
    brass = Part("Chrome")
    for sx in (-1, 1):
        lathe(brass, [(0.0, 0.0), (1.1, 0.0), (1.1, 1.2), (0.0, 1.6)], Matrix.Translation((sx * 4.0, hd + 0.1, CAB_BASE - 12))
              @ Matrix.Rotation(math.radians(-90), 4, "X"), sides=12)
    lathe(brass, [(0.0, 0.0), (0.9, 0.0), (0.9, 1.0), (0.0, 1.4)], Matrix.Translation((x1 - 4.0, hd + 0.1, CAB_BASE + 50))
          @ Matrix.Rotation(math.radians(-90), 4, "X"), sides=12)
    return [wood, glass, brass]


def cabinet_door():
    """The right glazed door. Origin on its hinge (the cabinet's right outer edge as the viewer sees
    it, at the door's back face, on the glazed case's floor); shut, the leaf runs along -X and faces
    +Y. A negative yaw opens it outwards."""
    wood = Part("Wood", bevel=0.4)
    top = CAB_H - 3.0 - CAB_BASE
    w = CAB_DOOR_W
    cbox(wood, -2.5, 1.1, top / 2, 5.0, 2.2, top)
    cbox(wood, -w + 2.5, 1.1, top / 2, 5.0, 2.2, top)
    cbox(wood, -w / 2, 1.1, 3.0, w, 2.2, 6.0)
    cbox(wood, -w / 2, 1.1, top - 3.0, w, 2.2, 6.0)
    cbox(wood, -w / 2, 1.1, top / 2, w - 8, 1.6, 2.0)
    glass = Part("Glass")
    glass_pane(glass, -w / 2, 0.9, top / 2, w - 9, top - 11)
    brass = Part("Chrome")
    lathe(brass, [(0.0, 0.0), (0.9, 0.0), (0.9, 1.0), (0.0, 1.4)], Matrix.Translation((-w + 4.0, 2.3, 50.0))
          @ Matrix.Rotation(math.radians(-90), 4, "X"), sides=12)
    return [wood, glass, brass]


# ---------------------------------------------------------------------------------------------
# Cameras, oldest to newest. Each faces +Y (the lens), origin under its middle.
# ---------------------------------------------------------------------------------------------

def lens_barrel(chrome, black, lens, y0, length, r, z, rings=True):
    m = Matrix.Translation((0, y0, z)) @ Matrix.Rotation(math.radians(-90), 4, "X")
    lathe(black, [(0.0, 0.0), (r, 0.0), (r, length), (r * 0.82, length), (0.0, length - 0.4)], m, sides=24)
    if rings:
        lathe(chrome, [(0.0, length * 0.35), (r + 0.15, length * 0.35), (r + 0.15, length * 0.5), (0.0, length * 0.5)], m, sides=24)
    lathe(lens, [(0.0, length - 0.5), (r * 0.72, length - 0.5), (r * 0.72, length - 0.3), (0.0, length - 0.25)], m, sides=20)


def cam_box():
    """A 1920s box camera: a leatherette box, a small lens, two brilliant finders, a winding key."""
    leather = Part("Leather", bevel=0.3)
    cbox(leather, 0, 0, 6.0, 9.0, 13.0, 12.0)
    black, chrome, lens = Part("Black"), Part("Chrome"), Part("Lens")
    cbox(black, 0, 6.6, 6.0, 7.6, 0.4, 9.0)
    lens_barrel(chrome, black, lens, 6.8, 0.8, 1.2, 5.0, rings=False)
    for (x, z) in ((2.8, 10.2), (-3.2, 9.6)):
        cbox(lens, x, 6.7, z, 1.2, 0.2, 1.2)
    lathe(chrome, [(0.0, 0.0), (1.0, 0.0), (1.0, 0.8), (0.0, 0.8)], Matrix.Translation((4.6, -3, 9)) @ Matrix.Rotation(math.radians(90), 4, "Y"), sides=12)
    tube(leather, [(-3.0, 0, 12.0), (-3.0, 0, 14.0), (3.0, 0, 14.0), (3.0, 0, 12.0)], 0.5, sides=6)
    return [leather, black, chrome, lens]


def cam_folding():
    """A folding roll-film camera opened: the leatherette body, the bed dropped, the bellows out to
    the lens standard on its struts."""
    leather = Part("Leather", bevel=0.3)
    cbox(leather, 0, -1.5, 5.0, 16.0, 4.0, 10.0)
    chrome, black, lens = Part("Chrome", bevel=0.1), Part("Black"), Part("Lens")
    cbox(chrome, 0, -1.5, 10.2, 15.0, 3.6, 0.6)
    cbox(leather, 0, 5.5, 0.4, 9.0, 10.0, 0.6)
    cbox(chrome, 0, 5.0, 0.9, 1.0, 9.0, 0.4)
    bellows = Part("Bellows")
    y = 0.5
    for k in range(7):
        s = 8.2 - k * 0.45 if k % 2 == 0 else 7.6 - k * 0.45
        cbox(bellows, 0, y + 0.5, 5.2, s, 1.0, s)
        y += 1.0
    cbox(chrome, 0, y + 0.3, 5.2, 6.0, 0.6, 6.0)
    lens_barrel(chrome, black, lens, y + 0.6, 1.4, 1.8, 5.2)
    for sx in (-1, 1):
        tube(chrome, [(sx * 2.6, y, 2.4), (sx * 2.6, 3.0, 0.9)], 0.15, sides=5)
    return [leather, chrome, black, bellows, lens]


def cam_tlr():
    """A twin-lens reflex: the tall box, two lenses one over the other, the waist-level hood open,
    the crank on the side."""
    leather = Part("Leather", bevel=0.3)
    cbox(leather, 0, 0, 7.0, 8.0, 9.0, 14.0)
    black, chrome, lens = Part("Black", bevel=0.2), Part("Chrome", bevel=0.1), Part("Lens")
    cbox(black, 0, 4.9, 7.0, 7.4, 0.8, 12.0)
    lens_barrel(chrome, black, lens, 5.3, 2.2, 1.7, 4.4)
    lens_barrel(chrome, black, lens, 5.3, 2.0, 1.6, 9.8)
    cbox(black, 0, 0, 14.4, 7.4, 8.4, 0.8)
    for sx in (-1, 1):
        cbox(black, sx * 3.6, 0, 17.0, 0.3, 7.6, 4.4)
    cbox(black, 0, -3.8, 17.0, 7.4, 0.3, 4.4)
    lathe(chrome, [(0.0, 0.0), (1.4, 0.0), (1.4, 0.8), (0.0, 0.8)], Matrix.Translation((4.0, 0, 10)) @ Matrix.Rotation(math.radians(90), 4, "Y"), sides=14)
    tube(chrome, [(4.8, 0, 10), (4.8, 1.0, 12.4)], 0.25, sides=6)
    return [leather, black, chrome, lens]


def cam_rangefinder():
    """A 1950s 35mm rangefinder: a chrome top plate over a leatherette body, a short lens, the two
    finder windows."""
    leather = Part("Leather", bevel=1.2, segments=4)
    cbox(leather, 0, 0, 3.6, 13.6, 3.4, 5.6)
    chrome, black, lens = Part("Chrome", bevel=0.6, segments=3), Part("Black"), Part("Lens")
    cbox(chrome, 0, 0, 7.4, 13.6, 3.4, 2.4)
    cbox(chrome, 0, 0, 0.5, 13.6, 3.4, 1.0)
    for x in (-4.4, 2.6):
        cbox(lens, x, 1.75, 7.4, 1.6, 0.1, 1.0)
    lens_barrel(chrome, black, lens, 1.7, 2.6, 2.2, 3.8)
    lathe(chrome, [(0.0, 0.0), (0.9, 0.0), (0.9, 0.7), (0.0, 0.7)], Matrix.Translation((4.6, 0, 8.6)), sides=14)
    cbox(chrome, -4.6, 0, 8.9, 1.6, 1.4, 0.6)
    return [leather, chrome, black, lens]


def cam_slr():
    """A 1970s 35mm SLR: black body, chrome top plate, the pentaprism hump, a standard lens."""
    leather = Part("Leather", bevel=0.4)
    cbox(leather, 0, 0, 4.0, 14.0, 4.4, 6.4)
    chrome, black, lens = Part("Chrome", bevel=0.3), Part("Black", bevel=0.3), Part("Lens")
    cbox(chrome, 0, 0, 7.8, 14.0, 4.4, 1.4)
    cbox(black, 0, 0.2, 9.6, 5.6, 4.0, 2.4)
    cbox(black, 0, 0.6, 11.2, 3.8, 3.0, 1.2)
    lens_barrel(chrome, black, lens, 2.2, 5.4, 3.0, 4.2)
    for x in (4.6, -4.4):
        lathe(chrome, [(0.0, 0.0), (1.1, 0.0), (1.1, 0.9), (0.0, 0.9)], Matrix.Translation((x, 0, 8.5)), sides=14)
    return [leather, chrome, black, lens]


def cam_dslr():
    """A modern digital SLR: matt black plastic, a deep grip on its right, a zoom lens."""
    plastic = Part("Plastic", bevel=1.0, segments=3)
    cbox(plastic, 0, 0, 4.8, 13.6, 6.0, 8.4)
    cbox(plastic, -5.6, 1.4, 4.4, 3.4, 8.2, 8.0)
    cbox(plastic, 0, 0.4, 10.0, 5.6, 5.0, 3.0)
    black, chrome, lens = Part("Black", bevel=0.2), Part("Silver"), Part("Lens")
    lens_barrel(chrome, black, lens, 3.0, 8.4, 3.6, 4.6)
    rubber = Part("Rubber")
    lathe(rubber, [(0.0, 3.0), (3.7, 3.0), (3.7, 6.0), (0.0, 6.0)], Matrix.Translation((0, 3.0, 4.6)) @ Matrix.Rotation(math.radians(-90), 4, "X"), sides=24)
    cbox(lens, 2.6, -3.05, 6.0, 6.4, 0.1, 4.6)
    return [plastic, black, chrome, lens, rubber]


def cam_compact():
    """A small silver digital compact, its lens retracted, a screen on the back."""
    silver = Part("Silver", bevel=0.6, segments=3)
    cbox(silver, 0, 0, 3.1, 9.6, 2.2, 6.0)
    black, lens = Part("Black"), Part("Lens")
    lathe(black, [(0.0, 0.0), (1.8, 0.0), (1.8, 0.5), (1.2, 0.6), (0.0, 0.6)], Matrix.Translation((2.2, 1.1, 3.4)) @ Matrix.Rotation(math.radians(-90), 4, "X"), sides=20)
    cbox(lens, 2.2, 1.75, 3.4, 1.4, 0.1, 1.4)
    cbox(lens, -0.4, -1.12, 3.2, 6.4, 0.1, 4.6)
    cbox(black, -3.2, 0, 6.3, 1.2, 0.8, 0.4)
    return [silver, black, lens]


def spare_lens():
    """A telephoto lens stood on its rear cap."""
    black, chrome, lens = Part("Black", bevel=0.2), Part("Chrome"), Part("Lens")
    lathe(black, [(0.0, 0.0), (2.6, 0.0), (2.8, 1.0), (2.8, 9.0), (3.2, 9.4), (3.2, 13.0), (2.6, 13.0), (0.0, 12.8)], Matrix.Identity(4), sides=24)
    lathe(chrome, [(0.0, 5.0), (2.9, 5.0), (2.9, 6.0), (0.0, 6.0)], Matrix.Identity(4), sides=24)
    lathe(lens, [(0.0, 12.85), (2.4, 12.85), (2.4, 12.95), (0.0, 12.95)], Matrix.Identity(4), sides=20)
    return [black, chrome, lens]


# ---------------------------------------------------------------------------------------------
# Film.
# ---------------------------------------------------------------------------------------------

def film_can():
    """A 35mm cassette lying on its side, the leader out, its label round it. Axis along X."""
    tin = Part("Label")
    m = Matrix.Translation((0, 0, 1.25)) @ Matrix.Rotation(math.radians(90), 4, "Y")
    lathe(tin, [(0.0, -2.0), (1.25, -2.0), (1.25, 2.0), (0.0, 2.0)], m, sides=20)

    def uvs(bm):
        uv = bm.loops.layers.uv.verify()
        for face in bm.faces:
            for loop in face.loops:
                co = loop.vert.co
                a = math.atan2(co.z - 1.25, co.y)
                loop[uv].uv = atlas_uv("canister", min(max((co.x + 2.0) / 4.0, 0.0), 1.0), (a + math.pi) / (2 * math.pi))
    tin.uv_fn = uvs
    black = Part("Black")
    for sx in (-1, 1):
        lathe(black, [(0.0, 0.0), (1.3, 0.0), (1.3, 0.3), (0.0, 0.3)], Matrix.Translation((sx * 2.15 - 0.15, 0, 1.25)) @ Matrix.Rotation(math.radians(90), 4, "Y"), sides=20)
    lathe(black, [(0.0, 0.0), (0.45, 0.0), (0.45, 0.8), (0.0, 0.8)], Matrix.Translation((2.3, 0, 1.25)) @ Matrix.Rotation(math.radians(90), 4, "Y"), sides=10)
    film = Part("Film")
    cbox(film, 0.2, 2.6, 0.08, 3.4, 3.2, 0.12)
    return [tin, black, film]


def film_pot():
    """A black plastic film pot with a grey snap lid, standing."""
    black, lid = Part("Plastic"), Part("Silver")
    lathe(black, [(0.0, 0.0), (1.5, 0.0), (1.5, 5.0), (0.0, 5.0)], Matrix.Identity(4), sides=20)
    lathe(lid, [(0.0, 4.8), (1.65, 4.8), (1.65, 5.6), (0.0, 5.6)], Matrix.Identity(4), sides=20)
    return [black, lid]


def film_box():
    """A small carton of roll film standing up, its printed front facing +Y."""
    card = Part("Yellow", bevel=0.1)
    cbox(card, 0, 0, 3.0, 4.0, 2.8, 6.0)
    label = Part("Label")
    cbox(label, 0, 1.42, 3.0, 3.9, 0.05, 5.9)
    label.uv_fn = face_label_uvs("filmbox", -1.95, 1.95, 0.05, 5.95)
    return [card, label]


def cine_reel():
    """An 8mm cine reel lying flat: two grey flanges, the film wound on its hub."""
    grey = Part("Silver")
    for z in (0.0, 1.3):
        lathe(grey, [(0.0, z), (9.0, z), (9.0, z + 0.15), (0.0, z + 0.15)], Matrix.Identity(4), sides=40)
    lathe(grey, [(0.0, 0.0), (1.6, 0.0), (1.6, 1.45), (0.0, 1.45)], Matrix.Identity(4), sides=16)
    film = Part("Film")
    lathe(film, [(1.6, 0.18), (6.4, 0.18), (6.4, 1.27), (1.6, 1.27)], Matrix.Identity(4), sides=40)
    return [grey, film]


def dev_tank():
    """A stainless film developing tank, its lid off beside it and a spiral reel standing in it."""
    steel = Part("Chrome")
    lathe(steel, [(0.0, 0.0), (4.6, 0.0), (4.8, 0.4), (4.8, 11.0), (4.5, 11.0), (4.5, 0.6), (0.0, 0.6)], Matrix.Identity(4), sides=28)
    lathe(steel, [(0.0, 0.0), (5.0, 0.0), (5.0, 1.6), (1.4, 2.0), (1.4, 3.4), (0.0, 3.4)], Matrix.Translation((11.0, 1.0, 0.0)), sides=28)
    for k in range(18):
        a = k * 0.7
        r = 1.0 + k * 0.18
        tube(steel, [(math.cos(a) * r, math.sin(a) * r, 2.0), (math.cos(a) * r, math.sin(a) * r, 12.5)], 0.07, sides=4)
    return [steel]


# ---------------------------------------------------------------------------------------------
# The pendant safelight.
# ---------------------------------------------------------------------------------------------

def pendant():
    """A darkroom safelight hung from the ceiling: a ceiling rose, the flex, a black spun-steel
    shade and the deep red glass bowl under it. Origin at the rose, on the ceiling; the bowl's
    rim PENDANT_DROP below."""
    black = Part("Black", bevel=0.2)
    lathe(black, [(0.0, -2.2), (5.0, -2.2), (5.0, -0.6), (4.2, 0.0), (0.0, 0.0)], Matrix.Identity(4), sides=24)
    cord = Part("Cord")
    tube(cord, [(0, 0, -2.0), (0, 0, -PENDANT_DROP + 13.0)], 0.35, sides=8)
    z = -PENDANT_DROP
    lathe(black, [(0.0, z + 13.4), (1.6, z + 13.4), (1.8, z + 9.0), (8.0, z + 5.0), (13.0, z + 1.0), (13.4, z + 0.4),
                  (12.9, z + 0.4), (7.8, z + 4.4), (1.5, z + 8.6), (0.0, z + 8.6)], Matrix.Identity(4), sides=32)
    glass = Part("SafeGlass")
    lathe(glass, [(0.0, z - 6.0), (5.0, z - 5.4), (9.0, z - 3.0), (12.6, z + 0.6), (0.0, z + 0.6)], Matrix.Identity(4), sides=32)
    return [black, cord, glass]


MODELS = [
    ("darkroom_wet_bench", wet_bench), ("darkroom_dry_bench", dry_bench), ("darkroom_enlarger", enlarger),
    ("darkroom_tray", tray),
    ("darkroom_bottle_dev", lambda: bottle("dev")), ("darkroom_bottle_stop", lambda: bottle("stop")),
    ("darkroom_bottle_fix", lambda: bottle("fix")), ("darkroom_carboy", carboy), ("darkroom_jug", jug),
    ("darkroom_timer", timer), ("darkroom_tongs", tongs), ("darkroom_neg_box", neg_box),
    ("darkroom_paper_box", paper_box), ("darkroom_bin", bin_),
    ("darkroom_camera_cabinet", camera_cabinet), ("darkroom_cabinet_door", cabinet_door),
    ("darkroom_cam_box", cam_box), ("darkroom_cam_folding", cam_folding), ("darkroom_cam_tlr", cam_tlr),
    ("darkroom_cam_rangefinder", cam_rangefinder), ("darkroom_cam_slr", cam_slr), ("darkroom_cam_dslr", cam_dslr),
    ("darkroom_cam_compact", cam_compact), ("darkroom_spare_lens", spare_lens),
    ("darkroom_film_can", film_can), ("darkroom_film_pot", film_pot), ("darkroom_film_box", film_box),
    ("darkroom_cine_reel", cine_reel), ("darkroom_dev_tank", dev_tank), ("darkroom_pendant", pendant),
]

BIG = {"darkroom_wet_bench", "darkroom_dry_bench", "darkroom_enlarger", "darkroom_camera_cabinet"}


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
        open(os.path.join(K.MODEL_DIR, name, "GENERATED"), "w").close()
        baked.append(obj)
        print("DARKROOM baked {} ({} verts)".format(name, len(obj.data.vertices)))

    big = [o for o in baked if o.name in BIG]
    small = [o for o in baked if o not in big]
    x = 0.0
    for obj in big:
        width = obj.dimensions.x
        obj.location = (x + width * 0.5, 0.0, 0.0)
        x += width + 0.3
    for i, obj in enumerate(small):
        obj.location = (-0.6 - (i % 4) * 0.45, 2.0 + (i // 4) * 0.45, 0.0)
    K.preview(baked, PREVIEW_DIR, "models", [
        ("big", (x * 0.5, -6.0, 2.6), (x * 0.5, 0.0, 0.7)),
        ("enlarger", (x - 0.4, -1.6, 1.6), (x - 0.25, 0.0, 1.0)),
        ("small", (-1.3, 0.6, 1.6), (-1.3, 2.7, 0.1)),
    ])


main()
