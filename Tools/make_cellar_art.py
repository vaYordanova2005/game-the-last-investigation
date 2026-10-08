#!/usr/bin/env python3
"""
Bakes the wine cellar's printed and written paper into texture sets the art pipeline imports.

    python Tools/make_cellar_art.py

Two sets, written as <set>_Diffuse / _nor_dx / _arm (and _opacity) into Art/Source/Textures:

    wine_paper   a 4 x 4 atlas of 512px cells:
                   cells 0-7    eight bottle labels, each in the top 512 x 410 of its cell (a label
                                wrapped round 150 degrees of a bottle is about 10 x 8 cm)
                   cells 8, 9   the inventory book open: left and right page (366 x 512, left-aligned)
                   cells 10, 11 the leather notebook open: left and right page, the same size
                   cells 12-13  the bin chart: which wine is in which rack, 1024 x 512 across both
                   cell 14      a small engraved view of a chateau, for a frame in the alcove
    wine_brands  the names of the vineyards burned into the end boards of the crates: 2 x 4 cells
                 of 512 x 256, dark letters with a border, and an opacity map so only the burn shows.

Why baked: a label is a picture, and so is a page of handwriting. Neither the photographed library
nor a material graph has them, and handwriting built from planes (the kitchen's Writing) reads at
arm's length but not as a ruled ledger page. The handwriting here is a cursive *hand*, not text:
loops along a baseline, words of uneven length, nothing legible. The clue writing is the user's to
do when the game is built (the 09-30 decision), so nothing in this file says anything.

The only typeface is Cinzel (SIL Open Font License, Content/Fonts/OFL.txt), which is what the
house's other inscriptions are set in. The vineyard names are invented.
"""

import math
import os
import random

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXTURES = os.path.join(ROOT, "Art", "Source", "Textures")
PREVIEW = os.path.join(ROOT, "Saved", "CellarPreview")
FONT_BOLD = os.path.join(ROOT, "Content", "Fonts", "Cinzel-Bold.ttf")
FONT_REGULAR = os.path.join(ROOT, "Content", "Fonts", "Cinzel-Regular.ttf")
FONT_BLACK = os.path.join(ROOT, "Content", "Fonts", "Cinzel-Black.ttf")

SIZE = 2048
CELL = 512
# Everything is drawn at twice the size and brought down, so strokes come out antialiased.
SS = 2

LABEL_H = 410
PAGE_W = 366


def font(path, size):
    return ImageFont.truetype(path, int(size * SS))


def srgb_to_linear(a):
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)


def noise(w, h, scale, seed, octaves=4):
    """Value noise in [0, 1], summed over octaves: foxing, grime and the wander of a hand."""
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w), dtype=np.float32)
    amp, total = 1.0, 0.0
    for o in range(octaves):
        cells = max(2, int(scale * (2 ** o)))
        grid = rng.random((cells + 1, cells + 1)).astype(np.float32)
        img = Image.fromarray((grid * 255).astype(np.uint8), "L").resize((w, h), Image.BICUBIC)
        out += np.asarray(img, dtype=np.float32) / 255.0 * amp
        total += amp
        amp *= 0.5
    return out / total


# ---------------------------------------------------------------------------------------------
# Ageing: what fifty years in a damp cellar does to paper.
# ---------------------------------------------------------------------------------------------

def age_paper(rgb, seed, foxing=1.0, damp=1.0, edge=1.0, dust=1.0):
    """rgb: float sRGB array (h, w, 3). Darkened edges, foxing spots, a tide-mark, dust."""
    h, w, _ = rgb.shape
    rng = np.random.default_rng(seed)
    out = rgb.copy()

    # Edges brown first: the paper oxidises from where the air gets at it.
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    d = np.minimum(np.minimum(xx, w - 1 - xx), np.minimum(yy, h - 1 - yy)) / (min(w, h) * 0.5)
    wob = noise(w, h, 6, seed + 1)
    edge_t = np.clip(1.0 - d * 6.0 + (wob - 0.5) * 0.8, 0.0, 1.0) * edge
    brown = np.array([0.42, 0.30, 0.17], dtype=np.float32)
    out = out * (1.0 - edge_t[..., None] * 0.55) + brown * edge_t[..., None] * 0.55 * 0.6

    # A tide-mark of damp: a soft brown bloom with a darker rim where the water stopped.
    if damp > 0:
        cx, cy = rng.uniform(0.1, 0.9) * w, rng.uniform(0.5, 1.1) * h
        r = rng.uniform(0.35, 0.7) * max(w, h)
        dist = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2) / r + (noise(w, h, 5, seed + 2) - 0.5) * 0.5
        inside = np.clip((1.0 - dist) * 3.0, 0.0, 1.0)
        rim = np.exp(-((dist - 1.0) ** 2) / 0.0012)
        stain = (inside * 0.22 + rim * 0.35) * damp
        out = out * (1.0 - stain[..., None] * 0.5) + np.array([0.48, 0.36, 0.20]) * stain[..., None] * 0.5

    # Foxing: rust-brown specks, mostly small, a few with a halo.
    if foxing > 0:
        layer = Image.new("L", (w, h), 0)
        draw = ImageDraw.Draw(layer)
        for _ in range(int(60 * foxing * (w * h) / (512 * 410))):
            x, y = rng.uniform(0, w), rng.uniform(0, h)
            s = rng.choice([1.0, 1.5, 2.5, 4.0, 7.0], p=[0.4, 0.3, 0.17, 0.09, 0.04])
            draw.ellipse([x - s, y - s, x + s, y + s], fill=int(rng.uniform(80, 200)))
        spots = np.asarray(layer.filter(ImageFilter.GaussianBlur(1.2)), dtype=np.float32) / 255.0
        out = out * (1.0 - spots[..., None] * 0.6) + np.array([0.45, 0.26, 0.12]) * spots[..., None] * 0.6

    # Grime in the grain, and a grey film of dust that lightens and flattens everything.
    grain = noise(w, h, 40, seed + 3, octaves=3)
    out *= (0.9 + grain * 0.2)[..., None]
    if dust > 0:
        film = noise(w, h, 3, seed + 4) * dust
        out = out * (1.0 - film[..., None] * 0.35) + np.array([0.55, 0.53, 0.50]) * film[..., None] * 0.35
    return np.clip(out, 0.0, 1.0)


def tear_mask(w, h, seed, corners=1, bites=1):
    """Alpha for a label that has lost a corner or had a bite taken out of its edge."""
    rng = np.random.default_rng(seed)
    mask = Image.new("L", (w, h), 255)
    draw = ImageDraw.Draw(mask)
    for _ in range(corners):
        cx = rng.choice([0, w])
        cy = rng.choice([0, h])
        pts = []
        steps = 9
        a0 = rng.uniform(0.18, 0.32) * w
        b0 = rng.uniform(0.18, 0.32) * h
        for k in range(steps + 1):
            t = k / steps
            px = cx + (a0 if cx == 0 else -a0) * (1 - t) + rng.uniform(-6, 6)
            py = cy + (b0 if cy == 0 else -b0) * t + rng.uniform(-6, 6)
            pts.append((px, py))
        pts.append((cx, cy))
        draw.polygon(pts, fill=0)
    for _ in range(bites):
        x = rng.uniform(0.2, 0.8) * w
        y = rng.choice([0, h])
        r = rng.uniform(10, 26)
        pts = [(x + math.cos(a) * r * rng.uniform(0.7, 1.3), y + math.sin(a) * r * rng.uniform(0.7, 1.3))
               for a in np.linspace(0, 2 * math.pi, 14, endpoint=False)]
        draw.polygon(pts, fill=0)
    return np.asarray(mask.filter(ImageFilter.GaussianBlur(0.8)), dtype=np.float32) / 255.0


# ---------------------------------------------------------------------------------------------
# Handwriting: a cursive hand, not text.
# ---------------------------------------------------------------------------------------------

def cursive_word(draw, x, y, length, xheight, rng, colour, width):
    """One word: a run of loops along the baseline, the odd ascender and descender."""
    pts = []
    t = 0.0
    step = xheight * 0.5
    while t < length:
        # A letter is one or two humps; now and then a tall stroke or a tail below the line.
        kind = rng.random()
        if kind < 0.12:
            peak = -xheight * rng.uniform(1.9, 2.4)
        elif kind < 0.2:
            peak = xheight * rng.uniform(0.9, 1.3)
        else:
            peak = -xheight * rng.uniform(0.75, 1.05)
        pts.append((x + t, y))
        pts.append((x + t + step * 0.35, y + peak))
        pts.append((x + t + step * 0.7, y + peak * 0.25))
        t += step * rng.uniform(0.85, 1.25)
    pts.append((x + length, y - xheight * 0.2))
    # Smooth the zig-zag into a hand: average neighbours twice.
    for _ in range(2):
        pts = [pts[0]] + [((pts[i - 1][0] + 2 * pts[i][0] + pts[i + 1][0]) / 4, (pts[i - 1][1] + 2 * pts[i][1] + pts[i + 1][1]) / 4)
                          for i in range(1, len(pts) - 1)] + [pts[-1]]
    draw.line(pts, fill=colour, width=width, joint="curve")
    # A cross-bar or a dot over a few.
    if rng.random() < 0.35:
        cx = x + rng.uniform(0.2, 0.8) * length
        draw.line([(cx - xheight * 0.6, y - xheight * 1.6), (cx + xheight * 0.7, y - xheight * 1.7)], fill=colour, width=max(1, width - 1))


def handwriting(draw, x0, y0, x1, line_pitch, lines, rng, colour, width, xheight, short_last=True, indent=0.0):
    y = y0
    for l in range(lines):
        last = short_last and (rng.random() < 0.18 or l == lines - 1)
        end = x0 + (x1 - x0) * (rng.uniform(0.3, 0.65) if last else rng.uniform(0.88, 1.0))
        cursor = x0 + (indent if l > 0 and rng.random() < 0.2 else 0.0)
        while cursor < end - xheight * 2:
            length = min(rng.uniform(2.0, 7.5) * xheight, end - cursor)
            cursive_word(draw, cursor, y + rng.uniform(-0.6, 0.6) * SS, length, xheight, rng, colour, width)
            cursor += length + rng.uniform(1.4, 2.2) * xheight
        y += line_pitch


def figures(draw, x, y, count, xheight, rng, colour, width):
    """A column of numbers in a hand: short upright strokes and loops, the shape of figures."""
    cx = x
    for _ in range(count):
        kind = rng.integers(0, 4)
        h = xheight * 1.4
        if kind == 0:
            draw.line([(cx, y), (cx + xheight * 0.2, y - h)], fill=colour, width=width)
        elif kind == 1:
            draw.ellipse([cx - xheight * 0.35, y - h, cx + xheight * 0.35, y], outline=colour, width=width)
        elif kind == 2:
            draw.arc([cx - xheight * 0.4, y - h, cx + xheight * 0.4, y - h * 0.4], 180, 30, fill=colour, width=width)
            draw.line([(cx + xheight * 0.3, y - h * 0.55), (cx - xheight * 0.4, y), (cx + xheight * 0.45, y)], fill=colour, width=width)
        else:
            draw.arc([cx - xheight * 0.4, y - h, cx + xheight * 0.4, y - h * 0.5], 160, 400, fill=colour, width=width)
            draw.arc([cx - xheight * 0.45, y - h * 0.55, cx + xheight * 0.45, y], 200, 520, fill=colour, width=width)
        cx += xheight * 0.95


# ---------------------------------------------------------------------------------------------
# Labels.
# ---------------------------------------------------------------------------------------------

def draw_chateau(draw, cx, base, scale, colour, width):
    """An engraved chateau: a body, two towers with pointed roofs, a door, windows, a line of vines."""
    s = scale
    draw.rectangle([cx - 60 * s, base - 50 * s, cx + 60 * s, base], outline=colour, width=width)
    draw.polygon([(cx - 66 * s, base - 50 * s), (cx, base - 82 * s), (cx + 66 * s, base - 50 * s)], outline=colour, width=width)
    for tx in (-75, 75):
        draw.rectangle([cx + (tx - 14) * s, base - 72 * s, cx + (tx + 14) * s, base], outline=colour, width=width)
        draw.polygon([(cx + (tx - 18) * s, base - 72 * s), (cx + tx * s, base - 110 * s), (cx + (tx + 18) * s, base - 72 * s)], outline=colour, width=width)
        draw.rectangle([cx + (tx - 5) * s, base - 56 * s, cx + (tx + 5) * s, base - 44 * s], outline=colour, width=max(1, width - 1))
    draw.arc([cx - 11 * s, base - 30 * s, cx + 11 * s, base - 8 * s], 180, 360, fill=colour, width=width)
    draw.line([(cx - 11 * s, base - 19 * s), (cx - 11 * s, base)], fill=colour, width=width)
    draw.line([(cx + 11 * s, base - 19 * s), (cx + 11 * s, base)], fill=colour, width=width)
    for wx in (-42, -24, 24, 42):
        draw.rectangle([cx + (wx - 5) * s, base - 40 * s, cx + (wx + 5) * s, base - 26 * s], outline=colour, width=max(1, width - 1))
    # Hatching on the roof and the ground: what makes a line drawing an engraving.
    for k in range(9):
        x = cx - 50 * s + k * 12 * s
        draw.line([(x, base - 52 * s), (x + 12 * s, base - 70 * s)], fill=colour, width=max(1, width - 2))
    for row in range(3):
        y = base + (8 + row * 9) * s
        for k in range(14):
            x = cx - 120 * s + k * 18 * s + (row % 2) * 9 * s
            draw.arc([x, y - 5 * s, x + 12 * s, y + 5 * s], 180, 360, fill=colour, width=max(1, width - 2))


def draw_grapes(draw, cx, cy, scale, colour, width):
    s = scale
    rows = [3, 4, 3, 2, 1]
    y = cy - 30 * s
    for r, n in enumerate(rows):
        for k in range(n):
            x = cx + (k - (n - 1) / 2) * 15 * s
            draw.ellipse([x - 7.5 * s, y - 7.5 * s, x + 7.5 * s, y + 7.5 * s], outline=colour, width=width)
        y += 12.5 * s
    draw.line([(cx, cy - 38 * s), (cx + 6 * s, cy - 56 * s)], fill=colour, width=width)
    leaf = [(cx + 6 * s, cy - 50 * s), (cx + 30 * s, cy - 62 * s), (cx + 44 * s, cy - 48 * s), (cx + 34 * s, cy - 40 * s), (cx + 18 * s, cy - 42 * s)]
    draw.polygon(leaf, outline=colour, width=width)
    draw.arc([cx - 30 * s, cy - 66 * s, cx - 2 * s, cy - 40 * s], 200, 340, fill=colour, width=max(1, width - 1))


def draw_crest(draw, cx, cy, scale, colour, width, fill=None):
    s = scale
    shield = [(cx - 26 * s, cy - 32 * s), (cx + 26 * s, cy - 32 * s), (cx + 26 * s, cy + 2 * s),
              (cx, cy + 30 * s), (cx - 26 * s, cy + 2 * s)]
    draw.polygon(shield, outline=colour, fill=fill, width=width)
    draw.line([(cx - 26 * s, cy - 10 * s), (cx + 26 * s, cy - 10 * s)], fill=colour, width=width)
    draw.line([(cx, cy - 32 * s), (cx, cy + 30 * s)], fill=colour, width=width)
    for k, (a, b) in enumerate(((-13, -22), (13, 8))):
        draw.ellipse([cx + a * s - 5 * s, cy + b * s - 5 * s, cx + a * s + 5 * s, cy + b * s + 5 * s], outline=colour, width=width)
    # Laurels either side.
    for side in (-1, 1):
        for k in range(6):
            t = k / 5
            x = cx + side * (34 + 8 * math.sin(t * 2.5)) * s
            y = cy + (24 - t * 52) * s
            draw.ellipse([x - 4 * s, y - 7 * s, x + 4 * s, y + 7 * s], outline=colour, width=max(1, width - 1))


def centred(draw, text, cx, y, fnt, colour, spacing=0):
    if spacing:
        # Letter-spaced capitals, as a label printer set them.
        widths = [draw.textlength(ch, font=fnt) for ch in text]
        total = sum(widths) + spacing * (len(text) - 1)
        x = cx - total / 2
        for ch, w in zip(text, widths):
            draw.text((x, y), ch, font=fnt, fill=colour)
            x += w + spacing
    else:
        w = draw.textlength(text, font=fnt)
        draw.text((cx - w / 2, y), text, font=fnt, fill=colour)


LABELS = [
    # name lines, sub line, year, paper (sRGB), ink, accent, emblem, seed
    (["CHATEAU", "VALMONT"], "GRAND VIN DE BORDEAUX", "1947", "#ecdfc0", "#2a2118", "#7a1c18", "chateau", 11),
    (["DOMAINE", "DES BRUMES"], "COTE DE NUITS", "1952", "#e8e2cc", "#1d1a16", "#8a6a22", "grapes", 12),
    (["CLOS", "SAINT-AUBIN"], "PREMIER CRU", "1959", "#f0e4c6", "#30221a", "#5d1420", "crest", 13),
    (["MAISON", "LAVERGNE"], "VIN ROUGE", "1961", "#e4d6b4", "#221c15", "#24384a", "chateau", 14),
    (["CHATEAU", "DE CORBAS"], "SAINT-EMILION", "1955", "#efe6cc", "#2b2017", "#6e2a14", "crest", 15),
    (["DOMAINE", "ROCHEVAL"], "VIN DE GARDE", "1964", "#ddd3b6", "#1e1a14", "#3e4a20", "grapes", 16),
    (["LES COTEAUX", "D'ARLANE"], "RESERVE", "1949", "#ece2c8", "#271e16", "#7d6224", "chateau", 17),
    (["CHATEAU", "MONTFERRAND"], "GRAND CRU CLASSE", "1945", "#e9dbb8", "#251c14", "#6a1218", "crest", 18),
]


def hexrgb(code):
    return tuple(int(code[i:i + 2], 16) for i in (1, 3, 5))


def make_label(index):
    names, sub, year, paper, ink, accent, emblem, seed = LABELS[index]
    rng = np.random.default_rng(seed)
    w, h = CELL * SS, LABEL_H * SS
    img = Image.new("RGB", (w, h), hexrgb(paper))
    draw = ImageDraw.Draw(img)
    inkc, accc = hexrgb(ink), hexrgb(accent)

    # The border: a double rule, the inner one with clipped corners.
    m = 16 * SS
    draw.rectangle([m, m, w - m, h - m], outline=inkc, width=3 * SS)
    m2 = 26 * SS
    c = 14 * SS
    draw.polygon([(m2 + c, m2), (w - m2 - c, m2), (w - m2, m2 + c), (w - m2, h - m2 - c), (w - m2 - c, h - m2),
                  (m2 + c, h - m2), (m2, h - m2 - c), (m2, m2 + c)], outline=accc, width=SS + 1)

    cx = w / 2
    big = font(FONT_BLACK, 50 if max(len(n) for n in names) < 10 else 40)
    small = font(FONT_BOLD, 17)
    tiny = font(FONT_REGULAR, 13)
    centred(draw, sub, cx, 44 * SS, small, accc, spacing=3 * SS)
    draw.line([(cx - 120 * SS, 72 * SS), (cx + 120 * SS, 72 * SS)], fill=inkc, width=SS)

    if emblem == "chateau":
        draw_chateau(draw, cx, 196 * SS, 0.95 * SS, inkc, 2 * SS)
    elif emblem == "grapes":
        draw_grapes(draw, cx, 160 * SS, 1.5 * SS, inkc, 2 * SS)
    else:
        draw_crest(draw, cx, 150 * SS, 1.5 * SS, inkc, 2 * SS)

    y = 214 * SS
    for name in names:
        centred(draw, name, cx, y, big, inkc, spacing=2 * SS)
        y += 50 * SS if len(names[0]) < 10 else 44 * SS
    centred(draw, "MIS EN BOUTEILLE AU CHATEAU", cx, y + 2 * SS, tiny, inkc, spacing=2 * SS)
    # The vintage in the accent colour, in a cartouche at the foot.
    yy = h - 56 * SS
    draw.rounded_rectangle([cx - 54 * SS, yy - 4 * SS, cx + 54 * SS, yy + 32 * SS], radius=10 * SS, outline=accc, width=2 * SS)
    centred(draw, year, cx, yy - 2 * SS, font(FONT_BOLD, 28), accc, spacing=2 * SS)

    img = img.resize((CELL, LABEL_H), Image.LANCZOS)
    rgb = np.asarray(img, dtype=np.float32) / 255.0
    # A slight smear of the print, as letterpress on rag paper has.
    rgb = np.asarray(Image.fromarray((rgb * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.4)), dtype=np.float32) / 255.0
    rgb = age_paper(rgb, seed, foxing=rng.uniform(0.6, 1.4), damp=rng.uniform(0.3, 1.0), edge=1.0, dust=rng.uniform(0.5, 1.0))
    # Torn and scuffed: where the paper is gone the glass shows, so the mask darkens to bottle green.
    mask = tear_mask(CELL, LABEL_H, seed + 50, corners=int(rng.integers(0, 2)), bites=int(rng.integers(0, 2)))
    glass = np.array([0.05, 0.08, 0.06], dtype=np.float32)
    rgb = rgb * mask[..., None] + glass * (1.0 - mask[..., None])
    return rgb


# ---------------------------------------------------------------------------------------------
# Pages.
# ---------------------------------------------------------------------------------------------

def make_ledger_page(right, seed):
    """The inventory book: ruled columns, a heading line, entries down the page in a brown ink."""
    rng = np.random.default_rng(seed)
    w, h = PAGE_W * SS, CELL * SS
    img = Image.new("RGB", (w, h), hexrgb("#e6dbbf"))
    draw = ImageDraw.Draw(img)
    rule = hexrgb("#8fa0b0")
    red = hexrgb("#a2463c")
    inkc = hexrgb("#3d2618")
    top = 58 * SS
    pitch = 17 * SS
    for k in range(int((h - top - 20 * SS) / pitch)):
        y = top + k * pitch
        draw.line([(10 * SS, y), (w - 10 * SS, y)], fill=rule, width=1)
    # Columns: bin, wine, vintage, bottles in, bottles out.
    cols = [26, 70, 250, 300, 334] if not right else [22, 52, 232, 284, 322]
    for cx in cols:
        draw.line([(cx * SS, top - 26 * SS), (cx * SS, h - 16 * SS)], fill=red, width=SS)
    draw.line([(10 * SS, top - 2 * SS), (w - 10 * SS, top - 2 * SS)], fill=red, width=SS)
    # Headings, written.
    for a, b in zip(cols[:-1], cols[1:]):
        cursive_word(draw, (a + 6) * SS, top - 9 * SS, (b - a - 14) * SS * 0.7, 4.5 * SS, rng, inkc, SS + 1)
    # Entries: a bin number, a name, a year, tallies.
    rows = int((h - top - 30 * SS) / pitch)
    stop = rows if not right else int(rows * rng.uniform(0.55, 0.7))
    for k in range(stop):
        y = top + (k + 1) * pitch - 4 * SS
        figures(draw, (cols[0] + 6) * SS, y, int(rng.integers(1, 3)), 5 * SS, rng, inkc, SS + 1)
        cursive_word(draw, (cols[1] + 6) * SS, y, rng.uniform(0.45, 0.95) * (cols[2] - cols[1] - 12) * SS, 4.6 * SS, rng, inkc, SS + 1)
        figures(draw, (cols[2] + 8) * SS, y, 4, 4.6 * SS, rng, inkc, SS + 1)
        figures(draw, (cols[3] + 8) * SS, y, int(rng.integers(1, 3)), 4.6 * SS, rng, inkc, SS + 1)
        if rng.random() < 0.6:
            figures(draw, (cols[4] + 6) * SS, y, 1, 4.6 * SS, rng, inkc, SS + 1)
        # A struck-through line now and then: a wine drunk to the last bottle.
        if rng.random() < 0.12:
            draw.line([((cols[1] + 4) * SS, y - 3 * SS), ((cols[2] - 6) * SS, y - 4 * SS)], fill=inkc, width=SS)
    img = img.resize((PAGE_W, CELL), Image.LANCZOS)
    rgb = np.asarray(img, dtype=np.float32) / 255.0
    rgb = age_paper(rgb, seed, foxing=0.8, damp=0.6, edge=0.7, dust=0.4)
    # The gutter side is shaded where the page curves down into the binding.
    gutter = np.linspace(0, 1, PAGE_W, dtype=np.float32)
    if not right:
        gutter = gutter[::-1]
    shade = np.clip(1.0 - (1.0 - gutter) ** 8 * 0.6, 0.0, 1.0)
    return rgb * shade[None, :, None]


def make_notebook_page(right, seed):
    """The tasting notebook: plain cream paper, a date at the head, notes in a quick blue-black hand."""
    rng = np.random.default_rng(seed)
    w, h = PAGE_W * SS, CELL * SS
    img = Image.new("RGB", (w, h), hexrgb("#ece3cb"))
    draw = ImageDraw.Draw(img)
    inkc = hexrgb("#1f2433")
    # The date, underlined.
    cursive_word(draw, 30 * SS, 52 * SS, 120 * SS, 6 * SS, rng, inkc, SS + 1)
    draw.line([(28 * SS, 58 * SS), (160 * SS, 57 * SS)], fill=inkc, width=SS)
    lines = 21 if not right else 9
    handwriting(draw, 30 * SS, 92 * SS, w - 26 * SS, 19 * SS, lines, rng, inkc, SS + 1, 5.4 * SS, indent=18 * SS)
    if not right:
        # A little sketch of a glass in the margin, as somebody waiting for a wine to open draws.
        gx, gy = w - 70 * SS, h - 90 * SS
        draw.arc([gx - 22 * SS, gy - 46 * SS, gx + 22 * SS, gy], 0, 180, fill=inkc, width=SS + 1)
        draw.line([(gx - 22 * SS, gy - 23 * SS), (gx - 20 * SS, gy - 52 * SS)], fill=inkc, width=SS + 1)
        draw.line([(gx + 22 * SS, gy - 23 * SS), (gx + 20 * SS, gy - 52 * SS)], fill=inkc, width=SS + 1)
        draw.line([(gx, gy), (gx, gy + 40 * SS)], fill=inkc, width=SS + 1)
        draw.line([(gx - 18 * SS, gy + 40 * SS), (gx + 18 * SS, gy + 40 * SS)], fill=inkc, width=SS + 1)
    else:
        # The right-hand page stops partway down: the last thing written in it.
        pass
    img = img.resize((PAGE_W, CELL), Image.LANCZOS)
    rgb = np.asarray(img, dtype=np.float32) / 255.0
    # A wine ring: the foot of a glass set down on the page.
    if right:
        layer = Image.new("L", (PAGE_W, CELL), 0)
        d = ImageDraw.Draw(layer)
        cx, cy, r = PAGE_W * 0.62, CELL * 0.72, 34
        d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=200, width=4)
        ring = np.asarray(layer.filter(ImageFilter.GaussianBlur(1.6)), dtype=np.float32) / 255.0
        ring *= noise(PAGE_W, CELL, 18, seed + 9) * 1.4
        rgb = rgb * (1.0 - ring[..., None] * 0.55) + np.array([0.42, 0.12, 0.14]) * ring[..., None] * 0.55
    rgb = age_paper(rgb, seed, foxing=0.5, damp=0.3, edge=0.6, dust=0.35)
    gutter = np.linspace(0, 1, PAGE_W, dtype=np.float32)
    if not right:
        gutter = gutter[::-1]
    return rgb * np.clip(1.0 - (1.0 - gutter) ** 8 * 0.55, 0.0, 1.0)[None, :, None]


def make_bin_chart(seed):
    """The bin chart, pinned in the cellar: a grid of the racks with a wine written into each bin."""
    rng = np.random.default_rng(seed)
    w, h = 2 * CELL * SS, CELL * SS
    img = Image.new("RGB", (w, h), hexrgb("#e3d7b6"))
    draw = ImageDraw.Draw(img)
    inkc = hexrgb("#2c2016")
    red = hexrgb("#8c3a30")
    centred(draw, "CELLAR BOOK", w / 2, 22 * SS, font(FONT_BOLD, 30), inkc, spacing=4 * SS)
    draw.line([(80 * SS, 64 * SS), (w - 80 * SS, 64 * SS)], fill=red, width=SS * 2)
    cols, rows = 8, 5
    x0, y0, x1, y1 = 40 * SS, 84 * SS, w - 40 * SS, h - 34 * SS
    cw, ch = (x1 - x0) / cols, (y1 - y0) / rows
    for c in range(cols + 1):
        draw.line([(x0 + c * cw, y0), (x0 + c * cw, y1)], fill=inkc, width=SS * 2)
    for r in range(rows + 1):
        draw.line([(x0, y0 + r * ch), (x1, y0 + r * ch)], fill=inkc, width=SS * 2)
    for c in range(cols):
        for r in range(rows):
            bx, by = x0 + c * cw, y0 + r * ch
            figures(draw, bx + 8 * SS, by + 18 * SS, 2, 5 * SS, rng, red, SS + 1)
            if rng.random() < 0.85:
                cursive_word(draw, bx + 10 * SS, by + 44 * SS, cw * rng.uniform(0.45, 0.8), 5 * SS, rng, inkc, SS + 1)
                figures(draw, bx + 12 * SS, by + 70 * SS, 4, 4.5 * SS, rng, inkc, SS + 1)
            if rng.random() < 0.15:
                draw.line([(bx + 6 * SS, by + ch - 8 * SS), (bx + cw - 6 * SS, by + 8 * SS)], fill=inkc, width=SS)
    img = img.resize((2 * CELL, CELL), Image.LANCZOS)
    rgb = np.asarray(img, dtype=np.float32) / 255.0
    return age_paper(rgb, seed, foxing=1.2, damp=1.0, edge=1.0, dust=0.8)


def make_engraving(seed):
    """A small engraved view of a chateau among its vines, for a frame in the alcove."""
    w, h = CELL * SS, CELL * SS
    img = Image.new("RGB", (w, h), hexrgb("#e8dfc6"))
    draw = ImageDraw.Draw(img)
    inkc = hexrgb("#231c16")
    draw.rectangle([30 * SS, 30 * SS, w - 30 * SS, h - 30 * SS], outline=inkc, width=2 * SS)
    draw_chateau(draw, w / 2, 300 * SS, 2.0 * SS, inkc, 2 * SS)
    # Sky hatching.
    for k in range(40):
        y = 50 * SS + k * 6 * SS
        if y > 160 * SS:
            break
        draw.line([(40 * SS, y), (w - 40 * SS, y)], fill=inkc, width=1)
    # Vine rows running down the hill in perspective.
    for k in range(9):
        t = k / 8
        draw.line([(w / 2 + (t - 0.5) * 120 * SS, 340 * SS), (w / 2 + (t - 0.5) * 520 * SS, h - 50 * SS)], fill=inkc, width=SS + 1)
    centred(draw, "VUE DU CHATEAU", w / 2, h - 80 * SS, font(FONT_REGULAR, 16), inkc, spacing=3 * SS)
    img = img.resize((CELL, CELL), Image.LANCZOS)
    rgb = np.asarray(img, dtype=np.float32) / 255.0
    return age_paper(rgb, seed, foxing=1.0, damp=0.5, edge=0.8, dust=0.6)


def build_paper():
    atlas = np.ones((SIZE, SIZE, 3), dtype=np.float32) * np.array([0.88, 0.84, 0.74], dtype=np.float32)
    for i in range(8):
        col, row = i % 4, i // 4
        atlas[row * CELL:row * CELL + LABEL_H, col * CELL:(col + 1) * CELL] = make_label(i)
    pages = [make_ledger_page(False, 21), make_ledger_page(True, 22), make_notebook_page(False, 23), make_notebook_page(True, 24)]
    for k, page in enumerate(pages):
        atlas[2 * CELL:3 * CELL, k * CELL:k * CELL + PAGE_W] = page
    atlas[3 * CELL:4 * CELL, 0:2 * CELL] = make_bin_chart(25)
    atlas[3 * CELL:4 * CELL, 2 * CELL:3 * CELL] = make_engraving(26)
    Image.fromarray((np.clip(atlas, 0, 1) * 255 + 0.5).astype(np.uint8), "RGB").save(os.path.join(TEXTURES, "wine_paper_Diffuse.png"))
    flat = np.zeros((64, 64, 3), dtype=np.uint8)
    flat[...] = (128, 128, 255)
    Image.fromarray(flat, "RGB").save(os.path.join(TEXTURES, "wine_paper_nor_dx.png"))
    arm = np.zeros((64, 64, 3), dtype=np.uint8)
    arm[...] = (255, 225, 0)  # AO 1, roughness ~0.88, no metal: paper
    Image.fromarray(arm, "RGB").save(os.path.join(TEXTURES, "wine_paper_arm.png"))
    return atlas


# ---------------------------------------------------------------------------------------------
# Brands burned into the crate ends.
# ---------------------------------------------------------------------------------------------

BRANDS = [
    ("CHATEAU VALMONT", "1947"), ("DOMAINE DES BRUMES", "BOURGOGNE"), ("CLOS SAINT-AUBIN", "1959"), ("MAISON LAVERGNE", "1961"),
    ("CHATEAU DE CORBAS", "GRAND VIN"), ("DOMAINE ROCHEVAL", "1964"), ("CHATEAU MONTFERRAND", "1945"), ("FRAGILE", "12 BOUTEILLES"),
]


def build_brands():
    bw, bh = 512, 256
    w, h = 2 * bw, 4 * bh
    alpha = Image.new("L", (w * SS, h * SS), 0)
    draw = ImageDraw.Draw(alpha)
    for i, (name, sub) in enumerate(BRANDS):
        col, row = i % 2, i // 2
        x0, y0 = col * bw * SS, row * bh * SS
        cx = x0 + bw * SS / 2
        draw.rectangle([x0 + 22 * SS, y0 + 22 * SS, x0 + (bw - 22) * SS, y0 + (bh - 22) * SS], outline=255, width=5 * SS)
        size = 40 if len(name) < 14 else (32 if len(name) < 17 else 28)
        centred(draw, name, cx, y0 + 66 * SS, font(FONT_BLACK, size), 255, spacing=2 * SS)
        centred(draw, sub, cx, y0 + 140 * SS, font(FONT_BOLD, 30), 255, spacing=6 * SS)
    alpha = alpha.resize((w, h), Image.LANCZOS)
    a = np.asarray(alpha, dtype=np.float32) / 255.0
    # A brand burned into deal is not a clean print: the iron took unevenly, the grain broke it, and
    # sixty years have worn it. Noise eats into it, and the dark spreads a little into the wood.
    wear = noise(w, h, 30, 31, octaves=4)
    grain = noise(w, h * 8, 6, 32, octaves=3)
    grain = np.asarray(Image.fromarray((grain * 255).astype(np.uint8)).resize((w, h)), dtype=np.float32) / 255.0
    a = np.clip(a * (0.55 + wear * 0.9) - (grain < 0.35) * 0.35, 0.0, 1.0)
    spread = np.asarray(Image.fromarray((a * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(2.2)), dtype=np.float32) / 255.0
    opacity = np.clip(np.maximum(a, spread * 0.6) * 1.2, 0.0, 1.0)
    colour = np.zeros((h, w, 3), dtype=np.float32)
    colour[...] = (0.10, 0.06, 0.035)
    colour = colour * (0.7 + 0.6 * wear[..., None])
    Image.fromarray((np.clip(colour, 0, 1) * 255).astype(np.uint8), "RGB").save(os.path.join(TEXTURES, "wine_brands_Diffuse.png"))
    Image.fromarray((opacity * 255).astype(np.uint8), "L").convert("RGB").save(os.path.join(TEXTURES, "wine_brands_opacity.png"))
    flat = np.zeros((64, 64, 3), dtype=np.uint8)
    flat[...] = (128, 128, 255)
    Image.fromarray(flat, "RGB").save(os.path.join(TEXTURES, "wine_brands_nor_dx.png"))
    arm = np.zeros((64, 64, 3), dtype=np.uint8)
    arm[...] = (255, 235, 0)
    Image.fromarray(arm, "RGB").save(os.path.join(TEXTURES, "wine_brands_arm.png"))
    return opacity


def main():
    os.makedirs(PREVIEW, exist_ok=True)
    atlas = build_paper()
    brands = build_brands()
    Image.fromarray((np.clip(atlas, 0, 1) * 255).astype(np.uint8), "RGB").resize((1024, 1024), Image.LANCZOS).save(os.path.join(PREVIEW, "wine_paper.png"))
    on_wood = np.ones(brands.shape + (3,), dtype=np.float32) * np.array([0.62, 0.48, 0.32])
    on_wood = on_wood * (1 - brands[..., None]) + np.array([0.10, 0.06, 0.035]) * brands[..., None]
    Image.fromarray((on_wood * 255).astype(np.uint8), "RGB").resize((512, 512), Image.LANCZOS).save(os.path.join(PREVIEW, "wine_brands.png"))
    print("Wrote wine_paper, wine_brands to", TEXTURES)


if __name__ == "__main__":
    main()
