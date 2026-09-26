"""
Bakes the stained-glass window over the half-landing of the stair hall.

Run with the system Python, not the Editor's — this script does not import unreal:

    python Tools/make_stained_glass.py

It writes into Art/Source/Generated/:

    stained_glass_color.png    RGB: the glass as it looks with the storm behind it — colour, the
                               paint on it, the grime in it. A: whether there is glass there at
                               all (0 where a piece has fallen out of its lead)
    stained_glass_lead.png     the cames, the lead strips the pieces are held in: what the lamp
                               behind the window prints across the stairs
    stained_glass_preview.png  lit from behind as the lightning lights it and as the overcast sky
                               does between strikes, for judging it without a build

WHY A BAKED TEXTURE, and not panes of coloured geometry:

Stained glass is not coloured rectangles in a grid, which is what a window of tinted boxes would
be. It is pieces of glass *cut to a drawing* — each one a polygon whose edges follow the figure
it belongs to — held in lead that runs along every one of those edges. The lead is the drawing,
and it is the thing the eye reads first: what makes a figure in a window a figure is that the
lead line goes round it. A generator can lay that out (a region per colour, cut into pieces,
with the came along every cut) and no arrangement of primitives can.

THE DESIGN is a Victorian memorial window, three lights under a transom and three small lights
above it, which is exactly what a house of this size and date put over its half-landing:

    centre light   a woman holding a little girl by the hand, in a mandorla on a ruby ground.
                   Iconographically a Madonna and child, and nothing more than that to anybody
                   who has not seen the road. Nothing in the house says who they are.
    left light     a lantern in a roundel.
    right light    a clock in a roundel, stopped at four minutes past eleven.
    top lights     three roses.
    inscription    THY WILL BE DONE, across the foot of the centre light.

Units are centimetres of window, measured from the bottom-left corner as seen from inside the
hall. The layout constants must match AGrandStaircaseActor::BuildStainedGlass — the mullions and
the transom are geometry laid over this sheet, and the drawing has to leave room for them.
"""

import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage
from scipy.spatial import cKDTree


HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(HERE)
OUT_DIR = os.path.join(PROJECT_DIR, "Art", "Source", "Generated")
FONT = os.path.join(PROJECT_DIR, "Content", "Fonts", "Cinzel-Bold.ttf")

# The window, in cm. Shared with the C++ (see the module docstring).
WIN_W = 300.0
WIN_H = 450.0
MULLIONS = [(88.0, 100.0), (200.0, 212.0)]
TRANSOM = (340.0, 352.0)

# Square and a power of two, so the engine gives it a full mip chain. The window is not square,
# so a centimetre is 6.8 pixels across and 4.6 up; everything is drawn in centimetres and only
# converted at the last moment, which keeps that out of the design.
RES = 2048
SX = RES / WIN_W
SY = RES / WIN_H

SEED = 1104

LEAD_HALF_CM = 0.62       # a came is about an inch and a quarter of lead, seen face on
FRAME_BAND_CM = 4.0       # the edge of every light is leaded into the frame with a heavier came


# Glass, as display colour with the sky behind it. Victorian glass was never the saturated plastic
# of a modern church kit: pot-metal ruby is nearly brown until light comes through it, and the
# "white" is a greenish, seedy grey.
RUBY = (138, 22, 26)
RUBY_DEEP = (96, 14, 20)
BLUE = (28, 48, 118)
BLUE_DEEP = (18, 30, 82)
AMBER = (196, 132, 36)
GOLD = (214, 168, 64)
PALE = (176, 172, 140)
GRISAILLE = (158, 166, 138)
GREEN = (44, 92, 58)
PURPLE = (84, 40, 92)
FLESH = (206, 170, 138)
WHITE = (204, 200, 182)
LEAD = (8, 8, 9)


def px(u, v):
    """Centimetres (from the bottom left, v up) to pixels (from the top left, y down)."""
    return (u * SX, (WIN_H - v) * SY)


def poly_px(points):
    return [px(u, v) for u, v in points]


class Window:
    """
    A label image (which region every pixel belongs to) plus, per region, a colour and how it is
    to be cut into pieces. Regions are painted in order, so a later one covers an earlier one —
    the figure over the ground, the halo under the head.
    """

    def __init__(self):
        self.label = Image.new("I", (RES, RES), 0)
        self.draw = ImageDraw.Draw(self.label)
        self.regions = [None]  # 0 is "nothing": the mullions, the transom, outside everything

    def region(self, color, cut="voronoi", piece_cm=16.0, jitter=0.12):
        self.regions.append({"color": color, "cut": cut, "piece": piece_cm, "jitter": jitter})
        return len(self.regions) - 1

    def poly(self, points, color, **kw):
        index = self.region(color, **kw)
        self.draw.polygon(poly_px(points), fill=index)
        return index

    def rect(self, u0, v0, u1, v1, color, **kw):
        return self.poly([(u0, v0), (u1, v0), (u1, v1), (u0, v1)], color, **kw)

    def ellipse(self, cu, cv, ru, rv, color, **kw):
        index = self.region(color, **kw)
        x0, y0 = px(cu - ru, cv + rv)
        x1, y1 = px(cu + ru, cv - rv)
        self.draw.ellipse([x0, y0, x1, y1], fill=index)
        return index


def ellipse_points(cu, cv, ru, rv, steps=48, start=0.0, end=2.0 * math.pi):
    return [(cu + ru * math.cos(start + (end - start) * i / steps), cv + rv * math.sin(start + (end - start) * i / steps))
            for i in range(steps + 1)]


def pointed_arch(u0, u1, spring, apex):
    """The head of a lancet: two arcs meeting at a point, from the spring line up to the apex."""
    half = (u1 - u0) * 0.5
    rise = apex - spring
    # An equilateral-ish arch: each side is an arc centred on the far springing point.
    radius = (half * half + rise * rise) / (2.0 * half)
    points = []
    for i in range(17):
        t = i / 16.0
        # Left side, from the left springing up to the apex.
        angle = t * math.asin(rise / radius)
        points.append((u0 + radius - radius * math.cos(angle), spring + radius * math.sin(angle)))
    for i in range(17):
        t = 1.0 - i / 16.0
        angle = t * math.asin(rise / radius)
        points.append((u1 - radius + radius * math.cos(angle), spring + radius * math.sin(angle)))
    return points


def build_design(win, rng):
    lights = [(0.0, MULLIONS[0][0]), (MULLIONS[0][1], MULLIONS[1][0]), (MULLIONS[1][1], WIN_W)]
    top_v0, top_v1 = TRANSOM[1], WIN_H
    main_v0, main_v1 = 0.0, TRANSOM[0]
    border = 5.5

    for index, (u0, u1) in enumerate(lights):
        cu = (u0 + u1) * 0.5

        # ---- the main light ----
        # The border: a run of short ruby and amber pieces, the one element every window of the
        # period has, and the thing that makes three lights read as one design.
        win.rect(u0, main_v0, u1, main_v1, RUBY_DEEP, cut="band", piece_cm=13.0)
        win.rect(u0 + border, main_v0 + border, u1 - border, main_v1 - border, BLUE_DEEP, piece_cm=18.0)

        # A canopy over each field: a pointed arch, blue in the spandrels either side of it.
        field_u0, field_u1 = u0 + border + 1.5, u1 - border - 1.5
        spring = main_v1 - border - 48.0
        apex = main_v1 - border - 6.0
        arch = pointed_arch(field_u0, field_u1, spring, apex)
        inner = [(field_u0, 46.0)] + arch + [(field_u1, 46.0)]

        if index == 1:
            # The centre: a ruby ground behind the figures.
            win.poly(inner, RUBY, piece_cm=15.0)
        else:
            # The sides: grisaille quarries — the cheap pale diamonds that filled most of every
            # window of the time, which is what makes the coloured parts matter.
            win.poly(inner, GRISAILLE, cut="quarry", piece_cm=13.0, jitter=0.06)

        # The arch itself drawn as a gold band just inside the canopy line.
        ring_outer = pointed_arch(field_u0, field_u1, spring, apex)
        ring_inner = pointed_arch(field_u0 + 3.5, field_u1 - 3.5, spring, apex - 4.5)
        win.poly(ring_outer + ring_inner[::-1], GOLD, cut="band", piece_cm=9.0)

        # The inscription panel across the foot.
        win.rect(u0 + border, main_v0 + border, u1 - border, 46.0, PALE, piece_cm=40.0, jitter=0.05)
        win.rect(u0 + border, 44.5, u1 - border, 46.0, GOLD, cut="band", piece_cm=20.0)

        if index == 1:
            build_figures(win, cu)
        else:
            # A roundel in each side light: a blue disc ringed in gold.
            rc_v = 196.0
            win.ellipse(cu, rc_v, 30.0, 30.0, GOLD, cut="ring", piece_cm=10.0)
            win.ellipse(cu, rc_v, 26.0, 26.0, BLUE, piece_cm=14.0)
            if index == 0:
                build_lantern(win, cu, rc_v)
            else:
                build_clock(win, cu, rc_v)

            # Small ruby jewels up and down the quarry field, the way the quarries were enlivened.
            for v in (86.0, 136.0, 256.0):
                win.poly([(cu, v + 7.0), (cu + 6.0, v), (cu, v - 7.0), (cu - 6.0, v)], RUBY)

        # ---- the top light over it ----
        win.rect(u0, top_v0, u1, top_v1, BLUE_DEEP, piece_cm=16.0)
        tcv = (top_v0 + top_v1) * 0.5
        radius = min(u1 - u0, top_v1 - top_v0) * 0.5 - 7.0
        win.ellipse(cu, tcv, radius + 3.5, radius + 3.5, GOLD, cut="ring", piece_cm=9.0)
        win.ellipse(cu, tcv, radius, radius, BLUE, piece_cm=12.0)
        # The rose: eight petals round an amber heart.
        for petal in range(8):
            a = petal * math.pi / 4.0 + (math.pi / 8.0 if index == 1 else 0.0)
            pc = (cu + math.cos(a) * radius * 0.52, tcv + math.sin(a) * radius * 0.52)
            win.ellipse(pc[0], pc[1], radius * 0.24, radius * 0.24, RUBY if petal % 2 == 0 else RUBY_DEEP, cut="single")
        win.ellipse(cu, tcv, radius * 0.26, radius * 0.26, AMBER, cut="single")


def build_figures(win, cu):
    """The woman and the child, in the centre light."""
    # The mandorla: the almond of light the figures stand in, a paler blue so they read against it.
    mcv, mru, mrv = 176.0, 36.0, 118.0
    win.ellipse(cu, mcv, mru + 3.0, mrv + 3.0, GOLD, cut="ring", piece_cm=9.0)
    win.ellipse(cu, mcv, mru, mrv, BLUE, piece_cm=14.0)

    # Ground: a green mound under their feet.
    win.poly(ellipse_points(cu, 60.0, 34.0, 18.0, start=0.0, end=math.pi) + [(cu - 34.0, 64.0)], GREEN, piece_cm=12.0)

    # The woman, a little left of centre, turned towards the child.
    wu = cu - 9.0
    feet = 70.0
    # Halo first, so the head covers its lower half.
    win.ellipse(wu + 1.0, 238.0, 15.5, 15.5, GOLD, cut="ring", piece_cm=8.0)
    # Mantle: blue over the head and down to the hem, open at the front.
    mantle = [(wu - 23.0, feet), (wu - 19.0, 150.0), (wu - 15.0, 205.0), (wu - 12.5, 234.0), (wu - 10.0, 242.0), (wu - 5.0, 247.0),
              (wu + 1.0, 248.5), (wu + 7.0, 246.0), (wu + 11.0, 238.0), (wu + 14.0, 214.0), (wu + 20.0, 170.0), (wu + 24.0, feet)]
    win.poly(mantle, BLUE_DEEP, piece_cm=11.0)
    # Gown: the white of it shows down the middle where the mantle falls open.
    gown = [(wu - 9.0, feet), (wu - 7.0, 150.0), (wu - 5.0, 205.0), (wu + 5.0, 205.0), (wu + 8.0, 150.0), (wu + 12.0, feet)]
    win.poly(gown, WHITE, piece_cm=10.0)
    # Face, under the hood.
    win.ellipse(wu + 1.5, 232.0, 7.0, 8.5, FLESH, cut="single")
    # Her arm across to the child.
    win.poly([(wu + 10.0, 178.0), (wu + 22.0, 150.0), (wu + 26.0, 143.0), (wu + 21.0, 140.0), (wu + 15.0, 150.0), (wu + 6.0, 170.0)],
             BLUE_DEEP, piece_cm=20.0)

    # The child, at her side, reaching up.
    cu2 = cu + 17.0
    win.ellipse(cu2, 160.0, 10.0, 10.0, GOLD, cut="ring", piece_cm=6.0)
    frock = [(cu2 - 10.0, feet), (cu2 - 7.0, 120.0), (cu2 - 5.0, 146.0), (cu2 + 5.0, 146.0), (cu2 + 7.0, 120.0), (cu2 + 10.0, feet)]
    win.poly(frock, AMBER, piece_cm=9.0)
    win.ellipse(cu2, 155.0, 5.0, 6.0, FLESH, cut="single")
    # Her raised arm, and the two hands meeting.
    win.poly([(cu2 - 4.0, 138.0), (cu2 - 9.0, 142.0), (cu2 - 7.5, 145.0), (cu2 - 2.0, 142.0)], AMBER, cut="single")
    win.ellipse(cu2 - 9.5, 142.5, 2.4, 2.4, FLESH, cut="single")


def build_lantern(win, cu, cv):
    """A hanging lantern in the left roundel: a gold frame and an amber flame behind glass."""
    win.poly([(cu - 9.0, cv - 14.0), (cu + 9.0, cv - 14.0), (cu + 7.0, cv + 8.0), (cu - 7.0, cv + 8.0)], PALE, cut="single")
    win.ellipse(cu, cv - 3.0, 3.5, 6.5, AMBER, cut="single")
    win.poly([(cu - 10.0, cv + 8.0), (cu + 10.0, cv + 8.0), (cu, cv + 17.0)], GOLD, cut="single")
    win.poly([(cu - 11.0, cv - 18.0), (cu + 11.0, cv - 18.0), (cu + 9.0, cv - 14.0), (cu - 9.0, cv - 14.0)], GOLD, cut="single")
    win.ellipse(cu, cv + 20.0, 3.0, 3.0, GOLD, cut="single")


def build_clock(win, cu, cv):
    """A clock face in the right roundel. The hands are painted on it (see paint_clock_hands)."""
    win.ellipse(cu, cv, 17.0, 17.0, GOLD, cut="ring", piece_cm=7.0)
    win.ellipse(cu, cv, 14.5, 14.5, WHITE, cut="single")


def cut_pieces(win, rng):
    """Every region cut into the pieces a glazier would cut it into. Returns a piece id per pixel."""
    label = np.asarray(win.label, dtype=np.int32)
    pieces = np.zeros_like(label)
    next_id = 1

    ys, xs = np.mgrid[0:RES, 0:RES]
    # Centimetre coordinates of every pixel centre.
    uu = (xs + 0.5) / SX
    vv = WIN_H - (ys + 0.5) / SY

    for index in range(1, len(win.regions)):
        info = win.regions[index]
        mask = label == index
        if not mask.any():
            continue
        cut = info["cut"]
        size = info["piece"]
        u = uu[mask]
        v = vv[mask]

        if cut == "single":
            ids = np.zeros(u.shape, dtype=np.int64)
        elif cut == "quarry":
            # Diamonds: a lattice at +-60 degrees to the horizontal, which is how quarries were
            # always set — tall diamonds, not squares on their corner.
            a = u / size + v / (size * 1.7)
            b = -u / size + v / (size * 1.7)
            ids = (np.floor(a).astype(np.int64) * 1000 + np.floor(b).astype(np.int64))
        elif cut in ("band", "ring"):
            # A border cut across its length every so often. Along a ring, "along" is the angle.
            cu, cv = u.mean(), v.mean()
            if cut == "ring":
                along = np.arctan2(v - cv, u - cu) * 30.0
            else:
                span_u = u.max() - u.min()
                span_v = v.max() - v.min()
                along = v if span_v > span_u else u
            ids = np.floor(along / size).astype(np.int64)
        else:
            # Voronoi: seeds scattered through the region, each pixel belonging to its nearest.
            # A Voronoi cell is a convex polygon with straight sides, which is exactly what a cut
            # piece of glass is — nobody cuts glass along a curve they do not have to.
            area = mask.sum() / (SX * SY)
            count = max(1, int(area / (size * size)))
            pick = rng.choice(len(u), size=min(count, len(u)), replace=False)
            tree = cKDTree(np.stack([u[pick], v[pick]], axis=1))
            _, ids = tree.query(np.stack([u, v], axis=1))
            ids = ids.astype(np.int64)

        # Renumber into the global piece ids.
        unique, inverse = np.unique(ids, return_inverse=True)
        pieces[mask] = next_id + inverse
        next_id += len(unique)

    return pieces, label


def lead_lines(pieces):
    """The cames: wherever two pieces meet, and all round the edge of every light."""
    edge = np.zeros(pieces.shape, dtype=bool)
    edge[:, 1:] |= pieces[:, 1:] != pieces[:, :-1]
    edge[:, :-1] |= pieces[:, 1:] != pieces[:, :-1]
    edge[1:, :] |= pieces[1:, :] != pieces[:-1, :]
    edge[:-1, :] |= pieces[1:, :] != pieces[:-1, :]
    # Distance in centimetres, not pixels: the texture is stretched and the lead must not be.
    distance = ndimage.distance_transform_edt(~edge, sampling=(1.0 / SY, 1.0 / SX))
    return distance


def value_noise(rng, cells_u, cells_v, octaves=4):
    """Smooth noise over the whole sheet, for streaks in the glass and the grime on it."""
    total = np.zeros((RES, RES), dtype=np.float32)
    amplitude = 1.0
    norm = 0.0
    for octave in range(octaves):
        cu = int(cells_u * 2 ** octave) + 2
        cv = int(cells_v * 2 ** octave) + 2
        grid = rng.random((cv, cu)).astype(np.float32)
        zoomed = ndimage.zoom(grid, (RES / (cv - 1), RES / (cu - 1)), order=3)[:RES, :RES]
        total += zoomed * amplitude
        norm += amplitude
        amplitude *= 0.5
    return total / norm


def paint(image, strokes, width_cm, strength=0.9):
    """Grisaille: dark vitreous paint fired onto the glass. Lines on the glass, not lead between it."""
    layer = Image.new("L", (RES, RES), 0)
    draw = ImageDraw.Draw(layer)
    width = max(1, int(width_cm * (SX + SY) * 0.5))
    for stroke in strokes:
        draw.line(poly_px(stroke), fill=255, width=width, joint="curve")
    mask = np.asarray(layer, dtype=np.float32) / 255.0
    mask = ndimage.gaussian_filter(mask, 0.8)
    image *= (1.0 - mask * strength)[..., None]


def paint_details(image, window_center):
    cu = window_center
    wu = cu - 9.0
    # The fall of the mantle, a few long strokes, and her eyes closed, looking down.
    folds = [[(wu - 16.0, 80.0), (wu - 13.0, 150.0), (wu - 11.0, 200.0)],
             [(wu + 18.0, 80.0), (wu + 15.0, 150.0), (wu + 12.0, 200.0)],
             [(wu - 2.0, 80.0), (wu - 1.0, 140.0)],
             [(wu + 5.0, 80.0), (wu + 4.0, 130.0)]]
    face = [[(wu - 2.0, 233.5), (wu - 0.2, 232.3), (wu + 0.6, 233.2)],
            [(wu + 2.6, 233.2), (wu + 3.6, 232.3), (wu + 5.2, 233.5)],
            [(wu + 0.2, 227.0), (wu + 2.8, 226.6)]]
    child = [[(cu + 15.0, 156.0), (cu + 16.4, 155.4)], [(cu + 18.0, 155.4), (cu + 19.4, 156.0)],
             [(cu + 11.0, 80.0), (cu + 12.5, 120.0)], [(cu + 22.0, 80.0), (cu + 21.0, 120.0)]]
    paint(image, folds, 0.55)
    paint(image, face + child, 0.4, strength=0.8)


def paint_clock_hands(image, cu, cv):
    """Four minutes past eleven: the time every clock in the house has stopped at."""
    strokes = []
    for hour in range(12):
        a = math.radians(90.0 - hour * 30.0)
        strokes.append([(cu + math.cos(a) * 11.0, cv + math.sin(a) * 11.0), (cu + math.cos(a) * 13.5, cv + math.sin(a) * 13.5)])
    hour_a = math.radians(90.0 - (11.0 + 4.0 / 60.0) * 30.0)
    minute_a = math.radians(90.0 - 4.0 * 6.0)
    strokes.append([(cu, cv), (cu + math.cos(hour_a) * 7.5, cv + math.sin(hour_a) * 7.5)])
    strokes.append([(cu, cv), (cu + math.cos(minute_a) * 11.5, cv + math.sin(minute_a) * 11.5)])
    paint(image, strokes, 1.1, strength=0.95)


def paint_inscription(image, cu, text):
    """Letters fired onto the inscription panel, the way a glazier's writer did it: dark on pale."""
    try:
        font = ImageFont.truetype(FONT, int(7.0 * SY))
    except OSError:
        font = ImageFont.load_default()
    layer = Image.new("L", (RES, RES), 0)
    draw = ImageDraw.Draw(layer)
    x, y = px(cu, 27.0)
    box = draw.textbbox((0, 0), text, font=font)
    width = box[2] - box[0]
    height = box[3] - box[1]
    # Squeezed across: the sheet is stretched horizontally, so text drawn square would come out
    # half again as wide as it is tall on the wall.
    wide = Image.new("L", (int(width + 8), int(height + 8)), 0)
    ImageDraw.Draw(wide).text((4 - box[0], 4 - box[1]), text, font=font, fill=255)
    wide = wide.resize((max(1, int(wide.width * SX / SY * 0.62)), wide.height), Image.LANCZOS)
    layer.paste(wide, (int(x - wide.width / 2), int(y - wide.height / 2)))
    mask = np.asarray(layer, dtype=np.float32) / 255.0
    image *= (1.0 - mask * 0.88)[..., None]


def damage(pieces, label, lead_distance, rng):
    """
    What fifty winters and one thrown stone have done. Glass leaves a leaded window a whole piece
    at a time — the came holds the edge, the piece cracks, and one day it drops out — so the
    losses are whole pieces with the lead still standing round the hole. Where the stone went
    through, the lead went with it.
    """
    glass = np.ones(pieces.shape, dtype=bool)
    ys, xs = np.mgrid[0:RES, 0:RES]
    uu = (xs + 0.5) / SX
    vv = WIN_H - (ys + 0.5) / SY

    # The hole, low in the left light, where something came through from outside. Its edge is
    # torn by noise so it is no shape anybody drew.
    hc = np.array([44.0, 118.0])
    angle = np.arctan2(vv - hc[1], uu - hc[0])
    radius = np.hypot(uu - hc[0], (vv - hc[1]) * 0.85)
    ragged = 14.0 + 4.5 * np.sin(angle * 3.0 + 1.1) + 2.5 * np.sin(angle * 7.0 + 0.4) + 1.5 * np.sin(angle * 13.0)
    hole = radius < ragged
    glass &= ~hole
    lead_gone = radius < ragged * 0.72

    # Whole pieces dropped out elsewhere: a few, and not evenly.
    candidates = [(150.0, 300.0), (128.0, 262.0), (236.0, 96.0), (268.0, 312.0), (262.0, 402.0), (22.0, 380.0), (60.0, 290.0)]
    for u, v in candidates:
        x, y = int(u * SX), int((WIN_H - v) * SY)
        piece = pieces[y, x]
        if label[y, x] != 0:
            glass &= pieces != piece

    return glass, lead_gone


def cracks(image, rng):
    """Splits running across single pieces — dark hairlines, not lead."""
    strokes = []
    for _ in range(14):
        u = rng.uniform(8.0, WIN_W - 8.0)
        v = rng.uniform(10.0, WIN_H - 10.0)
        length = rng.uniform(6.0, 20.0)
        a = rng.uniform(0.0, math.pi)
        points = [(u, v)]
        for step in range(3):
            a += rng.uniform(-0.6, 0.6)
            u += math.cos(a) * length / 3.0
            v += math.sin(a) * length / 3.0
            points.append((u, v))
        strokes.append(points)
    paint(image, strokes, 0.18, strength=0.85)


def main():
    rng = np.random.default_rng(SEED)
    os.makedirs(OUT_DIR, exist_ok=True)

    win = Window()
    build_design(win, rng)

    pieces, label = cut_pieces(win, rng)

    # Nothing under the mullions and the transom: they are timber laid over the sheet.
    ys, xs = np.mgrid[0:RES, 0:RES]
    uu = (xs + 0.5) / SX
    vv = WIN_H - (ys + 0.5) / SY
    structure = np.zeros((RES, RES), dtype=bool)
    for u0, u1 in MULLIONS:
        structure |= (uu > u0) & (uu < u1)
    structure |= (vv > TRANSOM[0]) & (vv < TRANSOM[1])
    pieces[structure] = 0

    lead_distance = lead_lines(pieces)

    # Colour per piece: every piece is its own sheet of glass from its own pot, so no two pieces
    # of "ruby" are the same ruby.
    image = np.zeros((RES, RES, 3), dtype=np.float32)
    piece_ids = np.unique(pieces)
    base = np.zeros((piece_ids.max() + 1, 3), dtype=np.float32)
    region_of_piece = np.zeros(piece_ids.max() + 1, dtype=np.int32)
    region_of_piece[pieces.ravel()] = label.ravel()
    for pid in piece_ids:
        region = region_of_piece[pid]
        if pid == 0 or region == 0:
            continue
        info = win.regions[region]
        color = np.array(info["color"], dtype=np.float32) / 255.0
        jitter = info["jitter"]
        color = color * rng.uniform(1.0 - jitter * 1.4, 1.0 + jitter)
        color = color + rng.normal(0.0, jitter * 0.08, 3)
        base[pid] = np.clip(color, 0.0, 1.0)
    image = base[pieces]

    # Streaks and seed in the glass: antique glass is never even, and the streaks run one way.
    streak = value_noise(rng, 3, 14, octaves=4)
    image *= (0.78 + 0.44 * streak)[..., None]

    paint_details(image, (MULLIONS[0][1] + MULLIONS[1][0]) * 0.5)
    paint_clock_hands(image, (MULLIONS[1][1] + WIN_W) * 0.5, 196.0)
    paint_inscription(image, (MULLIONS[0][1] + MULLIONS[1][0]) * 0.5, "THY WILL BE DONE")
    cracks(image, rng)

    # Grime. Dirt gathers against the lead in every piece, heaviest at the bottom of the window
    # where the rain runs down the outside and dries, and in blooms where the damp has got in.
    against_lead = np.clip(lead_distance / 2.4, 0.0, 1.0)
    grime = value_noise(rng, 6, 9, octaves=5)
    low = np.clip(vv / 90.0, 0.0, 1.0)
    dirt = (0.58 + 0.42 * against_lead) * (0.62 + 0.38 * grime) * (0.55 + 0.45 * low)
    image *= dirt[..., None]

    glass, lead_gone = damage(pieces, label, lead_distance, rng)

    lead = (lead_distance < LEAD_HALF_CM) | structure
    # The heavier came where each light meets the frame.
    lead |= (uu < FRAME_BAND_CM * 0.5) | (uu > WIN_W - FRAME_BAND_CM * 0.5)
    lead |= (vv < FRAME_BAND_CM * 0.5) | (vv > WIN_H - FRAME_BAND_CM * 0.5)
    lead &= ~lead_gone

    image[lead] = np.array(LEAD, dtype=np.float32) / 255.0
    # The lead is drawn into the glass as well as onto its own sheet: black, opaque, where a came is.
    # The came sheet is what casts the shadow; this is what guarantees no line of sky shows between
    # two pieces that are still in their lead.
    alpha = (glass & (pieces != 0)) | lead

    color_out = np.dstack([np.clip(image, 0.0, 1.0), alpha.astype(np.float32)])
    Image.fromarray((color_out * 255.0 + 0.5).astype(np.uint8), mode="RGBA").save(
        os.path.join(OUT_DIR, "stained_glass_color.png"))
    Image.fromarray((lead.astype(np.float32) * 255.0).astype(np.uint8), mode="L").save(
        os.path.join(OUT_DIR, "stained_glass_lead.png"))

    # Preview: the window as the hall sees it, lit from behind by the flash (left) and by the dull
    # sky between strikes (right), at the window's real proportions.
    sky = np.linspace(0.30, 0.12, RES, dtype=np.float32)[:, None, None] * np.array([0.55, 0.65, 0.85], dtype=np.float32)
    lit = np.where(alpha[..., None], image, np.where(lead[..., None], 0.02, sky))
    dim = np.where(alpha[..., None], image * 0.28, np.where(lead[..., None], 0.01, sky * 0.25))
    tall = int(RES * 0.5 * WIN_H / WIN_W)
    frames = [Image.fromarray((np.clip(f, 0, 1) * 255).astype(np.uint8), mode="RGB").resize((RES // 2, tall), Image.LANCZOS)
              for f in (lit, dim)]
    sheet = Image.new("RGB", (RES, tall), (0, 0, 0))
    sheet.paste(frames[0], (0, 0))
    sheet.paste(frames[1], (RES // 2, 0))
    sheet.save(os.path.join(OUT_DIR, "stained_glass_preview.png"))
    print("Wrote stained_glass_color.png, stained_glass_lead.png, stained_glass_preview.png to " + OUT_DIR)


if __name__ == "__main__":
    main()
