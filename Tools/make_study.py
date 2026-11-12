"""
Builds the detective's study: its door, its furniture and the things on it, in Blender, headless,
and exports each as an FBX the art pipeline imports.

    blender --background --factory-startup --python Tools/make_study.py [-- --only=a,b]

The study is the room on the north side of the upstairs corridor, west of the bedroom: the man of
the house worked here. Black lacquer, burgundy leather and gold: a black partners' desk under a
burgundy leather top with gilt mounts, a tall buttoned chair behind it and two Chesterfield club
chairs in front, a Chesterfield sofa, a drinks cabinet, a safe, a card table, black bookcases, gilt
sconces with burgundy shades, and a black door with gilt beading off the corridor. His case board
is on the west wall.

Outputs Art/Source/Models/<name>/<name>.fbx with an empty GENERATED file beside it (re-imported
every run with its own normals), and Workbench previews in Saved/StudyPreview/.

Frame, as every prop in the house: centimetres, origin on the floor under the middle of the
footprint, the front facing local +Y. Wall-hung things (the board, the frames, the sconce) have
their origin on the wall plane at their middle. The door leaf has its origin on the hinge, the
leaf running along +Y and its corridor face to +X (AHallDoorActor's convention); the door case has
its origin at the foot of the opening's middle in the wall's middle plane, +Y into the study. The
numbers AStudyActor needs are the constants below, and must stay in step with StudyActor.h.

Material slots are set by name in C++ (AStudyActor, AHallDoorActor). Texel sizes match the surface
each is given:
    Lacquer     black_oak_veneer (RoomSurfaces::DarkOak, 100cm), tinted to black lacquer
    Leather     brown_leather (RoomSurfaces::Leather, 40cm), tinted burgundy
    Pictures    study_pictures, laid on the atlas by hand (make_study_art.py: PICTURES)
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
from blender_kit import Part, at, box, cbox, ellipsoid, lathe, tube  # noqa: E402
from make_bedroom import cushion, no_uvs  # noqa: E402

PREVIEW_DIR = os.path.join(K.ROOT, "Saved", "StudyPreview")

LACQUER = 100.0
LEATHER = 40.0
FLAT = 100.0

SLOTS = {
    "Lacquer": ((0.03, 0.03, 0.03, 1.0), LACQUER),
    "Leather": ((0.30, 0.04, 0.06, 1.0), LEATHER),
    "Gilt": ((0.75, 0.55, 0.20, 1.0), FLAT),
    "Steel": ((0.08, 0.08, 0.085, 1.0), FLAT),
    "Shadow": ((0.01, 0.01, 0.01, 1.0), FLAT),
    "Felt": ((0.30, 0.04, 0.06, 1.0), FLAT),
    "Glass": ((0.75, 0.80, 0.80, 1.0), FLAT),
    "Liquor": ((0.45, 0.22, 0.05, 1.0), FLAT),
    "Card": ((0.85, 0.83, 0.78, 1.0), FLAT),
    "CardBack": ((0.35, 0.05, 0.07, 1.0), FLAT),
    "ChipRed": ((0.45, 0.06, 0.06, 1.0), FLAT),
    "ChipBlack": ((0.04, 0.04, 0.04, 1.0), FLAT),
    "ChipIvory": ((0.80, 0.76, 0.66, 1.0), FLAT),
    "Cigar": ((0.25, 0.13, 0.06, 1.0), FLAT),
    "Ash": ((0.45, 0.44, 0.42, 1.0), FLAT),
    "Buff": ((0.65, 0.52, 0.32, 1.0), FLAT),
    "Paper": ((0.85, 0.82, 0.74, 1.0), FLAT),
    "Cardboard": ((0.50, 0.38, 0.24, 1.0), FLAT),
    "Shade": ((0.35, 0.05, 0.07, 1.0), FLAT),
    "Bulb": ((0.75, 0.72, 0.65, 1.0), FLAT),
    "Felt2": ((0.04, 0.035, 0.035, 1.0), FLAT),
    "Ribbon": ((0.25, 0.04, 0.05, 1.0), FLAT),
    "Pictures": ((0.60, 0.50, 0.40, 1.0), FLAT),
}

# Shared with make_study_art.py: PICTURES (the 2048 atlas).
ATLAS = 2048.0
PICTURES = {
    "board": (0, 0, 1280, 800),
    "portrait": (1280, 0, 512, 640),
    "map": (1280, 660, 768, 512),
    "folder": (0, 820, 384, 288),
    "photo_a": (420, 820, 192, 144),
    "photo_b": (640, 820, 192, 144),
}

# Shared with StudyActor.h.
DOOR_OPENING_W = 120.0     # the opening in the corridor's north wall
DOOR_OPENING_H = 250.0
DOOR_LEAF_W = 116.0        # the leaf: 2 short of each jamb
DOOR_LEAF_H = 248.0
DOOR_LEAF_T = 5.0
WALL_T = 20.0
CASE_WIDTH = 16.0          # the architrave each side of the opening
DESK_W = 230.0
DESK_D = 105.0
DESK_TOP = 78.0
BAR_W = 170.0
BAR_D = 52.0
BAR_TOP = 98.0
SAFE_W = 72.0
SAFE_D = 66.0
SAFE_H = 116.0
BOARD_W = 200.0
BOARD_H = 125.0
PORTRAIT_W = 92.0          # the canvas; the frame stands out round it by PORTRAIT_MOULD
PORTRAIT_H = 115.0
PORTRAIT_MOULD = 13.0
MAP_W = 120.0
MAP_H = 80.0
MAP_MOULD = 7.0
CARD_TABLE_R = 55.0
CARD_TABLE_TOP = 74.0
COFFEE_W = 120.0
COFFEE_D = 62.0
COFFEE_TOP = 44.0
CLUB_W = 98.0
SOFA_W = 224.0
CHESTER_D = 92.0
CHESTER_SEAT = 46.0
CHESTER_TOP = 74.0
CASE_W = 120.0             # a bookcase
CASE_D = 42.0
CASE_H = 262.0
CASE_SHELVES = (88.0, 130.0, 172.0, 214.0)   # the top of each board books stand on
CASE_INTERIOR = 112.0
COAT_STAND_H = 186.0
TRAY_TOP = 1.6


# ---------------------------------------------------------------------------------------------
# Helpers.
# ---------------------------------------------------------------------------------------------

def uv_quad(part, corners, name, thickness=0.05):
    """A picture's face: a closed thin box whose front shows one rectangle of the atlas; corners
    (bl, br, tr, tl) as seen from the front. The back goes the way (right x up) points, which in the
    game's left-handed frame is away from whoever looks at it (the 10-22 note)."""
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


def wall_picture(part, name, w, h, proud):
    """A picture on a wall-hung thing whose front faces +Y: centred on x = 0, z = 0, at y = proud."""
    uv_quad(part, ((-w / 2, proud, -h / 2), (w / 2, proud, -h / 2), (w / 2, proud, h / 2), (-w / 2, proud, h / 2)), name)


def bead_rect(part, x0, x1, z0, z1, y, radius=0.45, axis="y"):
    """A gilt bead run round a rectangle on a face: in XZ at depth y (axis 'y'), or in YZ at x (axis 'x')."""
    if axis == "y":
        pts = [(x0, y, z0), (x1, y, z0), (x1, y, z1), (x0, y, z1), (x0, y, z0)]
    else:
        pts = [(y, x0, z0), (y, x1, z0), (y, x1, z1), (y, x0, z1), (y, x0, z0)]
    for a, b in zip(pts, pts[1:]):
        tube(part, [a, b], radius, sides=8)
        ellipsoid(part, a, (radius, radius, radius), rings=4, sides=8)


def turned_foot(part, x, y, height, r=3.2):
    """A short turned bun foot."""
    lathe(part, [(0.0, 0.0), (r * 0.7, 0.0), (r, height * 0.35), (r * 0.8, height * 0.75), (r * 0.9, height), (0.0, height)], at(x, y, 0.0), sides=14)


# ---------------------------------------------------------------------------------------------
# The door.
# ---------------------------------------------------------------------------------------------

def door():
    """The study's door leaf: black lacquer, four raised and fielded panels on both faces with a gilt
    bead round every field, a gilt lever on a tall backplate each side, three gilt hinges on the
    study face. Hinge at the origin, the leaf along +Y, the corridor face to +X."""
    lacquer = Part("Lacquer", bevel=0.25)
    fields = Part("Lacquer", bevel=1.4, segments=2, angle=30.0)
    gilt = Part("Gilt")
    shadow = Part("Shadow")
    W, H, T = DOOR_LEAF_W, DOOR_LEAF_H, DOOR_LEAF_T
    box(lacquer, -T / 2, T / 2, 0.0, W, 0.0, H)
    stile, top_rail, lock_rail, bottom_rail = 15.0, 15.0, 22.0, 28.0
    lock_z = 104.0
    panels = []
    mid = W / 2
    for y0, y1 in ((stile, mid - 6.0), (mid + 6.0, W - stile)):
        panels.append((y0, y1, lock_z + lock_rail / 2, H - top_rail))
        panels.append((y0, y1, bottom_rail, lock_z - lock_rail / 2))
    for side in (1.0, -1.0):
        face = side * T / 2
        for y0, y1, z0, z1 in panels:
            # The field: raised in the middle, bevelled down to the frame round it.
            box(fields, min(face, face + side * 1.2), max(face, face + side * 1.2), y0 + 3.0, y1 - 3.0, z0 + 3.0, z1 - 3.0)
            # The sunk margin between field and frame, dark, and the gilt bead on the frame's edge.
            box(shadow, min(face, face + side * 0.15), max(face, face + side * 0.15), y0, y1, z0, z1)
            bead_rect(gilt, y0, y1, z0, z1, face + side * 0.35, radius=0.5, axis="x")
        # A gilt line round the leaf's edge a hand in from it.
        bead_rect(gilt, 5.0, W - 5.0, 5.0, H - 5.0, face + side * 0.3, radius=0.32, axis="x")
        # The lever on its backplate, the lever pointing back towards the hinge.
        hy = W - 9.0
        cbox(gilt, face + side * 0.45, hy, lock_z, 0.9, 5.4, 30.0)
        ellipsoid(gilt, (face + side * 1.3, hy, lock_z + 6.0), (1.4, 1.8, 1.8), rings=6, sides=12)
        tube(gilt, [(face + side * 0.9, hy, lock_z + 6.0), (face + side * 5.2, hy, lock_z + 6.0), (face + side * 6.2, hy - 2.0, lock_z + 6.0),
                    (face + side * 6.4, hy - 13.0, lock_z + 5.4)], [0.9, 0.9, 0.85, 0.8], sides=12)
        ellipsoid(gilt, (face + side * 6.4, hy - 13.4, lock_z + 5.3), (1.0, 1.4, 1.1), rings=6, sides=10)
        cbox(shadow, face + side * 1.0, hy, lock_z - 7.0, 0.4, 1.2, 3.4)
        ellipsoid(shadow, (face + side * 1.0, hy, lock_z - 5.0), (0.25, 1.0, 1.0), rings=4, sides=8)
    # Gilt hinges, their knuckles on the study face (-X).
    for hz in (24.0, H * 0.5, H - 28.0):
        cbox(gilt, -T / 2 - 0.2, 4.0, hz, 0.5, 8.0, 12.0)
        lathe(gilt, [(0.0, -7.0), (1.3, -7.0), (1.3, 7.0), (0.0, 7.0)], at(-T / 2 - 1.2, 0.0, hz), sides=12)
        ellipsoid(gilt, (-T / 2 - 1.2, 0.0, hz + 7.6), (1.3, 1.3, 0.9), rings=4, sides=10)
    return [lacquer, fields, gilt, shadow]


def door_case():
    """Round the study's door, through the wall: the reveal lined in black, an architrave on both
    faces with a gilt bead along its inner edge and plinth blocks at its foot, and over the head on
    each face a frieze with a gilt egg-and-dart cornice. On the corridor face the whole stays under
    the corridor's cornice (from 281). Origin at the foot of the opening's middle in the wall's
    middle plane; +Y is the study."""
    lacquer = Part("Lacquer", bevel=0.4)
    gilt = Part("Gilt")
    half = DOOR_OPENING_W / 2
    h = DOOR_OPENING_H
    t = WALL_T / 2
    # The lining, inside the opening.
    for sx in (-1, 1):
        box(lacquer, sx * half - (2.0 if sx > 0 else 0.0), sx * half + (0.0 if sx > 0 else 2.0), -t, t, 0.0, h)
    box(lacquer, -half, half, -t, t, h - 2.0, h)
    cw = CASE_WIDTH
    for face in (-1, 1):
        y0 = face * t
        y1 = face * (t + 3.0)
        lo, hi = min(y0, y1), max(y0, y1)
        for sx in (-1, 1):
            box(lacquer, min(sx * half, sx * (half + cw)), max(sx * half, sx * (half + cw)), lo, hi, 0.0, h + cw)
            # The plinth block: wider, deeper, to the dado of a skirting.
            box(lacquer, min(sx * (half - 0.5), sx * (half + cw + 1.5)), max(sx * (half - 0.5), sx * (half + cw + 1.5)),
                min(y0, face * (t + 4.2)), max(y0, face * (t + 4.2)), 0.0, 30.0)
            # The gilt bead down the inner edge, and a gilt line in the face.
            tube(gilt, [(sx * (half + 0.8), face * (t + 3.2), 30.0), (sx * (half + 0.8), face * (t + 3.2), h + 0.8)], 0.55, sides=8)
            tube(gilt, [(sx * (half + cw - 3.0), face * (t + 3.1), 32.0), (sx * (half + cw - 3.0), face * (t + 3.1), h + cw - 3.0)], 0.3, sides=6)
        box(lacquer, -half - cw, half + cw, lo, hi, h, h + cw)
        tube(gilt, [(-half - 0.8, face * (t + 3.2), h + 0.8), (half + 0.8, face * (t + 3.2), h + 0.8)], 0.55, sides=8)
        tube(gilt, [(-half - cw + 3.0, face * (t + 3.1), h + cw - 3.0), (half + cw - 3.0, face * (t + 3.1), h + cw - 3.0)], 0.3, sides=6)
        # The frieze and the cornice over it.
        fz0, fz1 = h + cw, h + cw + 9.0
        box(lacquer, -half - cw - 2.0, half + cw + 2.0, min(y0, face * (t + 3.6)), max(y0, face * (t + 3.6)), fz0, fz1)
        cz0, cz1 = fz1, fz1 + 5.0
        box(lacquer, -half - cw - 6.0, half + cw + 6.0, min(y0, face * (t + 8.0)), max(y0, face * (t + 8.0)), cz0, cz1)
        # The egg-and-dart under the cornice's lip: gilt eggs on a gilt fillet.
        tube(gilt, [(-half - cw - 4.0, face * (t + 6.0), cz0 - 0.5), (half + cw + 4.0, face * (t + 6.0), cz0 - 0.5)], 0.5, sides=6)
        x = -half - cw - 1.5
        while x < half + cw + 1.5:
            ellipsoid(gilt, (x, face * (t + 4.6), fz1 - 2.2), (1.1, 0.9, 1.6), rings=5, sides=8)
            x += 3.6
        tube(gilt, [(-half - cw - 6.0, face * (t + 8.2), cz1 - 0.6), (half + cw + 6.0, face * (t + 8.2), cz1 - 0.6)], 0.6, sides=8)
        # A gilt cartouche in the frieze's middle.
        ellipsoid(gilt, (0.0, face * (t + 4.2), (fz0 + fz1) * 0.5), (7.0, 1.0, 3.6), rings=6, sides=16)
    return [lacquer, gilt]


# ---------------------------------------------------------------------------------------------
# The desk and what is on it.
# ---------------------------------------------------------------------------------------------

def desk():
    """His desk: a black lacquer pedestal desk 230 x 105, three drawers in each pedestal on the
    sitter's side (-Y) with gilt pulls, the visitor's side (+Y) panelled and beaded in gilt, a
    burgundy leather top sunk inside a gilt-edged lacquer border, gilt mounts at the corners and a
    plinth round the foot. Faces +Y (the visitors); the sitter is at -Y."""
    lacquer = Part("Lacquer", bevel=0.5)
    top = Part("Lacquer", bevel=1.2, segments=3)
    leather = Part("Leather")
    gilt = Part("Gilt")
    shadow = Part("Shadow")
    W, D, Z = DESK_W, DESK_D, DESK_TOP
    ped = 62.0
    # The top: a lacquer slab, the leather sunk into it, a gilt bead round the leather and the edge.
    box(top, -W / 2, W / 2, -D / 2, D / 2, Z - 4.5, Z)
    box(leather, -W / 2 + 9.0, W / 2 - 9.0, -D / 2 + 9.0, D / 2 - 9.0, Z - 0.6, Z + 0.15)
    bead_rect(gilt, -W / 2 + 9.0, W / 2 - 9.0, -D / 2 + 9.0, D / 2 - 9.0, 0.0, radius=0.35, axis="y")
    for a in gilt.bm.verts:
        pass
    # bead_rect lays its rectangle in XZ; the leather's edge is in XY, so lay it by hand.
    gilt.bm.clear()
    rect = [(-W / 2 + 9.0, -D / 2 + 9.0), (W / 2 - 9.0, -D / 2 + 9.0), (W / 2 - 9.0, D / 2 - 9.0), (-W / 2 + 9.0, D / 2 - 9.0)]
    for (ax, ay), (bx, by) in zip(rect, rect[1:] + rect[:1]):
        tube(gilt, [(ax, ay, Z + 0.1), (bx, by, Z + 0.1)], 0.35, sides=8)
    edge = [(-W / 2, -D / 2), (W / 2, -D / 2), (W / 2, D / 2), (-W / 2, D / 2)]
    for (ax, ay), (bx, by) in zip(edge, edge[1:] + edge[:1]):
        tube(gilt, [(ax, ay, Z - 4.6), (bx, by, Z - 4.6)], 0.5, sides=8)
    # The pedestals and the kneehole's back panel, under an apron.
    body_z0, body_z1 = 9.0, Z - 4.5
    for sx in (-1, 1):
        x0, x1 = sorted((sx * W / 2 - sx * 2.0, sx * (W / 2 - 2.0 - ped)))
        box(lacquer, x0, x1, -D / 2 + 2.0, D / 2 - 2.0, body_z0, body_z1)
        # The plinth under each pedestal, with gilt sabots at the corners.
        box(lacquer, x0 - 1.5, x1 + 1.5, -D / 2 + 0.5, D / 2 - 0.5, 0.0, body_z0)
        for cx in (x0 - 1.5, x1 + 1.5):
            for cy in (-D / 2 + 0.5, D / 2 - 0.5):
                cbox(gilt, cx, cy, 4.5, 3.6, 3.6, 9.4)
        # The drawers on the sitter's side: three, deepest at the bottom.
        z = body_z1 - 2.0
        for height in (16.0, 18.0, 24.0):
            z0 = z - height
            cbox(shadow, (x0 + x1) / 2, -D / 2 + 1.6, (z0 + z) / 2, x1 - x0 - 5.0, 0.6, height - 1.0)
            box(lacquer, x0 + 3.0, x1 - 3.0, -D / 2 + 0.6, -D / 2 + 2.2, z0 + 0.8, z - 0.8)
            bead_rect(gilt, x0 + 5.0, x1 - 5.0, z0 + 2.6, z - 2.6, -D / 2 + 0.5, radius=0.25)
            # A gilt bail pull on two roses.
            pz = (z0 + z) / 2
            px = (x0 + x1) / 2
            for rx in (-5.5, 5.5):
                lathe(gilt, [(0.0, 0.0), (1.4, 0.0), (1.2, 0.8), (0.0, 1.1)], at(px + rx, -D / 2 + 0.6, pz + 1.0, pitch=0, roll=90), sides=12)
            tube(gilt, [(px - 5.5, -D / 2 - 0.4, pz + 1.0), (px - 4.5, -D / 2 - 1.6, pz - 2.0), (px + 4.5, -D / 2 - 1.6, pz - 2.0), (px + 5.5, -D / 2 - 0.4, pz + 1.0)],
                 0.45, sides=8)
            z = z0
        # The visitor's side: one long panel per pedestal with a gilt bead, and a gilt corner mount.
        box(lacquer, x0 + 4.0, x1 - 4.0, D / 2 - 2.0, D / 2 - 0.8, body_z0 + 5.0, body_z1 - 5.0)
        bead_rect(gilt, x0 + 6.0, x1 - 6.0, body_z0 + 7.0, body_z1 - 7.0, D / 2 - 0.6, radius=0.4)
        for cx in (x0 - 0.3, x1 + 0.3):
            ellipsoid(gilt, (cx, D / 2 - 1.0, body_z1 - 6.0), (1.6, 1.6, 5.0), rings=6, sides=10)
        # The ends of the pedestals panelled too.
        for ex in (x0, x1):
            if abs(ex) > W / 2 - 4.0:
                bead_rect(gilt, -D / 2 + 8.0, D / 2 - 8.0, body_z0 + 7.0, body_z1 - 7.0, ex + math.copysign(0.4, ex), radius=0.4, axis="x")
    kx = W / 2 - 2.0 - ped
    # The modesty panel at the back of the kneehole (on the visitors' side) and the apron drawer.
    box(lacquer, -kx, kx, D / 2 - 4.0, D / 2 - 1.5, body_z0 + 14.0, body_z1)
    bead_rect(gilt, -kx + 5.0, kx - 5.0, body_z0 + 18.0, body_z1 - 14.0, D / 2 - 1.3, radius=0.4)
    box(lacquer, -kx, kx, -D / 2 + 2.0, D / 2 - 4.0, body_z1 - 11.0, body_z1)
    cbox(shadow, 0.0, -D / 2 + 1.7, body_z1 - 5.5, kx * 2.0 - 10.0, 0.6, 9.0)
    box(lacquer, -kx + 4.0, kx - 4.0, -D / 2 + 1.0, -D / 2 + 2.4, body_z1 - 10.0, body_z1 - 1.0)
    cbox(shadow, 0.0, -D / 2 + 0.9, body_z1 - 5.5, 3.0, 0.3, 1.6)
    return [lacquer, top, leather, gilt, shadow]


def tumbler(part_glass=None, part_liquor=None, x=0.0, y=0.0, level=2.6, matrix=None):
    """A heavy cut-glass whisky tumbler, a closed shell, with a dram left in it."""
    glass = part_glass or Part("Glass")
    liquor = part_liquor or Part("Liquor")
    m = matrix or at(x, y, 0.0)
    lathe(glass, [(0.0, 0.0), (3.9, 0.0), (4.2, 0.4), (4.4, 9.2), (4.05, 9.2), (3.8, 1.6), (0.0, 1.6)], m, sides=24)
    if level > 0.0:
        lathe(liquor, [(0.0, 1.7), (3.78, 1.7), (3.8 + level * 0.02, 1.7 + level), (0.0, 1.7 + level)], m, sides=24)
    return glass, liquor


def tumbler_model():
    glass, liquor = tumbler(level=2.4)
    return [glass, liquor]


def tumbler_empty():
    glass, liquor = tumbler(level=0.0)
    return [glass]


def phone():
    """A rotary telephone in black bakelite with a gilt dial ring and gilt cradle, the handset on
    it, the cord coiled away off the back. Faces +Y (the dial towards the caller)."""
    body = Part("Steel", bevel=1.2, segments=3)
    gilt = Part("Gilt")
    dial = Part("Card")
    shadow = Part("Shadow")
    # The body: a sloped-front block, rounded hard.
    # The profile is drawn front-to-back in XZ and turned so its X runs along +Y: the front slopes.
    K.prism_xz(body, [(-9.0, 0.0), (10.0, 0.0), (10.0, 4.0), (5.0, 11.5), (-6.0, 11.5), (-9.0, 8.0)], -11.0, 11.0, matrix=at(0, 0, 0, yaw=90.0))
    # The dial on the sloping front, a gilt ring round a pale card with ten finger holes.
    face = at(0.0, 7.6, 7.9, roll=-56.0)
    lathe(gilt, [(0.0, 0.0), (5.6, 0.0), (5.8, 0.5), (5.4, 0.9), (0.0, 0.9)], face, sides=32)
    lathe(dial, [(0.0, 0.0), (4.7, 0.0), (4.7, 1.0), (0.0, 1.0)], face, sides=32)
    for k in range(10):
        a = math.radians(60.0 + k * 26.0)
        ellipsoid(shadow, tuple(face @ Vector((math.cos(a) * 3.5, math.sin(a) * 3.5, 1.05))), (0.7, 0.7, 0.3), rings=4, sides=10)
    lathe(gilt, [(0.0, 0.0), (1.4, 0.0), (1.2, 1.4), (0.0, 1.5)], face, sides=16)
    # The cradle's two gilt forks and the handset across them.
    for sx in (-1, 1):
        tube(gilt, [(sx * 7.0, -2.0, 11.0), (sx * 7.5, -2.0, 14.0), (sx * 8.0, -1.0, 15.0)], 0.6, sides=8)
        tube(gilt, [(sx * 7.0, -3.5, 11.0), (sx * 7.5, -3.5, 14.0), (sx * 8.0, -4.5, 15.0)], 0.6, sides=8)
    tube(body, [(-12.0, -2.8, 16.2), (-6.0, -2.8, 17.6), (6.0, -2.8, 17.6), (12.0, -2.8, 16.2)], 1.5, sides=12)
    for sx in (-1, 1):
        ellipsoid(body, (sx * 12.5, -2.8, 15.4), (3.0, 3.6, 2.6), rings=8, sides=14)
    # The cord, coiled, off the back and down.
    coil = []
    for k in range(90):
        t = k / 89.0
        a = t * 22 * math.pi
        coil.append((11.0 + math.cos(a) * 0.9, -8.0 - t * 22.0, 4.0 - t * 3.6 + math.sin(a) * 0.9))
    tube(shadow, coil, 0.28, sides=6)
    return [body, gilt, dial, shadow]


def humidor():
    """A cigar box in black lacquer, its gilt-cornered lid shut, a gilt escutcheon on the front."""
    lacquer = Part("Lacquer", bevel=0.3)
    gilt = Part("Gilt")
    box(lacquer, -14.0, 14.0, -10.0, 10.0, 0.0, 9.0)
    box(lacquer, -14.4, 14.4, -10.4, 10.4, 9.1, 11.8)
    for sx in (-1, 1):
        for sy in (-1, 1):
            cbox(gilt, sx * 13.8, sy * 9.8, 10.5, 2.0, 2.0, 3.0)
            cbox(gilt, sx * 13.4, sy * 9.4, 0.8, 2.0, 2.0, 1.6)
    cbox(gilt, 0.0, 10.3, 7.5, 3.0, 0.4, 3.6)
    return [lacquer, gilt]


def ashtray():
    """A heavy ashtray in gilt bronze, its three rests round the rim, a half-smoked cigar in one and
    the ash of it gone grey."""
    gilt = Part("Gilt")
    ash = Part("Ash")
    cigar = Part("Cigar")
    lathe(gilt, [(0.0, 0.0), (7.0, 0.0), (8.0, 1.0), (8.2, 3.0), (7.4, 3.2), (6.4, 1.6), (0.0, 1.6)], Matrix.Identity(4), sides=36)
    lathe(ash, [(0.0, 1.6), (5.6, 1.6), (4.0, 2.0), (0.0, 2.2)], Matrix.Identity(4), sides=24)
    tube(cigar, [(6.5, 1.0, 3.6), (0.0, 0.6, 2.9), (-3.0, 0.4, 2.6)], 0.85, sides=12)
    lathe(ash, [(0.0, 0.0), (0.8, 0.0), (0.8, 2.2), (0.0, 2.4)], at(-3.0, 0.4, 2.6, pitch=-92.0), sides=10)
    return [gilt, ash, cigar]


def folders():
    """A stack of case files on the desk: four buff folders a little askew, papers showing at their
    edges, the top one's front from the atlas. Its label reads +Y-up when laid as the desk's sitter
    sees it: the bottom of the folder's face towards -Y."""
    buff = Part("Buff", bevel=0.1)
    paper = Part("Paper")
    pics = Part("Pictures")
    pics.uv_fn = no_uvs
    w, d = 31.0, 23.5
    z = 0.0
    rng = random.Random(7)
    for k in range(4):
        yaw = rng.uniform(-6.0, 6.0)
        m = at(rng.uniform(-1.5, 1.5), rng.uniform(-1.5, 1.5), z, yaw=yaw)
        cbox(paper, 0.4, -0.3, 0.9, w - 1.4, d - 1.2, 1.2, matrix=m, bm=paper.bm)
        cbox(buff, 0.0, 0.0, 0.25, w, d, 0.5, matrix=m, bm=buff.bm)
        cbox(buff, 0.0, 0.0, 1.75, w, d, 0.5, matrix=m, bm=buff.bm)
        if k == 3:
            corners = [m @ Vector(c) for c in ((-w / 2, -d / 2, 2.02), (w / 2, -d / 2, 2.02), (w / 2, d / 2, 2.02), (-w / 2, d / 2, 2.02))]
            uv_quad(pics, [tuple(c) for c in corners], "folder", thickness=-0.03)
        z += 2.0
    return [buff, paper, pics]


def photos():
    """Two prints from the board's set, lying loose on the leather: the road and the two figures.
    Seen by somebody at -Y looking +Y."""
    pics = Part("Pictures")
    pics.uv_fn = no_uvs
    for name, x, y, yaw in (("photo_a", -6.0, 0.0, 9.0), ("photo_b", 9.0, -4.0, -14.0)):
        m = at(x, y, 0.0, yaw=yaw)
        w, h = 13.0, 9.75
        corners = [m @ Vector(c) for c in ((-w / 2, -h / 2, 0.12), (w / 2, -h / 2, 0.12), (w / 2, h / 2, 0.12), (-w / 2, h / 2, 0.12))]
        uv_quad(pics, [tuple(c) for c in corners], name, thickness=-0.1)
    return [pics]


# ---------------------------------------------------------------------------------------------
# Seating.
# ---------------------------------------------------------------------------------------------

def chesterfield(width, seats, seed):
    """A Chesterfield in burgundy leather: arms and back to the same height, all of it deep-buttoned
    in a diamond grid, the arms rolled over outwards and the roll running on round the back, loose
    seat cushions, on short turned black feet. Faces +Y."""
    rng = random.Random(seed)
    body = Part("Leather", bevel=2.2, segments=3, angle=30.0)
    soft = Part("Leather", smooth=True)
    buttons = Part("Leather")
    lacquer = Part("Lacquer")
    shadow = Part("Shadow")
    D = CHESTER_D
    top = CHESTER_TOP
    arm_w = 17.0
    back_d = 20.0
    half = width / 2
    base_z = 12.0
    # The base and the seat's deck.
    box(body, -half, half, -D / 2, D / 2, base_z, 34.0)
    # The back and the arms as stuffed walls, their inner faces buttoned.
    box(body, -half, half, -D / 2, -D / 2 + back_d, 34.0, top - 6.0)
    for sx in (-1, 1):
        box(body, min(sx * half, sx * (half - arm_w)), max(sx * half, sx * (half - arm_w)), -D / 2 + back_d - 2.0, D / 2, 34.0, top - 9.0)
    # The roll: one stuffed tube from the front of one arm, round the back, to the front of the other.
    roll_r = 8.0
    path = []
    for sx_y in [(-half + arm_w * 0.45, D / 2 - roll_r * 0.6), (-half + arm_w * 0.45, -D / 2 + back_d * 0.5)]:
        path.append((sx_y[0], sx_y[1], top - roll_r * 0.9))
    path = [(-half + arm_w * 0.5, D / 2 - 4.0, top - 9.0), (-half + arm_w * 0.5, -D / 2 + back_d * 0.4, top - 6.5),
            (-half + arm_w * 0.8, -D / 2 + back_d * 0.5, top - roll_r * 0.5)]
    path += [(x, -D / 2 + back_d * 0.5, top - roll_r * 0.5) for x in (-half * 0.5, 0.0, half * 0.5)]
    path += [(half - arm_w * 0.8, -D / 2 + back_d * 0.5, top - roll_r * 0.5), (half - arm_w * 0.5, -D / 2 + back_d * 0.4, top - 6.5),
             (half - arm_w * 0.5, D / 2 - 4.0, top - 9.0)]
    tube(soft, K.catmull(path, 6), roll_r, sides=20)
    # The scrolled arm fronts: a disc of leather pleated into the roll.
    for sx in (-1, 1):
        ellipsoid(soft, (sx * (half - arm_w * 0.5), D / 2 - 2.5, top - 12.0), (arm_w * 0.55, 3.0, 13.0), rings=10, sides=18)
        for k in range(7):
            a = math.radians(-60 + k * 20)
            p = (sx * (half - arm_w * 0.5) + math.sin(a) * 5.0, D / 2 + 0.3, top - 12.0 + math.cos(a) * 5.0)
            tube(shadow, [p, (sx * (half - arm_w * 0.5) + math.sin(a) * 9.0, D / 2 - 0.1, top - 12.0 + math.cos(a) * 9.0)], 0.2, sides=4)
    # The buttons: a diamond grid on the inner face of the back and of both arms.
    back_y = -D / 2 + back_d + 0.3
    for row, z in enumerate((42.0, 51.0, 60.0)):
        x = -half + arm_w + 6.0 + (6.0 if row % 2 else 0.0)
        while x < half - arm_w - 4.0:
            ellipsoid(buttons, (x, back_y, z), (0.9, 0.7, 0.9), rings=4, sides=8)
            for dx, dz in ((-6.0, -4.5), (6.0, -4.5)):
                if row < 2 and -half + arm_w + 2.0 < x + dx < half - arm_w - 2.0:
                    tube(shadow, [(x, back_y - 0.05, z), (x + dx, back_y - 0.05, z + dz)], 0.18, sides=4)
            x += 12.0
    for sx in (-1, 1):
        ax = sx * (half - arm_w - 0.3)
        for row, z in enumerate((42.0, 52.0)):
            y = -D / 2 + back_d + 6.0 + (5.0 if row % 2 else 0.0)
            while y < D / 2 - 6.0:
                ellipsoid(buttons, (ax, y, z), (0.7, 0.9, 0.9), rings=4, sides=8)
                y += 10.0
    # The loose seat cushions, a little sunk and pushed about.
    inner = width - 2 * arm_w
    for k in range(seats):
        cw = inner / seats
        cx = -inner / 2 + cw * (k + 0.5) + rng.uniform(-0.8, 0.8)
        cushion(soft, (cx, 5.0 + rng.uniform(-1.0, 1.0), CHESTER_SEAT - 5.5), (cw - 1.2, D - back_d - 4.0, 11.0),
                matrix=at(0, 0, 0, yaw=rng.uniform(-1.5, 1.5)), round_v=0.35, round_h=0.18, pinch=0.15)
    # The plinth's welt and the turned feet.
    tube(shadow, [(-half, D / 2 + 0.1, base_z + 0.8), (half, D / 2 + 0.1, base_z + 0.8)], 0.5, sides=6)
    for sx in (-1, 1):
        for sy in (-1, 1):
            turned_foot(lacquer, sx * (half - 7.0), sy * (D / 2 - 7.0), base_z)
    return [body, soft, buttons, lacquer, shadow]


def club_chair():
    return chesterfield(CLUB_W, 1, 31)


def sofa():
    return chesterfield(SOFA_W, 3, 32)


def coffee_table():
    """A low black lacquer table with a gilt edge and a shelf under, on four square tapered legs
    with gilt sabots."""
    lacquer = Part("Lacquer", bevel=0.6)
    gilt = Part("Gilt")
    W, D, Z = COFFEE_W, COFFEE_D, COFFEE_TOP
    box(lacquer, -W / 2, W / 2, -D / 2, D / 2, Z - 4.0, Z)
    edge = [(-W / 2, -D / 2), (W / 2, -D / 2), (W / 2, D / 2), (-W / 2, D / 2)]
    for (ax, ay), (bx, by) in zip(edge, edge[1:] + edge[:1]):
        tube(gilt, [(ax, ay, Z - 2.0), (bx, by, Z - 2.0)], 0.6, sides=8)
    box(lacquer, -W / 2 + 6.0, W / 2 - 6.0, -D / 2 + 6.0, D / 2 - 6.0, 12.0, 14.5)
    for sx in (-1, 1):
        for sy in (-1, 1):
            x, y = sx * (W / 2 - 5.0), sy * (D / 2 - 5.0)
            K.prism(lacquer, [(x - 2.2, y - 2.2), (x + 2.2, y - 2.2), (x + 2.2, y + 2.2), (x - 2.2, y + 2.2)], 5.0, Z - 4.0)
            cbox(gilt, x, y, 2.5, 3.4, 3.4, 5.0)
    return [lacquer, gilt]


def card_table():
    """A round card table: a burgundy baize top inside a black rim with a gilt bead, a deep apron,
    and a turned black pedestal on three splayed feet with gilt toes. Top at CARD_TABLE_TOP."""
    lacquer = Part("Lacquer", bevel=0.5)
    gilt = Part("Gilt")
    felt = Part("Felt")
    R, Z = CARD_TABLE_R, CARD_TABLE_TOP
    lathe(lacquer, [(0.0, Z - 9.0), (R - 1.0, Z - 9.0), (R, Z - 7.0), (R, Z - 0.5), (R - 0.6, Z + 0.4), (R - 6.0, Z + 0.4), (R - 6.0, Z - 0.3), (0.0, Z - 0.3)],
          Matrix.Identity(4), sides=64)
    lathe(felt, [(0.0, Z - 0.6), (R - 5.9, Z - 0.6), (R - 5.9, Z), (0.0, Z)], Matrix.Identity(4), sides=64)
    lathe(gilt, [(R - 6.4, Z - 0.1), (R - 5.6, Z - 0.1), (R - 5.6, Z + 0.6), (R - 6.4, Z + 0.6)], Matrix.Identity(4), sides=64)
    lathe(gilt, [(R + 0.05, Z - 6.0), (R + 0.5, Z - 6.0), (R + 0.5, Z - 5.0), (R + 0.05, Z - 5.0)], Matrix.Identity(4), sides=64)
    lathe(lacquer, [(0.0, 10.0), (9.0, 10.0), (8.0, 14.0), (4.6, 18.0), (4.0, 34.0), (6.0, 44.0), (4.4, 52.0), (5.6, 60.0), (7.0, Z - 9.0), (0.0, Z - 9.0)],
          Matrix.Identity(4), sides=24)
    for k in range(3):
        a = math.radians(90.0 + k * 120.0)
        c, s = math.cos(a), math.sin(a)
        tube(lacquer, [(c * 5.0, s * 5.0, 16.0), (c * 18.0, s * 18.0, 10.0), (c * 30.0, s * 30.0, 4.0), (c * 36.0, s * 36.0, 3.0)], [3.0, 2.6, 2.0, 1.8], sides=12)
        ellipsoid(gilt, (c * 37.0, s * 37.0, 2.2), (2.6, 2.6, 2.2), rings=6, sides=12)
    return [lacquer, gilt, felt]


def cards():
    """The game left on the table: a hand spread face up, another face down, the deck squared off
    with its top card turned, and stacks of chips, one knocked over. Origin on the baize."""
    card = Part("Card")
    back = Part("CardBack")
    chips = {"ChipRed": Part("ChipRed"), "ChipBlack": Part("ChipBlack"), "ChipIvory": Part("ChipIvory")}
    rng = random.Random(41)
    cw, ch = 6.3, 8.8
    for k in range(5):
        m = at(-20.0 + k * 1.6, -14.0, 0.05 + k * 0.03, yaw=-20.0 + k * 9.0)
        cbox(card, 0.0, 0.0, 0.0, cw, ch, 0.03, matrix=m, bm=card.bm)
    for k in range(5):
        m = at(16.0 + rng.uniform(-1.0, 1.0), 10.0 + rng.uniform(-1.0, 1.0), 0.05 + k * 0.03, yaw=140.0 + rng.uniform(-8.0, 8.0))
        cbox(back, 0.0, 0.0, 0.0, cw, ch, 0.03, matrix=m, bm=back.bm)
    m = at(-4.0, 16.0, 0.0, yaw=8.0)
    cbox(back, 0.0, 0.0, 0.8, cw, ch, 1.6, matrix=m, bm=back.bm)
    cbox(card, 0.0, 0.0, 1.62, cw, ch, 0.03, matrix=at(-2.0, 18.0, 0.0, yaw=-18.0), bm=card.bm)
    for k in range(3):
        cbox(back, 0.0, 0.0, 0.02, cw, ch, 0.03, matrix=at(rng.uniform(-6.0, 6.0), rng.uniform(-4.0, 4.0), 0.02 + k * 0.03, yaw=rng.uniform(0.0, 360.0)), bm=back.bm)

    def chip(slot, x, y, z, m=None):
        mm = (m or Matrix.Identity(4)) @ at(x, y, z)
        lathe(chips[slot], [(0.0, 0.0), (1.95, 0.0), (2.0, 0.17), (1.95, 0.34), (0.0, 0.34)], mm, sides=20)

    for (x, y), slot, count in (((22.0, -12.0), "ChipRed", 9), ((26.5, -10.0), "ChipBlack", 6), ((24.0, -16.0), "ChipIvory", 12),
                                 ((-26.0, 8.0), "ChipRed", 5), ((-22.0, 12.0), "ChipBlack", 11)):
        for k in range(count):
            chip(slot, x + rng.uniform(-0.15, 0.15), y + rng.uniform(-0.15, 0.15), k * 0.34)
    # One stack pushed over into a slant line across the baize.
    for k in range(7):
        chip(("ChipRed", "ChipIvory")[k % 2], 0.0, 0.0, 0.0, at(4.0 + k * 1.5, -2.0 + k * 0.3, 1.9, pitch=78.0, yaw=15.0))
    return [card, back] + list(chips.values())


def tray():
    """A gilt tray with a gallery round it, for the decanters on the drinks cabinet."""
    gilt = Part("Gilt", bevel=0.2)
    box(gilt, -24.0, 24.0, -16.0, 16.0, 0.0, 0.8)
    edge = [(-24.0, -16.0), (24.0, -16.0), (24.0, 16.0), (-24.0, 16.0)]
    for (ax, ay), (bx, by) in zip(edge, edge[1:] + edge[:1]):
        tube(gilt, [(ax, ay, 2.6), (bx, by, 2.6)], 0.35, sides=8)
        n = int(math.hypot(bx - ax, by - ay) / 3.2)
        for k in range(n):
            t = (k + 0.5) / n
            tube(gilt, [(ax + (bx - ax) * t, ay + (by - ay) * t, 0.8), (ax + (bx - ax) * t, ay + (by - ay) * t, 2.6)], 0.18, sides=5)
    for sx in (-1, 1):
        tube(gilt, [(sx * 24.0, -6.0, 2.6), (sx * 27.0, -5.0, 3.6), (sx * 27.0, 5.0, 3.6), (sx * 24.0, 6.0, 2.6)], 0.45, sides=8)
    return [gilt]


# ---------------------------------------------------------------------------------------------
# Cabinets.
# ---------------------------------------------------------------------------------------------

def bar():
    """The drinks cabinet: black lacquer, two cupboard doors with gilt-beaded fields and gilt
    handles, a gilt gallery round the back and sides of the top, on a plinth. Faces +Y."""
    lacquer = Part("Lacquer", bevel=0.5)
    gilt = Part("Gilt")
    shadow = Part("Shadow")
    W, D, Z = BAR_W, BAR_D, BAR_TOP
    box(lacquer, -W / 2 + 1.0, W / 2 - 1.0, -D / 2 + 1.0, D / 2 - 2.0, 10.0, Z - 3.0)
    box(lacquer, -W / 2 - 1.5, W / 2 + 1.5, -D / 2 - 0.5, D / 2 + 1.0, Z - 3.0, Z)
    box(lacquer, -W / 2, W / 2, -D / 2, D / 2 - 1.0, 0.0, 10.0)
    tube(gilt, [(-W / 2 - 1.5, D / 2 + 1.0, Z - 1.5), (W / 2 + 1.5, D / 2 + 1.0, Z - 1.5)], 0.5, sides=8)
    # The doors: four of them, the middle two a pair.
    n = 3
    dw = (W - 6.0) / n
    for k in range(n):
        x0 = -W / 2 + 3.0 + k * dw
        cbox(shadow, x0 + dw / 2, D / 2 - 1.8, (14.0 + Z - 6.0) / 2, dw - 0.6, 0.4, Z - 20.0)
        box(lacquer, x0 + 0.4, x0 + dw - 0.4, D / 2 - 2.0, D / 2 - 0.3, 14.0, Z - 6.0)
        bead_rect(gilt, x0 + 6.0, x0 + dw - 6.0, 20.0, Z - 12.0, D / 2 - 0.1, radius=0.4)
        hx = x0 + (dw - 5.0 if k == 0 else 5.0 if k == 2 else dw - 5.0)
        tube(gilt, [(hx, D / 2 - 0.3, 50.0), (hx, D / 2 + 1.6, 51.0), (hx, D / 2 + 1.6, 63.0), (hx, D / 2 - 0.3, 64.0)], 0.5, sides=8)
    # The gallery: a gilt rail on spindles round the back and the two ends of the top.
    rails = [(-W / 2 + 1.0, D / 2 - 6.0), (-W / 2 + 1.0, -D / 2 + 1.0), (W / 2 - 1.0, -D / 2 + 1.0), (W / 2 - 1.0, D / 2 - 6.0)]
    for (ax, ay), (bx, by) in zip(rails, rails[1:]):
        tube(gilt, [(ax, ay, Z + 7.0), (bx, by, Z + 7.0)], 0.45, sides=8)
        n = max(1, int(math.hypot(bx - ax, by - ay) / 6.0))
        for k in range(n + 1):
            t = k / n
            tube(gilt, [(ax + (bx - ax) * t, ay + (by - ay) * t, Z), (ax + (bx - ax) * t, ay + (by - ay) * t, Z + 7.0)], 0.3, sides=6)
    return [lacquer, gilt, shadow]


def safe():
    """A safe in black-painted steel: a heavy door with a gilt pinstripe a hand in from its edge,
    the combination dial in gilt and black, a three-spoked gilt handle, a gilt maker's plate, and
    the hinges' knuckles down the right. On a lacquer plinth. Faces +Y."""
    steel = Part("Steel", bevel=2.5, segments=3)
    door_part = Part("Steel", bevel=1.2, segments=2)
    gilt = Part("Gilt")
    shadow = Part("Shadow")
    lacquer = Part("Lacquer", bevel=0.4)
    W, D, H = SAFE_W, SAFE_D, SAFE_H
    base = 10.0
    box(lacquer, -W / 2 - 2.0, W / 2 + 2.0, -D / 2 - 2.0, D / 2 + 2.0, 0.0, base)
    box(steel, -W / 2, W / 2, -D / 2, D / 2 - 3.0, base, H)
    cbox(shadow, 0.0, D / 2 - 2.9, (base + H) / 2 + 1.0, W - 9.0, 0.5, H - base - 10.0)
    box(door_part, -W / 2 + 5.0, W / 2 - 5.0, D / 2 - 3.2, D / 2, base + 5.5, H - 5.5)
    bead_rect(gilt, -W / 2 + 11.0, W / 2 - 11.0, base + 11.0, H - 11.0, D / 2 + 0.1, radius=0.25)
    bead_rect(gilt, -W / 2 + 12.5, W / 2 - 12.5, base + 12.5, H - 12.5, D / 2 + 0.1, radius=0.15)
    dz = base + (H - base) * 0.62
    face = at(0.0, D / 2, dz, roll=-90.0)
    lathe(gilt, [(0.0, 0.0), (7.0, 0.0), (7.2, 0.6), (6.6, 1.2), (0.0, 1.2)], face, sides=40)
    lathe(steel, [(0.0, 1.2), (5.6, 1.2), (5.4, 3.0), (4.6, 3.4), (0.0, 3.4)], face, sides=40)
    for k in range(20):
        a = 2 * math.pi * k / 20
        p = face @ Vector((math.cos(a) * 6.3, math.sin(a) * 6.3, 1.25))
        tube(gilt, [tuple(p), tuple(face @ Vector((math.cos(a) * 5.8, math.sin(a) * 5.8, 1.25)))], 0.12, sides=4)
    lathe(gilt, [(0.0, 3.4), (1.4, 3.4), (1.0, 4.0), (0.0, 4.1)], face, sides=16)
    hz = base + (H - base) * 0.36
    hub = at(0.0, D / 2, hz, roll=-90.0)
    lathe(gilt, [(0.0, 0.0), (3.4, 0.0), (3.0, 3.0), (1.6, 3.6), (0.0, 3.6)], hub, sides=24)
    for k in range(3):
        a = math.radians(90.0 + k * 120.0 + 15.0)
        tip = hub @ Vector((math.cos(a) * 11.0, math.sin(a) * 11.0, 2.6))
        tube(gilt, [tuple(hub @ Vector((0.0, 0.0, 2.6))), tuple(tip)], 0.7, sides=10)
        ellipsoid(gilt, tuple(tip), (1.3, 1.3, 1.3), rings=6, sides=10)
    cbox(gilt, 0.0, D / 2 + 0.1, H - 17.0, 22.0, 0.3, 5.0)
    for zz in (base + 18.0, H - 18.0):
        lathe(steel, [(0.0, -8.0), (2.2, -8.0), (2.2, 8.0), (0.0, 8.0)], at(W / 2 - 4.5, D / 2 - 1.2, zz), sides=14)
    return [steel, door_part, gilt, shadow, lacquer]


def bookcase():
    """A black lacquer bookcase 120 wide: a cupboard to the counter with two beaded doors, open
    shelves over it (CASE_SHELVES), a dentil cornice, gilt lines up the pilasters. The interior is
    lacquer too, so the books are what shows. Faces +Y."""
    lacquer = Part("Lacquer", bevel=0.4)
    gilt = Part("Gilt")
    shadow = Part("Shadow")
    W, D, H = CASE_W, CASE_D, CASE_H
    side = (W - CASE_INTERIOR) / 2
    for sx in (-1, 1):
        box(lacquer, min(sx * W / 2, sx * (W / 2 - side)), max(sx * W / 2, sx * (W / 2 - side)), -D / 2, D / 2, 0.0, H - 14.0)
        tube(gilt, [(sx * (W / 2 - side / 2), D / 2 + 0.2, 92.0), (sx * (W / 2 - side / 2), D / 2 + 0.2, H - 18.0)], 0.3, sides=6)
    box(lacquer, -W / 2, W / 2, -D / 2, -D / 2 + 1.5, 0.0, H - 14.0)
    box(lacquer, -W / 2 + 0.5, W / 2 - 0.5, -D / 2, D / 2 + 0.5, 0.0, 10.0)
    # The cupboard: a counter board over two doors.
    box(lacquer, -W / 2 - 1.0, W / 2 + 1.0, -D / 2, D / 2 + 2.0, CASE_SHELVES[0] - 3.0, CASE_SHELVES[0])
    tube(gilt, [(-W / 2 - 1.0, D / 2 + 2.0, CASE_SHELVES[0] - 1.5), (W / 2 + 1.0, D / 2 + 2.0, CASE_SHELVES[0] - 1.5)], 0.4, sides=8)
    for sx in (-1, 1):
        x0, x1 = sorted((sx * 0.3, sx * (CASE_INTERIOR / 2)))
        cbox(shadow, (x0 + x1) / 2, D / 2 - 1.2, (12.0 + CASE_SHELVES[0] - 3.5) / 2, x1 - x0, 0.4, CASE_SHELVES[0] - 16.0)
        box(lacquer, x0 + 0.3, x1 - 0.3, D / 2 - 1.5, D / 2, 12.0, CASE_SHELVES[0] - 3.5)
        bead_rect(gilt, x0 + 5.0, x1 - 5.0, 18.0, CASE_SHELVES[0] - 10.0, D / 2 + 0.2, radius=0.35)
        ellipsoid(gilt, (sx * 4.0, D / 2 + 0.9, 52.0), (1.0, 1.0, 1.0), rings=6, sides=10)
    for z in CASE_SHELVES[1:]:
        box(lacquer, -CASE_INTERIOR / 2, CASE_INTERIOR / 2, -D / 2 + 1.5, D / 2 - 1.0, z - 2.4, z)
        tube(gilt, [(-CASE_INTERIOR / 2, D / 2 - 0.8, z - 1.2), (CASE_INTERIOR / 2, D / 2 - 0.8, z - 1.2)], 0.25, sides=6)
    # The top and the cornice: a frieze, a row of dentils, a projecting cap.
    box(lacquer, -W / 2, W / 2, -D / 2, D / 2, H - 16.0, H - 8.0)
    x = -W / 2 + 2.0
    while x < W / 2 - 2.0:
        cbox(lacquer, x, D / 2 + 1.0, H - 6.5, 2.0, 2.0, 3.0)
        x += 4.0
    box(lacquer, -W / 2 - 3.0, W / 2 + 3.0, -D / 2, D / 2 + 3.5, H - 5.0, H)
    tube(gilt, [(-W / 2 - 3.0, D / 2 + 3.6, H - 2.5), (W / 2 + 3.0, D / 2 + 3.6, H - 2.5)], 0.5, sides=8)
    tube(gilt, [(-W / 2, D / 2 + 0.2, H - 12.0), (W / 2, D / 2 + 0.2, H - 12.0)], 0.3, sides=6)
    return [lacquer, gilt, shadow]


def archive_boxes():
    """Three file boxes of brown card on the floor under the board: two stacked, one beside them with
    its lid off and leaning on it, folders standing in it."""
    card = Part("Cardboard", bevel=0.2)
    buff = Part("Buff")
    shadow = Part("Shadow")
    w, d, h = 40.0, 30.0, 27.0
    for (x, z, yaw) in ((-22.0, 0.0, 2.0), (-21.0, h + 0.2, -4.0)):
        m = at(x, 0.0, z, yaw=yaw)
        cbox(card, 0.0, 0.0, h / 2, w, d, h, matrix=m, bm=card.bm)
        cbox(card, 0.0, 0.0, h - 2.5, w + 0.8, d + 0.8, 5.0, matrix=m, bm=card.bm)
        cbox(shadow, 0.0, d / 2 + 0.45, h * 0.45, 9.0, 0.1, 3.0, matrix=m, bm=shadow.bm)
    m = at(26.0, 2.0, 0.0, yaw=-8.0)
    for bx0, bx1, by0, by1 in ((-w / 2, w / 2, -d / 2, -d / 2 + 0.8), (-w / 2, w / 2, d / 2 - 0.8, d / 2),
                               (-w / 2, -w / 2 + 0.8, -d / 2, d / 2), (w / 2 - 0.8, w / 2, -d / 2, d / 2)):
        box(card, bx0, bx1, by0, by1, 0.0, h, matrix=m)
    box(card, -w / 2, w / 2, -d / 2, d / 2, 0.0, 0.8, matrix=m)
    for k in range(9):
        cbox(buff, -w / 2 + 3.0 + k * 4.0, 0.0, h * 0.5 + 3.0 + (k % 3), 0.6, d - 3.0, h - 2.0, matrix=m @ at(0, 0, 0, roll=(k % 4 - 1.5) * 3.0), bm=buff.bm)
    # The lid, off and leant against the front of the stack.
    cbox(card, 0.0, 0.0, 0.0, w + 0.8, 5.0, d + 0.8, matrix=at(-22.0, d / 2 + 4.0, (d + 0.8) / 2 * 0.97, roll=-14.0), bm=card.bm)
    return [card, buff, shadow]


# ---------------------------------------------------------------------------------------------
# On the walls.
# ---------------------------------------------------------------------------------------------

def board():
    """The case board: cork in a black frame with a gilt slip, its face from the atlas. Origin on the
    wall at its middle, the face towards +Y."""
    lacquer = Part("Lacquer", bevel=0.3)
    gilt = Part("Gilt")
    pics = Part("Pictures")
    pics.uv_fn = no_uvs
    w, h = BOARD_W, BOARD_H
    box(lacquer, -w / 2, w / 2, 0.0, 2.0, -h / 2, h / 2)
    wall_picture(pics, "board", w - 1.0, h - 1.0, 2.05)
    f = 5.0
    for x0, x1, z0, z1 in ((-w / 2 - f, w / 2 + f, h / 2, h / 2 + f), (-w / 2 - f, w / 2 + f, -h / 2 - f, -h / 2),
                           (-w / 2 - f, -w / 2, -h / 2, h / 2), (w / 2, w / 2 + f, -h / 2, h / 2)):
        box(lacquer, x0, x1, 0.0, 4.0, z0, z1)
    bead_rect(gilt, -w / 2 - 0.4, w / 2 + 0.4, -h / 2 - 0.4, h / 2 + 0.4, 3.0, radius=0.5)
    return [lacquer, gilt, pics]


def gilt_frame(name, art_w, art_h, mould, depth):
    """A carved gilt frame round a canvas: a stepped moulding with a bead at its sight edge, a hollow,
    a raised ogee and a beaded outer edge; corner leaves. Origin on the wall at its middle."""
    gilt = Part("Gilt", bevel=0.6, segments=2, angle=30.0)
    beads = Part("Gilt")
    shadow = Part("Shadow")
    pics = Part("Pictures")
    pics.uv_fn = no_uvs
    w, h = art_w, art_h
    box(shadow, -w / 2, w / 2, 0.0, 1.0, -h / 2, h / 2)
    wall_picture(pics, name, w, h, 1.05)
    steps = ((0.0, 0.18, 3.0), (0.18, 0.5, 4.5), (0.5, 0.82, depth), (0.82, 1.0, depth - 1.5))
    for a, b, y in steps:
        for x0, x1, z0, z1 in ((-w / 2 - mould * b, w / 2 + mould * b, h / 2 + mould * a, h / 2 + mould * b),
                               (-w / 2 - mould * b, w / 2 + mould * b, -h / 2 - mould * b, -h / 2 - mould * a),
                               (-w / 2 - mould * b, -w / 2 - mould * a, -h / 2 - mould * a, h / 2 + mould * a),
                               (w / 2 + mould * a, w / 2 + mould * b, -h / 2 - mould * a, h / 2 + mould * a)):
            box(gilt, x0, x1, 0.0, y, z0, z1)
    bead_rect(beads, -w / 2 - 0.5, w / 2 + 0.5, -h / 2 - 0.5, h / 2 + 0.5, 3.2, radius=0.55)
    outer = mould * 0.9
    k = 0
    for x0, z0, x1, z1 in ((-w / 2 - outer, h / 2 + outer, w / 2 + outer, h / 2 + outer), (w / 2 + outer, -h / 2 - outer, -w / 2 - outer, -h / 2 - outer),
                           (-w / 2 - outer, -h / 2 - outer, -w / 2 - outer, h / 2 + outer), (w / 2 + outer, h / 2 + outer, w / 2 + outer, -h / 2 - outer)):
        n = int(math.hypot(x1 - x0, z1 - z0) / 2.4)
        for i in range(n):
            t = (i + 0.5) / n
            ellipsoid(beads, (x0 + (x1 - x0) * t, depth - 1.2, z0 + (z1 - z0) * t), (0.8, 0.6, 0.8), rings=4, sides=8)
            k += 1
    for sx in (-1, 1):
        for sz in (-1, 1):
            cx, cz = sx * (w / 2 + mould * 0.55), sz * (h / 2 + mould * 0.55)
            for a in (0.0, 120.0, 240.0):
                r = math.radians(a + 45.0 * sx * sz)
                ellipsoid(beads, (cx + math.cos(r) * 2.2, depth + 0.4, cz + math.sin(r) * 2.2), (2.4, 1.2, 1.2),
                          matrix=Matrix.Rotation(-r, 4, "Y"), rings=5, sides=10)
    return [gilt, beads, shadow, pics]


def portrait_frame():
    return gilt_frame("portrait", PORTRAIT_W, PORTRAIT_H, PORTRAIT_MOULD, 7.5)


def map_frame():
    """The city map in a black frame with a gilt slip."""
    lacquer = Part("Lacquer", bevel=0.4)
    gilt = Part("Gilt")
    shadow = Part("Shadow")
    pics = Part("Pictures")
    pics.uv_fn = no_uvs
    w, h, m = MAP_W, MAP_H, MAP_MOULD
    box(shadow, -w / 2, w / 2, 0.0, 1.0, -h / 2, h / 2)
    wall_picture(pics, "map", w, h, 1.05)
    for x0, x1, z0, z1 in ((-w / 2 - m, w / 2 + m, h / 2, h / 2 + m), (-w / 2 - m, w / 2 + m, -h / 2 - m, -h / 2),
                           (-w / 2 - m, -w / 2, -h / 2, h / 2), (w / 2, w / 2 + m, -h / 2, h / 2)):
        box(lacquer, x0, x1, 0.0, 3.6, z0, z1)
    bead_rect(gilt, -w / 2 - 0.6, w / 2 + 0.6, -h / 2 - 0.6, h / 2 + 0.6, 2.4, radius=0.6)
    bead_rect(gilt, -w / 2 - m + 0.8, w / 2 + m - 0.8, -h / 2 - m + 0.8, h / 2 + m - 0.8, 3.7, radius=0.3)
    return [lacquer, gilt, shadow, pics]


def sconce():
    """A gilt wall sconce: an oval backplate with a beaded edge, two scrolled arms, a candle cup on
    each with a dead bulb in it and a small pleated burgundy shade over it. Origin on the wall at the
    backplate's middle, out along +Y."""
    gilt = Part("Gilt")
    shade = Part("Shade", smooth=True)
    bulb = Part("Bulb")
    ellipsoid(gilt, (0.0, 0.8, 0.0), (5.0, 1.0, 9.0), rings=8, sides=20)
    for k in range(18):
        a = 2 * math.pi * k / 18
        ellipsoid(gilt, (math.cos(a) * 5.0, 1.0, math.sin(a) * 9.0), (0.5, 0.5, 0.5), rings=4, sides=6)
    ellipsoid(gilt, (0.0, 2.2, -1.0), (1.8, 1.8, 1.8), rings=6, sides=12)
    for sx in (-1, 1):
        arm = [(0.0, 2.0, -1.0), (sx * 4.0, 7.0, -5.0), (sx * 10.0, 12.0, -3.0), (sx * 14.0, 13.0, 2.0), (sx * 15.0, 12.5, 5.0)]
        tube(gilt, K.catmull(arm, 5), 0.75, sides=10)
        cup = at(sx * 15.0, 12.5, 5.0)
        lathe(gilt, [(0.0, 0.0), (1.2, 0.0), (2.6, 1.6), (2.4, 2.4), (1.4, 2.6), (1.4, 6.0), (0.0, 6.0)], cup, sides=16)
        lathe(bulb, [(0.0, 6.0), (1.0, 6.0), (1.4, 8.0), (1.0, 10.0), (0.0, 10.6)], cup, sides=12)
        # The shade: a pleated drum, wider at the foot.
        ring_lo, ring_hi, sides = 5.8, 3.6, 24
        prof = []
        for t, r in ((4.6, ring_lo), (12.6, ring_hi)):
            prof.append((r, t))
        verts = []
        bm = shade.bm
        rings = []
        for r, z in prof:
            ring = []
            for k in range(sides * 2):
                a = 2 * math.pi * k / (sides * 2)
                rr = r * (1.0 + (0.04 if k % 2 else -0.02))
                ring.append(bm.verts.new(cup @ Vector((math.cos(a) * rr, math.sin(a) * rr, z))))
            rings.append(ring)
        inner = [[bm.verts.new(cup @ ((v.co - cup.translation) * 0.0 + Vector((0, 0, 0)))) for v in []]]
        n = sides * 2
        for k in range(n):
            j = (k + 1) % n
            bm.faces.new((rings[0][k], rings[0][j], rings[1][j], rings[1][k]))
        # Give it thickness: a second, slightly smaller drum facing in, joined at the rims.
        rings_in = []
        for ring in rings:
            ring_in = []
            for v in ring:
                local = cup.inverted() @ v.co
                local.x *= 0.94
                local.y *= 0.94
                ring_in.append(bm.verts.new(cup @ local))
            rings_in.append(ring_in)
        for k in range(n):
            j = (k + 1) % n
            bm.faces.new((rings_in[0][j], rings_in[0][k], rings_in[1][k], rings_in[1][j]))
            bm.faces.new((rings[0][j], rings[0][k], rings_in[0][k], rings_in[0][j]))
            bm.faces.new((rings[1][k], rings[1][j], rings_in[1][j], rings_in[1][k]))
        tube(gilt, [tuple(cup @ Vector((0, 0, 10.6))), tuple(cup @ Vector((0, 0, 12.8)))], 0.25, sides=6)
    return [gilt, shade, bulb]


# ---------------------------------------------------------------------------------------------
# The coat stand and the hat.
# ---------------------------------------------------------------------------------------------

def coat_stand():
    """A black bentwood coat stand: a turned pole on three splayed feet with gilt toes, a ring of
    six gilt hooks at the top under a gilt finial, and a ring of umbrella hoop lower down."""
    lacquer = Part("Lacquer")
    gilt = Part("Gilt")
    H = COAT_STAND_H
    lathe(lacquer, [(0.0, 8.0), (2.6, 8.0), (2.2, 14.0), (1.8, 60.0), (2.2, 64.0), (1.7, 70.0), (1.6, H - 12.0), (2.4, H - 9.0), (1.8, H - 5.0), (0.0, H - 5.0)],
          Matrix.Identity(4), sides=16)
    lathe(gilt, [(0.0, H - 5.0), (1.8, H - 5.0), (2.0, H - 3.0), (1.0, H - 1.0), (0.0, H)], Matrix.Identity(4), sides=16)
    for k in range(3):
        a = math.radians(90.0 + k * 120.0)
        c, s = math.cos(a), math.sin(a)
        path = [(c * 1.5, s * 1.5, 22.0), (c * 12.0, s * 12.0, 12.0), (c * 22.0, s * 22.0, 4.0), (c * 30.0, s * 30.0, 2.0)]
        tube(lacquer, K.catmull(path, 4), [1.6] * (len(K.catmull(path, 4)) - 1) + [1.3], sides=10)
        ellipsoid(gilt, (c * 30.5, s * 30.5, 1.6), (1.8, 1.8, 1.6), rings=6, sides=10)
    for k in range(6):
        a = math.radians(30.0 + k * 60.0)
        c, s = math.cos(a), math.sin(a)
        hook = [(c * 1.6, s * 1.6, H - 22.0), (c * 7.0, s * 7.0, H - 18.0), (c * 11.0, s * 11.0, H - 12.0), (c * 11.5, s * 11.5, H - 8.0)]
        tube(gilt, K.catmull(hook, 4), 0.55, sides=8)
        ellipsoid(gilt, (c * 11.5, s * 11.5, H - 7.6), (0.9, 0.9, 0.9), rings=4, sides=8)
    ring = [(math.cos(2 * math.pi * k / 36) * 16.0, math.sin(2 * math.pi * k / 36) * 16.0, 62.0) for k in range(37)]
    tube(lacquer, ring, 1.0, sides=8)
    for k in range(3):
        a = math.radians(90.0 + k * 120.0)
        tube(lacquer, [(math.cos(a) * 1.8, math.sin(a) * 1.8, 62.0), (math.cos(a) * 16.0, math.sin(a) * 16.0, 62.0)], 0.8, sides=8)
    return [lacquer, gilt]


def fedora():
    """A black felt hat with a pinched crown, a burgundy band, the brim turned down at the front.
    Origin at the bottom of the crown in the middle, the front towards +Y."""
    felt = Part("Felt2", smooth=True)
    band = Part("Ribbon")
    bm = felt.bm
    nu, nv = 48, 16
    # The brim: a ring, turned up at the back and sides, down at the front, a closed thin solid.
    rows_top, rows_bot = [], []
    for i in range(nv + 1):
        r = 8.6 + (16.0 - 8.6) * i / nv
        top_row, bot_row = [], []
        for k in range(nu):
            a = 2 * math.pi * k / nu
            x, y = math.cos(a) * r * 1.08, math.sin(a) * r
            lift = ((r - 8.6) / 7.4) ** 2 * (2.2 - 3.4 * max(0.0, math.sin(a)) ** 2)
            top_row.append(bm.verts.new((x, y, lift + 0.3)))
            bot_row.append(bm.verts.new((x, y, lift - 0.3)))
        rows_top.append(top_row)
        rows_bot.append(bot_row)
    for i in range(nv):
        for k in range(nu):
            j = (k + 1) % nu
            bm.faces.new((rows_top[i][k], rows_top[i][j], rows_top[i + 1][j], rows_top[i + 1][k]))
            bm.faces.new((rows_bot[i + 1][k], rows_bot[i + 1][j], rows_bot[i][j], rows_bot[i][k]))
    for k in range(nu):
        j = (k + 1) % nu
        bm.faces.new((rows_top[nv][k], rows_top[nv][j], rows_bot[nv][j], rows_bot[nv][k]))
        bm.faces.new((rows_bot[0][k], rows_bot[0][j], rows_top[0][j], rows_top[0][k]))
    # The crown: an oval tapering up, the top creased down the middle and pinched at the front.
    crown = Part("Felt2", smooth=True)
    rings = []
    levels = 14
    for i in range(levels + 1):
        z = 12.0 * i / levels
        rings.append([])
        for k in range(nu):
            a = 2 * math.pi * k / nu
            r = 9.0 - 1.6 * (i / levels) ** 1.5
            x, y = math.cos(a) * r * 1.1, math.sin(a) * r
            pinch = max(0.0, math.sin(a)) ** 4 * 1.8 * (i / levels) ** 2
            x *= 1.0 - pinch * 0.08
            zz = z - (1.8 * (1.0 - abs(math.cos(a))) ** 3 if i == levels else 0.0)
            rings[-1].append(crown.bm.verts.new((x, y, zz)))
    for a_ring, b_ring in zip(rings, rings[1:]):
        for k in range(nu):
            j = (k + 1) % nu
            crown.bm.faces.new((a_ring[k], a_ring[j], b_ring[j], b_ring[k]))
    centre = crown.bm.verts.new((0.0, 0.0, 10.4))
    bottom = crown.bm.verts.new((0.0, 0.0, 0.0))
    for k in range(nu):
        j = (k + 1) % nu
        crown.bm.faces.new((rings[-1][k], rings[-1][j], centre))
        crown.bm.faces.new((rings[0][j], rings[0][k], bottom))
    lathe(band, [(0.0, 0.2), (9.2, 0.2), (9.25, 3.2), (0.0, 3.2)], Matrix.Diagonal((1.1, 1.0, 1.0, 1.0)), sides=nu)
    ellipsoid(band, (-9.6, 2.0, 1.8), (0.8, 2.4, 1.6), rings=6, sides=10)
    return [felt, crown, band]


MODELS = [
    ("study_door", door),
    ("study_door_case", door_case),
    ("study_desk", desk),
    ("study_tumbler", tumbler_model),
    ("study_tumbler_empty", tumbler_empty),
    ("study_phone", phone),
    ("study_humidor", humidor),
    ("study_ashtray", ashtray),
    ("study_folders", folders),
    ("study_photos", photos),
    ("study_club_chair", club_chair),
    ("study_sofa", sofa),
    ("study_coffee_table", coffee_table),
    ("study_card_table", card_table),
    ("study_cards", cards),
    ("study_tray", tray),
    ("study_bar", bar),
    ("study_safe", safe),
    ("study_bookcase", bookcase),
    ("study_archive_boxes", archive_boxes),
    ("study_board", board),
    ("study_portrait", portrait_frame),
    ("study_map", map_frame),
    ("study_sconce", sconce),
    ("study_coat_stand", coat_stand),
    ("study_fedora", fedora),
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
        print("STUDY baked {} ({} verts, {:.1f} x {:.1f} x {:.1f} cm)".format(name, len(obj.data.vertices), dims.x * 100, dims.y * 100, dims.z * 100))

    big = [o for o in baked if max(o.dimensions) > 0.6]
    small = [o for o in baked if o not in big]
    x = 0.0
    for obj in big:
        width = obj.dimensions.x
        obj.location = (x + width * 0.5, 0.0, 1.2 if obj.name in ("study_board", "study_portrait", "study_map") else 0.0)
        x += width + 0.3
    for i, obj in enumerate(small):
        obj.location = (-0.6 - (i % 6) * 0.45, 1.5 + (i // 6) * 0.45, 0.0)
    K.preview(baked, PREVIEW_DIR, "models", [
        ("all", (x * 0.5, -16.0, 4.0), (x * 0.5, 0.0, 0.8)),
        ("left", (x * 0.12, -4.5, 2.0), (x * 0.12, 0.0, 0.7)),
        ("midleft", (x * 0.37, -4.5, 2.0), (x * 0.37, 0.0, 0.7)),
        ("midright", (x * 0.62, -4.5, 2.0), (x * 0.62, 0.0, 0.7)),
        ("right", (x * 0.87, -4.5, 2.0), (x * 0.87, 0.0, 0.7)),
        ("small", (-1.6, -0.6, 1.1), (-1.6, 2.0, 0.0)),
    ])


if __name__ == "__main__":
    main()
