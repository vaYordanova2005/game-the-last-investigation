"""
Builds the library's joinery, furniture and the things on its desk in Blender, headless, and
exports each as an FBX the art pipeline imports.

    blender --background --factory-startup --python Tools/make_library.py [-- --only=a,b]

Poly Haven has a bust, a clock and frames, and nothing a library is made of: no bookcase that is not
a catalogue line-up, no rolling ladder, no partners' desk, no globe, no gramophone, no green banker's
lamp. The house has shown what primitives do to things like those, so they are modelled here: the
bookcases are panelled cupboards under open shelves with a moulded cornice, the books are rows of
real bindings on the baked spine atlas (Tools/make_library_art.py), the globe is gored with the
coastlines, the chessmen are turned.

Outputs Art/Source/Models/<name>/<name>.fbx with an empty GENERATED file beside it (re-imported every
run with its own normals) and, for the book rows that go on the shelves by the thousand, an empty
NANITE file. Workbench previews go to Saved/LibraryPreview/.

Frame, as every prop in the house: centimetres, origin on the floor under the middle of the
footprint, front facing local +Y. Exceptions are noted on the model: the bookcase has its origin on
the wall plane (its back), wall-hung things on the wall plane, small things at their own base, the
arches in the plane of their wall.

The numbers ALibraryActor also needs are the constants below, and must stay in step with
LibraryActor.h: the bookcase, the shelves, the book rows, the rail, the desk, the arches.

Material slots are set by name in C++. Their texel sizes here match the surface each is given
there, whose instances have their tiling at one:
    Oak          black_oak_veneer (100cm)          Mahogany   lacquered_cherry_wood (120cm)
    Leather      brown_leather (40cm)              Stone      medieval_blocks_03 (170cm)
    Marble       marble_01 (150cm)                 Brick      castle_brick_01 (150cm)
    Fabric       rough_linen as drapery (34cm)     Panel      dark_paneled_wood (140cm)
    Books, Paper, Globe, Rug   the baked atlases, with their own UVs
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
from blender_kit import Part, at, box, cbox, ellipsoid, lathe, prism, prism_xz, tube  # noqa: E402

PREVIEW_DIR = os.path.join(K.ROOT, "Saved", "LibraryPreview")

FLAT = 100.0
SLOTS = {
    "Oak": ((0.16, 0.10, 0.06, 1.0), 100.0),
    "Mahogany": ((0.28, 0.08, 0.04, 1.0), 120.0),
    "Leather": ((0.30, 0.13, 0.05, 1.0), 40.0),
    "LeatherGreen": ((0.08, 0.20, 0.12, 1.0), 40.0),
    "Stone": ((0.50, 0.46, 0.40, 1.0), 170.0),
    "Marble": ((0.75, 0.73, 0.70, 1.0), 150.0),
    "Brick": ((0.12, 0.08, 0.06, 1.0), 150.0),
    "Fabric": ((0.55, 0.50, 0.40, 1.0), 34.0),
    "Panel": ((0.20, 0.12, 0.07, 1.0), 140.0),
    "Books": ((0.40, 0.20, 0.12, 1.0), FLAT),
    "Paper": ((0.85, 0.80, 0.68, 1.0), FLAT),
    "Globe": ((0.70, 0.62, 0.45, 1.0), FLAT),
    "Rug": ((0.45, 0.12, 0.10, 1.0), FLAT),
    "Fringe": ((0.75, 0.70, 0.58, 1.0), FLAT),
    "Brass": ((0.60, 0.45, 0.20, 1.0), FLAT),
    "Gilt": ((0.75, 0.60, 0.25, 1.0), FLAT),
    "Iron": ((0.15, 0.12, 0.10, 1.0), FLAT),
    "Glass": ((0.70, 0.75, 0.78, 1.0), FLAT),
    "Shade": ((0.05, 0.35, 0.15, 1.0), FLAT),
    "ShadeInside": ((0.85, 0.85, 0.78, 1.0), FLAT),
    "Felt": ((0.08, 0.14, 0.10, 1.0), FLAT),
    "Record": ((0.02, 0.02, 0.02, 1.0), FLAT),
    "Bone": ((0.85, 0.80, 0.68, 1.0), FLAT),
    "Ebony": ((0.05, 0.04, 0.04, 1.0), FLAT),
    "Shadow": ((0.02, 0.02, 0.02, 1.0), FLAT),
    "Trim": ((0.80, 0.78, 0.72, 1.0), FLAT),
    "CorridorWall": ((0.55, 0.50, 0.45, 1.0), 104.0),
    "LibraryWall": ((0.25, 0.15, 0.09, 1.0), 140.0),
    "Ash": ((0.40, 0.38, 0.35, 1.0), FLAT),
    "Charcoal": ((0.05, 0.04, 0.04, 1.0), FLAT),
    "Wax": ((0.45, 0.06, 0.05, 1.0), FLAT),
    "Ink": ((0.02, 0.02, 0.05, 1.0), FLAT),
    "Ceramic": ((0.80, 0.78, 0.72, 1.0), FLAT),
    "Aluminium": ((0.25, 0.25, 0.27, 1.0), FLAT),
    "Velvet": ((0.30, 0.05, 0.08, 1.0), FLAT),
    "Card": ((0.70, 0.60, 0.42, 1.0), FLAT),
    "Bristle": ((0.30, 0.22, 0.12, 1.0), FLAT),
    "CurtainVelvet": ((0.45, 0.03, 0.05, 1.0), 34.0),
    "Gold": ((0.75, 0.58, 0.22, 1.0), FLAT),
}

# ---------------------------------------------------------------------------------------------
# Shared with LibraryActor.h.
# ---------------------------------------------------------------------------------------------

# A bookcase bay: a panelled cupboard to the counter, open shelves above, a cornice. Nominal 100
# wide (the C++ fits a whole number of bays to a run and scales them a few per cent to fill it).
BAY_WIDTH = 100.0
UPRIGHT = 2.5
LOWER_DEPTH = 46.0
UPPER_DEPTH = 34.0
PLINTH = 10.0
COUNTER_Z = 92.0
SHELF_PITCH = 36.0
SHELF_LEVELS = 10          # the counter and nine shelves over it carry books
SHELF_BOARD = 2.4
TOP_Z = COUNTER_Z + SHELF_LEVELS * SHELF_PITCH   # 452: the underside of the case's top
CASE_TOP = TOP_Z + 18.0                          # 470 with the cornice

# The rail the library ladder hangs from, on the front of the uprights.
RAIL_Z = 434.0
RAIL_PROUD = UPPER_DEPTH + 6.0
LADDER_FOOT = LOWER_DEPTH + 70.0

# Book rows: each a run of bindings standing on z = 0, spines along y = 0 facing +Y, from x = 0 to
# x = its length. Lengths are exact.
ROW_LENGTHS = [46.0, 44.0, 47.0, 42.0, 45.0, 46.0, 43.0, 47.0, 30.0, 34.0]

# The desk and the arches.
DESK_W = 180.0
DESK_D = 90.0
DESK_TOP = 78.0
ARCH_HALF = 80.0
# The arch is off the stair hall's south gallery: its hall face's architrave over the crown (14) and
# the keystone stand above the hall's picture rail (262), which the hall cuts round them
# (AGrandStaircaseActor::CutAroundTrim), and under the hall's ceiling.
ARCH_SPRING = 185.0
ARCH_CROWN = ARCH_SPRING + ARCH_HALF
ARCH_WALL = 20.0
WINDOW_HALF = 75.0

# Frames: the size of the picture each holds.
MAP_ART = (72.0, 72.0)
PORTRAIT_LARGE_ART = (88.0, 110.0)
PORTRAIT_SMALL_ART = (64.0, 80.0)

# library_books atlas (Tools/make_library_art.py).
ATLAS = 2048.0
SPINE_W = 128.0
SPINE_H = 768.0
PAGES = (0.0, 1536.0, 1024.0, 512.0)
COVERS_X = 1024.0
COVER_W = 128.0
# The binding each spine cell is in, written by make_library_art.py beside the atlas: the cover
# patch to use with it.
with open(os.path.join(K.ROOT, "Art", "Source", "Textures", "library_books.json"), "r", encoding="utf-8") as _handle:
    SPINE_BINDING = __import__("json").load(_handle)["bindings"]

# library_paper atlas rectangles (x, y, w, h).
PAPER = {
    "notebook_l": (0, 0, 366, 512), "notebook_r": (512, 0, 366, 512),
    "book_l": (1024, 0, 366, 512), "book_r": (1536, 0, 366, 512),
    "letter": (1024, 512, 362, 512), "newspaper": (0, 1024, 1024, 724),
}


def binding_of(cell):
    return SPINE_BINDING[cell]


# ---------------------------------------------------------------------------------------------
# Atlas-mapped boxes.
# ---------------------------------------------------------------------------------------------

def uv_box(part, x0, x1, y0, y1, z0, z1, rects, matrix=None, page_top=False):
    """A box whose six faces each show a rectangle of an atlas, upright to whoever looks at them.

    rects: face -> (u0, v0, u1, v1) in atlas pixels, v down the image. Faces: py (front, +Y), ny,
    px, nx, pz (top), nz. The leaves' faces (pz, nz, ny) are laid so the page lines run along the
    leaves; with page_top the top is a sheet read from -Y instead (its top edge at +Y). UVs are
    worked out from the local coordinates, before the matrix moves the box."""
    bm = part.bm
    uv = bm.loops.layers.uv.verify()
    verts = [bm.verts.new((x, y, z)) for z in (z0, z1) for y in (y0, y1) for x in (x0, x1)]
    dx, dy, dz = x1 - x0, y1 - y0, z1 - z0

    def st(face, co):
        x, y, z = co
        if face == "py":
            return (x - x0) / dx, (z1 - z) / dz
        if face == "ny":
            return (z1 - z) / dz, (x - x0) / dx
        if face == "px":
            return (y1 - y) / dy, (z1 - z) / dz
        if face == "nx":
            return (y - y0) / dy, (z1 - z) / dz
        if page_top and face == "pz":
            # Read from -Y, facing +Y: the reader's right is -X (UE is left-handed).
            return (x1 - x) / dx, (y1 - y) / dy
        return (y - y0) / dy, (x - x0) / dx

    faces = {"nz": (0, 2, 3, 1), "pz": (4, 5, 7, 6), "ny": (0, 1, 5, 4), "py": (2, 6, 7, 3), "nx": (0, 4, 6, 2), "px": (1, 3, 7, 5)}
    for name, idx in faces.items():
        face = bm.faces.new([verts[i] for i in idx])
        u0, v0, u1, v1 = rects[name]
        for loop in face.loops:
            s, t = st(name, tuple(loop.vert.co))
            loop[uv].uv = ((u0 + s * (u1 - u0)) / ATLAS, 1.0 - (v0 + t * (v1 - v0)) / ATLAS)
    if matrix is not None:
        bmesh.ops.transform(bm, matrix=matrix, verts=verts)
    return verts


def no_uvs(bm):
    """For parts whose UVs were laid while they were built: keeps box_uvs off them."""
    return None


def sheet_strip(part, rows, uvs, thickness=0.06):
    """A sheet of paper as a closed thin solid along a strip of stations. rows: (near, far) points
    per station; uvs: (near uv, far uv) per station, for the top face. The bake recalculates normals
    part by part, and on an open sheet that is a coin toss: a page facing down under a single-sided
    material is a page that is not there."""
    bm = part.bm
    uv = bm.loops.layers.uv.verify()
    down = Vector((0.0, 0.0, -thickness))
    top = [(bm.verts.new(Vector(a)), bm.verts.new(Vector(b))) for a, b in rows]
    bot = [(bm.verts.new(Vector(a) + down), bm.verts.new(Vector(b) + down)) for a, b in rows]
    edge_uv = uvs[0][0]

    def face(verts, coords):
        f = bm.faces.new(verts)
        for loop, c in zip(f.loops, coords):
            loop[uv].uv = c
        return f

    for i in range(len(rows) - 1):
        face((top[i][0], top[i + 1][0], top[i + 1][1], top[i][1]), (uvs[i][0], uvs[i + 1][0], uvs[i + 1][1], uvs[i][1]))
        face((bot[i][1], bot[i + 1][1], bot[i + 1][0], bot[i][0]), (uvs[i][1], uvs[i + 1][1], uvs[i + 1][0], uvs[i][0]))
        face((top[i][0], bot[i][0], bot[i + 1][0], top[i + 1][0]), (edge_uv,) * 4)
        face((top[i + 1][1], bot[i + 1][1], bot[i][1], top[i][1]), (edge_uv,) * 4)
    face((top[0][1], bot[0][1], bot[0][0], top[0][0]), (edge_uv,) * 4)
    face((top[-1][0], bot[-1][0], bot[-1][1], top[-1][1]), (edge_uv,) * 4)


def atlas_uv(rect, s, t):
    """(s, t) across a rectangle of the 2048 atlas, t down the image, as a Blender UV."""
    x, y, w, h = rect
    return ((x + s * w) / ATLAS, 1.0 - (y + t * h) / ATLAS)


def book(part, x, y_front, thickness, depth, height, cell, matrix=None, rng=None):
    """One bound book standing on z = 0: spine at y_front facing +Y, boards at the sides, the
    leaves at the top, the bottom and the fore-edge (the back)."""
    col, row = cell % 16, cell // 16
    su, sv = col * SPINE_W, row * SPINE_H
    cover = binding_of(cell)
    cu = COVERS_X + cover * COVER_W
    rng = rng or random.Random(cell)
    # Each book takes a different patch of the leaves and the boards, or every top in a row is the
    # same few square centimetres of foxing.
    pu = PAGES[0] + rng.uniform(0, PAGES[2] - 200)
    pv = PAGES[1] + rng.uniform(0, PAGES[3] - 60)
    leaves = (pu, pv, pu + depth * 6.0, pv + thickness * 6.0)
    fore = (pu, pv, pu + height * 4.0, pv + thickness * 6.0)
    cv = 1536.0 + rng.uniform(0, 512 - height * 6.0 - 1)
    boards = (cu + 4, cv, cu + COVER_W - 4, cv + height * 6.0)
    rects = {"py": (su, sv, su + SPINE_W, sv + SPINE_H), "ny": fore, "px": boards, "nx": boards, "pz": leaves, "nz": leaves}
    m = matrix if matrix is not None else Matrix.Identity(4)
    m = m @ Matrix.Translation((x, 0.0, 0.0))
    return uv_box(part, 0.0, thickness, y_front - depth, y_front, 0.0, height, rects, m)


# ---------------------------------------------------------------------------------------------
# The bookcase.
# ---------------------------------------------------------------------------------------------

def bookcase():
    """One bay: a plinth, a cupboard with two fielded doors to the counter, nine shelves over it in
    an open case, a stepped cornice. Origin on the wall plane at the middle of its back; front +Y."""
    hw = BAY_WIDTH * 0.5
    oak = Part("Oak", bevel=0.35)
    # The plinth, set back under the doors.
    cbox(oak, 0.0, (LOWER_DEPTH - 3.0) * 0.5, PLINTH * 0.5, BAY_WIDTH, LOWER_DEPTH - 3.0, PLINTH)
    # Cupboard sides and the counter.
    for sx in (-1, 1):
        cbox(oak, sx * (hw - UPRIGHT * 0.5), LOWER_DEPTH * 0.5, (PLINTH + COUNTER_Z - 3.0) * 0.5, UPRIGHT, LOWER_DEPTH, COUNTER_Z - 3.0 - PLINTH)
    counter = Part("Oak", bevel=0.8, segments=3)
    cbox(counter, 0.0, (LOWER_DEPTH + 2.0) * 0.5, COUNTER_Z - 1.5, BAY_WIDTH + 1.0, LOWER_DEPTH + 2.0, 3.0)
    doors = Part("Oak", bevel=0.4)
    fields = Part("Oak", bevel=1.6, segments=2, angle=30.0)
    dw = (BAY_WIDTH - 2 * UPRIGHT - 1.0) * 0.5
    for sx in (-1, 1):
        cx = sx * (dw * 0.5 + 0.25)
        cbox(doors, cx, LOWER_DEPTH - 1.0, (PLINTH + 1.0 + COUNTER_Z - 4.0) * 0.5, dw, 2.0, COUNTER_Z - 5.0 - PLINTH)
        cbox(fields, cx, LOWER_DEPTH + 0.3, (PLINTH + 1.0 + COUNTER_Z - 4.0) * 0.5, dw - 12.0, 1.4, COUNTER_Z - 19.0 - PLINTH)
    brass = Part("Brass")
    for sx in (-1, 1):
        lathe(brass, [(0.0, 0.0), (0.7, 0.0), (1.2, 0.9), (0.9, 1.8), (0.0, 1.9)], at(sx * 3.0, LOWER_DEPTH, 62.0, roll=-90.0), sides=12)
        # Keyhole escutcheon on the left door.
    cbox(brass, -3.0, LOWER_DEPTH + 0.05, 56.0, 1.2, 0.2, 2.4)
    # The open case: uprights, shelves with a lipped front, the back, the top.
    case = Part("Oak", bevel=0.3)
    for sx in (-1, 1):
        cbox(case, sx * (hw - UPRIGHT * 0.5), UPPER_DEPTH * 0.5, (COUNTER_Z + TOP_Z) * 0.5, UPRIGHT, UPPER_DEPTH, TOP_Z - COUNTER_Z)
        # A flat pilaster strip on the front of each upright, and a fillet down it.
        cbox(case, sx * (hw - UPRIGHT), UPPER_DEPTH + 0.6, (COUNTER_Z + TOP_Z) * 0.5, 5.0, 1.2, TOP_Z - COUNTER_Z)
    shelves = Part("Oak", bevel=0.4)
    for k in range(1, SHELF_LEVELS):
        z = COUNTER_Z + k * SHELF_PITCH
        cbox(shelves, 0.0, (UPPER_DEPTH - 1.0) * 0.5 + 0.2, z - SHELF_BOARD * 0.5, BAY_WIDTH - 2 * UPRIGHT, UPPER_DEPTH - 1.0, SHELF_BOARD)
        cbox(shelves, 0.0, UPPER_DEPTH - 0.6, z - SHELF_BOARD - 0.8, BAY_WIDTH - 2 * UPRIGHT, 1.2, 1.6)
    back = Part("Shadow")
    cbox(back, 0.0, 0.5, (COUNTER_Z + TOP_Z) * 0.5, BAY_WIDTH - 2 * UPRIGHT, 1.0, TOP_Z - COUNTER_Z)
    cbox(case, 0.0, UPPER_DEPTH * 0.5, TOP_Z + 1.25, BAY_WIDTH, UPPER_DEPTH, 2.5)
    cornice = Part("Oak", bevel=0.7, segments=3)
    for k, (z0, z1, d) in enumerate(((TOP_Z + 2.5, TOP_Z + 7.0, UPPER_DEPTH + 1.5), (TOP_Z + 7.0, TOP_Z + 11.0, UPPER_DEPTH + 3.5),
                                     (TOP_Z + 11.0, TOP_Z + 14.0, UPPER_DEPTH + 1.0), (TOP_Z + 14.0, CASE_TOP, UPPER_DEPTH + 5.5))):
        cbox(cornice, 0.0, d * 0.5, (z0 + z1) * 0.5, BAY_WIDTH + 0.2, d, z1 - z0)
    return [oak, counter, doors, fields, brass, case, shelves, back, cornice]


# ---------------------------------------------------------------------------------------------
# Books.
# ---------------------------------------------------------------------------------------------

def book_row(length, seed):
    """A run of books to fill exactly `length`: sets kept together, journals together, novels
    together, singles in between; heights, thicknesses and depths each their own, every spine flush
    or nearly so with the next, as books stand when somebody has used them."""
    rng = random.Random(seed)
    part = Part("Books")
    part.uv_fn = no_uvs
    x = 0.0
    runs = []
    while x < length:
        kind = rng.random()
        if kind < 0.18:
            start = rng.randint(0, 3)
            cells = list(range(start, min(8, start + rng.randint(3, 5))))
            runs.append([(c, 5.4, 22.0, 28.5) for c in cells])
        elif kind < 0.26:
            cells = list(range(8, 8 + rng.randint(2, 4)))
            runs.append([(c, 3.6, 17.5, 24.5) for c in cells])
        elif kind < 0.42:
            runs.append([(rng.randint(12, 18), rng.uniform(2.6, 3.8), rng.uniform(13.5, 15.0), rng.uniform(19.0, 21.0)) for _ in range(rng.randint(2, 4))])
        else:
            runs.append([(rng.randint(19, 31), rng.uniform(2.4, 6.8), rng.uniform(15.0, 24.0), rng.uniform(21.0, 31.0)) for _ in range(rng.randint(2, 5))])
        x += sum(b[1] + 0.08 for b in runs[-1])
    books = [b for r in runs for b in r]
    # Squeeze or stretch the last book so the row is exactly its length.
    total = 0.0
    out = []
    for cell, t, d, h in books:
        if total + t + 0.08 > length:
            t = length - total - 0.08
            if t < 1.6:
                break
            out.append((cell, t, d, h))
            total += t + 0.08
            break
        out.append((cell, t, d, h))
        total += t + 0.08
    x = 0.0
    for cell, t, d, h in out:
        front = -rng.uniform(0.0, 2.2)
        book(part, x, front, t, d, h, cell, rng=rng)
        x += t + 0.08
    return [part]


def books_leaning():
    """Three books at the end of a row with nothing to hold them up: the first upright, the other
    two gone over onto it. Origin where the upright one touches the row on its left (x = 0)."""
    rng = random.Random(77)
    part = Part("Books")
    part.uv_fn = no_uvs
    book(part, 0.0, -1.0, 4.2, 20.0, 27.0, 20, rng=rng)
    # Two gone over to the left like dominoes, each on its bottom-left edge, the top of the first on
    # the upright one's shoulder. Turning about Y by -a takes a book's top over to -X and lifts its
    # bottom-right corner; two at one angle touch along their faces when the second's pivot is the
    # first's thickness over cos(a) along.
    a = math.radians(19.0)
    h1 = 25.0
    pivot = 4.2 + h1 * math.sin(a)
    for k, (t, d, h, cell) in enumerate(((3.2, 18.0, h1, 23), (5.0, 21.0, 26.0, 27))):
        m = Matrix.Translation((pivot, 0.0, 0.0)) @ Matrix.Rotation(-a, 4, "Y")
        book(part, 0.0, -1.5 - k, t, d, h, cell, matrix=m, rng=rng)
        pivot += t / math.cos(a)
    return [part]


def books_lying(seed, count):
    """A pile of books laid flat where there was a gap, the biggest at the bottom, each a little
    turned on the one under it. Origin at the middle of the bottom."""
    rng = random.Random(seed)
    part = Part("Books")
    part.uv_fn = no_uvs
    z = 0.0
    sizes = sorted(((rng.uniform(2.4, 5.0), rng.uniform(16.0, 22.0), rng.uniform(22.0, 30.0), rng.randint(19, 31)) for _ in range(count)),
                   key=lambda s: -s[2])
    for t, d, h, cell in sizes:
        yaw = rng.uniform(-8.0, 8.0)
        # Laid on its side, spine still to +Y: a quarter turn about Y takes the book's thickness
        # (local X) down to -Z and its height (local Z) along +X; then up by its thickness and back
        # by half its height, so it lies centred on the one under it.
        m = at(0.0, 0.0, z, yaw=yaw) @ Matrix.Translation((-h * 0.5, 0.0, t)) @ Matrix.Rotation(math.radians(90.0), 4, "Y")
        book(part, 0.0, d * 0.5, t, d, h, cell, matrix=m, rng=rng)
        z += t + 0.05
    return [part]


# ---------------------------------------------------------------------------------------------
# Architecture.
# ---------------------------------------------------------------------------------------------

def semicircle(cx, cz, r, steps, a0=0.0, a1=math.pi):
    return [(cx + r * math.cos(a0 + (a1 - a0) * k / steps), cz + r * math.sin(a0 + (a1 - a0) * k / steps)) for k in range(steps + 1)]


def spandrel(part, half, spring, crown_top, y0, y1, steps=24):
    """The masonry over a round-headed opening: the rectangle from the springing to crown_top, less
    the semicircle. Two pieces, one each side of the crown, each a profile in XZ extruded along Y.
    Where crown_top is the crown itself the piece comes to nothing at the middle, so the arc's top
    point is not repeated there."""
    for side in (-1, 1):
        # The arc from the springing on this side (angle 0 or pi) up to the crown (pi / 2).
        arc = semicircle(0.0, spring, half, steps, 0.0 if side > 0 else math.pi, math.pi * 0.5)
        outline = [(side * half, crown_top)] + arc
        if crown_top - (spring + half) > 0.01:
            outline.append((0.0, crown_top))
        prism_xz(part, outline, y0, y1)


def moulding_path(part, path, profile, closed=False):
    """A moulding swept along a path in the XZ plane: profile points (out of the wall, across the
    path) in cm. Out is +Y. Each station is a ring of the profile, oriented by the path's tangent."""
    bm = part.bm
    rings = []
    n = len(path)
    for i, (x, z) in enumerate(path):
        a = path[max(i - 1, 0)]
        b = path[min(i + 1, n - 1)]
        tx, tz = b[0] - a[0], b[1] - a[1]
        L = math.hypot(tx, tz) or 1.0
        nx, nz = -tz / L, tx / L  # the normal in the wall plane, to the left of the path
        rings.append([bm.verts.new((x + nx * across, out, z + nz * across)) for across, out in profile])
    m = len(profile)
    for r0, r1 in zip(rings, rings[1:]):
        for k in range(m):
            j = (k + 1) % m
            bm.faces.new((r0[k], r0[j], r1[j], r1[k]))
    bm.faces.new(rings[0])
    bm.faces.new(rings[-1][::-1])


def architrave_profile(width, depth, flip=1.0):
    """An architrave's section: (across, out). A flat face stepping up to a rounded outer bead."""
    return [(0.0, 0.0), (width, 0.0), (width, depth * 0.7 * flip), (width - 1.2, depth * flip), (width - 3.0, depth * flip),
            (width - 3.6, depth * 0.6 * flip), (0.0, depth * 0.45 * flip)]


def corridor_arch():
    """The way into the library from the stair hall's south gallery (the name is from when it opened
    off the corridor): a round-headed arch through the hall's south wall, lined in oak, with an
    architrave on each face. The CorridorWall and Trim slots are the hall face's plaster and staircase
    oak. Origin at the opening's middle on the floor, in the middle of the wall; the hall is at -Y, the
    library at +Y."""
    half, spring, crown = ARCH_HALF, ARCH_SPRING, ARCH_CROWN
    hy = ARCH_WALL * 0.5
    # The masonry over the curve, in each room's wall finish: a thin skin on each face, a core.
    core = Part("Shadow")
    spandrel(core, half, spring, crown, -hy + 0.6, hy - 0.6)
    corridor = Part("CorridorWall")
    spandrel(corridor, half, spring, crown, -hy - 0.4, -hy + 0.6)
    library = Part("LibraryWall")
    spandrel(library, half, spring, crown, hy - 0.6, hy + 0.4)
    # The lining: jambs and the soffit, a board two centimetres thick round the inside.
    lining = Part("Oak", bevel=0.3)
    for side in (-1, 1):
        cbox(lining, side * (half - 1.0), 0.0, spring * 0.5, 2.0, ARCH_WALL + 1.0, spring)
    steps = 32
    arc_in = semicircle(0.0, spring, half - 2.0, steps)
    arc_out = semicircle(0.0, spring, half + 0.2, steps)
    bm = lining.bm
    near = [bm.verts.new((x, -hy - 0.5, z)) for x, z in arc_out] + [bm.verts.new((x, -hy - 0.5, z)) for x, z in reversed(arc_in)]
    far = [bm.verts.new((x, hy + 0.5, z)) for x, z in arc_out] + [bm.verts.new((x, hy + 0.5, z)) for x, z in reversed(arc_in)]
    count = len(near)
    for k in range(count):
        j = (k + 1) % count
        bm.faces.new((near[k], near[j], far[j], far[k]))
    for k in range(steps):
        a, b = k, k + 1
        c, d = count - 1 - (k + 1), count - 1 - k
        bm.faces.new((near[a], near[b], near[c], near[d]))
        bm.faces.new((far[d], far[c], far[b], far[a]))
    # The architraves: up each jamb, round the head, on both faces. The hall face's takes the hall's
    # staircase oak (the Trim slot); the library's is the library's oak. The path runs up the left jamb, over the head left to
    # right and down the right jamb, so the moulding's positive "across" is always away from the
    # opening.
    path = ([(-half - 0.2, 0.0), (-half - 0.2, spring)] + semicircle(0.0, spring, half + 0.2, 48, math.pi, 0.0)[1:-1]
            + [(half + 0.2, spring), (half + 0.2, 0.0)])
    corridor_trim = Part("Trim", bevel=0.2)
    oak_trim = Part("Oak", bevel=0.2)
    width = 14.0
    profile = architrave_profile(width, 3.2)
    moulding_path(corridor_trim, path, [(a, -hy - 0.2 - o) for a, o in profile])
    moulding_path(oak_trim, path, [(a, hy + 0.2 + o) for a, o in profile])
    # Plinth blocks at the foot of each jamb, and a keystone at the crown.
    blocks_c = Part("Trim", bevel=0.4)
    blocks_o = Part("Oak", bevel=0.4)
    for side in (-1, 1):
        cbox(blocks_c, side * (half + width * 0.5), -hy - 2.2, 12.0, width + 2.0, 4.4, 24.0)
        cbox(blocks_o, side * (half + width * 0.5), hy + 2.2, 12.0, width + 2.0, 4.4, 24.0)
    key = [(-8.0, crown - 4.0), (8.0, crown - 4.0), (10.0, crown + 14.0), (-10.0, crown + 14.0)]
    prism_xz(blocks_c, key, -hy - 5.0, -hy)
    prism_xz(blocks_o, key, hy, hy + 5.0)
    sill = Part("Oak", bevel=0.4)
    cbox(sill, 0.0, 0.0, 1.0, half * 2.0, ARCH_WALL + 4.0, 2.0)
    return [core, corridor, library, lining, corridor_trim, oak_trim, blocks_c, blocks_o, sill]


def window_arch():
    """The round head of a tall window, on the room side: the plaster over the curve through to the
    sash, a soffit lining it, and an oak architrave round the curve with a keystone. The jambs below
    the springing are boxes in C++. Origin on the room face at the opening's middle, z = 0 at the
    top of the opening; the room is +Y, the wall -Y."""
    half = WINDOW_HALF
    spring = -half
    fill = Part("Trim")
    spandrel(fill, half, spring, 0.0, -8.5, 0.2)
    soffit = Part("Trim")
    steps = 32
    bm = soffit.bm
    arc_a = semicircle(0.0, spring, half - 0.1, steps)
    arc_b = semicircle(0.0, spring, half - 1.6, steps)
    ring = [bm.verts.new((x, y, z)) for (x, z) in arc_a for y in (-8.5, 0.4)]
    inner = [bm.verts.new((x, y, z)) for (x, z) in arc_b for y in (-8.5, 0.4)]
    for k in range(steps):
        a0, a1 = ring[2 * k], ring[2 * k + 1]
        b0, b1 = ring[2 * (k + 1)], ring[2 * (k + 1) + 1]
        c0, c1 = inner[2 * k], inner[2 * k + 1]
        d0, d1 = inner[2 * (k + 1)], inner[2 * (k + 1) + 1]
        bm.faces.new((c0, d0, d1, c1))
        bm.faces.new((a0, a1, b1, b0))
        bm.faces.new((a1, c1, d1, b1))
        bm.faces.new((a0, b0, d0, c0))
    # Closed at both springings, so the bake's normals have an inside to point away from.
    for k in (0, steps):
        bm.faces.new((ring[2 * k], ring[2 * k + 1], inner[2 * k + 1], inner[2 * k]))
    trim = Part("Oak", bevel=0.2)
    path = semicircle(0.0, spring, half + 0.1, 40, math.pi, 0.0)
    moulding_path(trim, path, [(a, 0.2 + o) for a, o in architrave_profile(12.0, 2.8)])
    key = Part("Oak", bevel=0.4)
    prism_xz(key, [(-7.0, -3.0), (7.0, -3.0), (9.5, 14.0), (-9.5, 14.0)], 0.2, 4.6)
    return [fill, soffit, trim, key]


def cornice():
    """The plaster crown moulding where the walls meet the ceiling: a cove between two steps, a
    dentil course under it. One metre long, along X; origin on the wall at the ceiling line; it
    hangs down -Z and stands out +Y. The C++ lays it end to end and scales the last to fit."""
    part = Part("Trim", bevel=0.25)
    profile = [(0.0, 0.0), (0.0, -34.0), (2.5, -34.0), (2.5, -30.0), (5.0, -28.5), (5.0, -25.5)]
    for k in range(9):
        a = math.pi * 0.5 * k / 8
        profile.append((5.0 + 18.0 * (1 - math.cos(a)), -25.5 + 18.0 * math.sin(a)))
    profile += [(26.0, -7.0), (26.0, -3.0), (30.0, -3.0), (30.0, 0.0)]
    pts = [(y, z) for y, z in profile]
    bm = part.bm
    left = [bm.verts.new((-50.0, y, z)) for y, z in pts]
    right = [bm.verts.new((50.0, y, z)) for y, z in pts]
    bm.faces.new(left)
    bm.faces.new(right[::-1])
    for k in range(len(pts)):
        j = (k + 1) % len(pts)
        bm.faces.new((left[k], left[j], right[j], right[k]))
    dentils = Part("Trim", bevel=0.15)
    for k in range(20):
        cbox(dentils, -47.5 + k * 5.0, 3.6, -31.0, 2.6, 2.4, 3.2)
    return [part, dentils]


# ---------------------------------------------------------------------------------------------
# The fireplace.
# ---------------------------------------------------------------------------------------------

def fireplace():
    """A carved stone chimneypiece, 210 wide and 140 to the mantel shelf: fluted pilasters on plinths,
    a frieze with a tablet, a moulded shelf. Inside, a firebox lined in soot-black brick with a cast
    iron fire basket, burnt-down logs and a bed of ash, and a stone hearth in front. Origin on the
    floor at the middle of the chimney breast's face; front +Y; the firebox goes back to -46."""
    stone = Part("Marble", bevel=0.5)
    ow, oh = 96.0, 94.0          # the opening
    # Pilasters and their plinths.
    for sx in (-1, 1):
        x = sx * (ow * 0.5 + 18.0)
        cbox(stone, x, 6.0, 6.0, 40.0, 12.0, 12.0)
        cbox(stone, x, 4.0, 62.0, 34.0, 8.0, 100.0)
        cbox(stone, x, 5.0, 114.0, 38.0, 10.0, 6.0)
    flutes = Part("Shadow")
    for sx in (-1, 1):
        for k in range(5):
            x = sx * (ow * 0.5 + 18.0) + (k - 2) * 5.6
            cbox(flutes, x, 8.05, 64.0, 2.2, 0.2, 84.0)
    # The frieze over the opening, a tablet in its middle, the shelf over all.
    cbox(stone, 0.0, 3.5, (oh + 116.0) * 0.5, ow + 76.0, 7.0, 116.0 - oh + 1.0)
    cbox(stone, 0.0, 7.5, (oh + 116.0) * 0.5, 44.0, 2.0, 16.0)
    shelf = Part("Marble", bevel=1.0, segments=3)
    cbox(shelf, 0.0, 6.0, 120.0, ow + 92.0, 12.0, 8.0)
    cbox(shelf, 0.0, 12.0, 127.0, ow + 110.0, 26.0, 6.0)
    cbox(shelf, 0.0, 14.5, 134.0, ow + 118.0, 31.0, 8.0)
    # The firebox: brick sides, back and roof, all soot.
    brick = Part("Brick")
    cbox(brick, 0.0, -44.0, oh * 0.5, ow, 4.0, oh)
    for sx in (-1, 1):
        prism(brick, [(sx * ow * 0.5, 0.0), (sx * (ow * 0.5 - 14.0), -44.0), (sx * (ow * 0.5 - 18.0), -44.0), (sx * (ow * 0.5 - 4.0), 0.0)][::sx], 0.0, oh)
    cbox(brick, 0.0, -22.0, oh + 2.0, ow, 46.0, 4.0)
    floor = Part("Brick")
    cbox(floor, 0.0, -22.0, 1.0, ow, 44.0, 2.0)
    # The hearth stone in front, a step proud of the floor.
    hearth = Part("Marble", bevel=0.6)
    cbox(hearth, 0.0, 22.0, 1.5, ow + 120.0, 44.0, 3.0)
    # The fire basket: a grate of bars on four legs, a fret front.
    iron = Part("Iron")
    gx0, gx1, gy0, gy1, gz = -30.0, 30.0, -32.0, -8.0, 16.0
    for x in (gx0, gx1):
        for y in (gy0, gy1):
            tube(iron, [(x, y, 0.0), (x, y, gz)], 1.0, sides=8)
    for k in range(9):
        x = gx0 + k * (gx1 - gx0) / 8
        tube(iron, [(x, gy0, gz), (x, gy1, gz), (x, gy1 + 2.0, gz + 10.0)], 0.6, sides=6)
    tube(iron, [(gx0, gy1 + 2.0, gz + 10.0), (gx1, gy1 + 2.0, gz + 10.0)], 0.7, sides=6)
    tube(iron, [(gx0, gy1, gz), (gx1, gy1, gz)], 0.9, sides=8)
    tube(iron, [(gx0, gy0, gz), (gx1, gy0, gz)], 0.9, sides=8)
    for x in (gx0 + 4.0, gx1 - 4.0):
        lathe(iron, [(0.0, 0.0), (2.0, 0.0), (1.6, 6.0), (2.6, 10.0), (0.0, 13.0)], at(x, gy1 + 3.0, gz + 9.0), sides=10)
    # Burnt logs: split and charred, one half-fallen through the bars.
    logs = Part("Charcoal")
    rng = random.Random(31)
    for k, (x, y, z, yaw, pitch, r, L) in enumerate(((-6.0, -20.0, gz + 4.0, 8.0, 0.0, 5.0, 46.0), (6.0, -24.0, gz + 9.0, -14.0, 6.0, 4.0, 40.0),
                                                     (0.0, -14.0, gz + 3.0, 80.0, 20.0, 3.4, 30.0))):
        m = at(x, y, z, yaw=yaw, pitch=pitch) @ Matrix.Rotation(math.radians(90.0), 4, "Y")
        prof = [(0.0, -L * 0.5)] + [(r * rng.uniform(0.8, 1.05), -L * 0.5 + L * i / 6) for i in range(7)] + [(0.0, L * 0.5)]
        lathe(logs, prof, m, sides=9)
    ash = Part("Ash")
    lathe(ash, [(0.0, 0.0), (34.0, 0.0), (30.0, 2.0), (20.0, 4.0), (8.0, 5.5), (0.0, 6.0)], at(0.0, -20.0, 2.0) @ Matrix.Diagonal((1.3, 0.6, 1.0, 1.0)), sides=24)
    return [stone, flutes, shelf, brick, floor, hearth, iron, logs, ash]


def fire_tools():
    """A companion set on its stand: poker, tongs, shovel and brush, hanging from a cross-bar over a
    weighted base. Origin on the floor under the stand's column."""
    brass = Part("Brass")
    iron = Part("Iron")
    bristle = Part("Bristle")
    lathe(brass, [(0.0, 0.0), (10.0, 0.0), (10.0, 1.6), (7.0, 3.0), (5.0, 3.6), (2.0, 4.0), (1.4, 6.0), (1.2, 64.0), (1.8, 66.0), (2.6, 68.0), (1.2, 70.0), (2.0, 73.0), (0.0, 75.0)], Matrix.Identity(4), sides=20)
    tube(brass, [(-11.0, 0.0, 66.0), (11.0, 0.0, 66.0)], 0.6, sides=8)
    for k, x in enumerate((-9.5, -3.2, 3.2, 9.5)):
        # Each hangs from a ring on the bar, its head down by the base.
        tube(brass, [(x, 0.0, 65.0), (x, 0.4, 62.0)], 0.5, sides=6)
        lathe(brass, [(0.0, 52.0), (1.2, 52.0), (1.6, 55.0), (1.2, 58.0), (0.0, 60.0)], at(x, 0.6, 0.0), sides=10)
        tube(iron, [(x, 0.6, 52.0), (x, 1.2, 10.0)], 0.55, sides=8)
        if k == 0:
            # The poker's hook.
            tube(iron, [(x, 1.2, 10.0), (x, 1.4, 6.0), (x + 2.6, 1.4, 4.0)], 0.5, sides=6)
        elif k == 1:
            cbox(iron, x, 1.4, 6.0, 9.0, 0.4, 10.0)
        elif k == 2:
            tube(iron, [(x - 1.2, 1.2, 10.0), (x - 1.6, 1.2, 4.0)], 0.4, sides=6)
            tube(iron, [(x + 1.2, 1.2, 10.0), (x + 1.6, 1.2, 4.0)], 0.4, sides=6)
        else:
            lathe(bristle, [(0.0, 3.0), (3.0, 3.4), (3.6, 7.0), (2.4, 10.0), (0.0, 10.4)], at(x, 1.2, 0.0) @ Matrix.Diagonal((1.0, 0.5, 1.0, 1.0)), sides=12)
    return [brass, iron, bristle]


# ---------------------------------------------------------------------------------------------
# The desk and what is on it.
# ---------------------------------------------------------------------------------------------

def desk():
    """A partners' desk in mahogany, 180 x 90: two pedestals of three drawers on plinths, a
    kneehole with a drawer over it, a top with a moulded edge and a tooled leather inset. The sitter
    is at -Y; the visitor's side (+Y) is a panelled front. The +X pedestal's top drawer (the
    sitter's left hand) is library_desk_drawer, pulled out: its opening here is a dark hole."""
    hw, hd = DESK_W * 0.5, DESK_D * 0.5
    ped = 46.0
    wood = Part("Mahogany", bevel=0.5)
    for sx in (-1, 1):
        cx = sx * (hw - ped * 0.5)
        cbox(wood, cx, 0.0, 5.0, ped + 2.0, DESK_D - 2.0, 10.0)
        cbox(wood, cx, 0.0, (10.0 + DESK_TOP - 3.0) * 0.5, ped, DESK_D - 6.0, DESK_TOP - 13.0)
    # The kneehole's back, the visitor's front panel, and the drawer rail.
    cbox(wood, 0.0, hd - 4.0, (10.0 + DESK_TOP - 3.0) * 0.5, DESK_W - 2 * ped, 2.0, DESK_TOP - 13.0)
    cbox(wood, 0.0, 0.0, DESK_TOP - 9.0, DESK_W - 2 * ped, DESK_D - 6.0, 12.0)
    top = Part("Mahogany", bevel=1.2, segments=3)
    cbox(top, 0.0, 0.0, DESK_TOP - 1.75, DESK_W + 4.0, DESK_D + 4.0, 3.5)
    inset = Part("LeatherGreen")
    cbox(inset, 0.0, 0.0, DESK_TOP + 0.05, DESK_W - 24.0, DESK_D - 24.0, 0.2)
    gilt = Part("Gilt")
    for sy in (-1, 1):
        cbox(gilt, 0.0, sy * (DESK_D * 0.5 - 13.5), DESK_TOP + 0.1, DESK_W - 26.0, 0.5, 0.15)
    for sx in (-1, 1):
        cbox(gilt, sx * (DESK_W * 0.5 - 13.5), 0.0, DESK_TOP + 0.1, 0.5, DESK_D - 26.0, 0.15)
    fronts = Part("Mahogany", bevel=0.6)
    brass = Part("Brass")
    hole = Part("Shadow")
    heights = [(13.0, 33.0), (34.0, 52.0), (53.0, DESK_TOP - 5.0)]
    for sx in (-1, 1):
        cx = sx * (hw - ped * 0.5)
        for k, (z0, z1) in enumerate(heights):
            if sx > 0 and k == 2:
                cbox(hole, cx, -hd + 3.5, (z0 + z1) * 0.5, ped - 4.0, 1.0, z1 - z0)
                continue
            cbox(fronts, cx, -hd + 2.0, (z0 + z1) * 0.5, ped - 4.0, 2.0, z1 - z0 - 1.0)
            # A brass bail handle and its two back-plates; a keyhole above it.
            for bx in (-6.0, 6.0):
                ellipsoid(brass, (cx + bx, -hd + 0.8, (z0 + z1) * 0.5 + 1.0), (1.1, 0.4, 1.4), rings=6, sides=10)
            tube(brass, [(cx - 6.0, -hd + 0.6, (z0 + z1) * 0.5 + 0.6), (cx - 5.0, -hd - 0.8, (z0 + z1) * 0.5 - 2.0), (cx + 5.0, -hd - 0.8, (z0 + z1) * 0.5 - 2.0), (cx + 6.0, -hd + 0.6, (z0 + z1) * 0.5 + 0.6)], 0.32, sides=6)
            cbox(brass, cx, -hd + 0.9, z1 - 4.5, 1.2, 0.3, 2.2)
    cbox(fronts, 0.0, -hd + 2.0, DESK_TOP - 9.0, DESK_W - 2 * ped - 4.0, 2.0, 10.0)
    cbox(brass, 0.0, -hd + 0.9, DESK_TOP - 7.0, 1.2, 0.3, 2.2)
    # The visitor's side: raised panels on the pedestals' ends and the kneehole's back.
    panels = Part("Mahogany", bevel=1.4, segments=2, angle=30.0)
    for cx, w in ((-(hw - ped * 0.5), ped - 12.0), (hw - ped * 0.5, ped - 12.0), (0.0, DESK_W - 2 * ped - 14.0)):
        cbox(panels, cx, hd - 1.6, (14.0 + DESK_TOP - 10.0) * 0.5, w, 1.4, DESK_TOP - 30.0)
    return [wood, top, inset, gilt, fronts, brass, hole, panels]


def desk_drawer():
    """The drawer that was pulled out and left: a tray 42 wide, 70 deep, 16 high, its front with the
    brass bail; empty here (the papers in it are planes in C++). Origin at the middle of the bottom
    of the front, the tray running back along -Y... front faces +Y."""
    wood = Part("Mahogany", bevel=0.3)
    w, d, h = 42.0, 66.0, 15.0
    cbox(wood, 0.0, 1.0, h * 0.5 + 0.5, w + 2.0, 2.0, h + 1.0)
    inner = Part("Oak")
    for sx in (-1, 1):
        cbox(inner, sx * (w * 0.5 - 0.6), -d * 0.5, h * 0.5, 1.2, d, h)
    cbox(inner, 0.0, -d + 0.6, h * 0.5, w, 1.2, h)
    cbox(inner, 0.0, -d * 0.5, 0.4, w, d, 0.8)
    brass = Part("Brass")
    for bx in (-6.0, 6.0):
        ellipsoid(brass, (bx, 2.4, h * 0.5 + 1.0), (1.1, 0.4, 1.4), rings=6, sides=10)
    tube(brass, [(-6.0, 2.6, h * 0.5 + 0.6), (-5.0, 3.8, h * 0.5 - 2.0), (5.0, 3.8, h * 0.5 - 2.0), (6.0, 2.6, h * 0.5 + 0.6)], 0.32, sides=6)
    return [wood, inner, brass]


def chair_shell(part, front_fn, back_fn, nu, nv):
    """A closed upholstered shell between two surfaces over the unit square: front_fn(s, t) the face
    the sitter leans on (tufted), back_fn(s, t) the plain leather behind it. The edges are closed
    round, so the bake's normals have an inside to point away from."""
    bm = part.bm
    front = [[bm.verts.new(front_fn(i / nu, j / nv)) for j in range(nv + 1)] for i in range(nu + 1)]
    back = [[bm.verts.new(back_fn(i / nu, j / nv)) for j in range(nv + 1)] for i in range(nu + 1)]
    for i in range(nu):
        for j in range(nv):
            bm.faces.new((front[i][j], front[i + 1][j], front[i + 1][j + 1], front[i][j + 1]))
            bm.faces.new((back[i][j + 1], back[i + 1][j + 1], back[i + 1][j], back[i][j]))
    for i in range(nu):
        for j in (0, nv):
            bm.faces.new((front[i][j], back[i][j], back[i + 1][j], front[i + 1][j]))
    for j in range(nv):
        for i in (0, nu):
            bm.faces.new((front[i][j], front[i][j + 1], back[i][j + 1], back[i][j]))


def desk_chair():
    """His desk chair, made for the desk: a tall button-tufted leather tub on a swivel, the back
    curving round into rolled arms that end in carved mahogany knuckles, a deep buttoned seat, and
    four splayed mahogany legs on iron castors. About 68 wide (over the arms' knuckles), 67 deep, 121
    to the top of the back's roll. Faces +Y (the sitter's back to -Y)."""
    wood = Part("Mahogany", bevel=0.4)
    iron = Part("Iron")
    seat = Part("Leather", bevel=3.5, segments=4, angle=30.0)
    shell = Part("Leather", smooth=True)
    rolls = Part("Leather", smooth=True)
    buttons = Part("Leather")

    seat_z = 49.0
    r_in, r_out = 23.0, 30.0
    spread = 72.0

    def top(phi):
        # Highest behind the sitter's head, coming down round the sides into the arms.
        return 117.0 - 47.0 * (phi / spread) ** 2

    def lean(z):
        # The back leans back about seven degrees.
        return -(z - seat_z) * 0.12

    def point(phi, z, r):
        a = math.radians(-90.0 + phi)
        return Vector((math.cos(a) * r, math.sin(a) * r * 0.95 + 2.0 + lean(z), z))

    # The buttons on a diamond grid; the leather between them is stuffed out towards the sitter and
    # pulled back in at every button, which is what makes a back read as buttoned rather than quilted.
    tufts = []
    for row, z in enumerate((62.0, 74.0, 86.0, 98.0, 109.0)):
        offset = 0.0 if row % 2 == 0 else 10.0
        for k in range(-4, 5):
            phi = k * 20.0 + offset
            if abs(phi) <= 62.0 and z < top(phi) - 7.0:
                tufts.append((phi, z))
    per_degree = r_in * math.pi / 180.0

    def stuffing(phi, z):
        d2 = min(((phi - bp) * per_degree) ** 2 + (z - bz) ** 2 for bp, bz in tufts)
        # Faded to nothing at the piped edges.
        s = (phi + spread) / (2.0 * spread)
        t = (z - seat_z) / max(top(phi) - seat_z, 1.0)
        edge = min(1.0, min(s, 1.0 - s) * 9.0, min(t, 1.0 - t) * 7.0)
        return 2.6 * (1.0 - math.exp(-d2 / 16.0)) * max(edge, 0.0)

    def surface(s, t, front):
        phi = -spread + 2.0 * spread * s
        z = seat_z + t * (top(phi) - seat_z)
        r = r_in - stuffing(phi, z) if front else r_out
        return point(phi, z, r)

    chair_shell(shell, lambda s, t: surface(s, t, True), lambda s, t: surface(s, t, False), 48, 30)
    for phi, z in tufts:
        p = point(phi, z, r_in - 0.2)
        ellipsoid(buttons, tuple(p), (0.95, 0.95, 0.95), rings=6, sides=10)

    # The roll along the top of the back, running on round into the arms.
    r_mid = (r_in + r_out) * 0.5
    roll = [tuple(point(-spread + 2.0 * spread * k / 36, top(-spread + 2.0 * spread * k / 36), r_mid)) for k in range(37)]
    tube(rolls, roll, 4.4, sides=16)
    for sx in (-1, 1):
        end = point(sx * spread, top(spread), r_mid)
        arm = [tuple(end), (sx * 27.5, 2.0, 68.5), (sx * 28.5, 16.0, 67.5), (sx * 28.5, 25.0, 66.5)]
        tube(rolls, K.catmull(arm, 6), 5.0, sides=16)
        # Under the roll, the arm's side panel down to the seat rail.
        cbox(seat, sx * 28.5, 4.0, 54.0, 6.5, 40.0, 24.0)
        # The carved knuckle at the arm's end and the post under it.
        ellipsoid(wood, (sx * 28.5, 28.0, 64.5), (5.4, 3.4, 6.4), rings=10, sides=16)
        tube(wood, [(sx * 28.5, 26.5, 41.0), (sx * 28.5, 27.5, 52.0), (sx * 28.5, 27.0, 60.0)], [2.6, 2.2, 2.6], sides=12)
    # The seat: a deep cushion, buttoned, on a moulded mahogany rail.
    cbox(seat, 0.0, 3.0, 46.5, 52.0, 50.0, 8.0)
    for bx in (-12.0, 0.0, 12.0):
        for by in (-6.0, 12.0):
            ellipsoid(buttons, (bx + (6.0 if by > 0 else 0.0), by, 50.4), (0.9, 0.9, 0.6), rings=6, sides=10)
    cbox(wood, 0.0, 3.0, 40.5, 58.0, 54.0, 5.0)
    cbox(wood, 0.0, 27.6, 38.0, 54.0, 2.0, 3.0)

    # The swivel and the four splayed legs on castors.
    lathe(iron, [(0.0, 12.0), (3.4, 12.0), (3.0, 16.0), (2.4, 36.0), (7.0, 38.0), (0.0, 38.0)], Matrix.Identity(4), sides=16)
    for k in range(4):
        a = math.radians(45.0 + 90.0 * k)
        c, s = math.cos(a), math.sin(a)
        tube(wood, [(c * 2.0, s * 2.0, 17.0), (c * 15.0, s * 15.0, 11.0), (c * 31.0, s * 31.0, 6.5)], [3.0, 2.4, 1.8], sides=12)
        lathe(iron, [(0.0, 0.0), (2.6, 0.4), (2.8, 2.6), (1.2, 4.6), (0.0, 6.5)], at(c * 31.0, s * 31.0, 0.0), sides=10)
    return [wood, iron, seat, shell, rolls, buttons]


def bankers_lamp():
    """The green banker's lamp: a brass base on four little feet, a stem rising from its back, a
    harp holding the cased-glass shade out over the blotter, a pull chain. Off. Front +Y."""
    brass = Part("Brass")
    lathe(brass, [(0.0, 0.0), (9.5, 0.0), (10.0, 1.0), (9.0, 2.2), (7.0, 3.0), (2.0, 3.6), (0.0, 3.6)], Matrix.Diagonal((1.3, 1.0, 1.0, 1.0)), sides=32)
    tube(brass, [(0.0, -5.0, 3.0), (0.0, -5.0, 30.0), (0.0, -3.0, 36.0), (0.0, 2.0, 37.0)], 0.9, sides=10)
    for sx in (-1, 1):
        tube(brass, [(0.0, 2.0, 37.0), (sx * 10.0, 3.5, 36.0), (sx * 12.5, 4.0, 34.0)], 0.4, sides=6)
    # The shade: a long trough of green cased glass, white inside, 26 long. Each layer a closed
    # shell (a section swept along X with its ends capped), so the bake's normals face out.
    L, R = 26.0, 8.0
    steps = 20

    def trough(part, r_out, r_in, x0, x1):
        angles = [math.radians(-10.0 + 200.0 * k / steps) for k in range(steps + 1)]
        section = [(4.0 + r_out * math.cos(a), 30.0 + r_out * math.sin(a)) for a in angles]
        section += [(4.0 + r_in * math.cos(a), 30.0 + r_in * math.sin(a)) for a in reversed(angles)]
        bm = part.bm
        near = [bm.verts.new((x0, y, z)) for y, z in section]
        far = [bm.verts.new((x1, y, z)) for y, z in section]
        n = len(section)
        for k in range(n):
            j = (k + 1) % n
            bm.faces.new((near[k], near[j], far[j], far[k]))
        # The end caps, one quad per step of the section's strip.
        for k in range(steps):
            a, b, c, d = k, k + 1, n - 2 - k, n - 1 - k
            bm.faces.new((near[a], near[b], near[c], near[d]))
            bm.faces.new((far[d], far[c], far[b], far[a]))

    glass = Part("Shade")
    trough(glass, R, R - 0.3, -L * 0.5, L * 0.5)
    inside = Part("ShadeInside")
    trough(inside, R - 0.32, R - 0.5, -L * 0.5 + 0.3, L * 0.5 - 0.3)
    brass2 = Part("Brass")
    lathe(brass2, [(0.0, 0.0), (1.2, 0.0), (1.0, 2.0), (0.0, 2.4)], at(0.0, 4.0, 38.2), sides=10)
    tube(brass2, [(3.0, 2.0, 26.0), (3.0, 2.0, 16.0)], 0.12, sides=4)
    ellipsoid(brass2, (3.0, 2.0, 15.4), (0.6, 0.6, 0.9), rings=6, sides=8)
    return [brass, glass, inside, brass2]


def ink_bottle():
    """A square-shouldered ink bottle with a dried black tide inside and its cap off beside it."""
    glass = Part("Glass")
    lathe(glass, [(0.0, 0.0), (3.2, 0.0), (3.4, 0.4), (3.4, 4.6), (2.4, 5.6), (1.3, 6.0), (1.3, 7.2), (1.05, 7.2), (1.05, 6.2), (0.0, 6.0)], Matrix.Identity(4), sides=8)
    ink = Part("Ink")
    lathe(ink, [(0.0, 0.3), (3.0, 0.3), (3.0, 1.5), (0.0, 1.5)], Matrix.Identity(4), sides=8)
    cap = Part("Ebony")
    lathe(cap, [(0.0, 0.0), (1.5, 0.0), (1.6, 2.2), (0.0, 2.3)], at(6.0, 1.0, 0.0, roll=88.0), sides=14)
    return [glass, ink, cap]


def fountain_pen():
    """A black fountain pen with gold bands, cap posted on the end, lying along X."""
    body = Part("Ebony")
    lathe(body, [(0.0, -7.0), (0.55, -6.6), (0.62, -2.0), (0.6, 4.0), (0.5, 6.0), (0.0, 6.2)], at(0.0, 0.0, 0.62, pitch=90.0), sides=14)
    gilt = Part("Gilt")
    lathe(gilt, [(0.63, -2.2), (0.66, -2.2), (0.66, -1.6), (0.63, -1.6)], at(0.0, 0.0, 0.62, pitch=90.0), sides=14)
    lathe(gilt, [(0.0, 6.2), (0.35, 6.3), (0.08, 8.2), (0.0, 8.3)], at(0.0, 0.0, 0.62, pitch=90.0) @ Matrix.Diagonal((1.0, 0.4, 1.0, 1.0)), sides=10)
    cbox(gilt, -4.0, 0.62, 1.2, 4.0, 0.2, 0.3)
    return [body, gilt]


def magnifier():
    """A reading glass: a brass rim round a lens, a turned horn handle. Lying flat."""
    brass = Part("Brass")
    ring = [(math.cos(a) * 5.0, math.sin(a) * 5.0, 1.0) for a in [2 * math.pi * k / 40 for k in range(41)]]
    tube(brass, ring, 0.45, sides=8)
    lens = Part("Glass")
    lathe(lens, [(0.0, 0.7), (4.7, 0.85), (4.7, 1.15), (0.0, 1.3)], Matrix.Identity(4), sides=32)
    handle = Part("Ebony")
    lathe(handle, [(0.0, 0.0), (0.5, 0.0), (0.8, 3.0), (0.9, 8.0), (0.7, 11.0), (0.0, 11.4)], at(5.2, 0.0, 1.0, pitch=90.0), sides=12)
    return [brass, lens, handle]


def reading_glasses():
    """Round wire spectacles, folded, lying on their arms."""
    wire = Part("Gilt")
    lens = Part("Glass")
    for sx in (-1, 1):
        cx = sx * 3.4
        tube(wire, [(cx + math.cos(a) * 2.2, 0.0, 2.6 + math.sin(a) * 2.2) for a in [2 * math.pi * k / 24 for k in range(25)]], 0.08, sides=5)
        lathe(lens, [(0.0, -0.05), (2.15, -0.04), (2.15, 0.04), (0.0, 0.05)], at(cx, 0.0, 2.6, roll=90.0), sides=20)
        # The arms, folded back across behind the lenses.
        tube(wire, [(cx + sx * 2.2, 0.0, 2.8), (cx + sx * 2.4, -0.6, 2.6), (-cx * 0.2, -1.4, 1.2), (-cx * 0.8, -1.6, 0.5)], 0.07, sides=5)
    tube(wire, [(-1.2, 0.0, 3.0), (0.0, 0.0, 3.6), (1.2, 0.0, 3.0)], 0.08, sides=5)
    return [wire, lens]


def blotter():
    """The leather writing pad: a board with leather corners and a sheet of blotting paper under
    them (the paper is the atlas's blotter, with its own UVs). Origin at the middle of its bottom."""
    w, d = 56.0, 40.0
    board = Part("Oak")
    cbox(board, 0.0, 0.0, 0.3, w, d, 0.6)
    paper = Part("Paper")
    paper.uv_fn = no_uvs
    x, y, pw, ph = 1024.0, 1536.0, 1024.0, 512.0
    uv_box(paper, -w * 0.5 + 0.3, w * 0.5 - 0.3, -d * 0.5 + 0.3, d * 0.5 - 0.3, 0.6, 0.7,
           {f: (x, y, x + pw, y + ph) for f in ("py", "ny", "px", "nx", "nz")} | {"pz": (x, y, x + pw, y + ph)})
    leather = Part("LeatherGreen", bevel=0.2)
    for sx in (-1, 1):
        for sy in (-1, 1):
            cx, cy = sx * (w * 0.5 - 4.0), sy * (d * 0.5 - 4.0)
            prism(leather, [(sx * w * 0.5, sy * d * 0.5), (sx * (w * 0.5 - 11.0), sy * d * 0.5), (sx * w * 0.5, sy * (d * 0.5 - 11.0))][::sx * sy], 0.6, 0.95)
    return [board, paper, leather]


def seal_set():
    """A brass seal on a turned handle, lying, and a stick of red sealing wax with its end melted."""
    handle = Part("Ebony")
    lathe(handle, [(0.0, 0.0), (0.8, 0.0), (1.0, 2.0), (0.7, 4.0), (1.2, 6.5), (0.9, 8.0), (0.0, 8.4)], at(0.0, 0.0, 1.2, pitch=90.0), sides=12)
    brass = Part("Brass")
    lathe(brass, [(0.0, -1.5), (1.2, -1.5), (1.2, -0.6), (0.8, 0.0), (0.0, 0.0)], at(0.0, 0.0, 1.2, pitch=90.0), sides=16)
    wax = Part("Wax", bevel=0.15)
    cbox(wax, 0, 0, 0, 10.0, 1.2, 1.2, matrix=at(1.0, 4.0, 0.6, yaw=-14.0))
    ellipsoid(wax, (-4.0, 5.3, 0.65), (0.9, 0.8, 0.6), rings=6, sides=10)
    # A spent seal on the desk: a blob of red pressed flat.
    lathe(wax, [(0.0, 0.0), (1.6, 0.0), (1.7, 0.2), (1.4, 0.35), (0.0, 0.4)], at(8.0, -3.0, 0.0), sides=16)
    return [handle, brass, wax]


def laptop():
    """A notebook computer, closed, dark grey: the one modern thing in the room. 34 x 24 x 2."""
    case = Part("Aluminium", bevel=0.6, segments=3)
    cbox(case, 0.0, 0.0, 0.85, 34.0, 24.0, 1.4)
    lid = Part("Aluminium", bevel=0.5, segments=3)
    cbox(lid, 0.0, 0.0, 1.85, 33.8, 23.8, 0.6)
    seam = Part("Shadow")
    cbox(seam, 0.0, 0.0, 1.55, 33.4, 23.4, 0.08)
    feet = Part("Ebony")
    for sx in (-1, 1):
        for sy in (-1, 1):
            cbox(feet, sx * 14.0, sy * 9.0, 0.08, 3.0, 1.0, 0.16)
    return [case, lid, seam, feet]


def small_key():
    """A small brass key, the kind that opens a drawer or a box. Lying flat; origin at its middle."""
    brass = Part("Brass")
    ring = [(-2.4 + math.cos(a) * 0.9, math.sin(a) * 0.9, 0.18) for a in [2 * math.pi * k / 20 for k in range(21)]]
    tube(brass, ring, 0.22, sides=6)
    tube(brass, [(-1.4, 0.0, 0.18), (2.6, 0.0, 0.18)], 0.18, sides=8)
    cbox(brass, 2.0, 0.45, 0.18, 0.5, 0.7, 0.3)
    cbox(brass, 2.6, 0.38, 0.18, 0.4, 0.55, 0.3)
    return [brass]


def mug():
    """A plain glazed mug with a dried ring of coffee in it. Origin on its base."""
    ceramic = Part("Ceramic")
    lathe(ceramic, [(0.0, 0.0), (3.6, 0.0), (3.9, 0.4), (4.0, 9.0), (4.1, 9.4), (3.7, 9.4), (3.6, 1.0), (0.0, 0.9)], Matrix.Identity(4), sides=28)
    tube(ceramic, [(3.9, 0.0, 7.6), (6.2, 0.0, 7.4), (6.8, 0.0, 5.0), (6.0, 0.0, 2.8), (3.9, 0.0, 2.4)], 0.55, sides=10)
    stain = Part("Charcoal")
    lathe(stain, [(0.0, 1.0), (3.55, 1.0), (3.55, 1.35), (0.0, 1.35)], Matrix.Identity(4), sides=28)
    return [ceramic, stain]


def open_book():
    """A big book lying open: two boards, the blocks of leaves either side of the spine, the open
    pages curving down into the gutter (the atlas's printed pages), a ribbon. 52 x 36 open."""
    leather = Part("Leather", bevel=0.2)
    pw, ph = 25.0, 34.0
    for sx in (-1, 1):
        cbox(leather, sx * (pw * 0.5 + 0.6), 0.0, 0.2, pw + 1.4, ph + 1.2, 0.4)
    leaves = Part("Card")
    pages = Part("Paper")
    pages.uv_fn = no_uvs
    steps = 14
    block = 2.4
    # Read from -Y, the reader's left is +X: the left-hand page lies on the +X side.
    for side, key in ((1, "book_l"), (-1, "book_r")):
        # The block of leaves under the open page.
        cbox(leaves, side * pw * 0.5, 0.0, 0.4 + block * 0.5, pw - 0.4, ph - 0.6, block)
        rows, uvs = [], []
        for i in range(steps + 1):
            t = i / steps
            # From the gutter (t = 0) out to the fore-edge: down into the gutter, up over the
            # shoulder, and gently down to the edge.
            x = side * (0.2 + t * (pw - 0.4))
            z = 0.4 + block + 1.2 * math.sin(min(t * 3.0, 1.0) * math.pi * 0.5) - 0.8 * t - (1.4 if i == 0 else 0.0) + 0.08
            rows.append(((x, -(ph - 0.8) * 0.5, z), (x, (ph - 0.8) * 0.5, z)))
            # The gutter is the left page's right edge and the right page's left edge.
            s = 1.0 - t if side > 0 else t
            # The reader is at -Y: the top of the page is at +Y.
            uvs.append((atlas_uv(PAPER[key], s, 1.0), atlas_uv(PAPER[key], s, 0.0)))
        if side < 0:
            rows = [(b, a) for a, b in rows]
            uvs = [(b, a) for a, b in uvs]
        sheet_strip(pages, rows, uvs, thickness=0.08)
    ribbon = Part("Velvet")
    tube(ribbon, [(1.0, 4.0, 3.9), (1.6, 10.0, 3.6), (2.4, ph * 0.5, 0.6), (3.2, ph * 0.5 + 4.0, 0.3)], 0.25, sides=4)
    return [leather, leaves, pages, ribbon]


def notebook_open():
    """His notebook, open: black cloth boards, two pages of his hand (the atlas), an elastic strap
    hanging off the right board. 30 x 21 open."""
    cloth = Part("Ebony", bevel=0.15)
    pw, ph = 14.6, 21.0
    for sx in (-1, 1):
        cbox(cloth, sx * (pw * 0.5 + 0.3), 0.0, 0.15, pw + 0.6, ph + 0.4, 0.3)
    pages = Part("Paper")
    pages.uv_fn = no_uvs
    edge = (PAGES[0], PAGES[1], PAGES[0] + 40.0, PAGES[1] + 40.0)
    # Read from -Y: the left-hand page is on the +X side.
    for side, key in ((1, "notebook_l"), (-1, "notebook_r")):
        x0, y0, w0, h0 = PAPER[key]
        lo, hi = sorted((side * 0.15, side * (pw - 0.1)))
        # The block of pages, its top the page read from -Y (top edge at +Y).
        uv_box(pages, lo, hi, -ph * 0.5 + 0.3, ph * 0.5 - 0.3, 0.3, 1.1,
               {"pz": (x0, y0, x0 + w0, y0 + h0), "nz": edge, "py": edge, "ny": edge, "px": edge, "nx": edge}, page_top=True)
    strap = Part("Ebony")
    tube(strap, [(pw + 0.6, -6.0, 0.35), (pw + 1.4, -6.0, 0.1), (pw + 2.2, -5.0, 0.05)], 0.15, sides=4)
    return [cloth, pages, strap]


def letter_folded():
    """A letter in three folds, opened out and not quite flat: each panel tilted on its crease.
    The atlas's letter on its own UVs. 20 x 27 opened; origin at its middle."""
    paper = Part("Paper")
    paper.uv_fn = no_uvs
    W, H = 20.0, 27.0
    # Stations up the sheet from its foot (-Y, nearest the reader) to its head, lifted on the creases.
    lift = [(0.0, 0.0), (1.0 / 3, 1.4), (2.0 / 3, 0.25), (1.0, 1.8)]
    rows, uvs = [], []
    for f, z in lift:
        y = -H * 0.5 + H * f
        # Read from -Y, the reader's left is +X.
        rows.append(((W * 0.5, y, z + 0.06), (-W * 0.5, y, z + 0.06)))
        uvs.append((atlas_uv(PAPER["letter"], 0.0, 1.0 - f), atlas_uv(PAPER["letter"], 1.0, 1.0 - f)))
    sheet_strip(paper, rows, uvs, thickness=0.05)
    return [paper]


def newspaper_folded():
    """The newspaper folded in half, lying with the masthead up, its fold a soft roll. 38 x 28."""
    paper = Part("Paper")
    paper.uv_fn = no_uvs
    W, H = 38.0, 28.0
    # The upper half of the front page shows, the masthead at the far (+Y) edge, which is the fold;
    # a few millimetres of the paper's thickness under it.
    rows, uvs = [], []
    for f in (0.0, 0.5, 1.0):
        y = -H * 0.5 + H * f
        z = 0.7 + 0.25 * math.sin(f * math.pi)
        rows.append(((W * 0.5, y, z), (-W * 0.5, y, z)))
        uvs.append((atlas_uv(PAPER["newspaper"], 0.0, 0.5 - 0.5 * f), atlas_uv(PAPER["newspaper"], 1.0, 0.5 - 0.5 * f)))
    sheet_strip(paper, rows, uvs, thickness=0.6)
    roll = Part("Card")
    tube(roll, [(-W * 0.5, H * 0.5, 0.35), (W * 0.5, H * 0.5, 0.35)], 0.36, sides=8)
    return [paper, roll]


# ---------------------------------------------------------------------------------------------
# The reading corner.
# ---------------------------------------------------------------------------------------------

def side_table():
    """A small round table in mahogany, 50 across, a lower shelf, three turned legs."""
    wood = Part("Mahogany", bevel=0.4)
    lathe(wood, [(0.0, 64.0), (24.5, 64.0), (25.2, 65.0), (25.2, 66.6), (24.4, 67.4), (0.0, 67.4)], Matrix.Identity(4), sides=40)
    lathe(wood, [(0.0, 24.0), (19.0, 24.0), (19.0, 26.0), (0.0, 26.0)], Matrix.Identity(4), sides=32)
    for k in range(3):
        a = 2 * math.pi * k / 3 + math.pi / 2
        c, s = math.cos(a), math.sin(a)
        lathe(wood, [(0.0, 0.0), (1.6, 0.0), (1.8, 4.0), (1.4, 14.0), (2.2, 24.0), (1.4, 32.0), (1.8, 48.0), (1.6, 64.0), (0.0, 64.0)],
              at(c * 18.0, s * 18.0, 0.0), sides=12)
    return [wood]


def floor_lamp():
    """A standard lamp: a brass column on a tripod foot, a pleated silk shade gone brown, a finial.
    It does not work. 165 tall."""
    brass = Part("Brass")
    lathe(brass, [(0.0, 3.0), (6.0, 3.0), (6.4, 5.0), (4.0, 7.0), (1.6, 10.0), (1.2, 40.0), (2.0, 42.0), (1.2, 44.0), (1.0, 130.0),
                  (1.8, 132.0), (1.0, 134.0), (1.0, 142.0), (0.0, 142.0)], Matrix.Identity(4), sides=16)
    for k in range(3):
        a = 2 * math.pi * k / 3
        c, s = math.cos(a), math.sin(a)
        tube(brass, [(c * 3.0, s * 3.0, 6.0), (c * 12.0, s * 12.0, 3.0), (c * 20.0, s * 20.0, 0.8)], [1.2, 1.0, 0.8], sides=8)
        ellipsoid(brass, (c * 20.0, s * 20.0, 0.8), (1.4, 1.4, 0.8), rings=6, sides=8)
    for k in range(4):
        a = 2 * math.pi * k / 4
        tube(brass, [(0.0, 0.0, 141.0), (math.cos(a) * 9.0, math.sin(a) * 9.0, 158.0)], 0.2, sides=4)
    shade = Part("Fabric")
    # Pleated: the profile's radius wanders round, done as a closed shell with a thickness.
    sides = 48
    bm = shade.bm
    rings = []
    for z, r in ((134.0, 26.0), (163.0, 12.0)):
        for rr in (r, r - 0.6):
            rings.append([bm.verts.new((math.cos(2 * math.pi * k / sides) * rr * (1.0 + 0.025 * (k % 2)),
                                        math.sin(2 * math.pi * k / sides) * rr * (1.0 + 0.025 * (k % 2)), z)) for k in range(sides)])
    out_lo, in_lo, out_hi, in_hi = rings
    for k in range(sides):
        j = (k + 1) % sides
        bm.faces.new((out_lo[k], out_lo[j], out_hi[j], out_hi[k]))
        bm.faces.new((in_lo[j], in_lo[k], in_hi[k], in_hi[j]))
        bm.faces.new((out_lo[j], out_lo[k], in_lo[k], in_lo[j]))
        bm.faces.new((out_hi[k], out_hi[j], in_hi[j], in_hi[k]))
    finial = Part("Brass")
    lathe(finial, [(0.0, 163.0), (1.4, 163.0), (2.0, 165.0), (0.0, 168.0)], Matrix.Identity(4), sides=12)
    return [brass, shade, finial]


# ---------------------------------------------------------------------------------------------
# Decoration.
# ---------------------------------------------------------------------------------------------

def globe():
    """A library globe on its stand: three cabriole legs on a stretcher, a horizon ring with its
    paper band, a brass meridian holding the globe on its axis at twenty-three and a half degrees.
    The gores are the baked library_globe (equirectangular, the top half of the sheet)."""
    R = 25.0
    cz = 74.0
    tilt = math.radians(23.44)
    sphere = Part("Globe")

    def globe_uvs(bm):
        uvl = bm.loops.layers.uv.verify()
        for face in bm.faces:
            us = []
            for loop in face.loops:
                p = loop.vert.co - Vector((0.0, 0.0, cz))
                # Back into the globe's own frame (undo the tilt about X).
                y = p.y * math.cos(tilt) - p.z * math.sin(tilt)
                z = p.y * math.sin(tilt) + p.z * math.cos(tilt)
                x = p.x
                lon = math.atan2(y, x)
                lat = math.asin(max(-1.0, min(1.0, z / R)))
                us.append(((lon + math.pi) / (2 * math.pi), (math.pi * 0.5 - lat) / math.pi))
            # Keep a face that straddles the seam on one side of it.
            if max(u for u, _ in us) - min(u for u, _ in us) > 0.5:
                us = [(u + 1.0 if u < 0.5 else u, v) for u, v in us]
            for loop, (u, v) in zip(face.loops, us):
                loop[uvl].uv = (u, 1.0 - v * 0.5)
    sphere.uv_fn = globe_uvs
    rows, cols = 32, 64
    bm = sphere.bm
    m = Matrix.Translation((0.0, 0.0, cz)) @ Matrix.Rotation(tilt, 4, "X")
    south = bm.verts.new(m @ Vector((0.0, 0.0, -R)))
    north = bm.verts.new(m @ Vector((0.0, 0.0, R)))
    verts = []
    for i in range(1, rows):
        lat = -math.pi * 0.5 + math.pi * i / rows
        verts.append([bm.verts.new(m @ Vector((R * math.cos(lat) * math.cos(2 * math.pi * k / cols),
                                                 R * math.cos(lat) * math.sin(2 * math.pi * k / cols), R * math.sin(lat)))) for k in range(cols)])
    for k in range(cols):
        j = (k + 1) % cols
        bm.faces.new((south, verts[0][j], verts[0][k]))
        bm.faces.new((north, verts[-1][k], verts[-1][j]))
    for i in range(rows - 2):
        for k in range(cols):
            j = (k + 1) % cols
            bm.faces.new((verts[i][k], verts[i][j], verts[i + 1][j], verts[i + 1][k]))
    brass = Part("Brass")
    meridian = [tuple(m @ Vector((0.0, math.cos(a) * (R + 1.6), math.sin(a) * (R + 1.6)))) for a in [2 * math.pi * k / 64 for k in range(65)]]
    tube(brass, meridian, 0.6, sides=8)
    lathe(brass, [(0.0, 0.0), (0.8, 0.0), (0.6, 2.0), (0.0, 2.4)], m @ Matrix.Translation((0.0, 0.0, R + 1.2)), sides=8)
    wood = Part("Oak", bevel=0.3)
    # The horizon: a broad flat ring at the globe's equator, standing on the legs.
    hr = []
    bm = wood.bm
    for k in range(64):
        a = 2 * math.pi * k / 64
        hr.append([bm.verts.new((math.cos(a) * r, math.sin(a) * r, z)) for r, z in ((R + 3.0, cz - 1.0), (R + 9.0, cz - 1.0), (R + 9.0, cz + 0.8), (R + 3.0, cz + 0.8))])
    for k in range(64):
        j = (k + 1) % 64
        for e in range(4):
            f = (e + 1) % 4
            bm.faces.new((hr[k][e], hr[j][e], hr[j][f], hr[k][f]))
    # The horizon's paper band, with its degrees and the signs of the zodiac, gone the colour of tea.
    band = Part("Card")
    lathe(band, [(R + 3.6, cz + 0.8), (R + 8.4, cz + 0.8), (R + 8.4, cz + 0.95), (R + 3.6, cz + 0.95)], Matrix.Identity(4), sides=64)
    for k in range(3):
        a = 2 * math.pi * k / 3 + math.pi / 2
        c, s = math.cos(a), math.sin(a)
        r = R + 6.0
        leg = [(c * r, s * r, cz - 1.0), (c * (r + 2.0), s * (r + 2.0), cz - 14.0), (c * (r - 2.0), s * (r - 2.0), 40.0),
               (c * (r + 1.0), s * (r + 1.0), 18.0), (c * (r + 8.0), s * (r + 8.0), 4.0), (c * (r + 10.0), s * (r + 10.0), 1.0)]
        tube(wood, leg, [2.4, 2.8, 2.2, 2.6, 2.0, 1.8], sides=12)
        tube(wood, [(c * (r - 1.0), s * (r - 1.0), 22.0), (0.0, 0.0, 16.0)], 1.2, sides=8)
    lathe(wood, [(0.0, 12.0), (4.0, 12.0), (4.4, 15.0), (3.0, 20.0), (1.6, 24.0), (0.0, 26.0)], Matrix.Identity(4), sides=16)
    # The bottom of the meridian sits in a cradle on the stretcher's post.
    tube(brass, [(0.0, 0.0, 26.0), tuple(m @ Vector((0.0, 0.0, -R - 1.6)))], 0.7, sides=8)
    return [sphere, brass, wood, band]


def gramophone():
    """A horn gramophone: an oak case with a crank, a turntable in green felt with a record on it,
    the tone arm laid on the record's run-out, a brass horn from the back sweeping up and forward.
    Origin under the middle of the case; front +Y."""
    oak = Part("Oak", bevel=0.6)
    cbox(oak, 0.0, 0.0, 7.0, 34.0, 34.0, 14.0)
    cbox(oak, 0.0, 0.0, 14.6, 36.0, 36.0, 1.2)
    for sx in (-1, 1):
        for sy in (-1, 1):
            lathe(oak, [(0.0, 0.0), (1.8, 0.0), (2.2, 1.0), (1.6, 2.0), (0.0, 2.0)], at(sx * 14.0, sy * 14.0, -2.0), sides=10)
    panel = Part("Oak", bevel=1.0, segments=2)
    cbox(panel, 0.0, 17.2, 7.0, 24.0, 0.8, 8.0)
    felt = Part("Felt")
    lathe(felt, [(0.0, 15.2), (13.0, 15.2), (13.0, 16.2), (0.0, 16.2)], at(0.0, 2.0, 0.0), sides=40)
    record = Part("Record")
    lathe(record, [(0.0, 16.2), (12.5, 16.2), (12.5, 16.45), (0.0, 16.45)], at(0.0, 2.0, 0.0), sides=48)
    label = Part("Wax")
    lathe(label, [(0.0, 16.46), (4.2, 16.46), (4.2, 16.5), (0.0, 16.5)], at(0.0, 2.0, 0.0), sides=24)
    brass = Part("Brass")
    tube(brass, [(13.0, -13.0, 15.2), (13.0, -13.0, 22.0)], 1.4, sides=12)
    tube(brass, [(13.0, -13.0, 21.0), (8.0, -6.0, 19.0), (3.0, 3.0, 17.6)], [0.9, 0.8, 0.6], sides=10)
    lathe(brass, [(0.0, 0.0), (1.8, 0.0), (1.8, 1.2), (0.6, 2.2), (0.0, 2.2)], at(3.0, 3.0, 16.6), sides=12)
    # The crank on the right side.
    tube(brass, [(18.0, 4.0, 8.0), (21.0, 4.0, 8.0), (21.0, 4.0, 3.0), (24.0, 4.0, 3.0)], 0.45, sides=8)
    ellipsoid(oak, (24.6, 4.0, 3.0), (1.0, 1.0, 1.6), rings=6, sides=10)
    # The horn: rings swept up from the arm's mount and out forward, flaring as they go.
    horn = Part("Brass")
    bm = horn.bm
    path = [(13.0, -13.0, 22.0), (13.0, -14.0, 32.0), (10.0, -12.0, 44.0), (6.0, -6.0, 54.0), (2.0, 4.0, 60.0), (0.0, 14.0, 62.0)]
    pts = K.catmull(path, 6)
    radii = [1.4 * (1.0 + 0.0 * i) * math.exp(2.9 * (i / (len(pts) - 1)) ** 2.2) for i in range(len(pts))]
    radii[-1] = 26.0
    sides = 32
    outer, inner = [], []
    up = Vector((1.0, 0.0, 0.0))
    for i, p in enumerate(pts):
        p = Vector(p)
        a = Vector(pts[max(i - 1, 0)])
        b = Vector(pts[min(i + 1, len(pts) - 1)])
        t = (b - a).normalized()
        n = t.cross(up).normalized()
        bn = t.cross(n)
        r = radii[i]
        outer.append([bm.verts.new(p + (n * math.cos(2 * math.pi * k / sides) + bn * math.sin(2 * math.pi * k / sides)) * r) for k in range(sides)])
        inner.append([bm.verts.new(p + (n * math.cos(2 * math.pi * k / sides) + bn * math.sin(2 * math.pi * k / sides)) * max(r - 0.35, 0.3)) for k in range(sides)])
    for i in range(len(pts) - 1):
        for k in range(sides):
            j = (k + 1) % sides
            bm.faces.new((outer[i][k], outer[i][j], outer[i + 1][j], outer[i + 1][k]))
            bm.faces.new((inner[i][j], inner[i][k], inner[i + 1][k], inner[i + 1][j]))
    # The lip at the bell and the ring at the throat close the shell.
    for k in range(sides):
        j = (k + 1) % sides
        bm.faces.new((outer[-1][k], outer[-1][j], inner[-1][j], inner[-1][k]))
        bm.faces.new((outer[0][j], outer[0][k], inner[0][k], inner[0][j]))
    return [oak, panel, felt, record, label, brass, horn]


def gramophone_cabinet():
    """A record cabinet the gramophone stands on: oak, 64 wide, 44 deep, 78 tall, two doors, the
    records' sleeves showing through a gap where one door has dropped on its hinge."""
    oak = Part("Oak", bevel=0.5)
    cbox(oak, 0.0, 0.0, 4.0, 62.0, 42.0, 8.0)
    for sx in (-1, 1):
        cbox(oak, sx * 30.5, 0.0, 42.0, 3.0, 44.0, 70.0)
    cbox(oak, 0.0, -21.0, 42.0, 58.0, 2.0, 70.0)
    top = Part("Oak", bevel=0.9, segments=3)
    cbox(top, 0.0, 0.5, 78.0 - 1.5, 66.0, 46.0, 3.0)
    doors = Part("Oak", bevel=0.5)
    fields = Part("Oak", bevel=1.4, segments=2, angle=30.0)
    cbox(doors, -14.6, 21.0, 42.0, 28.8, 2.0, 66.0)
    cbox(fields, -14.6, 22.2, 42.0, 18.0, 1.2, 52.0)
    # The right door has dropped: hanging a centimetre low and a few degrees open.
    m = at(29.0, 21.0, -1.2, yaw=-9.0)
    cbox(doors, 0, 0, 0, 28.8, 2.0, 66.0, matrix=m @ Matrix.Translation((-14.4, 0.0, 42.0)))
    cbox(fields, 0, 0, 0, 18.0, 1.2, 52.0, matrix=m @ Matrix.Translation((-14.4, 1.2, 42.0)))
    sleeves = Part("Card")
    for k in range(18):
        cbox(sleeves, 6.0 + k * 1.2, 2.0, 40.0, 0.4, 32.0, 32.0)
    shadow = Part("Shadow")
    cbox(shadow, 0.0, 0.0, 42.0, 56.0, 38.0, 68.0)
    brass = Part("Brass")
    for x in (-3.0, 3.0):
        lathe(brass, [(0.0, 0.0), (0.7, 0.0), (1.1, 0.8), (0.0, 1.6)], at(x, 22.3, 46.0, roll=-90.0), sides=10)
    return [oak, top, doors, fields, sleeves, shadow, brass]


PIECES = {
    "pawn": [(0.0, 0.0), (1.4, 0.0), (1.4, 0.4), (1.0, 0.8), (0.6, 1.6), (0.5, 2.6), (0.9, 2.8), (0.5, 3.0), (0.8, 3.6), (0.7, 4.1), (0.0, 4.3)],
    "rook": [(0.0, 0.0), (1.6, 0.0), (1.6, 0.5), (1.1, 1.0), (0.9, 3.4), (1.2, 3.6), (1.2, 4.6), (0.9, 4.6), (0.9, 4.2), (0.0, 4.2)],
    "knight": [(0.0, 0.0), (1.6, 0.0), (1.6, 0.5), (1.1, 1.0), (0.9, 2.2), (1.0, 2.6), (0.0, 2.7)],
    "bishop": [(0.0, 0.0), (1.6, 0.0), (1.6, 0.5), (1.0, 1.0), (0.6, 3.0), (1.0, 3.2), (0.6, 3.4), (0.9, 4.4), (0.5, 5.4), (0.3, 5.6), (0.0, 5.9)],
    "queen": [(0.0, 0.0), (1.7, 0.0), (1.7, 0.5), (1.1, 1.1), (0.7, 3.6), (1.2, 3.8), (0.7, 4.0), (1.1, 5.4), (1.2, 5.8), (0.6, 6.0), (0.4, 6.4), (0.0, 6.5)],
    "king": [(0.0, 0.0), (1.8, 0.0), (1.8, 0.5), (1.2, 1.1), (0.8, 4.0), (1.3, 4.2), (0.8, 4.4), (1.1, 5.8), (0.5, 6.2), (0.25, 6.4), (0.25, 7.4), (0.0, 7.4)],
}


def chessman(part, kind, x, y, z, yaw=0.0, lying=False):
    m = at(x, y, z, yaw=yaw) if not lying else at(x, y, z + 1.4, yaw=yaw, pitch=90.0) @ Matrix.Translation((0.0, 0.0, -2.0))
    lathe(part, PIECES[kind], m, sides=14)
    if kind == "knight":
        # The horse's head: a block set on the base, leaning forward.
        cbox(part, 0, 0, 0, 1.4, 2.4, 2.6, matrix=m @ Matrix.Translation((0.0, 0.4, 3.6)) @ Matrix.Rotation(math.radians(-18.0), 4, "X"))
        cbox(part, 0, 0, 0, 1.2, 1.4, 1.2, matrix=m @ Matrix.Translation((0.0, 1.5, 4.0)))
    if kind == "king":
        cbox(part, 0, 0, 0, 1.2, 0.3, 0.3, matrix=m @ Matrix.Translation((0.0, 0.0, 7.0)))


def chess_table():
    """A games table with a chessboard inlaid in its top, and a game left in the middle: a few
    pieces taken and standing by the board, the white king's side under pressure. Origin under the
    table; white plays from -Y."""
    wood = Part("Mahogany", bevel=0.5)
    top_z = 72.0
    cbox(wood, 0.0, 0.0, top_z - 1.5, 62.0, 62.0, 3.0)
    lathe(wood, [(0.0, 0.0), (20.0, 0.0), (20.0, 2.0), (8.0, 4.0), (4.0, 8.0), (3.0, 20.0), (4.2, 36.0), (3.0, 50.0), (4.0, 64.0), (8.0, 68.0), (0.0, 69.0)],
          Matrix.Identity(4), sides=24)
    light = Part("Bone")
    dark = Part("Ebony")
    sq = 5.2
    for i in range(8):
        for j in range(8):
            target = light if (i + j) % 2 else dark
            cbox(target, -4 * sq + (i + 0.5) * sq, -4 * sq + (j + 0.5) * sq, top_z + 0.05, sq, sq, 0.1)
    border = Part("Gilt")
    for s in (-1, 1):
        cbox(border, 0.0, s * (4 * sq + 0.3), top_z + 0.06, 8 * sq + 1.2, 0.5, 0.12)
        cbox(border, s * (4 * sq + 0.3), 0.0, top_z + 0.06, 0.5, 8 * sq + 1.2, 0.12)

    def sqc(f, r):
        return -4 * sq + (f + 0.5) * sq, -4 * sq + (r + 0.5) * sq

    z = top_z + 0.1
    white = [("king", 6, 0), ("rook", 5, 0), ("pawn", 5, 1), ("pawn", 6, 2), ("pawn", 7, 1), ("queen", 3, 2), ("bishop", 2, 3),
             ("knight", 5, 2), ("pawn", 0, 1), ("pawn", 1, 1), ("pawn", 3, 3), ("rook", 0, 0)]
    black = [("king", 6, 7), ("rook", 5, 7), ("pawn", 5, 6), ("pawn", 6, 5), ("pawn", 7, 6), ("queen", 3, 6), ("knight", 4, 4),
             ("bishop", 6, 3), ("pawn", 2, 5), ("pawn", 0, 6), ("pawn", 3, 4), ("rook", 2, 7)]
    for kind, f, r in white:
        x, y = sqc(f, r)
        chessman(light, kind, x, y, z, yaw=0.0)
    for kind, f, r in black:
        x, y = sqc(f, r)
        chessman(dark, kind, x, y, z, yaw=180.0)
    # Taken pieces, stood off the board at each side; one black bishop knocked over.
    for k, kind in enumerate(("pawn", "pawn", "knight", "bishop")):
        chessman(dark, kind, 26.0, -18.0 + k * 3.6, top_z)
    for k, kind in enumerate(("pawn", "bishop", "pawn")):
        chessman(light, kind, -26.0, 14.0 - k * 3.6, top_z)
    chessman(dark, "bishop", 2.0, 8.0, z, yaw=40.0, lying=True)
    return [wood, light, dark, border]


def side_chair():
    """A balloon-back side chair in mahogany with a stuffed seat in the green leather; for the
    chess table. Faces +Y."""
    wood = Part("Mahogany", bevel=0.3)
    seat_z = 46.0
    for sx in (-1, 1):
        lathe(wood, [(0.0, 0.0), (1.4, 0.0), (1.8, 6.0), (1.6, 18.0), (2.2, 30.0), (1.8, 40.0), (2.0, seat_z - 4.0), (0.0, seat_z - 4.0)], at(sx * 19.0, 17.0, 0.0), sides=12)
        tube(wood, [(sx * 18.0, -18.0, 0.0), (sx * 18.6, -19.0, 22.0), (sx * 18.0, -18.0, seat_z - 4.0)], [1.6, 1.8, 2.0], sides=10)
    cbox(wood, 0.0, 0.0, seat_z - 5.0, 42.0, 40.0, 5.0)
    # The balloon: a loop of rail from the back legs, with a carved cross-rail in its waist.
    loop = []
    for k in range(33):
        a = math.radians(-90.0 + 360.0 * k / 32)
        loop.append((math.cos(a) * 18.0 * (0.85 if math.sin(a) < 0 else 1.0), -19.0 - 2.0 * (1 - abs(math.cos(a))), seat_z + 24.0 + math.sin(a) * 22.0))
    tube(wood, loop[8:25], 1.6, sides=10)
    for sx in (-1, 1):
        tube(wood, [(sx * 18.0, -18.0, seat_z - 4.0), (sx * 15.0, -19.0, seat_z + 10.0), loop[8 if sx > 0 else 24]], 1.6, sides=10)
    tube(wood, [(-14.0, -20.0, seat_z + 14.0), (0.0, -21.0, seat_z + 11.0), (14.0, -20.0, seat_z + 14.0)], 1.4, sides=8)
    seat = Part("LeatherGreen", bevel=2.5, segments=3)
    cbox(seat, 0.0, 0.5, seat_z + 1.8, 40.0, 38.0, 6.0)
    return [wood, seat]


def pedestal():
    """A marble pedestal for the bust: a square plinth, a fluted column, a moulded top. 112 tall."""
    marble = Part("Marble", bevel=0.6)
    cbox(marble, 0.0, 0.0, 5.0, 42.0, 42.0, 10.0)
    cbox(marble, 0.0, 0.0, 12.0, 36.0, 36.0, 4.0)
    lathe(marble, [(0.0, 14.0), (14.0, 14.0), (13.0, 18.0), (12.0, 20.0), (11.0, 96.0), (12.5, 98.0), (14.0, 102.0), (0.0, 102.0)], Matrix.Identity(4), sides=32)
    cap = Part("Marble", bevel=0.8, segments=2)
    cbox(cap, 0.0, 0.0, 105.0, 36.0, 36.0, 6.0)
    cbox(cap, 0.0, 0.0, 110.0, 32.0, 32.0, 4.0)
    flutes = Part("Shadow")
    for k in range(16):
        a = 2 * math.pi * k / 16
        cbox(flutes, 0, 0, 0, 1.2, 0.2, 70.0, matrix=at(math.cos(a) * 11.7, math.sin(a) * 11.7, 58.0, yaw=math.degrees(a) + 90.0))
    return [marble, cap, flutes]


def filing_cabinet():
    """A four-drawer oak filing cabinet, 50 x 66 x 132: brass card-holders and cup pulls on every
    drawer. The second drawer from the top is library_filing_drawer, pulled out; here it is a hole."""
    oak = Part("Oak", bevel=0.6)
    w, d, h = 50.0, 66.0, 132.0
    for sx in (-1, 1):
        cbox(oak, sx * (w * 0.5 - 1.25), 0.0, h * 0.5, 2.5, d, h)
    cbox(oak, 0.0, -d * 0.5 + 1.0, h * 0.5, w, 2.0, h)
    cbox(oak, 0.0, 0.0, 4.0, w, d, 8.0)
    top = Part("Oak", bevel=1.0, segments=3)
    cbox(top, 0.0, 0.5, h + 1.5, w + 2.0, d + 1.0, 3.0)
    fronts = Part("Oak", bevel=0.8)
    brass = Part("Brass")
    hole = Part("Shadow")
    dh = (h - 10.0) / 4
    for k in range(4):
        z0 = 9.0 + k * dh
        if k == 2:
            cbox(hole, 0.0, d * 0.5 - 1.0, z0 + dh * 0.5, w - 6.0, 1.0, dh - 2.0)
            continue
        cbox(fronts, 0.0, d * 0.5 - 0.5, z0 + dh * 0.5, w - 5.0, 2.0, dh - 1.2)
        cbox(brass, 0.0, d * 0.5 + 0.6, z0 + dh * 0.72, 8.0, 0.3, 4.0)
        lathe(brass, [(0.0, 0.0), (3.6, 0.0), (3.6, 0.4), (3.0, 1.4), (0.0, 1.8)], at(0.0, d * 0.5 + 0.5, z0 + dh * 0.42, roll=-90.0) @ Matrix.Diagonal((1.0, 0.5, 1.0, 1.0)), sides=16)
    return [oak, top, fronts, brass, hole]


def filing_drawer():
    """The filing drawer pulled out: a deep tray with its front, and suspension files standing in it
    with their tabs up (the papers in them are the C++'s). Front at y = 0, the tray back along -Y."""
    oak = Part("Oak", bevel=0.4)
    w, d, h = 44.0, 60.0, 26.0
    cbox(oak, 0.0, -0.5, h * 0.5 + 1.0, w + 1.0, 2.0, h + 2.0)
    inner = Part("Oak")
    for sx in (-1, 1):
        cbox(inner, sx * (w * 0.5 - 0.8), -d * 0.5, h * 0.4, 1.6, d, h * 0.8)
    cbox(inner, 0.0, -d + 0.8, h * 0.4, w - 1.6, 1.6, h * 0.8)
    cbox(inner, 0.0, -d * 0.5, 0.5, w - 1.6, d, 1.0)
    brass = Part("Brass")
    cbox(brass, 0.0, 0.8, h * 0.72, 8.0, 0.3, 4.0)
    lathe(brass, [(0.0, 0.0), (3.6, 0.0), (3.6, 0.4), (3.0, 1.4), (0.0, 1.8)], at(0.0, 0.5, h * 0.42, roll=-90.0) @ Matrix.Diagonal((1.0, 0.5, 1.0, 1.0)), sides=16)
    card = Part("Card")
    rng = random.Random(9)
    y = -3.0
    k = 0
    while y > -d + 4.0:
        lean = rng.uniform(-6.0, 6.0)
        m = at(0.0, y, 1.0, roll=lean)
        cbox(card, 0, 0, 0, w - 4.0, 0.25, h - 2.0, matrix=m @ Matrix.Translation((0.0, 0.0, (h - 2.0) * 0.5)))
        tx = -w * 0.5 + 6.0 + (k % 4) * 9.0
        cbox(card, 0, 0, 0, 7.0, 0.3, 2.4, matrix=m @ Matrix.Translation((tx, 0.0, h - 1.0 + 1.2)))
        y -= rng.uniform(1.5, 3.2)
        k += 1
    return [oak, inner, brass, card]


def vitrine():
    """A display table for the rare books: a mahogany case on four turned legs, a velvet bed, a
    glazed lid sloping to the front and glass sides. 120 x 60, the bed at 82. The books inside are
    the C++'s. Front +Y. The glass is a closed shell (the wine cellar's lesson)."""
    wood = Part("Mahogany", bevel=0.5)
    w, d = 120.0, 60.0
    bed_z = 82.0
    cbox(wood, 0.0, 0.0, bed_z - 4.0, w, d, 8.0)
    for sx in (-1, 1):
        for sy in (-1, 1):
            lathe(wood, [(0.0, 0.0), (2.0, 0.0), (2.4, 4.0), (1.8, 20.0), (2.8, 40.0), (2.0, 60.0), (2.6, 74.0), (0.0, 74.0)], at(sx * (w * 0.5 - 4.0), sy * (d * 0.5 - 4.0), 0.0), sides=14)
    cbox(wood, 0.0, 0.0, 14.0, w - 10.0, 2.0, 2.0)
    velvet = Part("Velvet")
    cbox(velvet, 0.0, 0.0, bed_z + 0.3, w - 4.0, d - 4.0, 0.6)
    frame = Part("Mahogany", bevel=0.3)
    # The glazed case: corner posts, the back higher than the front, rails round the lid.
    back_h, front_h = 26.0, 16.0
    for sx in (-1, 1):
        cbox(frame, sx * (w * 0.5 - 1.0), -d * 0.5 + 1.0, bed_z + back_h * 0.5, 2.0, 2.0, back_h)
        cbox(frame, sx * (w * 0.5 - 1.0), d * 0.5 - 1.0, bed_z + front_h * 0.5, 2.0, 2.0, front_h)
    cbox(frame, 0.0, -d * 0.5 + 1.0, bed_z + back_h - 1.0, w, 2.0, 2.0)
    cbox(frame, 0.0, d * 0.5 - 1.0, bed_z + front_h - 1.0, w, 2.0, 2.0)
    slope = math.degrees(math.atan2(back_h - front_h, d - 2.0))
    for sx in (-1, 1):
        cbox(frame, 0, 0, 0, 2.0, math.hypot(d - 2.0, back_h - front_h), 2.0, matrix=at(sx * (w * 0.5 - 1.0), 0.0, bed_z + (back_h + front_h) * 0.5 - 1.0, roll=slope))
    glass = Part("Glass")
    cbox(glass, 0, 0, 0, w - 4.0, math.hypot(d - 2.0, back_h - front_h) - 2.0, 0.4, matrix=at(0.0, 0.0, bed_z + (back_h + front_h) * 0.5 - 0.6, roll=slope))
    cbox(glass, 0.0, d * 0.5 - 1.0, bed_z + front_h * 0.5, w - 4.0, 0.4, front_h - 2.0)
    # The end glasses: an outline drawn in (y, z) and given its thickness along X. prism() draws in XY
    # and extrudes along Z, so its axes are turned round: X to Y, Y to Z, Z to X.
    turn = Matrix(((0.0, 0.0, 1.0, 0.0), (1.0, 0.0, 0.0, 0.0), (0.0, 1.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0)))
    for sx in (-1, 1):
        prism(glass, [(-d * 0.5 + 2.0, bed_z + 0.6), (d * 0.5 - 2.0, bed_z + 0.6), (d * 0.5 - 2.0, bed_z + front_h - 2.0), (-d * 0.5 + 2.0, bed_z + back_h - 2.0)],
              sx * (w * 0.5 - 1.2) - 0.2, sx * (w * 0.5 - 1.2) + 0.2, matrix=turn)
    brass = Part("Brass")
    cbox(brass, 0.0, d * 0.5 + 0.2, bed_z + front_h - 3.0, 3.0, 0.3, 2.0)
    return [wood, velvet, frame, glass, brass]


def ladder():
    """The rolling library ladder: two oak stiles with treads between, brass hooks at the top over the
    rail, brass wheels at the foot. Origin on the wall plane at floor level under the hooks; the
    foot stands LADDER_FOOT out from the wall (+Y)."""
    oak = Part("Oak", bevel=0.4)
    top = Vector((0.0, RAIL_PROUD + 2.0, RAIL_Z - 2.0))
    foot = Vector((0.0, LADDER_FOOT, 3.0))
    along = (top - foot)
    length = along.length
    axis = along.normalized()
    pitch = math.degrees(math.atan2(-along.y, along.z))
    half = 24.0
    for sx in (-1, 1):
        mid = (top + foot) * 0.5
        cbox(oak, 0, 0, 0, 3.2, 7.0, length, matrix=Matrix.Translation(mid + Vector((sx * half, 0.0, 0.0))) @ Matrix.Rotation(math.radians(pitch), 4, "X"))
    treads = Part("Oak", bevel=0.5)
    n = int(length // 30.0)
    for k in range(1, n + 1):
        p = foot + axis * (k * 30.0 - 6.0)
        if (p - top).length < 14.0:
            continue
        cbox(treads, p.x, p.y + 1.5, p.z, half * 2.0, 9.0, 2.6)
    brass = Part("Brass")
    for sx in (-1, 1):
        x = sx * half
        tube(brass, [(x, top.y, top.z - 4.0), (x, top.y - 0.5, top.z + 4.0), (x, RAIL_PROUD - 2.5, RAIL_Z + 4.0), (x, RAIL_PROUD - 3.5, RAIL_Z - 1.0)], 0.6, sides=8)
        # The wheels roll along the wall: their axles run out from it, along Y.
        lathe(brass, [(0.0, -1.2), (3.0, -1.2), (3.0, 1.2), (0.0, 1.2)], at(x + sx * 2.4, foot.y, 3.0, roll=-90.0), sides=16)
    return [oak, treads, brass]


def frame(art_w, art_h, moulding, depth, name):
    """A gilt frame for a picture art_w x art_h: a moulding of `moulding` width stepped and rounded,
    a slip of gilt inside it. Origin on the wall plane at the middle of the picture; front +Y."""
    gilt = Part("Gilt", bevel=0.6, segments=2)
    ow, oh = art_w + 2 * moulding, art_h + 2 * moulding
    for sx in (-1, 1):
        cbox(gilt, sx * (art_w * 0.5 + moulding * 0.5), depth * 0.5, 0.0, moulding, depth, oh)
    for sz in (-1, 1):
        cbox(gilt, 0.0, depth * 0.5, sz * (art_h * 0.5 + moulding * 0.5), art_w, depth, moulding)
    bead = Part("Gilt", bevel=0.4)
    for sx in (-1, 1):
        cbox(bead, sx * (art_w * 0.5 + moulding * 0.5), depth + 0.6, 0.0, moulding * 0.45, 1.2, oh - moulding * 0.6)
    for sz in (-1, 1):
        cbox(bead, 0.0, depth + 0.6, sz * (art_h * 0.5 + moulding * 0.5), ow - moulding * 0.6, 1.2, moulding * 0.45)
    back = Part("Shadow")
    cbox(back, 0.0, 0.4, 0.0, art_w + 1.0, 0.8, art_h + 1.0)
    return [gilt, bead, back]


def rug():
    """The carpet under the desk: 400 x 300, a few centimetres of ripple where the desk's feet and the
    years have pushed it, a cut edge, and the fringe at the two short ends. The design is the baked
    library_rug on the whole sheet. Origin at its middle on the floor; length along X."""
    part = Part("Rug")
    W, D = 400.0, 300.0
    cols, rows = 80, 60
    bm = part.bm
    uv = bm.loops.layers.uv.verify()

    def height(x, y):
        return 0.8 + 0.6 * math.sin(x * 0.03 + 1.2) * math.sin(y * 0.05) * math.exp(-((x + 80) / 120.0) ** 2)

    top = [[bm.verts.new((-W * 0.5 + W * i / cols, -D * 0.5 + D * j / rows, height(-W * 0.5 + W * i / cols, -D * 0.5 + D * j / rows)))
            for j in range(rows + 1)] for i in range(cols + 1)]
    bottom = [[bm.verts.new((v.co.x, v.co.y, 0.05)) for v in col] for col in top]
    for i in range(cols):
        for j in range(rows):
            f = bm.faces.new((top[i][j], top[i + 1][j], top[i + 1][j + 1], top[i][j + 1]))
            for loop, (a, b) in zip(f.loops, ((i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1))):
                loop[uv].uv = (a / cols, b / rows)
            g = bm.faces.new((bottom[i][j + 1], bottom[i + 1][j + 1], bottom[i + 1][j], bottom[i][j]))
            for loop in g.loops:
                loop[uv].uv = (0.0, 0.0)
    # The edges.
    for i in range(cols):
        for j in (0, rows):
            f = bm.faces.new((top[i][j], top[i + 1][j], bottom[i + 1][j], bottom[i][j]))
            for loop in f.loops:
                loop[uv].uv = (0.02, 0.02)
    for j in range(rows):
        for i in (0, cols):
            f = bm.faces.new((top[i][j], top[i][j + 1], bottom[i][j + 1], bottom[i][j]))
            for loop in f.loops:
                loop[uv].uv = (0.02, 0.02)
    part.uv_fn = no_uvs
    fringe = Part("Fringe")
    rng = random.Random(41)
    for sx in (-1, 1):
        y = -D * 0.5 + 2.0
        while y < D * 0.5 - 2.0:
            L = rng.uniform(5.0, 8.0) if rng.random() > 0.08 else rng.uniform(1.0, 3.0)
            ang = rng.uniform(-14.0, 14.0)
            x0 = sx * W * 0.5
            tube(fringe, [(x0, y, 0.6), (x0 + sx * L * 0.6, y + math.sin(math.radians(ang)) * L * 0.6, 0.35), (x0 + sx * L, y + math.sin(math.radians(ang)) * L, 0.2)], 0.22, sides=4)
            y += rng.uniform(1.2, 1.8)
    return [part, fringe]


# ---------------------------------------------------------------------------------------------
# The valance over each window.
# ---------------------------------------------------------------------------------------------

VALANCE_WIDTH = 250.0
VALANCE_DEPTH = 36.0


def sheet_solid(part, fn, nu, nv, thickness):
    """A sheet of heavy cloth as a closed solid: fn(s, t) -> (x, y, z) is its front face over the unit
    square, the back is the same pushed back along -Y. A ragged hem is a t-range that ends at a
    different place in every column (fn's business), so there are never holes and the shell stays
    closed for the bake's normals."""
    bm = part.bm
    front = [[bm.verts.new(fn(i / nu, j / nv)) for j in range(nv + 1)] for i in range(nu + 1)]
    back = [[bm.verts.new(Vector(v.co) + Vector((0.0, -thickness, 0.0))) for v in col] for col in front]
    for i in range(nu):
        for j in range(nv):
            bm.faces.new((front[i][j], front[i + 1][j], front[i + 1][j + 1], front[i][j + 1]))
            bm.faces.new((back[i][j + 1], back[i + 1][j + 1], back[i + 1][j], back[i][j]))
    for i in range(nu):
        for j in (0, nv):
            bm.faces.new((front[i][j], back[i][j], back[i + 1][j], front[i + 1][j]))
    for j in range(nv):
        for i in (0, nu):
            bm.faces.new((front[i][j], front[i][j + 1], back[i][j + 1], back[i][j]))


def tassel(part, x, y, z_top, length, matrix=None):
    """A tassel hanging from z_top: a knot, a waist, a skirt of threads."""
    s = length / 30.0
    profile = [(0.0, -30.0), (4.2, -29.5), (3.6, -14.0), (2.4, -10.0), (1.6, -8.5), (2.6, -6.0), (2.0, -2.0), (1.0, -0.5), (0.0, 0.0)]
    m = Matrix.Translation((x, y, z_top)) @ (matrix or Matrix.Identity(4)) @ Matrix.Diagonal((s, s, s, 1.0))
    lathe(part, profile, m, sides=14)


def valance():
    """The pelmet over a window, after the user's theatre reference: a velvet-covered board with returns
    to the wall, three swags hung across its front in deep curved folds with a bullion fringe under
    them, a cascade at each end, a tassel at each meeting of the swags, a gold rope along the top. And
    torn, as everything in this house is: the hems ragged, the fringe gone in stretches, the right
    swag come off its nail at that end and hanging down, the right cascade torn short, one tassel
    gone. Origin on the wall plane at the middle of the board's top; the room is +Y."""
    rng = random.Random(61)
    W, D = VALANCE_WIDTH, VALANCE_DEPTH
    hw = W * 0.5
    velvet = Part("CurtainVelvet")
    cbox(velvet, 0.0, D * 0.5, -3.0, W + 2.0, D, 6.0)
    for sx in (-1, 1):
        cbox(velvet, sx * (hw + 0.5), D * 0.5, -21.0, 2.0, D, 42.0)

    gold = Part("Gold")
    swags = [(-hw, -40.0, 34.0, 0), (-44.0, 44.0, 42.0, 1), (40.0, hw, 34.0, 2)]
    hems = []
    for x0, x1, dip, k in swags:
        fy = D + 2.0 + k % 2 * 1.5   # the middle swag in front of the outer two
        ragged = [rng.uniform(-2.5, 2.5) for _ in range(41)]
        fallen = k == 2

        def fn(s, t, x0=x0, x1=x1, dip=dip, ragged=ragged, fy=fy, fallen=fallen):
            x = x0 + (x1 - x0) * s
            # The hem: a swag's curve, a little ragged everywhere (interpolated between the jags, or
            # every column steps) and eaten in two bites.
            f = s * 40.0
            i0 = min(int(f), 39)
            hem = 10.0 + dip * math.sin(math.pi * s) ** 0.85 + ragged[i0] + (ragged[i0 + 1] - ragged[i0]) * (f - i0)
            hem -= 9.0 * math.exp(-((s - 0.33) / 0.05) ** 2) + 6.0 * math.exp(-((s - 0.71) / 0.04) ** 2)
            z = -4.0 - t * hem
            # Folds following the swag's curve, deepest at its middle; it bellies out over the room.
            y = fy + 6.0 * math.sin(math.pi * s) * math.sin(math.pi * t * 0.8) + 1.7 * math.sin(t * 5.0 * 2 * math.pi) * math.sin(math.pi * s)
            if fallen:
                # Off its nail at the right-hand end: the end has dropped and the cloth hangs from there.
                drop = 30.0 * max(0.0, (s - 0.62) / 0.38) ** 1.6
                z -= drop * (0.4 + 0.6 * t)
                y += 3.0 * max(0.0, s - 0.62)
            return (x, y, z)

        # Fine enough down the drop that five folds are folds and not terraces.
        sheet_solid(velvet, fn, 64, 44, 1.2)
        hems.append([fn(i / 120.0, 1.0) for i in range(121)])
        # The gold rope it hangs from.
        if not fallen:
            tube(gold, [(x0 + (x1 - x0) * i / 12.0, fy + 0.8, -4.0) for i in range(13)], 1.0, sides=8)
        else:
            tube(gold, [(x0 + (x1 - x0) * i / 12.0, fy + 0.8, -4.0) for i in range(8)], 1.0, sides=8)

    # The cascades at the ends: pleated tails over the returns, the outer edge longest. The right one
    # was torn off a third of the way down.
    for sx, length in ((-1, 92.0), (1, 34.0)):
        torn = [rng.uniform(-4.0, 4.0) if sx > 0 else 0.0 for _ in range(19)]

        def cascade(s, t, sx=sx, length=length, torn=torn):
            x = sx * (hw - 2.0 - s * 30.0)
            f = s * 18.0
            i0 = min(int(f), 17)
            hem = length * (1.0 - 0.55 * s) + torn[i0] + (torn[i0 + 1] - torn[i0]) * (f - i0)
            z = -4.0 - t * hem
            y = D + 5.0 + 3.0 * abs(math.sin(s * 3.0 * math.pi))
            return (x, y, z)
        sheet_solid(velvet, cascade, 18, 16, 1.2)
        if sx < 0:
            hems.append([cascade(i / 40.0, 1.0) for i in range(41)])

    # The fringe under every hem that has one: bullion threads, a few gone here and there and whole
    # stretches gone where the cloth was bitten.
    for hem in hems:
        for i, (x, y, z) in enumerate(hem):
            if rng.random() < 0.12 or -32.0 < x < -18.0 or 26.0 < x < 38.0:
                continue
            L = rng.uniform(7.0, 9.5)
            tube(gold, [(x, y - 0.2, z + 0.5), (x + rng.uniform(-0.3, 0.3), y + rng.uniform(-0.2, 0.4), z - L)], 0.28, sides=4)
    # Tassels where the swags meet; the right-hand one is gone, its cord's stub left.
    tassel(gold, -42.0, D + 4.0, -6.0, 30.0)
    tube(gold, [(42.0, D + 4.0, -5.0), (42.5, D + 4.2, -11.0)], 0.6, sides=6)
    for sx in (-1, 1):
        ellipsoid(gold, (sx * 42.0, D + 4.5, -4.0), (3.0, 2.0, 3.0), rings=6, sides=10)
    return [velvet, gold]


# ---------------------------------------------------------------------------------------------

MODELS = [
    ("library_bookcase", bookcase),
    *[("library_books_{}".format(chr(ord("a") + i)), (lambda i=i: book_row(ROW_LENGTHS[i], 900 + i))) for i in range(len(ROW_LENGTHS))],
    ("library_books_leaning", books_leaning),
    ("library_books_pile_a", lambda: books_lying(51, 4)), ("library_books_pile_b", lambda: books_lying(52, 3)),
    ("library_arch", corridor_arch), ("library_window_arch", window_arch), ("library_cornice", cornice),
    ("library_fireplace", fireplace), ("library_fire_tools", fire_tools),
    ("library_desk", desk), ("library_desk_drawer", desk_drawer), ("library_desk_chair", desk_chair),
    ("library_bankers_lamp", bankers_lamp), ("library_ink_bottle", ink_bottle), ("library_pen", fountain_pen),
    ("library_magnifier", magnifier), ("library_glasses", reading_glasses), ("library_blotter", blotter),
    ("library_seal", seal_set), ("library_laptop", laptop), ("library_key", small_key), ("library_mug", mug),
    ("library_open_book", open_book), ("library_notebook", notebook_open), ("library_letter", letter_folded),
    ("library_newspaper", newspaper_folded),
    ("library_side_table", side_table), ("library_floor_lamp", floor_lamp),
    ("library_globe", globe), ("library_gramophone", gramophone), ("library_record_cabinet", gramophone_cabinet),
    ("library_chess_table", chess_table), ("library_side_chair", side_chair), ("library_pedestal", pedestal),
    ("library_filing_cabinet", filing_cabinet), ("library_filing_drawer", filing_drawer), ("library_vitrine", vitrine),
    ("library_ladder", ladder),
    ("library_frame_map", lambda: frame(MAP_ART[0], MAP_ART[1], 7.0, 4.0, "map")),
    ("library_frame_portrait", lambda: frame(PORTRAIT_LARGE_ART[0], PORTRAIT_LARGE_ART[1], 11.0, 6.0, "large")),
    ("library_frame_small", lambda: frame(PORTRAIT_SMALL_ART[0], PORTRAIT_SMALL_ART[1], 9.0, 5.0, "small")),
    ("library_rug", rug),
    ("library_valance", valance),
]

# Repeated by the hundred on the shelves: imported with Nanite on.
NANITE = {"library_bookcase"} | {"library_books_{}".format(chr(ord("a") + i)) for i in range(len(ROW_LENGTHS))}


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
        dims = obj.dimensions
        print("LIBRARY baked {} ({} verts, {:.1f} x {:.1f} x {:.1f} cm)".format(name, len(obj.data.vertices), dims.x * 100, dims.y * 100, dims.z * 100))

    # Previews: the big pieces in a row, the small things on a grid.
    big = [o for o in baked if o.dimensions.z > 0.5 or max(o.dimensions) > 1.2]
    small = [o for o in baked if o not in big]
    x = 0.0
    for obj in big:
        width = obj.dimensions.x
        obj.location = (x + width * 0.5, 0.0, 0.0)
        x += width + 0.3
    for i, obj in enumerate(small):
        obj.location = (-1.0 - (i % 6) * 0.6, 3.0 + (i // 6) * 0.6, 0.0)
    K.preview(baked, PREVIEW_DIR, "models", [
        ("furniture", (x * 0.5, -14.0, 3.6), (x * 0.5, 0.0, 1.2)),
        ("furniture_left", (x * 0.25, -6.0, 2.0), (x * 0.25, 0.0, 1.0)),
        ("furniture_right", (x * 0.75, -6.0, 2.0), (x * 0.75, 0.0, 1.0)),
        ("small", (-2.5, 1.2, 2.4), (-2.5, 4.0, 0.0)),
    ])


main()
