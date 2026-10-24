"""
Builds the music room's instruments, furniture, chandelier and joinery in Blender, headless, and
exports each as an FBX the art pipeline imports.

    blender --background --factory-startup --python Tools/make_music_room.py [-- --only=a,b]

Nothing free is a cello, a violin in its case, a classical guitar, a music stand, a metronome or a
crystal chandelier that is not a modern catalogue piece, so they are modelled here, as the library's
desk and globe were. The fiddles' bodies are lofted from one outline (two bouts and a waist) with
their plates arched; the chandelier is a turned brass stem, two tiers of scrolled arms, and its
crystal — swags of beads, drops and prisms, a few hundred of them, some gone. There is no piano:
the user's call (10-21).

Outputs Art/Source/Models/<name>/<name>.fbx with an empty GENERATED file beside it (re-imported every
run with its own normals). Workbench previews go to Saved/MusicPreview/.

Frame, as every prop in the house: centimetres, origin on the floor under the middle of the
footprint, front facing local +Y. Exceptions are noted on the model: the arch is in the plane of its
wall (the stair hall at -Y), wall-hung things on the wall plane, the chandelier and the ceiling rose
hang from z = 0, the cabinet's door has its origin on its hinge, small things at their own base.

The numbers AMusicRoomActor also needs are the constants below, and must stay in step with
MusicRoomActor.h: the arch, the stands' desks, the shelf, the sideboard, the bench, the tables, the
frames.

Material slots are set by name in C++. Their texel sizes here match the surface each is given there,
whose instances have their tiling at one:
    Oak          black_oak_veneer (100cm)          Mahogany   lacquered_cherry_wood (120cm)
    Varnish      lacquered_cherry_wood (120cm)     Leather    brown_leather (40cm)
    Velvet       music_velvet (34cm)               Panel      dark_paneled_wood (140cm)
    Plaster      ceiling_interior (200cm)          CorridorWall  cracked_concrete_wall (104cm)
    Marble       marble_01 (150cm)
    Paper, Books   the baked atlases, with their own UVs
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
from blender_kit import Part, at, blob, cbox, ellipsoid, lathe, prism, prism_xz, tube  # noqa: E402

PREVIEW_DIR = os.path.join(K.ROOT, "Saved", "MusicPreview")

FLAT = 100.0
SLOTS = {
    "Oak": ((0.16, 0.10, 0.06, 1.0), 100.0),
    "Mahogany": ((0.28, 0.08, 0.04, 1.0), 120.0),
    "Varnish": ((0.45, 0.16, 0.05, 1.0), 120.0),
    "Leather": ((0.10, 0.07, 0.05, 1.0), 40.0),
    "Velvet": ((0.05, 0.25, 0.14, 1.0), 34.0),
    "Panel": ((0.20, 0.12, 0.07, 1.0), 140.0),
    "RoomWall": ((0.20, 0.12, 0.07, 1.0), 140.0),
    "CorridorWall": ((0.55, 0.50, 0.45, 1.0), 104.0),
    "Plaster": ((0.70, 0.68, 0.62, 1.0), 200.0),
    "Marble": ((0.75, 0.73, 0.70, 1.0), 150.0),
    "Paper": ((0.85, 0.80, 0.68, 1.0), FLAT),
    "Books": ((0.40, 0.30, 0.20, 1.0), FLAT),
    "Trim": ((0.80, 0.78, 0.72, 1.0), FLAT),
    "Ebony": ((0.05, 0.04, 0.04, 1.0), FLAT),
    "Maple": ((0.70, 0.58, 0.40, 1.0), FLAT),
    "String": ((0.60, 0.58, 0.55, 1.0), FLAT),
    "Hair": ((0.85, 0.82, 0.74, 1.0), FLAT),
    "Bone": ((0.85, 0.80, 0.68, 1.0), FLAT),
    "Rosewood": ((0.20, 0.08, 0.05, 1.0), FLAT),
    "Inlay": ((0.70, 0.62, 0.45, 1.0), FLAT),
    "Iron": ((0.15, 0.12, 0.10, 1.0), FLAT),
    "Brass": ((0.60, 0.45, 0.20, 1.0), FLAT),
    "Gilt": ((0.75, 0.60, 0.25, 1.0), FLAT),
    "Crystal": ((0.80, 0.85, 0.90, 1.0), FLAT),
    "Candle": ((0.80, 0.76, 0.62, 1.0), FLAT),
    "Glass": ((0.70, 0.75, 0.78, 1.0), FLAT),
    "Plush": ((0.35, 0.06, 0.08, 1.0), FLAT),
    "Felt": ((0.08, 0.14, 0.10, 1.0), FLAT),
    "Card": ((0.70, 0.60, 0.42, 1.0), FLAT),
    "Shadow": ((0.02, 0.02, 0.02, 1.0), FLAT),
    "Stem": ((0.25, 0.20, 0.10, 1.0), FLAT),
    "Petal": ((0.40, 0.10, 0.10, 1.0), FLAT),
    "Bloom": ((0.50, 0.45, 0.40, 1.0), FLAT),
    "Ribbon": ((0.50, 0.08, 0.06, 1.0), FLAT),
    "Medal": ((0.70, 0.55, 0.25, 1.0), FLAT),
}

# ---------------------------------------------------------------------------------------------
# Shared with MusicRoomActor.h.
# ---------------------------------------------------------------------------------------------

# The arch from the stair hall's north gallery: a semi-ellipse on its springing. Its hall face's
# architrave (14) over the crown stands above the hall's picture rail (262), which the hall cuts round
# it (AGrandStaircaseActor::CutAroundTrim), and under the hall's ceiling. On the room face
# a carved surround stands out round the opening to ARCH_SURROUND_HALF either side and up to
# ARCH_SURROUND_TOP, and the room's panelling and silk are cut to that rectangle.
ARCH_HALF = 110.0
ARCH_SPRING = 192.0
ARCH_RISE = 72.0
ARCH_CROWN = ARCH_SPRING + ARCH_RISE
ARCH_WALL = 20.0
ARCH_SURROUND_HALF = 136.0
ARCH_SURROUND_TOP = 300.0

# The music stands' desks: the middle of the board, how far it leans back from upright, its size, and
# the ledge the music stands on. The desk faces local +Y; up the desk is (0, -sin, cos).
IRON_DESK_Z = 116.0
WOOD_DESK_Z = 106.0
DESK_LEAN = 22.0
DESK_W = 50.0
DESK_H = 34.0

# The display shelf: the tops of its boards, its inside width and depth.
SHELF_W = 150.0
SHELF_D = 34.0
SHELF_BOARDS = (10.5, 45.5, 80.5)
SHELF_TOP = 116.0
SHELF_INSIDE = SHELF_W - 6.0
BOOK_ROWS = (44.0, 38.0, 31.0)

# The sideboard, the side table, the window bench: their tops.
SIDEBOARD_W = 160.0
SIDEBOARD_D = 52.0
SIDEBOARD_TOP = 92.0
SIDE_TABLE_TOP = 66.0
BENCH_L = 104.0
BENCH_D = 40.0
BENCH_TOP = 46.0
CABINET_W = 130.0
CABINET_D = 48.0
CABINET_H = 226.0

# The pictures each frame holds (art width, art height).
CELLIST_ART = (80.0, 100.0)
LANDSCAPE_ART = (76.0, 57.0)
CERT_LAND_ART = (42.0, 29.7)
CERT_PORT_ART = (29.7, 42.0)

# music_paper (Tools/make_music_art.py: PAPER), and music_books.
ATLAS = 2048.0
PAPER = {
    "score_a": (0, 0, 362, 512), "score_b": (512, 0, 362, 512), "manuscript_a": (1024, 0, 362, 512),
    "manuscript_b": (1536, 0, 362, 512), "book_l": (0, 512, 362, 512), "book_r": (512, 512, 362, 512),
    "notebook_l": (1024, 512, 362, 512), "notebook_r": (1536, 512, 362, 512),
    "photo_a": (0, 1536, 256, 320), "photo_b": (256, 1536, 256, 320), "photo_c": (512, 1536, 320, 256),
    "cover": (1536, 1536, 362, 512),
}
BOOK_SHEET = 1024.0
SPINE_W = 32.0
SPINE_H = 512.0
LEAVES = (0.0, 512.0, 512.0, 256.0)
COVERS_X = 512.0
COVER_W = 64.0
with open(os.path.join(K.ROOT, "Art", "Source", "Textures", "music_books.json"), "r", encoding="utf-8") as _handle:
    SPINE_BINDING = __import__("json").load(_handle)["bindings"]


# ---------------------------------------------------------------------------------------------
# Helpers.
# ---------------------------------------------------------------------------------------------

def no_uvs(bm):
    """For parts whose UVs were laid while they were built: keeps box_uvs off them."""
    return None


def atlas_uv(rect, s, t, sheet=ATLAS):
    x, y, w, h = rect
    return ((x + s * w) / sheet, 1.0 - (y + t * h) / sheet)


def uv_quad(part, corners, rect, sheet=ATLAS, thickness=0.05):
    """A sheet of paper or a print as a closed thin box: corners (bl, br, tr, tl) of its face, the
    atlas rectangle on the front face, and the back and edges on the rectangle's corner. Closed, so
    the bake's normals have an inside to point away from (the library's sheet_strip note)."""
    bm = part.bm
    uv = bm.loops.layers.uv.verify()
    bl, br, tr, tl = (Vector(c) for c in corners)
    # The game frame is left-handed, so (right x up) points away from whoever reads the face: the
    # back goes that way. With it the other way, the back stood in front of the print and every
    # photograph on the sideboard showed a blank sheet of the rectangle's corner colour.
    n = (br - bl).cross(tl - bl).normalized() * thickness
    front = [bm.verts.new(v) for v in (bl, br, tr, tl)]
    back = [bm.verts.new(v + n) for v in (bl, br, tr, tl)]
    face = bm.faces.new(front)
    for loop, (s, t) in zip(face.loops, ((0, 1), (1, 1), (1, 0), (0, 0))):
        loop[uv].uv = atlas_uv(rect, s, t, sheet)
    edge = atlas_uv(rect, 0.02, 0.02, sheet)
    for f in ([back[3], back[2], back[1], back[0]], [front[0], back[0], back[1], front[1]], [front[1], back[1], back[2], front[2]],
              [front[2], back[2], back[3], front[3]], [front[3], back[3], back[0], front[0]]):
        face = bm.faces.new(f)
        for loop in face.loops:
            loop[uv].uv = edge


def uv_box(part, x0, x1, y0, y1, z0, z1, rects, matrix=None, sheet=BOOK_SHEET):
    """A box whose faces each show a rectangle of an atlas (the library's): py front, ny, px, nx, pz top, nz."""
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
        return (y - y0) / dy, (x - x0) / dx

    faces = {"nz": (0, 2, 3, 1), "pz": (4, 5, 7, 6), "ny": (0, 1, 5, 4), "py": (2, 6, 7, 3), "nx": (0, 4, 6, 2), "px": (1, 3, 7, 5)}
    for name, idx in faces.items():
        face = bm.faces.new([verts[i] for i in idx])
        u0, v0, u1, v1 = rects[name]
        for loop in face.loops:
            s, t = st(name, tuple(loop.vert.co))
            loop[uv].uv = ((u0 + s * (u1 - u0)) / sheet, 1.0 - (v0 + t * (v1 - v0)) / sheet)
    if matrix is not None:
        bmesh.ops.transform(bm, matrix=matrix, verts=verts)
    return verts


def transform_new(part, start, matrix):
    """Moves every vertex added to a part since `start` (its vertex count then) by a matrix."""
    part.bm.verts.ensure_lookup_table()
    bmesh.ops.transform(part.bm, matrix=matrix, verts=part.bm.verts[start:])


def count(part):
    return len(part.bm.verts)


def ellipse_pts(a, b, cz, steps, t0=0.0, t1=math.pi):
    return [(a * math.cos(t0 + (t1 - t0) * k / steps), cz + b * math.sin(t0 + (t1 - t0) * k / steps)) for k in range(steps + 1)]


def moulding_path(part, path, profile):
    """A moulding swept along a path in the XZ plane (the library's): profile points (across, out)."""
    bm = part.bm
    rings = []
    n = len(path)
    for i, (x, z) in enumerate(path):
        a = path[max(i - 1, 0)]
        b = path[min(i + 1, n - 1)]
        tx, tz = b[0] - a[0], b[1] - a[1]
        L = math.hypot(tx, tz) or 1.0
        nx, nz = -tz / L, tx / L
        rings.append([bm.verts.new((x + nx * across, out, z + nz * across)) for across, out in profile])
    m = len(profile)
    for r0, r1 in zip(rings, rings[1:]):
        for k in range(m):
            j = (k + 1) % m
            bm.faces.new((r0[k], r0[j], r1[j], r1[k]))
    bm.faces.new(rings[0])
    bm.faces.new(rings[-1][::-1])


def architrave_profile(width, depth, flip=1.0):
    return [(0.0, 0.0), (width, 0.0), (width, depth * 0.7 * flip), (width - 1.2, depth * flip), (width - 3.0, depth * flip),
            (width - 3.6, depth * 0.6 * flip), (0.0, depth * 0.45 * flip)]


def frame(art_w, art_h, moulding, depth, slot="Gilt", back=True):
    """A frame for a picture art_w x art_h (the library's): a moulding, a bead inside it. Origin on the
    wall at the middle of the picture; front +Y."""
    outer = Part(slot, bevel=0.6, segments=2)
    ow, oh = art_w + 2 * moulding, art_h + 2 * moulding
    for sx in (-1, 1):
        cbox(outer, sx * (art_w * 0.5 + moulding * 0.5), depth * 0.5, 0.0, moulding, depth, oh)
    for sz in (-1, 1):
        cbox(outer, 0.0, depth * 0.5, sz * (art_h * 0.5 + moulding * 0.5), art_w, depth, moulding)
    bead = Part(slot, bevel=0.4)
    for sx in (-1, 1):
        cbox(bead, sx * (art_w * 0.5 + moulding * 0.5), depth + 0.6, 0.0, moulding * 0.45, 1.2, oh - moulding * 0.6)
    for sz in (-1, 1):
        cbox(bead, 0.0, depth + 0.6, sz * (art_h * 0.5 + moulding * 0.5), ow - moulding * 0.6, 1.2, moulding * 0.45)
    parts = [outer, bead]
    if back:
        board = Part("Shadow")
        cbox(board, 0.0, 0.4, 0.0, art_w + 1.0, 0.8, art_h + 1.0)
        parts.append(board)
    return parts


# ---------------------------------------------------------------------------------------------
# The violin family. The body is lofted from one outline drawn in XZ — width across, length up —
# with its thickness along Y and the front at +Y: an instrument stood on its end, facing the room.
# ---------------------------------------------------------------------------------------------

def curve_fn(points, steps=24):
    """A half-width along a body from control points (t, half-width as a fraction of the length),
    smoothed through them with a Catmull-Rom spline and looked up by t."""
    dense = K.catmull([(t, w) for t, w in points], steps)
    ts = [p[0] for p in dense]
    ws = [p[1] for p in dense]

    def fn(t):
        if t <= ts[0]:
            return ws[0]
        for i in range(1, len(ts)):
            if t <= ts[i]:
                a = (t - ts[i - 1]) / max(ts[i] - ts[i - 1], 1e-6)
                return max(0.0, ws[i - 1] + (ws[i] - ws[i - 1]) * a)
        return ws[-1]
    return fn


# A fiddle's body, tail (t = 0) to neck (t = 1): the lower bout, the corners, the C-bouts' waist
# sharply in between them, the upper bout and the shoulders into the neck. A cello's measures: 75
# long, 44 across the lower bout, 34 across the upper, 23.5 at the waist; a violin is the same shape.
fiddle_half = curve_fn([(0.0, 0.0), (0.006, 0.07), (0.025, 0.15), (0.07, 0.225), (0.14, 0.27), (0.23, 0.293), (0.31, 0.287),
                        (0.37, 0.27), (0.395, 0.25), (0.43, 0.19), (0.5, 0.158), (0.57, 0.18), (0.61, 0.21), (0.635, 0.228),
                        (0.69, 0.226), (0.77, 0.227), (0.85, 0.212), (0.92, 0.17), (0.965, 0.11), (0.99, 0.05), (1.0, 0.0)])

# A classical guitar's: one easy line, a deep lower bout, a shallow waist, a smaller upper bout.
guitar_half = curve_fn([(0.0, 0.0), (0.008, 0.13), (0.04, 0.26), (0.1, 0.335), (0.2, 0.372), (0.28, 0.378), (0.37, 0.355),
                        (0.46, 0.29), (0.55, 0.247), (0.63, 0.262), (0.72, 0.284), (0.8, 0.286), (0.88, 0.262), (0.95, 0.19),
                        (0.985, 0.1), (1.0, 0.0)])


def outline(half_fn, length, z0, steps=40):
    """The closed outline, counter-clockwise seen from the front: up the right side, down the left."""
    ts = [0.004 + 0.992 * k / steps for k in range(steps + 1)]
    right = [(half_fn(t) * length, z0 + t * length) for t in ts]
    left = [(-x, z) for x, z in reversed(right)]
    return right + left


def loft(part, ring_outline, rings, centre_z):
    """A body from rings of one outline: each ring (scale, y) is the outline shrunk towards the body's
    middle by `scale` and put at depth y; both ends are closed on a point at the middle."""
    bm = part.bm
    loops = []
    for s, y in rings:
        loops.append([bm.verts.new((x * s, y, centre_z + (z - centre_z) * s)) for x, z in ring_outline])
    n = len(ring_outline)
    for a, b in zip(loops, loops[1:]):
        for k in range(n):
            j = (k + 1) % n
            bm.faces.new((a[k], a[j], b[j], b[k]))
    for loop, y in ((loops[0], rings[0][1]), (loops[-1], rings[-1][1])):
        centre = bm.verts.new((0.0, y, centre_z))
        for k in range(n):
            bm.faces.new((loop[k], loop[(k + 1) % n], centre))


def arch_rings(rib, arch_front, arch_back, edge=0.25):
    """The rings of an arched fiddle body, back to front: the back plate's arching, its edge standing
    a little proud of the ribs, the ribs, the front plate's edge and its arching."""
    back = [(0.22, -rib * 0.5 - arch_back * 0.97), (0.5, -rib * 0.5 - arch_back * 0.78), (0.74, -rib * 0.5 - arch_back * 0.48),
            (0.9, -rib * 0.5 - arch_back * 0.18), (1.015, -rib * 0.5 - 0.05), (1.015, -rib * 0.5 + edge), (1.0, -rib * 0.5 + edge)]
    front = [(1.0, rib * 0.5 - edge), (1.015, rib * 0.5 - edge), (1.015, rib * 0.5 + 0.05), (0.9, rib * 0.5 + arch_front * 0.18),
             (0.74, rib * 0.5 + arch_front * 0.48), (0.5, rib * 0.5 + arch_front * 0.78), (0.22, rib * 0.5 + arch_front * 0.97)]
    return back + front


def top_surface(rib, arch, r):
    """How far the front plate stands out at a fraction r of the way from the middle to the edge."""
    return rib * 0.5 + arch * max(0.0, 1.0 - r * r) ** 0.9


def f_hole(part, x, z, length, width, y, side):
    """An f-hole: a slender S cut into the front, with a round eye at each end. A dark strip laid on the
    plate's surface, the eyes a hair bigger than the stroke."""
    pts = []
    for k in range(17):
        t = k / 16
        pts.append((x + side * (math.sin((t - 0.5) * math.pi) * 0.22 + 0.08 * math.sin(t * math.pi * 2)) * length * 0.3, z + (t - 0.5) * length))
    bm = part.bm
    half = width * 0.5
    rows = []
    for i, (px, pz) in enumerate(pts):
        a = pts[max(i - 1, 0)]
        b = pts[min(i + 1, len(pts) - 1)]
        tx, tz = b[0] - a[0], b[1] - a[1]
        L = math.hypot(tx, tz) or 1.0
        nx, nz = -tz / L * half, tx / L * half
        rows.append((bm.verts.new((px + nx, y, pz + nz)), bm.verts.new((px - nx, y, pz - nz)),
                     bm.verts.new((px + nx, y - 0.1, pz + nz)), bm.verts.new((px - nx, y - 0.1, pz - nz))))
    for r0, r1 in zip(rows, rows[1:]):
        bm.faces.new((r0[0], r1[0], r1[1], r0[1]))
        bm.faces.new((r0[3], r1[3], r1[2], r0[2]))
        bm.faces.new((r0[2], r1[2], r1[0], r0[0]))
        bm.faces.new((r0[1], r1[1], r1[3], r0[3]))
    bm.faces.new((rows[0][1], rows[0][3], rows[0][2], rows[0][0]))
    bm.faces.new((rows[-1][0], rows[-1][2], rows[-1][3], rows[-1][1]))
    for px, pz in (pts[0], pts[-1]):
        ellipsoid(part, (px, y - 0.02, pz), (width * 1.1, 0.06, width * 1.1), rings=4, sides=8)


def scroll(part, base_z, y, size, sides=8):
    """The scroll at the head: a volute turned one and a half times down each cheek, and the pegbox's
    throat under it."""
    for cheek in (-1, 1):
        pts = []
        for k in range(36):
            a = k / 35 * math.pi * 3.0
            r = size * (0.5 - 0.42 * k / 35)
            pts.append((cheek * size * 0.18, y + math.sin(a) * r, base_z + size * 0.55 + math.cos(a) * r))
        tube(part, pts, [size * 0.17 * (1.0 - 0.6 * k / 35) for k in range(36)], sides=sides)


def fiddle(L, rib, arch, neck_len, head_len, board_w, bridge_h, endpin=0.0, kind="cello"):
    """A violin or a cello standing on its end, the tail at z = 0, the front at +Y: the arched body,
    f-holes, the neck and fingerboard, the pegbox and scroll, pegs, the tailpiece, the bridge and the
    strings. Returns the parts and the height of the top of its scroll."""
    body = Part("Varnish", smooth=True)
    z0 = 0.0
    zc = z0 + L * 0.5
    ring = outline(fiddle_half, L, z0, steps=44)
    loft(body, ring, arch_rings(rib, arch, arch * 0.9), zc)
    dark = Part("Shadow")
    fz = z0 + L * 0.5
    fx = L * 0.1
    for side in (-1, 1):
        r = fx / max(fiddle_half(0.5) * L, 1e-3) * 0.7
        f_hole(dark, side * fx, fz, L * 0.17, L * 0.0075, top_surface(rib, arch, r) + 0.15, side)
    # The neck rises from the top block; the fingerboard is set so the strings, run from the top of the
    # bridge to the nut, clear its end by a few millimetres; the pegbox and the scroll at its head.
    top = z0 + L
    top_c = top_surface(rib, arch, 0.0)
    bridge_top = top_c + bridge_h
    ny = rib * 0.5 + arch * 0.6
    board_t = board_w * 0.14
    nt = board_w * 0.55
    hz = top + neck_len
    neck = Part("Varnish", bevel=0.3)
    nb = board_w * 0.75
    prism_xz(neck, [(-nb * 0.5, top - 1.0), (nb * 0.5, top - 1.0), (nt * 0.5, hz), (-nt * 0.5, hz)], ny - rib * 0.45, ny)
    head = Part("Varnish", bevel=0.25)
    hy = ny - rib * 0.2
    cbox(head, 0.0, hy, hz + head_len * 0.32, nt * 0.95, rib * 0.4, head_len * 0.64)
    scroll(head, hz + head_len * 0.45, hy, head_len * 0.62)
    ebony = Part("Ebony", bevel=0.15)
    z_end = top - L * 0.36
    nut_top = ny + board_t + 0.1
    string_at_end = bridge_top + (nut_top - bridge_top) * (z_end - fz) / (hz - fz)
    yb_end = string_at_end - board_w * 0.07 - board_t
    slab(ebony, z_end, hz, board_w, nt, yb_end, ny, board_t)
    cbox(ebony, 0.0, ny + board_t * 0.5 + 0.05, hz, nt, board_t + 0.1, 0.6)
    # The pegs through the pegbox, their heads out to the sides.
    for k in range(4):
        side = -1 if k % 2 == 0 else 1
        pz = hz + head_len * (0.12 + 0.12 * k)
        tube(ebony, [(-side * nt * 0.7, hy, pz), (side * nt * 1.6, hy, pz)], board_w * 0.06, sides=6)
        ellipsoid(ebony, (side * nt * 2.0, hy, pz), (board_w * 0.08, board_w * 0.22, board_w * 0.18), rings=4, sides=8)
    # The tailpiece, hung from the end button and rising off the plate towards the bridge.
    tail_lo = top_c - arch * 0.75 + 0.3
    slab(ebony, z0 + L * 0.06, z0 + L * 0.31, board_w * 0.64, board_w * 1.1, tail_lo, top_c + bridge_h * 0.16, board_w * 0.12)
    if endpin:
        iron = Part("Iron")
        tube(iron, [(0.0, 0.0, z0 + 1.0), (0.0, 0.0, z0 - endpin)], 0.5, sides=8)
        lathe(iron, [(0.0, z0 - 1.5), (1.6, z0 - 1.5), (1.8, z0 + 0.5), (0.0, z0 + 0.5)], Matrix.Identity(4), sides=12)
    # The bridge: a thin arched plate standing straight up off the front between the f-holes' notches.
    maple = Part("Maple", bevel=0.05)
    bridge = [(-board_w * 0.75, 0.0), (-board_w * 0.3, 0.0), (-board_w * 0.22, bridge_h * 0.25), (board_w * 0.22, bridge_h * 0.25), (board_w * 0.3, 0.0),
              (board_w * 0.75, 0.0), (board_w * 0.6, bridge_h * 0.55), (board_w * 0.45, bridge_h), (-board_w * 0.45, bridge_h), (-board_w * 0.6, bridge_h * 0.55)]
    prism_xz(maple, bridge, -board_w * 0.04, board_w * 0.04,
             matrix=Matrix.Translation((0.0, top_c - 0.2, fz)) @ Matrix.Rotation(math.radians(-90.0), 4, "X"))
    strings = Part("String")
    tail_end = top_c + bridge_h * 0.16 + board_w * 0.12
    for k in range(4):
        x = (k - 1.5) / 1.5
        tail = (x * board_w * 0.4, tail_end, z0 + L * 0.3)
        over = (x * board_w * 0.32, bridge_top, fz)
        nut = (x * nt * 0.35, nut_top, hz)
        tube(strings, [tail, over, nut], board_w * (0.006 + 0.002 * (3 - k)), sides=4)
    parts = [body, dark, neck, head, ebony, maple, strings]
    if endpin:
        parts.append(iron)
    return parts, hz + head_len


def slab(part, z0, z1, w0, w1, y0, y1, thickness):
    """A tapered board running up the instrument from z0 to z1: w0 wide at its foot and w1 at its head,
    its underside at y0 and y1 at the two ends, `thickness` thick."""
    bm = part.bm
    v = []
    for z, w, y in ((z0, w0, y0), (z1, w1, y1)):
        for yy in (y, y + thickness):
            v.append((bm.verts.new((-w * 0.5, yy, z)), bm.verts.new((w * 0.5, yy, z))))
    (a0, a1), (b0, b1), (c0, c1), (d0, d1) = v
    for f in ((a0, a1, b1, b0), (c0, d0, d1, c1), (a0, c0, c1, a1), (b0, b1, d1, d0), (a0, b0, d0, c0), (a1, c1, d1, b1)):
        bm.faces.new(f)


def place(parts, matrix):
    for p in parts:
        bmesh.ops.transform(p.bm, matrix=matrix, verts=p.bm.verts)
    return parts


CELLO = dict(L=75.0, rib=12.0, arch=2.4, neck_len=28.0, head_len=19.0, board_w=6.8, bridge_h=8.6, endpin=0.0)
VIOLIN = dict(L=35.5, rib=3.2, arch=1.6, neck_len=13.0, head_len=9.0, board_w=3.4, bridge_h=3.2, endpin=0.0)
CELLO_LEAN = 14.0
CELLO_REST = 13.0


def cello_on_stand():
    """The cello in its stand beside the music stand: a wooden stand of two crossed feet, a padded
    cradle the body's lower edge rests in and a post up behind it to a yoke under the neck. The cello
    leans back on it at CELLO_LEAN. Faces +Y."""
    cello, head_top = fiddle(kind="cello", **CELLO)
    lean = Matrix.Translation((0.0, 0.0, CELLO_REST)) @ Matrix.Rotation(math.radians(CELLO_LEAN), 4, "X")
    place(cello, lean)
    wood = Part("Mahogany", bevel=0.4)
    for yaw in (0.0, 90.0):
        cbox(wood, 0.0, 0.0, 2.0, 56.0, 5.0, 4.0, matrix=at(0.0, -6.0, 0.0, yaw=yaw))
    # The cradle: a U of padded arms under the lower bout.
    felt = Part("Felt", bevel=0.6)
    for side in (-1, 1):
        tube(wood, [(side * 4.0, -6.0, 4.0), (side * 16.0, 0.0, CELLO_REST - 2.0), (side * 20.0, 4.0, CELLO_REST + 4.0)], 1.4, sides=8)
        ellipsoid(felt, (side * 20.0, 4.0, CELLO_REST + 4.0), (2.4, 2.4, 2.4), rings=6, sides=10)
    # The post up behind the cello's back to the yoke under the top of the neck.
    neck_d = CELLO["L"] + CELLO["neck_len"] * 0.6
    # Its yoke under the back of the neck (the neck's back is ny - rib * 0.45 from the axis, fiddle()).
    neck_back = CELLO["rib"] * 0.5 + CELLO["arch"] * 0.6 - CELLO["rib"] * 0.45
    axis = lean @ Vector((0.0, neck_back - 1.6, neck_d))
    tube(wood, [(0.0, -6.0, 4.0), (0.0, axis.y - 3.0, axis.z * 0.5), (0.0, axis.y, axis.z - 2.0)], 1.8, sides=10)
    for side in (-1, 1):
        tube(wood, [(0.0, axis.y, axis.z - 2.0), (side * 4.5, axis.y + 3.0, axis.z + 1.0), (side * 5.0, axis.y + 6.0, axis.z + 4.0)], 1.0, sides=8)
        ellipsoid(felt, (side * 5.0, axis.y + 6.0, axis.z + 4.0), (1.4, 1.4, 1.4), rings=5, sides=8)
    return cello + [wood, felt]


def bow(length, stick_r, camber, frog_len):
    """A bow lying along +X from its frog (x = 0) to its tip: the stick cambered down towards the hair,
    the ebony frog with its pearl eye and silver ferrule, the button, the hair as a flat ribbon, the
    ivory-white tip plate. Lies on z = 0."""
    stick = Part("Varnish")
    pts = []
    for k in range(17):
        t = k / 16
        pts.append((-2.0 + t * (length + 2.0), 0.0, frog_len * 0.55 + camber * math.sin(t * math.pi) * -1.0 + stick_r))
    tube(stick, pts, [stick_r * (1.15 - 0.45 * k / 16) for k in range(17)], sides=8)
    # The head: a small hatchet the hair is fixed into.
    prism_xz(stick, [(length - 1.0, stick_r * 0.5), (length + 0.4, stick_r * 0.5), (length + 0.6, frog_len * 0.55 + stick_r * 3.0),
                     (length - 0.6, frog_len * 0.55 + stick_r * 2.6)], -stick_r * 0.9, stick_r * 0.9)
    bone = Part("Bone")
    cbox(bone, length + 0.45, 0.0, frog_len * 0.3 + stick_r, 0.3, stick_r * 1.6, frog_len * 0.55)
    ebony = Part("Ebony", bevel=0.15)
    cbox(ebony, frog_len * 0.5, 0.0, frog_len * 0.3, frog_len, stick_r * 2.2, frog_len * 0.6)
    lathe(ebony, [(0.0, 0.0), (stick_r * 1.1, 0.0), (stick_r * 1.1, 2.4), (0.0, 2.4)],
          at(-4.0, 0.0, frog_len * 0.55 + stick_r, pitch=90.0), sides=8)
    silver = Part("String")
    cbox(silver, frog_len * 0.08, 0.0, frog_len * 0.2, 0.5, stick_r * 2.3, frog_len * 0.45)
    ellipsoid(silver, (frog_len * 0.55, stick_r * 1.15, frog_len * 0.35), (0.35, 0.05, 0.35), rings=4, sides=8)
    hair = Part("Hair")
    cbox(hair, (frog_len + length) * 0.5, 0.0, 0.45, length - frog_len, stick_r * 1.4, 0.12)
    return [stick, bone, ebony, silver, hair]


def violin_case_open():
    """The violin's case lying open on the window bench: an oblong case in black leathercloth, hollow
    and lined in plush, the violin in it on its back with its scroll to -X, the accessory box at the
    tail end, the lid up past upright with its empty bow clips (the bow is on the chair). The lid is
    hinged along the back (-Y); the latches face +Y. 78 x 26 x 14 shut; origin on the bench under the
    middle."""
    L, W, H = 78.0, 26.0, 14.0
    wall, base, rim = 1.4, 2.4, 10.0
    leather = Part("Leather", bevel=0.5, segments=2)
    cbox(leather, 0.0, 0.0, base * 0.5, L, W, base)
    for sy in (-1, 1):
        cbox(leather, 0.0, sy * (W - wall) * 0.5, rim * 0.5, L, wall, rim)
    for sx in (-1, 1):
        cbox(leather, sx * (L - wall) * 0.5, 0.0, rim * 0.5, wall, W - 2 * wall, rim)
    plush = Part("Plush", bevel=0.2)
    cbox(plush, 0.0, 0.0, base + 0.3, L - 2 * wall, W - 2 * wall, 0.6)
    for sy in (-1, 1):
        cbox(plush, 0.0, sy * (W * 0.5 - wall - 0.2), (base + rim) * 0.5, L - 2 * wall, 0.4, rim - base)
    for sx in (-1, 1):
        cbox(plush, sx * (L * 0.5 - wall - 0.2), 0.0, (base + rim) * 0.5, 0.4, W - 2 * wall, rim - base)
    # The box for rosin and strings at the tail end, its lid shut.
    cbox(plush, L * 0.5 - wall - 7.0, W * 0.22, base + 3.0, 12.0, 8.0, 5.0)
    brass = Part("Brass")
    for x in (-L * 0.3, L * 0.3):
        cbox(brass, x, W * 0.5 + 0.2, rim - 1.6, 3.0, 0.6, 2.4)
    cbox(brass, 0.0, W * 0.5 + 0.6, rim - 3.0, 9.0, 1.2, 1.4)
    # The lid, drawn shut over the case with its hinge line on the X axis, then opened past upright.
    lid = Part("Leather", bevel=0.5, segments=2)
    lid_plush = Part("Plush", bevel=0.2)
    hinge = at(0.0, -W * 0.5, rim, roll=100.0)
    lid_h = H - rim
    start = count(lid)
    cbox(lid, 0.0, W * 0.5, lid_h - 0.7, L, W, 1.4)
    for sy in (0.7, W - 0.7):
        cbox(lid, 0.0, sy, lid_h * 0.5, L, 1.4, lid_h)
    for sx in (-1, 1):
        cbox(lid, sx * (L - 1.4) * 0.5, W * 0.5, lid_h * 0.5, 1.4, W, lid_h)
    transform_new(lid, start, hinge)
    start = count(lid_plush)
    cbox(lid_plush, 0.0, W * 0.5, lid_h - 1.6, L - 3.0, W - 3.0, 0.4)
    for x in (-18.0, 10.0, 26.0):
        for k in range(2):
            cbox(lid_plush, x, W * 0.5 - 4.0 + k * 7.0, lid_h - 2.4, 3.0, 1.2, 1.4)
    transform_new(lid_plush, start, hinge)
    # The violin, built standing (length up +Z, front +Y) and laid on its back: length to -X, front up.
    violin, _ = fiddle(kind="violin", **VIOLIN)
    lay = Matrix(((0.0, 0.0, -1.0, 0.0), (-1.0, 0.0, 0.0, 0.0), (0.0, 1.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0)))
    back = VIOLIN["rib"] * 0.5 + VIOLIN["arch"] * 0.9
    place(violin, Matrix.Translation((27.0, -1.0, base + 0.7 + back)) @ lay)
    return [leather, plush, brass, lid, lid_plush] + violin


def guitar_on_stand():
    """The classical guitar on its stand by the east wall: a body with a flat top and back, a sound
    hole and its rosette, the tie-block bridge, a fingerboard to the twelfth fret on the body, a slotted
    head with its tuner rollers, six strings; a folding A-frame stand. Leans back 16 degrees. Faces +Y."""
    L, rib = 49.0, 9.6
    zc = L * 0.5
    top = Part("Maple", bevel=0.2)
    ring = outline(guitar_half, L, 0.0, steps=44)
    loft(top, ring, [(1.0, rib * 0.5 - 0.3), (1.0, rib * 0.5)], zc)
    sides = Part("Rosewood", bevel=0.3)
    loft(sides, ring, [(1.0, -rib * 0.5), (1.0, rib * 0.5 - 0.3)], zc)
    dark = Part("Shadow")
    hole_z = L * 0.62
    lathe(dark, [(0.0, 0.0), (4.3, 0.0), (4.3, 0.06), (0.0, 0.06)], at(0.0, rib * 0.5 + 0.01, hole_z, roll=-90.0), sides=32)
    inlay = Part("Inlay")
    for r0, r1 in ((5.0, 5.9), (6.4, 6.7)):
        prof = [(r0, 0.0), (r1, 0.0), (r1, 0.06), (r0, 0.06)]
        lathe(inlay, prof, at(0.0, rib * 0.5 + 0.02, hole_z, roll=-90.0), sides=32)
    rosewood = Part("Rosewood", bevel=0.2)
    cbox(rosewood, 0.0, rib * 0.5 + 0.5, L * 0.22, 18.0, 1.0, 2.6)
    bone = Part("Bone")
    cbox(bone, 0.0, rib * 0.5 + 1.2, L * 0.22 + 0.4, 8.0, 0.5, 0.3)
    neck = Part("Mahogany", bevel=0.4)
    prism_xz(neck, [(-3.0, L - 2.0), (3.0, L - 2.0), (2.6, L + 30.0), (-2.6, L + 30.0)], -1.0, rib * 0.5 - 0.1)
    head = Part("Mahogany", bevel=0.3)
    cbox(head, 0.0, rib * 0.5 - 1.6, L + 30.0 + 9.0, 7.2, 1.8, 18.0, matrix=None)
    for side in (-1, 1):
        cbox(dark, side * 1.6, rib * 0.5 - 1.6, L + 30.0 + 9.0, 1.2, 1.9, 13.0)
        for k in range(3):
            tube(inlay, [(-4.2, rib * 0.5 - 1.6, L + 34.0 + k * 3.6), (4.2, rib * 0.5 - 1.6, L + 34.0 + k * 3.6)], 0.35, sides=6)
            lathe(inlay, [(0.0, 0.0), (0.9, 0.0), (0.9, 0.4), (0.0, 0.4)], at(side * 4.4, rib * 0.5 - 1.6, L + 34.0 + k * 3.6, pitch=90.0 * side), sides=8)
    ebony = Part("Ebony", bevel=0.1)
    prism_xz(ebony, [(-3.0, L * 0.72), (3.0, L * 0.72), (2.6, L + 30.0), (-2.6, L + 30.0)], rib * 0.5, rib * 0.5 + 0.6)
    strings = Part("String")
    for k in range(6):
        x = (k - 2.5) / 2.5
        tube(strings, [(x * 3.6, rib * 0.5 + 1.3, L * 0.22 + 0.5), (x * 2.2, rib * 0.5 + 0.9, L + 30.0)], 0.05 + 0.02 * (5 - k), sides=4)
    guitar = [top, sides, dark, inlay, rosewood, bone, neck, head, ebony, strings]
    rest = 11.0
    lean_deg = 16.0
    lean = Matrix.Translation((0.0, 0.0, rest)) @ Matrix.Rotation(math.radians(lean_deg), 4, "X")
    place(guitar, lean)
    stand = Part("Iron")
    foot = Part("Felt", bevel=0.4)
    back = lean @ Vector((0.0, -rib * 0.5 - 1.6, L + 22.0))
    for side in (-1, 1):
        tube(stand, [(side * 16.0, 14.0, 0.5), (side * 9.0, 2.0, rest + 2.0), (side * 3.0, back.y, back.z)], 0.9, sides=8)
        tube(stand, [(side * 10.0, 4.0, rest), (side * 12.0, 8.0, rest + 3.0)], 0.8, sides=8)
        ellipsoid(foot, (side * 12.0, 8.0, rest + 3.5), (2.0, 2.0, 1.4), rings=5, sides=8)
        ellipsoid(foot, (side * 16.0, 14.0, 0.6), (1.6, 2.4, 0.6), rings=4, sides=8)
    tube(stand, [(0.0, back.y, back.z), (0.0, back.y - 20.0, 0.5)], 0.9, sides=8)
    ellipsoid(foot, (0.0, back.y - 20.0, 0.6), (1.6, 2.4, 0.6), rings=4, sides=8)
    tube(stand, [(-3.5, back.y + 0.5, back.z), (3.5, back.y + 0.5, back.z)], 1.0, sides=8)
    return guitar + [stand, foot]


# ---------------------------------------------------------------------------------------------
# The stands and the metronome.
# ---------------------------------------------------------------------------------------------

def desk_frame(centre_z):
    """The desk's frame: (centre, normal, up) for a desk leaning back DESK_LEAN from upright."""
    b = math.radians(DESK_LEAN)
    return Vector((0.0, 0.0, centre_z)), Vector((0.0, math.cos(b), math.sin(b))), Vector((0.0, -math.sin(b), math.cos(b)))


def desk_matrix(centre_z):
    """Maps a desk drawn flat in XZ at y = 0 (centre at the origin) onto the leaning desk."""
    return Matrix.Translation((0.0, 0.0, centre_z)) @ Matrix.Rotation(math.radians(DESK_LEAN), 4, "X")


def music_stand_iron():
    """A Victorian cast-iron stand: three scrolled feet, a column with a collar, a lyre-shaped desk of
    flat bars with scrollwork in it and a ledge at its foot. Faces +Y; the desk at IRON_DESK_Z."""
    iron = Part("Iron")
    hub = 14.0
    for k in range(3):
        a = 2 * math.pi * k / 3 + math.pi / 2
        c, s = math.cos(a), math.sin(a)
        pts = [(c * 1.5, s * 1.5, hub)]
        for t in (0.25, 0.5, 0.75, 1.0):
            r = 30.0 * t
            pts.append((c * r, s * r, hub * (1.0 - t) ** 1.6 + 2.0 * math.sin(t * math.pi)))
        tube(iron, pts, [1.4, 1.2, 1.0, 0.9, 0.8], sides=8)
        curl = [(c * (30.0 + 3.0 * math.sin(q)), s * (30.0 + 3.0 * math.sin(q)), 3.0 - 3.0 * math.cos(q)) for q in [i * 0.5 for i in range(8)]]
        tube(iron, curl, 0.7, sides=6)
    lathe(iron, [(0.0, hub - 4.0), (2.6, hub - 4.0), (3.2, hub - 1.0), (2.2, hub + 2.0), (1.2, hub + 6.0), (1.0, IRON_DESK_Z - DESK_H * 0.5 - 4.0),
                 (1.0, IRON_DESK_Z - 7.0), (0.0, IRON_DESK_Z - 6.0)], Matrix.Identity(4), sides=12)
    cbox(iron, 0.0, -1.6, IRON_DESK_Z - 5.0, 3.0, 2.4, 6.0)
    lathe(iron, [(0.0, 64.0), (2.0, 64.0), (2.4, 66.0), (2.0, 68.0), (0.0, 68.0)], Matrix.Identity(4), sides=12)
    # The desk: drawn flat in XZ, then leant back. Outer bars, a lyre of two horns, scrolls between.
    desk = Part("Iron", bevel=0.2)
    m = desk_matrix(IRON_DESK_Z)
    start = count(desk)
    hw, hh = DESK_W * 0.5, DESK_H * 0.5
    for x in (-hw, hw):
        cbox(desk, x, -0.4, 0.0, 1.6, 0.8, DESK_H)
    for z in (-hh, hh):
        cbox(desk, 0.0, -0.4, z, DESK_W, 0.8, 1.6)
    for side in (-1, 1):
        horn = [(side * 3.0, -0.4, -hh + 1.0), (side * 12.0, -0.4, -hh + 6.0), (side * 15.0, -0.4, 0.0), (side * 10.0, -0.4, hh - 6.0),
                (side * 14.0, -0.4, hh - 1.0)]
        tube(desk, horn, 0.6, sides=6)
        spiral = [(side * (hw - 8.0 + math.cos(k / 11 * math.pi * 1.7) * 6.0 * (1 - k / 15)), -0.4, -4.0 + math.sin(k / 11 * math.pi * 1.7) * 6.0 * (1 - k / 15))
                  for k in range(12)]
        tube(desk, spiral, 0.45, sides=6)
    for k in range(5):
        x = -8.0 + k * 4.0
        cbox(desk, x, -0.4, 0.0, 0.5, 0.6, DESK_H - 4.0)
    # The ledge the music stands on, a lip along its front.
    cbox(desk, 0.0, 2.2, -hh - 0.8, DESK_W + 4.0, 5.0, 0.9)
    cbox(desk, 0.0, 4.6, -hh + 0.3, DESK_W + 4.0, 0.6, 2.2)
    transform_new(desk, start, m)
    return [iron, desk]


def music_stand_wood():
    """A wooden stand: three splayed turned legs, a turned column with a brass collar, a desk of
    slats in a frame with a ledge at its foot. Faces +Y; the desk at WOOD_DESK_Z."""
    wood = Part("Mahogany", bevel=0.3)
    hub = 26.0
    for k in range(3):
        a = 2 * math.pi * k / 3 + math.pi / 2
        c, s = math.cos(a), math.sin(a)
        tube(wood, [(c * 2.0, s * 2.0, hub), (c * 14.0, s * 14.0, hub * 0.45), (c * 26.0, s * 26.0, 0.8)], [1.8, 1.5, 1.2], sides=8)
        ellipsoid(wood, (c * 26.0, s * 26.0, 1.0), (2.0, 2.0, 1.0), rings=5, sides=8)
    lathe(wood, [(0.0, hub - 6.0), (3.0, hub - 6.0), (3.4, hub - 2.0), (2.0, hub + 2.0), (1.6, hub + 30.0), (2.4, hub + 34.0), (1.4, hub + 38.0),
                 (1.3, WOOD_DESK_Z - 8.0), (2.0, WOOD_DESK_Z - 7.0), (0.0, WOOD_DESK_Z - 6.0)],
          Matrix.Identity(4), sides=14)
    brass = Part("Brass")
    lathe(brass, [(0.0, 72.0), (2.2, 72.0), (2.4, 74.0), (2.2, 76.0), (0.0, 76.0)], Matrix.Identity(4), sides=14)
    tube(brass, [(2.2, 0.0, 74.0), (5.0, 0.0, 74.0)], 0.5, sides=6)
    cbox(wood, 0.0, -1.8, WOOD_DESK_Z - 5.0, 3.4, 2.6, 7.0)
    ellipsoid(brass, (5.6, 0.0, 74.0), (1.0, 1.0, 1.0), rings=5, sides=8)
    desk = Part("Mahogany", bevel=0.25)
    m = desk_matrix(WOOD_DESK_Z)
    start = count(desk)
    hw, hh = DESK_W * 0.5, DESK_H * 0.5
    for x in (-hw, hw):
        cbox(desk, x, -0.6, 0.0, 2.4, 1.2, DESK_H)
    for z in (-hh, hh):
        cbox(desk, 0.0, -0.6, z, DESK_W, 1.2, 2.4)
    for k in range(7):
        cbox(desk, -hw + 6.0 + k * (DESK_W - 12.0) / 6.0, -0.5, 0.0, 3.4, 0.8, DESK_H - 2.0)
    cbox(desk, 0.0, 2.4, -hh - 1.0, DESK_W + 2.0, 5.0, 1.2)
    cbox(desk, 0.0, 4.6, -hh + 0.4, DESK_W + 2.0, 0.8, 2.4)
    transform_new(desk, start, m)
    return [wood, brass, desk]


def metronome():
    """A pyramid metronome in mahogany, its front cover off: the brass scale behind the pendulum, the
    pendulum stopped off true with its weight high on the rod, the key in the side. 23 tall. Faces +Y."""
    wood = Part("Mahogany", bevel=0.3)
    bw, bd, tw, td, h = 11.0, 10.0, 3.6, 3.4, 22.0
    bm = wood.bm
    base = [bm.verts.new((x, y, 1.6)) for x, y in ((-bw / 2, -bd / 2), (bw / 2, -bd / 2), (bw / 2, bd / 2), (-bw / 2, bd / 2))]
    top = [bm.verts.new((x, y, h)) for x, y in ((-tw / 2, -td / 2), (tw / 2, -td / 2), (tw / 2, td / 2), (-tw / 2, td / 2))]
    bm.faces.new(base[::-1])
    bm.faces.new(top)
    for k in range(4):
        j = (k + 1) % 4
        bm.faces.new((base[k], base[j], top[j], top[k]))
    cbox(wood, 0.0, 0.0, 0.8, bw + 1.6, bd + 1.6, 1.6)
    lathe(wood, [(0.0, h), (1.6, h), (0.8, h + 1.0), (0.0, h + 1.4)], Matrix.Identity(4), sides=8)
    dark = Part("Shadow")
    # The front opened: a recess down its face, the scale in it.
    face = Vector((0.0, (bd + td) * 0.25, (1.6 + h) * 0.5))
    lean = math.degrees(math.atan2((bd - td) * 0.5, h - 1.6))
    cbox(dark, 0.0, 0.0, 0.0, 4.4, 0.2, h - 4.0, matrix=at(face.x, face.y + 0.06, face.z, roll=lean))
    brass = Part("Brass")
    cbox(brass, 0.0, 0.0, 0.0, 1.4, 0.15, h - 6.0, matrix=at(face.x, face.y + 0.2, face.z + 0.5, roll=lean))
    pend = Part("String")
    tilt = math.radians(lean)
    pivot = Vector((0.0, bd * 0.5 - (5.0 - 1.6) * math.tan(tilt) + 0.5, 5.0))
    stop = math.radians(11.0)
    end = pivot + Vector((math.sin(stop), -math.cos(stop) * math.sin(tilt), math.cos(stop) * math.cos(tilt))) * 18.5
    tube(pend, [tuple(pivot), tuple(end)], 0.12, sides=5)
    w = pivot + (end - pivot) * 0.72
    cbox(brass, 0.0, 0.0, 0.0, 1.4, 1.0, 1.6, matrix=at(w.x, w.y + 0.3, w.z, pitch=-11.0))
    lathe(brass, [(0.0, 0.0), (0.4, 0.0), (0.4, 1.8), (0.0, 1.8)], at(bw * 0.34, 0.0, 5.0, pitch=90.0), sides=6)
    cbox(brass, bw * 0.34 + 2.2, 0.0, 5.0, 0.3, 2.4, 1.6)
    return [wood, dark, brass, pend]


# ---------------------------------------------------------------------------------------------
# The chandelier.
# ---------------------------------------------------------------------------------------------

def octahedron(part, centre, rx, rz, yaw=0.0):
    """A cut bead: a double pyramid, faceted (its faces meet at more than the bake's sharp angle)."""
    bm = part.bm
    c = Vector(centre)
    ring = [bm.verts.new(c + Vector((math.cos(yaw + k * math.pi / 2) * rx, math.sin(yaw + k * math.pi / 2) * rx, 0.0))) for k in range(4)]
    top = bm.verts.new(c + Vector((0.0, 0.0, rz)))
    bot = bm.verts.new(c - Vector((0.0, 0.0, rz)))
    for k in range(4):
        j = (k + 1) % 4
        bm.faces.new((ring[k], ring[j], top))
        bm.faces.new((ring[j], ring[k], bot))


def pendeloque(part, top, length, width, yaw=0.0):
    """A drop: a flat faceted pear, widest a third of the way down, hung from its tip."""
    t = Vector(top)
    prof = [(0.0, 0.0), (width * 0.25, -length * 0.08), (width * 0.5, -length * 0.36), (width * 0.38, -length * 0.7), (0.0, -length)]
    lathe(part, prof, Matrix.Translation(t) @ Matrix.Rotation(yaw, 4, "Z") @ Matrix.Diagonal((1.0, 0.45, 1.0, 1.0)), sides=6)


def prism_drop(part, top, length, width, yaw=0.0):
    """A long six-sided prism with a pointed end: the chandelier's icicles."""
    t = Vector(top)
    lathe(part, [(0.0, 0.0), (width * 0.5, -width * 0.6), (width * 0.5, -length + width * 1.4), (0.0, -length)],
          Matrix.Translation(t) @ Matrix.Rotation(yaw, 4, "Z"), sides=6)


def chandelier():
    """A Victorian crystal chandelier, dead: a turned gilt stem, a crown of spires, six scrolled arms
    over eight, a candle cup and drip pan at the end of each with drops hung round it, swags of cut
    beads looping from pan to pan, a ring of long prisms and a cut ball at the foot. Some crystal has
    gone: drops missing here and there, one swag broken and hanging from one end, two arms without
    their candles. Hangs from z = 0 (the ring at the top); about 125 tall and 110 across."""
    rng = random.Random(1104)
    gilt = Part("Gilt", bevel=0.15)
    crystal = Part("Crystal")
    candle = Part("Candle")
    lathe(gilt, [(0.0, 0.0), (1.0, 0.0), (1.0, -1.5), (0.0, -1.5)], Matrix.Identity(4), sides=8)
    tube(gilt, [(-1.6, 0.0, 1.2), (0.0, 0.0, 3.4), (1.6, 0.0, 1.2)], 0.4, sides=6)
    stem = [(0.0, -1.5), (1.6, -1.5), (2.4, -4.0), (1.2, -7.0), (1.4, -14.0), (4.0, -17.0), (5.5, -20.0), (3.0, -24.0), (1.8, -30.0),
            (2.6, -36.0), (6.0, -40.0), (2.6, -44.0), (2.0, -54.0), (3.6, -60.0), (2.4, -66.0), (2.4, -72.0), (8.0, -76.0), (9.0, -79.0),
            (6.0, -82.0), (3.0, -86.0), (2.0, -92.0), (3.2, -97.0), (1.4, -102.0), (0.0, -104.0)]
    lathe(gilt, stem, Matrix.Identity(4), sides=20)
    # The crown: a ring of spires, each with a prism standing on it.
    for k in range(8):
        a = 2 * math.pi * k / 8
        c, s = math.cos(a), math.sin(a)
        tube(gilt, [(c * 2.0, s * 2.0, -18.0), (c * 10.0, s * 10.0, -16.0), (c * 13.0, s * 13.0, -12.0)], 0.35, sides=5)
        prism_drop(crystal, (c * 13.0, s * 13.0, -3.0), 9.0, 1.4, yaw=a)
    def arm(z_root, radius, z_end, a, droop):
        c, s = math.cos(a), math.sin(a)
        pts = []
        for i in range(11):
            t = i / 10
            r = 3.0 + (radius - 3.0) * t
            z = z_root + (z_end - z_root) * t - droop * math.sin(t * math.pi) + 4.0 * math.sin(t * math.pi * 2.0)
            pts.append((c * r, s * r, z))
        tube(gilt, pts, [0.9 - 0.25 * i / 10 for i in range(11)], sides=7)
        # A curl hung under the arm at its elbow.
        elbow = Vector(pts[4])
        curl = [tuple(elbow + Vector((c * math.sin(q) * 3.0, s * math.sin(q) * 3.0, -2.0 - 3.0 * (1.0 - math.cos(q))))) for q in [j * 0.6 for j in range(8)]]
        tube(gilt, curl, 0.35, sides=5)
        return Vector((c * radius, s * radius, z_end))

    pans = []
    for tier, (count_arms, z_root, radius, z_end, droop, offset) in enumerate(((8, -78.0, 50.0, -62.0, 7.0, 0.0), (6, -48.0, 30.0, -36.0, 4.0, math.pi / 6))):
        for k in range(count_arms):
            a = offset + 2 * math.pi * k / count_arms
            end = arm(z_root, radius, z_end, a, droop)
            # The candle cup, its drip pan, and a candle burnt to a stub (two have none left).
            lathe(gilt, [(0.0, -4.0), (1.4, -4.0), (2.0, -2.4), (1.2, -1.2), (1.6, 0.0), (5.2, 0.4), (5.4, 1.0), (1.8, 1.2), (1.8, 3.6), (0.0, 3.6)],
                  Matrix.Translation(end), sides=14)
            stub = rng.uniform(2.0, 7.0)
            if not (tier == 0 and k in (2, 5)):
                lathe(candle, [(0.0, 3.4), (1.3, 3.4), (1.3, 3.4 + stub), (0.9, 3.8 + stub), (0.0, 3.9 + stub)], Matrix.Translation(end), sides=8)
            pans.append((tier, a, end))
            # Drops round the pan, a few gone.
            for d in range(6):
                if rng.random() < 0.18:
                    continue
                da = a + 2 * math.pi * d / 6 + 0.26
                hook = end + Vector((math.cos(da) * 4.8, math.sin(da) * 4.8, 0.3))
                tube(gilt, [tuple(hook), tuple(hook - Vector((0.0, 0.0, 1.6)))], 0.08, sides=3)
                pendeloque(crystal, hook - Vector((0.0, 0.0, 1.6)), rng.uniform(5.0, 6.5), 2.6, yaw=da)

    # The swags: beads looping from each pan to the next on the same tier, sagging.
    def swag(p0, p1, sag, beads, broken=False):
        for b in range(beads):
            t = (b + 0.5) / beads
            if broken:
                # Hung from p0 only, the string fallen straight down from it.
                p = p0 + Vector((0.0, 0.0, -1.0 - t * (p1 - p0).length * 0.9))
            else:
                p = p0.lerp(p1, t) + Vector((0.0, 0.0, -sag * math.sin(t * math.pi) - 1.0))
            octahedron(crystal, p, 0.75, 1.0, yaw=b * 0.6)
        if broken:
            pendeloque(crystal, p0 + Vector((0.0, 0.0, -1.6 - (p1 - p0).length * 0.9)), 5.0, 2.4)

    lower = [p for p in pans if p[0] == 0]
    upper = [p for p in pans if p[0] == 1]
    for group, sag, beads in ((lower, 14.0, 15), (upper, 9.0, 10)):
        for k in range(len(group)):
            p0, p1 = group[k][2], group[(k + 1) % len(group)][2]
            swag(p0 + Vector((0.0, 0.0, 0.2)), p1 + Vector((0.0, 0.0, 0.2)), sag, beads, broken=(group is lower and k == 3))
    # Swags from the upper pans in to the stem's collar.
    for _, a, end in upper:
        collar = Vector((math.cos(a) * 6.0, math.sin(a) * 6.0, -40.0))
        for b in range(6):
            t = (b + 0.5) / 6
            octahedron(crystal, end.lerp(collar, t) + Vector((0.0, 0.0, -5.0 * math.sin(t * math.pi))), 0.7, 0.95, yaw=b)
    # The ring of long prisms under the lower tier, and the cut ball at the foot.
    ring_z = -86.0
    tube(gilt, [(math.cos(2 * math.pi * k / 24) * 24.0, math.sin(2 * math.pi * k / 24) * 24.0, ring_z) for k in range(25)], 0.45, sides=6)
    for k in range(4):
        a = 2 * math.pi * k / 4 + 0.4
        tube(gilt, [(math.cos(a) * 3.0, math.sin(a) * 3.0, -82.0), (math.cos(a) * 24.0, math.sin(a) * 24.0, ring_z)], 0.4, sides=5)
    for k in range(24):
        if k in (4, 5, 17):
            continue
        a = 2 * math.pi * (k + 0.5) / 24
        prism_drop(crystal, (math.cos(a) * 24.0, math.sin(a) * 24.0, ring_z - 0.5), 10.0 + 3.0 * (k % 2), 1.3, yaw=a)
    lathe(crystal, [(0.0, -102.0), (4.0, -104.0), (6.4, -108.0), (6.6, -111.0), (4.6, -116.0), (0.0, -118.0)], Matrix.Identity(4), sides=8)
    pendeloque(crystal, (0.0, 0.0, -118.0), 8.0, 3.4)
    return [gilt, crystal, candle]


def ceiling_rose():
    """The plaster rose the chandelier hangs from: rings of moulding, a wreath of sixteen leaves, a
    boss in the middle. 96 across, 9 deep; hangs from z = 0."""
    plaster = Part("Plaster", bevel=0.3)
    lathe(plaster, [(0.0, 0.0), (48.0, 0.0), (48.0, -1.2), (45.0, -2.0), (42.0, -2.0), (40.5, -3.2), (36.0, -3.6), (30.0, -2.6), (22.0, -2.8),
                    (17.0, -4.6), (12.0, -5.2), (9.0, -6.6), (6.0, -8.6), (0.0, -9.2)], Matrix.Identity(4), sides=48)
    leaves = Part("Plaster", bevel=0.4)
    for k in range(16):
        a = 2 * math.pi * k / 16
        ellipsoid(leaves, (math.cos(a) * 28.0, math.sin(a) * 28.0, -3.4), (8.0, 3.6, 1.6),
                  matrix=Matrix.Rotation(a, 4, "Z"), rings=5, sides=10)
        ellipsoid(leaves, (math.cos(a + 0.2) * 40.0, math.sin(a + 0.2) * 40.0, -2.4), (2.6, 2.6, 1.0), rings=4, sides=8)
    return [plaster, leaves]


def ceiling_bead():
    """A run of the ceiling's panel moulding, a bead between two fillets: one metre along X, laid end to
    end by the C++ and scaled to fit. Hangs from z = 0; 9 wide."""
    part = Part("Plaster", bevel=0.2)
    profile = [(-4.5, 0.0), (4.5, 0.0), (4.5, -1.0), (3.4, -1.4), (3.0, -2.6)]
    for k in range(7):
        a = math.pi * k / 6
        profile.append((2.2 * math.cos(a), -2.6 - 2.2 * math.sin(a)))
    profile += [(-3.0, -2.6), (-3.4, -1.4), (-4.5, -1.0)]
    bm = part.bm
    left = [bm.verts.new((-50.0, y, z)) for y, z in profile]
    right = [bm.verts.new((50.0, y, z)) for y, z in profile]
    bm.faces.new(left[::-1])
    bm.faces.new(right)
    for k in range(len(profile)):
        j = (k + 1) % len(profile)
        bm.faces.new((left[k], right[k], right[j], left[j]))
    return [part]


# ---------------------------------------------------------------------------------------------
# The arch.
# ---------------------------------------------------------------------------------------------

def spandrels(part, half, rise, spring, crown_top, y0, y1, steps=28):
    """The masonry over an elliptical head: the rectangle from the springing to crown_top, less the
    half-ellipse, as two pieces meeting at the crown."""
    for side in (-1, 1):
        arc = ellipse_pts(half, rise, spring, steps, 0.0 if side > 0 else math.pi, math.pi * 0.5)
        outline_pts = [(side * half, crown_top)] + arc
        if crown_top - (spring + rise) > 0.01:
            outline_pts.append((0.0, crown_top))
        if side < 0:
            outline_pts = outline_pts[::-1]
        prism_xz(part, outline_pts, y0, y1)


def music_arch():
    """The way into the music room from the stair hall's north gallery: a wide elliptical arch through
    the hall's north wall, lined in oak. The hall face (slot names kept from when it opened off the
    corridor: CorridorWall is the hall's plaster, Trim its staircase oak) has an architrave like the
    hall's archway; the room face a carved surround: fluted pilasters with capitals, an archivolt round
    the head, a scrolled keystone, acanthus in relief on the spandrels, all on a panelled board out to
    ARCH_SURROUND_HALF and up to ARCH_SURROUND_TOP. Origin at the opening's middle on the floor, in the
    middle of the wall; the hall at -Y, the room at +Y (AMusicRoomActor turns it round, the hall being
    at +Y there)."""
    half, rise, spring, crown = ARCH_HALF, ARCH_RISE, ARCH_SPRING, ARCH_CROWN
    hy = ARCH_WALL * 0.5
    core = Part("Shadow")
    spandrels(core, half, rise, spring, crown, -hy + 0.6, hy - 0.6)
    corridor = Part("CorridorWall")
    spandrels(corridor, half, rise, spring, crown, -hy - 0.4, -hy + 0.6)
    # The room face's board: the whole surround rectangle less the opening.
    board = Part("RoomWall")
    sh, st = ARCH_SURROUND_HALF, ARCH_SURROUND_TOP
    for side in (-1, 1):
        prism_xz(board, [(side * half, 0.0), (side * sh, 0.0), (side * sh, st), (side * half, st)][:: (1 if side > 0 else -1)], hy - 0.6, hy + 0.6)
    spandrels(board, half, rise, spring, st, hy - 0.6, hy + 0.6)
    # The lining: the jambs, and the soffit round the head through the wall.
    lining = Part("Oak", bevel=0.3)
    for side in (-1, 1):
        cbox(lining, side * (half - 1.0), 0.0, spring * 0.5, 2.0, ARCH_WALL + 1.6, spring)
    steps = 40
    arc_in = ellipse_pts(half - 2.0, rise - 2.0, spring, steps)
    arc_out = ellipse_pts(half + 0.2, rise + 0.2, spring, steps)
    bm = lining.bm
    near = [bm.verts.new((x, -hy - 0.8, z)) for x, z in arc_out] + [bm.verts.new((x, -hy - 0.8, z)) for x, z in reversed(arc_in)]
    far = [bm.verts.new((x, hy + 0.8, z)) for x, z in arc_out] + [bm.verts.new((x, hy + 0.8, z)) for x, z in reversed(arc_in)]
    n = len(near)
    for k in range(n):
        j = (k + 1) % n
        bm.faces.new((near[k], near[j], far[j], far[k]))
    for k in range(steps):
        a, b = k, k + 1
        c, d = n - 1 - (k + 1), n - 1 - k
        bm.faces.new((near[a], near[b], near[c], near[d]))
        bm.faces.new((far[d], far[c], far[b], far[a]))
    # The hall face's architrave, up each jamb, round the head and down, with plinth blocks and a key.
    path = ([(-half - 0.2, 0.0), (-half - 0.2, spring)] + ellipse_pts(half + 0.2, rise + 0.2, spring, 56, math.pi, 0.0)[1:-1]
            + [(half + 0.2, spring), (half + 0.2, 0.0)])
    trim = Part("Trim", bevel=0.2)
    width = 14.0
    moulding_path(trim, path, [(a, -hy - 0.2 - o) for a, o in architrave_profile(width, 3.2)])
    blocks = Part("Trim", bevel=0.4)
    for side in (-1, 1):
        cbox(blocks, side * (half + width * 0.5), -hy - 2.2, 12.0, width + 2.0, 4.4, 24.0)
    # The keystones stand on the head's soffit, not below it: hung four centimetres into the opening,
    # the room face's read from the hall as a dark block hanging in the top of the arch.
    prism_xz(blocks, [(-8.0, crown + 0.2), (8.0, crown + 0.2), (10.0, crown + 13.5), (-10.0, crown + 13.5)], -hy - 5.0, -hy)
    # The room face. Pilasters from the floor to the springing, standing on the board.
    oak = Part("Oak", bevel=0.25)
    dark = Part("Shadow")
    pw = sh - half - 4.0
    face = hy + 0.6
    for side in (-1, 1):
        cx = side * (half + 2.0 + pw * 0.5)
        cbox(oak, cx, face + 2.5, 11.0, pw + 3.0, 5.0, 22.0)
        cbox(oak, cx, face + 2.0, 24.0, pw + 1.6, 4.0, 4.0)
        cbox(oak, cx, face + 1.6, (26.0 + spring - 22.0) * 0.5, pw, 3.2, spring - 22.0 - 26.0)
        for f in range(4):
            fx = cx - pw * 0.3 + f * pw * 0.2
            cbox(dark, fx, face + 3.25, (34.0 + spring - 30.0) * 0.5, 1.3, 0.1, spring - 30.0 - 34.0)
        # The capital: a necking, a bell of leaves, a moulded abacus with a volute at each corner.
        cbox(oak, cx, face + 2.0, spring - 20.0, pw + 1.2, 4.0, 2.0)
        cbox(oak, cx, face + 2.6, spring - 11.0, pw + 2.0, 5.2, 16.0)
        for lx in (-0.3, 0.0, 0.3):
            ellipsoid(oak, (cx + lx * pw, face + 5.2, spring - 11.0), (pw * 0.14, 1.2, 6.0), rings=5, sides=8)
        cbox(oak, cx, face + 3.2, spring - 1.5, pw + 6.0, 6.4, 3.0)
        for vx in (-1, 1):
            lathe(oak, [(0.0, 0.0), (2.6, 0.0), (2.6, 5.0), (0.0, 5.0)], at(cx + vx * (pw * 0.5 + 1.0), face + 1.0, spring - 5.0, roll=-90.0), sides=12)
    # The archivolt round the head, from capital to capital.
    head = ellipse_pts(half + 0.4, rise + 0.4, spring, 56, math.pi, 0.0)
    moulding_path(oak, head, [(a, face + 0.2 + o) for a, o in architrave_profile(15.0, 3.6)])
    # The keystone: a scrolled console with a leaf down its face.
    prism_xz(oak, [(-9.0, crown + 0.2), (9.0, crown + 0.2), (12.0, crown + 26.0), (-12.0, crown + 26.0)], face, face + 6.5)
    ellipsoid(oak, (0.0, face + 6.8, crown + 8.0), (5.0, 1.2, 12.0), rings=6, sides=10)
    for vx in (-1, 1):
        lathe(oak, [(0.0, 0.0), (3.0, 0.0), (3.0, 6.0), (0.0, 6.0)], at(vx * 12.5, face + 0.5, crown + 21.0, roll=-90.0), sides=12)
    # Acanthus on the spandrels: a scroll curling out from the keystone each side.
    for side in (-1, 1):
        pts = []
        for k in range(30):
            t = k / 29
            a = t * math.pi * 2.6
            r = 26.0 * (1.0 - 0.75 * t)
            pts.append((side * (24.0 + t * 46.0 + math.cos(a) * r * 0.35), face + 1.0, crown + 22.0 + math.sin(a) * r * 0.4 + (1 - t) * 3.0))
        tube(oak, pts, [1.4 - 0.9 * k / 29 for k in range(30)], sides=6)
        for k in range(6):
            p = pts[3 + k * 4]
            ellipsoid(oak, (p[0], face + 1.2, p[2] + 3.0), (3.2, 0.8, 1.6), matrix=Matrix.Rotation(side * 0.6, 4, "Y"), rings=4, sides=8)
    sill = Part("Oak", bevel=0.4)
    cbox(sill, 0.0, 0.0, 1.0, half * 2.0, ARCH_WALL + 4.0, 2.0)
    return [core, corridor, board, lining, trim, blocks, oak, dark, sill]


# ---------------------------------------------------------------------------------------------
# Furniture.
# ---------------------------------------------------------------------------------------------

def panel_door(part, dark, x0, x1, z0, z1, y, raised=True):
    """A frame-and-panel door face from x0..x1, z0..z1 standing at y: stiles, rails, a raised field."""
    w, h = x1 - x0, z1 - z0
    cbox(part, (x0 + x1) * 0.5, y, (z0 + z1) * 0.5, w, 2.0, h)
    st = min(6.0, w * 0.16)
    cbox(dark, (x0 + x1) * 0.5, y + 1.05, (z0 + z1) * 0.5, w - st * 2, 0.1, h - st * 2)
    if raised:
        cbox(part, (x0 + x1) * 0.5, y + 1.3, (z0 + z1) * 0.5, w - st * 2 - 2.0, 0.8, h - st * 2 - 2.0)


def instrument_cabinet():
    """The tall instrument cupboard on the west wall: a panelled cupboard to the counter, two glazed
    doors over it on three shelves, a moulded cornice. The left glazed door is shut; the right one is
    music_cabinet_door, open, and here an empty frame. Inside: a viola's case and a flute's on the
    counter, scores stacked on the shelf over them, a box of strings, a recorder in its case at the top.
    CABINET_W x CABINET_D x CABINET_H; back on the wall plane at y = -CABINET_D / 2. Faces +Y."""
    W, D, H = CABINET_W, CABINET_D, CABINET_H
    wood = Part("Mahogany", bevel=0.4)
    dark = Part("Shadow")
    cbox(wood, 0.0, 0.0, 5.0, W, D - 2.0, 10.0)
    for x in (-W * 0.5 + 1.5, W * 0.5 - 1.5):
        cbox(wood, x, 0.0, H * 0.5, 3.0, D, H - 16.0)
    cbox(wood, 0.0, -D * 0.5 + 1.0, H * 0.5, W, 2.0, H - 16.0)
    cbox(wood, 0.0, 0.5, 96.0, W + 2.0, D + 1.0, 3.0)
    for z in (100.0, 140.0, 178.0):
        cbox(wood, 0.0, -2.0, z - 1.0, W - 6.0, D - 10.0, 2.0)
    cbox(wood, 0.0, 0.0, H - 14.0, W, D, 2.0)
    # The cornice: a cove and two steps.
    cbox(wood, 0.0, 1.0, H - 10.0, W + 4.0, D + 2.0, 6.0)
    cbox(wood, 0.0, 2.0, H - 4.5, W + 8.0, D + 4.0, 5.0)
    cbox(wood, 0.0, 2.5, H - 1.0, W + 9.0, D + 5.0, 2.0)
    # The lower doors, shut.
    for side in (-1, 1):
        panel_door(wood, dark, side * 1.0 if side > 0 else -W * 0.5 + 3.0, W * 0.5 - 3.0 if side > 0 else -1.0, 12.0, 93.0, D * 0.5 - 1.0)
    brass = Part("Brass")
    for x in (-4.0, 4.0):
        ellipsoid(brass, (x, D * 0.5 + 1.0, 70.0), (1.0, 1.4, 1.0), rings=5, sides=8)
    # The left glazed door, shut: its frame, glazing bars, the pane.
    glass = Part("Glass")
    x0, x1 = -W * 0.5 + 3.0, -1.0
    z0, z1 = 100.0, H - 16.0
    y = D * 0.5 - 2.0
    for x in (x0 + 2.5, x1 - 2.5):
        cbox(wood, x, y, (z0 + z1) * 0.5, 5.0, 2.4, z1 - z0)
    for z in (z0 + 2.5, z1 - 2.5):
        cbox(wood, (x0 + x1) * 0.5, y, z, x1 - x0, 2.4, 5.0)
    for k in (1, 2):
        cbox(wood, (x0 + x1) * 0.5, y, z0 + (z1 - z0) * k / 3, x1 - x0 - 8.0, 1.4, 1.4)
    cbox(wood, (x0 + x1) * 0.5, y, (z0 + z1) * 0.5, 1.4, 1.4, z1 - z0 - 8.0)
    cbox(glass, (x0 + x1) * 0.5, y - 0.3, (z0 + z1) * 0.5, x1 - x0 - 8.0, 0.3, z1 - z0 - 8.0)
    ellipsoid(brass, (x1 - 3.0, y + 1.6, 150.0), (0.9, 1.2, 0.9), rings=5, sides=8)
    # The insides: the back lined in old green baize.
    felt = Part("Felt")
    cbox(felt, 0.0, -D * 0.5 + 2.2, (100.0 + H - 16.0) * 0.5, W - 6.0, 0.4, H - 16.0 - 100.0)
    leather = Part("Leather", bevel=1.0, segments=2)
    # A viola's case, shaped like a long coffin, lying on the counter; a flute's in front of it.
    vb = leather.bm
    shape = [(-36.0, -6.0), (-30.0, -10.0), (10.0, -12.0), (36.0, -9.0), (36.0, 9.0), (10.0, 12.0), (-30.0, 10.0), (-36.0, 6.0)]
    prism(leather, [(x * 0.9, y * 0.9) for x, y in shape], 100.0, 112.0, matrix=at(-14.0, -8.0, 0.0))
    cbox(leather, 18.0, 10.0, 103.0, 40.0, 8.0, 6.0)
    cbox(brass, 18.0, 14.2, 103.0, 2.0, 0.4, 1.4)
    # Scores stacked on the middle shelf, a box of strings beside them.
    paper = Part("Card", bevel=0.1)
    zz = 140.0
    for k in range(7):
        t = random.Random(30 + k).uniform(0.6, 2.2)
        cbox(paper, -24.0 + random.Random(40 + k).uniform(-2, 2), -4.0, zz + t * 0.5, 31.0, 24.0, t, matrix=at(0.0, 0.0, 0.0, yaw=random.Random(50 + k).uniform(-5, 5)))
        zz += t
    cbox(wood, 24.0, -2.0, 144.0, 16.0, 12.0, 8.0)
    # The recorder's case at the top, and two thin books leant against the side.
    cbox(leather, 10.0, -4.0, 181.0, 36.0, 6.0, 5.0)
    for k in range(3):
        cbox(paper, -W * 0.5 + 8.0 + k * 2.2, -6.0, 192.0, 1.8, 24.0, 28.0, matrix=at(0.0, 0.0, 0.0, roll=0.0))
    return [wood, dark, brass, glass, felt, leather, paper]


def cabinet_door():
    """The right-hand glazed door of the instrument cabinet, on its own so it can stand open: origin
    on its hinge (the right-hand edge, the bottom), the door running along -X; it opens by a negative
    yaw in its own frame."""
    wood = Part("Mahogany", bevel=0.4)
    glass = Part("Glass")
    brass = Part("Brass")
    w = CABINET_W * 0.5 - 4.0
    h = CABINET_H - 16.0 - 100.0
    for x in (-2.5, -w + 2.5):
        cbox(wood, x, 0.0, h * 0.5, 5.0, 2.4, h)
    for z in (2.5, h - 2.5):
        cbox(wood, -w * 0.5, 0.0, z, w, 2.4, 5.0)
    for k in (1, 2):
        cbox(wood, -w * 0.5, 0.0, h * k / 3, w - 8.0, 1.4, 1.4)
    cbox(wood, -w * 0.5, 0.0, h * 0.5, 1.4, 1.4, h - 8.0)
    cbox(glass, -w * 0.5, -0.3, h * 0.5, w - 8.0, 0.3, h - 8.0)
    ellipsoid(brass, (-w + 3.0, 1.6, 50.0), (0.9, 1.2, 0.9), rings=5, sides=8)
    for z in (12.0, h - 12.0):
        cbox(brass, 0.6, 0.0, z, 1.0, 2.8, 6.0)
    return [wood, glass, brass]


def display_shelf():
    """The low open shelf of music on the west wall: two scrolled sides, three boards, a top with a
    gallery rail along its back. SHELF_W x SHELF_D x SHELF_TOP; back on the wall plane. Faces +Y."""
    wood = Part("Mahogany", bevel=0.4)
    W, D = SHELF_W, SHELF_D
    for side in (-1, 1):
        prism_xz(wood, [(side * (W * 0.5 - 2.5), 0.0), (side * W * 0.5, 0.0), (side * W * 0.5, SHELF_TOP), (side * (W * 0.5 - 2.5), SHELF_TOP)][:: side],
                 -D * 0.5, D * 0.5)
        # The scroll at the front of each side's foot.
        tube(wood, [(side * (W * 0.5 - 1.2), D * 0.5 - 2.0 + 3.0 * math.sin(q), 6.0 + 5.0 * math.cos(q)) for q in [k * 0.55 for k in range(10)]], 1.2, sides=6)
    cbox(wood, 0.0, -D * 0.5 + 0.8, SHELF_TOP * 0.5, W - 4.0, 1.6, SHELF_TOP)
    for z in SHELF_BOARDS:
        cbox(wood, 0.0, 0.0, z - 1.2, W - 4.0, D - 1.0, 2.4)
    cbox(wood, 0.0, 0.6, 4.0, W - 4.0, D - 2.0, 8.0)
    cbox(wood, 0.0, 1.0, SHELF_TOP - 1.4, W + 3.0, D + 3.0, 2.8)
    cbox(wood, 0.0, -D * 0.5 + 1.0, SHELF_TOP + 4.0, W, 2.0, 8.0)
    for side in (-1, 1):
        lathe(wood, [(0.0, 0.0), (1.6, 0.0), (1.6, 6.0), (2.2, 8.0), (0.0, 10.0)], at(side * (W * 0.5 - 1.5), -D * 0.5 + 1.0, SHELF_TOP), sides=10)
    return [wood]


def book_row(length, seed):
    """A row of thin paper-bound volumes standing on z = 0, spines along y = 0 facing +Y, from x = 0 to
    x = length; each with its spine from the atlas, its cover's colour on its boards, the leaves at its
    top and fore-edge."""
    rng = random.Random(seed)
    part = Part("Books")
    part.uv_fn = no_uvs
    x = 0.0
    while x < length - 0.8:
        t = min(rng.uniform(0.7, 2.6), length - x)
        if t < 0.6:
            break
        h = rng.choice([31.0, 31.0, 30.5, 29.5, 27.0])
        d = 23.5 if h > 29 else 21.0
        cell = rng.randrange(0, 32)
        binding = SPINE_BINDING[cell]
        su = cell * SPINE_W
        cu = COVERS_X + binding * COVER_W
        lu, lv = LEAVES[0] + rng.uniform(0, 400), LEAVES[1] + rng.uniform(0, 200)
        rects = {"py": (su, 0.0, su + SPINE_W, SPINE_H), "ny": (lu, lv, lu + 40.0, lv + 20.0), "px": (cu + 2, 512.0, cu + COVER_W - 2, 768.0),
                 "nx": (cu + 2, 512.0, cu + COVER_W - 2, 768.0), "pz": (lu, lv, lu + 60.0, lv + 10.0), "nz": (lu, lv, lu + 60.0, lv + 10.0)}
        uv_box(part, x, x + t, -d, 0.0, 0.0, h, rects)
        x += t + rng.uniform(0.0, 0.15)
    return [part]


def books_leaning():
    """Four volumes at the end of a row, gone over against the side of the shelf: 24 along x, leaning
    to +X at 18 degrees, resting on z = 0."""
    rng = random.Random(77)
    part = Part("Books")
    part.uv_fn = no_uvs
    x = 0.0
    for k in range(4):
        t = rng.uniform(1.0, 2.2)
        cell = rng.randrange(0, 32)
        binding = SPINE_BINDING[cell]
        su = cell * SPINE_W
        cu = COVERS_X + binding * COVER_W
        rects = {"py": (su, 0.0, su + SPINE_W, SPINE_H), "ny": (10, 520, 50, 540), "px": (cu + 2, 512.0, cu + COVER_W - 2, 768.0),
                 "nx": (cu + 2, 512.0, cu + COVER_W - 2, 768.0), "pz": (20, 600, 80, 610), "nz": (20, 600, 80, 610)}
        lean = math.radians(18.0)
        uv_box(part, 0.0, t, -23.5, 0.0, 0.0, 31.0, rects, matrix=Matrix.Translation((x, 0.0, 0.0)) @ Matrix.Rotation(lean, 4, "Y"))
        x += t / math.cos(lean) + 0.05
    return [part]


def books_pile(seed, n):
    """Scores lying in a pile on the floor or a shelf, each turned a little off the one under it, the
    top one in the quartet's ochre paper cover from the paper atlas."""
    rng = random.Random(seed)
    part = Part("Books")
    part.uv_fn = no_uvs
    z = 0.0
    for k in range(n):
        t = rng.uniform(0.6, 1.8)
        cell = rng.randrange(0, 32)
        cu = COVERS_X + SPINE_BINDING[cell] * COVER_W
        su = cell * SPINE_W
        rects = {"py": (su, 0.0, su + SPINE_W, SPINE_H), "ny": (10, 520, 50, 540), "px": (cu + 2, 512.0, cu + COVER_W - 2, 768.0),
                 "nx": (cu + 2, 512.0, cu + COVER_W - 2, 768.0), "pz": (cu + 2, 512.0, cu + COVER_W - 2, 768.0), "nz": (cu + 2, 512.0, cu + COVER_W - 2, 768.0)}
        # Lying down: the spine to +Y still, the book's height along X.
        m = at(rng.uniform(-1.5, 1.5), rng.uniform(-1.5, 1.5), z, yaw=rng.uniform(-9.0, 9.0)) @ Matrix.Rotation(math.radians(90.0), 4, "Y")
        uv_box(part, -t, 0.0, -23.5, 0.0, -15.5, 15.5, rects, matrix=m)
        z += t
    cover = Part("Paper")
    cover.uv_fn = no_uvs
    w, h = 23.5, 31.0
    uv_quad(cover, [(h * 0.5, 0.0, z + 0.05), (h * 0.5, -w, z + 0.05), (-h * 0.5, -w, z + 0.05), (-h * 0.5, 0.0, z + 0.05)], PAPER["cover"])
    return [part, cover]


def sideboard():
    """The sideboard on the east wall, under the awards: a mahogany credenza on a plinth, three
    panelled doors with two drawers over them, a moulded top and a low gallery along the back.
    SIDEBOARD_W x SIDEBOARD_D, top at SIDEBOARD_TOP; back on the wall plane. Faces +Y."""
    W, D, T = SIDEBOARD_W, SIDEBOARD_D, SIDEBOARD_TOP
    wood = Part("Mahogany", bevel=0.4)
    dark = Part("Shadow")
    brass = Part("Brass")
    cbox(wood, 0.0, 0.0, 4.0, W - 4.0, D - 4.0, 8.0)
    cbox(wood, 0.0, -1.0, (8.0 + T - 3.0) * 0.5, W - 4.0, D - 6.0, T - 3.0 - 8.0)
    cbox(wood, 0.0, 1.0, T - 1.5, W + 3.0, D + 2.0, 3.0)
    cbox(wood, 0.0, -D * 0.5 + 1.5, T + 6.0, W - 4.0, 2.0, 12.0)
    for side in (-1, 1):
        lathe(wood, [(0.0, 0.0), (1.8, 0.0), (1.8, 10.0), (2.6, 12.0), (0.0, 15.0)], at(side * (W * 0.5 - 4.0), -D * 0.5 + 1.5, T), sides=10)
    dw = (W - 8.0) / 3.0
    front = D * 0.5 - 2.0
    for k in range(3):
        x0 = -W * 0.5 + 4.0 + k * dw
        panel_door(wood, dark, x0 + 0.6, x0 + dw - 0.6, 10.0, T - 24.0, front)
        ellipsoid(brass, (x0 + dw * (0.85 if k == 0 else 0.15 if k == 2 else 0.5), front + 2.0, T - 40.0), (0.9, 1.2, 0.9), rings=5, sides=8)
    for k in range(2):
        x0 = -W * 0.5 + 4.0 + k * (W - 8.0) / 2.0
        cbox(wood, x0 + (W - 8.0) / 4.0, front + 0.2, T - 13.5, (W - 8.0) / 2.0 - 1.2, 2.0, 15.0)
        cbox(dark, x0 + (W - 8.0) / 4.0, front + 1.25, T - 13.5, (W - 8.0) / 2.0 - 8.0, 0.1, 9.0)
        cbox(brass, x0 + (W - 8.0) / 4.0, front + 1.9, T - 13.5, 10.0, 1.0, 1.4)
    return [wood, dark, brass]


def side_table():
    """The small table between the armchairs: an oval two-tier table on four slender cabriole legs.
    56 x 42, top at SIDE_TABLE_TOP."""
    wood = Part("Mahogany", bevel=0.3)
    m = Matrix.Diagonal((1.0, 0.75, 1.0, 1.0))
    lathe(wood, [(0.0, SIDE_TABLE_TOP - 2.6), (27.0, SIDE_TABLE_TOP - 2.6), (28.0, SIDE_TABLE_TOP - 1.4), (28.0, SIDE_TABLE_TOP - 0.6),
                 (27.2, SIDE_TABLE_TOP), (0.0, SIDE_TABLE_TOP)], m, sides=40)
    lathe(wood, [(0.0, 20.0), (22.0, 20.0), (22.0, 22.0), (0.0, 22.0)], m, sides=36)
    lathe(wood, [(0.0, SIDE_TABLE_TOP - 7.0), (25.0, SIDE_TABLE_TOP - 7.0), (25.0, SIDE_TABLE_TOP - 2.6), (0.0, SIDE_TABLE_TOP - 2.6)], m, sides=36)
    for k in range(4):
        a = 2 * math.pi * k / 4 + math.pi / 4
        c, s = math.cos(a), math.sin(a) * 0.75
        tube(wood, [(c * 19.0, s * 19.0, SIDE_TABLE_TOP - 6.0), (c * 20.0, s * 20.0, 40.0), (c * 18.0, s * 18.0, 14.0), (c * 22.0, s * 22.0, 1.0)],
             [1.8, 1.4, 1.1, 1.3], sides=8)
    return [wood]


def window_bench():
    """The bench under the west window: a carved mahogany frame on four turned legs with stretchers, its
    seat stuffed and buttoned in the curtains' green velvet. BENCH_L x BENCH_D, the seat at BENCH_TOP."""
    wood = Part("Mahogany", bevel=0.3)
    L, D, T = BENCH_L, BENCH_D, BENCH_TOP
    for sx in (-1, 1):
        for sy in (-1, 1):
            lathe(wood, [(0.0, 0.0), (1.8, 0.0), (2.2, 3.0), (1.6, 8.0), (2.6, 14.0), (1.8, 22.0), (2.0, 32.0), (2.4, 34.0), (2.4, T - 6.0), (0.0, T - 6.0)],
                  at(sx * (L * 0.5 - 4.0), sy * (D * 0.5 - 4.0), 0.0), sides=12)
    cbox(wood, 0.0, 0.0, T - 9.0, L - 2.0, D - 2.0, 8.0)
    for sy in (-1, 1):
        tube(wood, [(-L * 0.5 + 4.0, sy * (D * 0.5 - 4.0), 12.0), (L * 0.5 - 4.0, sy * (D * 0.5 - 4.0), 12.0)], 1.1, sides=8)
    tube(wood, [(0.0, -D * 0.5 + 4.0, 12.0), (0.0, D * 0.5 - 4.0, 12.0)], 1.1, sides=8)
    # A carved apron: a shallow scalloped rail along each long side.
    for sy in (-1, 1):
        pts = [(-L * 0.5 + 6.0, T - 13.0)]
        for k in range(21):
            t = k / 20
            pts.append((-L * 0.5 + 6.0 + t * (L - 12.0), T - 13.0 - 3.0 * abs(math.sin(t * math.pi * 3.0))))
        pts += [(L * 0.5 - 6.0, T - 5.0), (-L * 0.5 + 6.0, T - 5.0)]
        prism_xz(wood, pts[::-1] if sy < 0 else pts, sy * (D * 0.5 - 1.0) - 0.6, sy * (D * 0.5 - 1.0) + 0.6)
    velvet = Part("Velvet", bevel=2.2, segments=3)
    cbox(velvet, 0.0, 0.0, T - 2.6, L - 1.0, D - 1.0, 5.4)
    buttons = Part("Velvet")
    for kx in range(5):
        for ky in range(2):
            ellipsoid(buttons, (-L * 0.36 + kx * L * 0.18 + (ky * L * 0.09), -D * 0.2 + ky * D * 0.4, T + 0.05), (0.9, 0.9, 0.5), rings=4, sides=8)
    return [wood, velvet, buttons]


def medal_box():
    """A shadow box on the east wall with her medal in it: a gilt frame, a dark velvet ground, the medal
    on its striped ribbon. Origin on the wall plane at the middle; front +Y. 26 x 32."""
    parts = frame(20.0, 26.0, 3.0, 5.0, slot="Gilt", back=False)
    ground = Part("Plush")
    cbox(ground, 0.0, 0.5, 0.0, 21.0, 1.0, 27.0)
    ribbon = Part("Ribbon")
    prism_xz(ribbon, [(-3.0, 9.0), (3.0, 9.0), (3.0, -1.0), (0.0, -3.0), (-3.0, -1.0)], 1.0, 1.4)
    medal = Part("Medal", bevel=0.2)
    lathe(medal, [(0.0, 0.0), (3.6, 0.0), (3.8, 0.3), (3.8, 0.5), (3.4, 0.6), (0.0, 0.7)], at(0.0, 1.4, -6.5, roll=-90.0), sides=24)
    return parts + [ground, ribbon, medal]


def photo_frame(key, art_w, art_h, slot="Gilt"):
    """A standing photograph frame for the sideboard: a narrow frame leaning back on its easel, the
    print in it from the paper atlas. Origin on the table under its front edge's middle; faces +Y."""
    lean = math.radians(10.0)
    m = Matrix.Translation((0.0, 0.0, 0.0)) @ Matrix.Rotation(-lean, 4, "X")
    border = 2.0
    ow, oh = art_w + border * 2, art_h + border * 2
    wood = Part(slot, bevel=0.25)
    start = count(wood)
    for sx in (-1, 1):
        cbox(wood, sx * (art_w * 0.5 + border * 0.5), -0.6, oh * 0.5, border, 1.2, oh)
    for z in (border * 0.5, oh - border * 0.5):
        cbox(wood, 0.0, -0.6, z, art_w, 1.2, border)
    cbox(wood, 0.0, -1.3, oh * 0.5, ow - 1.0, 0.3, oh - 1.0)
    transform_new(wood, start, m)
    photo = Part("Paper")
    photo.uv_fn = no_uvs
    start = count(photo)
    uv_quad(photo, [(-art_w * 0.5, -0.9, border), (art_w * 0.5, -0.9, border), (art_w * 0.5, -0.9, border + art_h), (-art_w * 0.5, -0.9, border + art_h)],
            PAPER[key], thickness=0.04)
    transform_new(photo, start, m)
    leg = Part(slot)
    tube(leg, [(0.0, -1.5, oh * 0.65), (0.0, -oh * 0.38, 0.3)], 0.5, sides=5)
    return [wood, photo, leg]


def dried_roses():
    """Roses left in their vase until they dried on the stem: nine stems gathered in the mouth, bowed
    and leaning out above it, the heads shrunk to dark crumpled knots, a few curled leaves. Origin at
    the vase's mouth; the stems go down 24 into it. Every stem passes the mouth within MOUTH of the
    middle and only leans out above it: fanned out from the bottom of the vase, they came out through
    its sides and the heads hung round its shoulder."""
    rng = random.Random(2104)
    stem = Part("Stem")
    petal = Part("Petal", smooth=True)
    leaf = Part("Stem")
    mouth = 1.8
    for k in range(9):
        a = rng.uniform(0, 2 * math.pi)
        lean = rng.uniform(0.25, 0.6)
        length = rng.uniform(19.0, 30.0)
        droop = rng.uniform(2.0, 7.0)
        r0 = mouth * rng.uniform(0.3, 1.0)
        pts = [(math.cos(a) * r0 * 0.4, math.sin(a) * r0 * 0.4, -24.0), (math.cos(a) * r0, math.sin(a) * r0, 0.0)]
        for i in range(1, 9):
            t = i / 8
            r = r0 + math.sin(lean) * length * t + droop * t ** 3 * 0.3
            pts.append((math.cos(a) * r, math.sin(a) * r, length * t * math.cos(lean) - droop * t ** 2.5))
        tube(stem, pts, 0.28, sides=5)
        head = Vector(pts[-1])
        bow = Vector((math.cos(a), math.sin(a), -0.6)).normalized()
        lumps = [(tuple(head + bow * 1.4), (1.7, 1.5, 1.9), None)]
        for q in range(6):
            u = q / 6 * 2 * math.pi + rng.uniform(-0.3, 0.3)
            off = Vector((math.cos(u) * 1.6, math.sin(u) * 1.6, rng.uniform(-0.6, 1.2)))
            lumps.append((tuple(head + bow * 1.6 + off), (rng.uniform(0.7, 1.2), rng.uniform(0.5, 0.9), rng.uniform(0.9, 1.4)),
                          Matrix.Rotation(u, 4, "Z")))
        blob(petal, lumps, resolution=0.35)
        for q in range(5):
            u = q / 5 * 2 * math.pi
            tip = head + Vector((math.cos(u) * 1.8, math.sin(u) * 1.8, -1.6))
            tube(leaf, [tuple(head), tuple(tip)], [0.35, 0.08], sides=4)
        for j in range(rng.randint(1, 2)):
            p = Vector(pts[3 + j * 2])
            side = Vector((-math.sin(a), math.cos(a), 0.0)) * (1 if j % 2 else -1)
            ellipsoid(leaf, tuple(p + side * 1.6), (2.2, 0.8, 0.25), matrix=Matrix.Rotation(a + 0.5, 4, "Z") @ Matrix.Rotation(0.6, 4, "X"), rings=4, sides=8)
    return [stem, petal, leaf]


def dried_hydrangea():
    """Hydrangea heads dried papery in the brass vase: four domes of florets on stiff stems, gathered
    in the narrow neck and leaning out only above it (see dried_roses). Origin at the vase's mouth."""
    rng = random.Random(3104)
    stem = Part("Stem")
    bloom = Part("Bloom", smooth=True)
    for k in range(4):
        a = k * math.pi / 2 + rng.uniform(-0.4, 0.4)
        lean = rng.uniform(0.2, 0.45)
        length = rng.uniform(16.0, 26.0)
        tip = Vector((math.cos(a) * math.sin(lean) * length, math.sin(a) * math.sin(lean) * length, length * math.cos(lean)))
        neck = Vector((math.cos(a) * 1.0, math.sin(a) * 1.0, 0.0))
        tube(stem, [(0.0, 0.0, -20.0), tuple(neck), tuple(neck + (tip - neck) * 0.4), tuple(tip)], 0.45, sides=5)
        # A head is a dome of small florets: sixteen big lumps read, up close, as a fist of fingers.
        elements = []
        for f in range(34):
            u = rng.uniform(0, 2 * math.pi)
            v = rng.uniform(0.0, 1.2)
            elements.append((tuple(tip + Vector((math.cos(u) * math.sin(v) * 6.0, math.sin(u) * math.sin(v) * 6.0, math.cos(v) * 3.8))), (1.25, 1.25, 0.9), None))
        blob(bloom, elements, resolution=0.45)
    return [stem, bloom]


def petals():
    """Petals fallen from the roses onto the sideboard: seven curled flakes within 30 of the origin."""
    rng = random.Random(4104)
    part = Part("Petal")
    for k in range(7):
        ellipsoid(part, (rng.uniform(-14.0, 14.0), rng.uniform(-8.0, 8.0), 0.25), (rng.uniform(0.9, 1.5), rng.uniform(0.7, 1.1), 0.22),
                  matrix=Matrix.Rotation(rng.uniform(0, math.pi), 4, "Z") @ Matrix.Rotation(rng.uniform(-0.4, 0.4), 4, "X"), rings=3, sides=8)
    return [part]


def open_score():
    """The orchestral score open on the iron stand: two card covers and the open pages, which curve
    down into the gutter. Drawn lying flat, pages up (+Z), its head towards +Y and its foot's middle at
    the origin: the C++ stands it on the desk's ledge. 54 x 34 open."""
    card = Part("Card", bevel=0.15)
    pw, ph = 26.0, 33.0
    for sx in (-1, 1):
        cbox(card, sx * (pw * 0.5 + 0.4), ph * 0.5, 0.15, pw + 0.8, ph + 0.8, 0.3)
    pages = Part("Paper")
    pages.uv_fn = no_uvs
    bm = pages.bm
    uv = bm.loops.layers.uv.verify()
    steps = 12
    for side, key in ((1, "book_l"), (-1, "book_r")):
        # Read from in front, the reader's left is +X: the left-hand page lies on the +X side.
        top_row, bot_row = [], []
        for i in range(steps + 1):
            t = i / steps
            x = side * (0.2 + t * (pw - 0.4))
            z = 0.5 + 1.0 * math.sin(min(t * 3.0, 1.0) * math.pi * 0.5) - 0.6 * t
            top_row.append((x, z))
        verts_front, verts_back = [], []
        for i, (x, z) in enumerate(top_row):
            s = 1.0 - i / steps if side > 0 else i / steps
            verts_front.append((bm.verts.new((x, 0.6, z)), bm.verts.new((x, ph - 0.6, z)), s))
            verts_back.append((bm.verts.new((x, 0.6, z - 0.08)), bm.verts.new((x, ph - 0.6, z - 0.08))))
        for i in range(steps):
            a, b = verts_front[i], verts_front[i + 1]
            quad = (a[0], b[0], b[1], a[1]) if side < 0 else (a[1], b[1], b[0], a[0])
            f = bm.faces.new(quad)
            coords = {a[0]: atlas_uv(PAPER[key], a[2], 1.0), b[0]: atlas_uv(PAPER[key], b[2], 1.0),
                      a[1]: atlas_uv(PAPER[key], a[2], 0.0), b[1]: atlas_uv(PAPER[key], b[2], 0.0)}
            for loop in f.loops:
                loop[uv].uv = coords[loop.vert]
            c, d = verts_back[i], verts_back[i + 1]
            f = bm.faces.new((c[1], d[1], d[0], c[0]) if side < 0 else (c[0], d[0], d[1], c[1]))
            edge = atlas_uv(PAPER[key], 0.02, 0.02)
            for loop in f.loops:
                loop[uv].uv = edge
            for e0, e1, e2, e3 in ((a[0], c[0], d[0], b[0]), (b[1], d[1], c[1], a[1])):
                f = bm.faces.new((e0, e1, e2, e3) if side < 0 else (e3, e2, e1, e0))
                for loop in f.loops:
                    loop[uv].uv = edge
        for i in (0, steps):
            a, c = verts_front[i], verts_back[i]
            quad = (a[0], a[1], c[1], c[0])
            if (i == 0) == (side > 0):
                quad = quad[::-1]
            f = bm.faces.new(quad)
            for loop in f.loops:
                loop[uv].uv = atlas_uv(PAPER[key], 0.02, 0.02)
    return [card, pages]


MODELS = [
    ("music_arch", music_arch),
    ("music_cello", cello_on_stand),
    ("music_violin_case", violin_case_open),
    ("music_violin_bow", lambda: bow(74.0, 0.42, 1.0, 3.2)),
    ("music_cello_bow", lambda: bow(71.0, 0.55, 1.1, 3.8)),
    ("music_guitar", guitar_on_stand),
    ("music_stand_iron", music_stand_iron),
    ("music_stand_wood", music_stand_wood),
    ("music_metronome", metronome),
    ("music_chandelier", chandelier),
    ("music_ceiling_rose", ceiling_rose),
    ("music_ceiling_bead", ceiling_bead),
    ("music_instrument_cabinet", instrument_cabinet),
    ("music_cabinet_door", cabinet_door),
    ("music_display_shelf", display_shelf),
    ("music_books_a", lambda: book_row(BOOK_ROWS[0], 1201)),
    ("music_books_b", lambda: book_row(BOOK_ROWS[1], 1202)),
    ("music_books_c", lambda: book_row(BOOK_ROWS[2], 1203)),
    ("music_books_leaning", books_leaning),
    ("music_books_pile", lambda: books_pile(1211, 6)),
    ("music_sideboard", sideboard),
    ("music_side_table", side_table),
    ("music_window_bench", window_bench),
    ("music_frame_cellist", lambda: frame(CELLIST_ART[0], CELLIST_ART[1], 10.0, 6.0)),
    ("music_frame_landscape", lambda: frame(LANDSCAPE_ART[0], LANDSCAPE_ART[1], 8.0, 5.0)),
    ("music_frame_cert_land", lambda: frame(CERT_LAND_ART[0], CERT_LAND_ART[1], 2.6, 2.4, slot="Ebony")),
    ("music_frame_cert_port", lambda: frame(CERT_PORT_ART[0], CERT_PORT_ART[1], 2.6, 2.4, slot="Ebony")),
    ("music_medal_box", medal_box),
    ("music_photo_a", lambda: photo_frame("photo_a", 12.0, 15.0)),
    ("music_photo_b", lambda: photo_frame("photo_b", 12.0, 15.0, slot="Ebony")),
    ("music_photo_c", lambda: photo_frame("photo_c", 15.0, 12.0)),
    ("music_dried_roses", dried_roses),
    ("music_dried_hydrangea", dried_hydrangea),
    ("music_petals", petals),
    ("music_open_score", open_score),
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
        print("MUSIC baked {} ({} verts, {:.1f} x {:.1f} x {:.1f} cm)".format(name, len(obj.data.vertices), dims.x * 100, dims.y * 100, dims.z * 100))

    big = [o for o in baked if o.dimensions.z > 0.5 or max(o.dimensions) > 1.2]
    small = [o for o in baked if o not in big]
    x = 0.0
    for obj in big:
        width = obj.dimensions.x
        obj.location = (x + width * 0.5, 0.0, 0.0 if obj.name not in ("music_chandelier", "music_ceiling_rose") else 2.2)
        x += width + 0.3
    for i, obj in enumerate(small):
        obj.location = (-1.0 - (i % 6) * 0.6, 3.0 + (i // 6) * 0.6, 0.0)
    K.preview(baked, PREVIEW_DIR, "models", [
        ("furniture", (x * 0.5, -12.0, 3.0), (x * 0.5, 0.0, 1.0)),
        ("furniture_left", (x * 0.2, -5.0, 1.8), (x * 0.2, 0.0, 1.0)),
        ("furniture_mid", (x * 0.5, -5.0, 1.8), (x * 0.5, 0.0, 1.0)),
        ("furniture_right", (x * 0.8, -5.0, 1.8), (x * 0.8, 0.0, 1.0)),
        ("small", (-2.5, 1.2, 2.0), (-2.5, 4.0, 0.0)),
    ])


main()
