#!/usr/bin/env python3
"""
Bakes the library's printed, painted, bound and woven surfaces into texture sets the art pipeline
imports.

    python Tools/make_library_art.py

Sets written as <set>_Diffuse / _nor_dx / _arm into Art/Source/Textures:

    library_books      the spines of the books on the shelves. A 2048 sheet: 32 spines of 128 x 768
                       in two rows (cells 0-31), the fore-edges of the leaves along the bottom left
                       (PAGES), and eight plain cover boards along the bottom right (COVERS), one per
                       binding colour. Tools/make_library.py lays its book rows out on it (BOOK_*).
    library_paper      the paper on the desk and the walls: a 4 x 4 atlas of 512 cells (PAPER below,
                       shared with make_library.py and ALibraryActor): the open notebook, the open
                       book, research notes, a letter and its envelope, the newspaper, the calendar,
                       the blotter.
    library_maps       two framed maps, 1024 x 1024 each: the world in two hemispheres and a survey
                       of the parish.
    library_globe      the globe's gores, equirectangular, 2048 x 1024 on a 2048 sheet.
    library_paintings  two oil portraits, 512 x 640 each: a woman with a little girl beside her, for
                       over the mantel, and an old man in black, for over the arch.
    library_rug        the oriental carpet under the desk, 400 x 300 cm laid on a square sheet.
    library_velvet     the curtains' and valances' crimson velvet, tiling at the drapes' 34cm.
                       `--velvet` bakes it alone.

Why baked, as for the wine cellar: a spine, a map and a painting are pictures, and neither the
photographed library nor a material graph has them.

The coastlines are Natural Earth's 1:50m land polygons (public domain, naturalearthdata.com), kept
in Art/Source/Reference/ne_50m_land.geojson and fetched there if missing. The only typeface is
Cinzel (SIL Open Font License, Content/Fonts/OFL.txt). Titles on the spines are subjects and
invented novels; the handwriting is a cursive hand, not text (make_cellar_art.py's helpers), because
the clue writing is the user's to do once the game is built (the 09-30 decision).
"""

import json
import math
import os
import urllib.request

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from make_cellar_art import (FONT_BLACK, FONT_BOLD, FONT_REGULAR, SS, TEXTURES, age_paper, centred, cursive_word, figures,
                             font, handwriting, hexrgb, noise)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PREVIEW = os.path.join(ROOT, "Saved", "LibraryPreview")
REFERENCE = os.path.join(ROOT, "Art", "Source", "Reference")
LAND_URL = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_50m_land.geojson"
SIZE = 2048

# ---------------------------------------------------------------------------------------------
# Shared with Tools/make_library.py and LibraryActor.cpp.
# ---------------------------------------------------------------------------------------------

# library_books: spine cells, then the leaves and the boards along the bottom.
BOOK_CELL_W = 128
BOOK_CELL_H = 768
BOOK_CELLS = 32
PAGES = (0, 1536, 1024, 512)
COVERS_X = 1024
COVER_W = 128

# library_paper, in pixels (x, y, w, h).
PAPER = {
    "notebook_l": (0, 0, 366, 512),
    "notebook_r": (512, 0, 366, 512),
    "book_l": (1024, 0, 366, 512),
    "book_r": (1536, 0, 366, 512),
    "notes_a": (0, 512, 362, 512),
    "notes_b": (512, 512, 362, 512),
    "letter": (1024, 512, 362, 512),
    "envelope": (1536, 512, 512, 300),
    "newspaper": (0, 1024, 1024, 724),
    "calendar": (1024, 1024, 366, 512),
    "card": (1536, 1024, 512, 320),
    "blotter": (1024, 1536, 1024, 512),
}

# ---------------------------------------------------------------------------------------------
# Small helpers.
# ---------------------------------------------------------------------------------------------


def to_u8(rgb):
    return (np.clip(rgb, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)


def flat_normal(path, size=64):
    flat = np.zeros((size, size, 3), dtype=np.uint8)
    flat[...] = (128, 128, 255)
    Image.fromarray(flat, "RGB").save(path)


def flat_arm(path, rough, size=64):
    arm = np.zeros((size, size, 3), dtype=np.uint8)
    arm[...] = (255, int(rough * 255), 0)
    Image.fromarray(arm, "RGB").save(path)


def normal_from_height(height, strength):
    """DirectX tangent-space normals (V runs down the image) from a height field in [0, 1]."""
    dy, dx = np.gradient(height.astype(np.float32))
    n = np.stack([-dx * strength, -dy * strength, np.ones_like(height)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return ((n * 0.5 + 0.5) * 255.0 + 0.5).astype(np.uint8)


def save_set(name, rgb, normal=None, arm=None, rough=0.85):
    Image.fromarray(to_u8(rgb), "RGB").save(os.path.join(TEXTURES, name + "_Diffuse.png"))
    if normal is None:
        flat_normal(os.path.join(TEXTURES, name + "_nor_dx.png"))
    else:
        Image.fromarray(normal, "RGB").save(os.path.join(TEXTURES, name + "_nor_dx.png"))
    if arm is None:
        flat_arm(os.path.join(TEXTURES, name + "_arm.png"), rough)
    else:
        Image.fromarray(arm, "RGB").save(os.path.join(TEXTURES, name + "_arm.png"))


def preview(name, rgb, size=1024):
    img = Image.fromarray(to_u8(rgb), "RGB")
    img.thumbnail((size, size), Image.LANCZOS)
    img.save(os.path.join(PREVIEW, name + ".png"))


def crackle(w, h, seed, scale=40.0):
    """Craquelure: the fine network an old varnish and paint layer splits into. The thin contour of
    a signed noise field, two of them crossed (the plaster cracks' trick): Voronoi cells this size
    came out as a stained-glass window across the faces."""
    lines = np.zeros((h, w), dtype=np.float32)
    for k, s in enumerate((scale, scale * 1.9)):
        n = noise(w, h, s, seed + k, octaves=2)
        lines = np.maximum(lines, np.clip(1.0 - np.abs(n - 0.5) * 60.0, 0.0, 1.0))
    return lines


def varnish(rgb, seed, amount=1.0):
    """An old varnish: yellowed, darkened towards the edges, and crazed."""
    h, w, _ = rgb.shape
    out = rgb * np.array([0.92, 0.80, 0.58]) * (0.85 + 0.15 * noise(w, h, 3, seed))[..., None]
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    d = np.sqrt(((xx - w / 2) / (w / 2)) ** 2 + ((yy - h / 2) / (h / 2)) ** 2)
    out *= (1.0 - np.clip(d - 0.55, 0.0, 1.0) * 0.6 * amount)[..., None]
    cr = crackle(w, h, seed + 1, scale=max(w, h) / 14.0)
    out *= (1.0 - cr * 0.22 * amount)[..., None]
    return np.clip(out, 0.0, 1.0)


# ---------------------------------------------------------------------------------------------
# Spines.
# ---------------------------------------------------------------------------------------------

# Bindings: (name, leather/cloth sRGB, tooling, kind). The eight cover patches follow this order.
BINDINGS = [
    ("oxblood", "#5c1d17", "gilt", "leather"),
    ("green", "#1f3a29", "gilt", "leather"),
    ("calf", "#6a4426", "gilt", "leather"),
    ("black", "#1c1916", "gilt", "leather"),
    ("vellum", "#a68f69", "ink", "vellum"),
    ("navy", "#1d2840", "gilt", "cloth"),
    ("maroon", "#4f1f27", "gilt", "cloth"),
    ("olive", "#4e4d2c", "ink", "cloth"),
]

SUBJECTS = ["HISTORY", "ESSAYS", "POEMS", "LETTERS", "SERMONS", "TRAVELS", "FLORA", "LAW", "OPTICKS", "ATLAS",
            "MEMOIRS", "DRAMA", "ANATOMY", "ORATIONS", "ANNALS", "ETHICS", "GEOLOGY", "VOYAGES"]
NOVELS = ["THE GREY HOUSE", "A STUDY IN ASH", "THE SILENT WITNESS", "THE LAST TRAIN", "THE WIDOW'S KEY",
          "DEATH ON THE MOOR", "THE LAST CASE"]

# What each of the 32 cells is: (binding index, design, words, volume).
SPINES = []


def plan_spines():
    rng = np.random.default_rng(301)
    spines = []
    # An encyclopedia in navy cloth, eight volumes: rows of it stand together on the shelves.
    for v in range(8):
        spines.append((5, "set", ["ENCYCLOPAEDIA"], v + 1))
    # Four of the family's journals: black cloth with a paper label and a span of years on it.
    for v in range(4):
        spines.append((3, "journal", [str(1951 + v * 2), str(1952 + v * 2)], 0))
    # Seven detective novels in cloth, the title in black or gilt on a printed band.
    for k, title in enumerate(NOVELS):
        spines.append(([6, 7, 2, 5, 0, 7, 1][k], "novel", title.split(" "), 0))
    # The old leather and vellum: subjects, raised bands, a label.
    while len(spines) < BOOK_CELLS:
        b = int(rng.choice([0, 0, 1, 2, 2, 3, 4]))
        spines.append((b, "leather", [SUBJECTS[len(spines) % len(SUBJECTS)]], int(rng.integers(0, 4))))
    return spines


ROMAN = ["", "I", "II", "III", "IV", "V", "VI", "VII", "VIII"]


def grain(w, h, seed, kind):
    """Leather is a fine pebble, cloth a weave, vellum a smooth skin with a cloud in it."""
    if kind == "cloth":
        yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
        weave = 0.5 + 0.25 * np.sin(xx * 1.9) * np.sin(yy * 1.9)
        return weave * 0.5 + noise(w, h, 40, seed, octaves=2) * 0.5
    if kind == "vellum":
        return noise(w, h, 6, seed, octaves=4)
    return noise(w, h, 60, seed, octaves=3) * 0.6 + noise(w, h, 12, seed + 1, octaves=2) * 0.4


def draw_spine(index, spec):
    """One spine, 128 x 768: returns colour (sRGB float), height, gilt mask."""
    binding, design, words, volume = spec
    _, colour, tooling, kind = BINDINGS[binding]
    w, h = BOOK_CELL_W, BOOK_CELL_H
    W, H = w * SS, h * SS
    rng = np.random.default_rng(1000 + index)

    base = np.array(hexrgb(colour), dtype=np.float32) / 255.0
    # No two copies of a binding faded the same: a shade either way, and the sun has had some.
    base = base * rng.uniform(0.85, 1.15) + rng.uniform(-0.015, 0.015, 3)
    g = grain(w, h, 2000 + index, kind)
    rgb = np.ones((h, w, 3), dtype=np.float32) * base
    rgb *= (0.82 + 0.36 * g)[..., None]

    # The raised bands, the label panels and the tooling are drawn as masks at twice the size.
    bands = Image.new("L", (W, H), 0)
    label = Image.new("L", (W, H), 0)
    gilt = Image.new("L", (W, H), 0)
    ink = Image.new("L", (W, H), 0)
    paper = Image.new("L", (W, H), 0)
    db, dl, dg, di, dp = (ImageDraw.Draw(m) for m in (bands, label, gilt, ink, paper))
    tool = dg if tooling == "gilt" else di
    m = 10 * SS

    if design == "leather":
        # Five raised bands; a dark label in the second panel; small tools in the others.
        ys = np.linspace(H * 0.12, H * 0.9, 6)
        for y in ys:
            db.rectangle([0, y - 9 * SS, W, y + 9 * SS], fill=255)
            tool.line([(m, y - 13 * SS), (W - m, y - 13 * SS)], fill=255, width=2 * SS)
            tool.line([(m, y + 13 * SS), (W - m, y + 13 * SS)], fill=255, width=2 * SS)
        y0, y1 = ys[0] + 18 * SS, ys[1] - 18 * SS
        dl.rectangle([m - 2 * SS, y0, W - m + 2 * SS, y1], fill=255)
        word = words[0]
        size = 22 if len(word) <= 6 else (17 if len(word) <= 8 else 13)
        centred(tool, word, W / 2, (y0 + y1) / 2 - size * SS * 0.7, font(FONT_BOLD, size), 255)
        if volume:
            centred(tool, ROMAN[volume], W / 2, ys[2] + 30 * SS, font(FONT_BOLD, 22), 255)
        for k in range(2, 5):
            cy = (ys[k] + ys[k + 1]) / 2
            if volume and k == 2:
                continue
            # A fleuron: a lozenge and four dots.
            r = 14 * SS
            tool.polygon([(W / 2, cy - r), (W / 2 + r * 0.6, cy), (W / 2, cy + r), (W / 2 - r * 0.6, cy)], outline=255, width=2 * SS)
            for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                tool.ellipse([W / 2 + dx * r * 1.3 - 3 * SS, cy + dy * r * 1.6 - 3 * SS, W / 2 + dx * r * 1.3 + 3 * SS, cy + dy * r * 1.6 + 3 * SS], fill=255)
    elif design == "set":
        # The encyclopedia: gilt rules top and bottom, a black label, the volume in a roundel.
        for y in (H * 0.06, H * 0.08, H * 0.92, H * 0.94):
            dg.line([(m, y), (W - m, y)], fill=255, width=3 * SS)
        y0, y1 = H * 0.15, H * 0.3
        dl.rectangle([m - 2 * SS, y0, W - m + 2 * SS, y1], fill=255)
        for k, part in enumerate(("ENCYCLO-", "PAEDIA")):
            centred(dg, part, W / 2, y0 + 14 * SS + k * 30 * SS, font(FONT_BOLD, 14), 255)
        cy = H * 0.5
        dg.ellipse([W / 2 - 34 * SS, cy - 34 * SS, W / 2 + 34 * SS, cy + 34 * SS], outline=255, width=3 * SS)
        centred(dg, ROMAN[volume], W / 2, cy - 18 * SS, font(FONT_BOLD, 26), 255)
        centred(dg, "LONDON", W / 2, H * 0.86, font(FONT_REGULAR, 12), 255)
    elif design == "journal":
        # Plain cloth, a paper label stuck on, the years written on it in ink.
        y0, y1 = H * 0.2, H * 0.36
        dp.rectangle([m, y0, W - m, y1], fill=255)
        for k, year in enumerate(words):
            centred(di, year, W / 2, y0 + 14 * SS + k * 40 * SS, font(FONT_REGULAR, 20), 255)
        di.line([(m + 8 * SS, (y0 + y1) / 2 + 4 * SS), (W - m - 8 * SS, (y0 + y1) / 2 + 2 * SS)], fill=255, width=SS)
    else:
        # A novel: a printed band near the head with the title, the publisher at the foot.
        y0, y1 = H * 0.08, H * 0.42
        tool.rectangle([m, y0, W - m, y0 + 4 * SS], fill=255)
        tool.rectangle([m, y1, W - m, y1 + 4 * SS], fill=255)
        y = y0 + 24 * SS
        for word in words:
            size = 20 if len(word) <= 5 else (16 if len(word) <= 7 else 13)
            centred(tool, word, W / 2, y, font(FONT_BOLD, size), 255)
            y += size * SS * 1.5
        centred(tool, "ASHLEIGH", W / 2, H * 0.9, font(FONT_REGULAR, 11), 255)

    def arr(img, blur=0.0):
        img = img.resize((w, h), Image.LANCZOS)
        if blur:
            img = img.filter(ImageFilter.GaussianBlur(blur))
        return np.asarray(img, dtype=np.float32) / 255.0

    band_m, label_m, gilt_m, ink_m, paper_m = arr(bands, 1.2), arr(label), arr(gilt), arr(ink), arr(paper)

    # A label is a darker or a red leather; a paper label is a yellowed slip.
    label_colour = np.array([0.08, 0.06, 0.05]) if binding != 3 else np.array([0.32, 0.08, 0.06])
    rgb = rgb * (1 - label_m[..., None]) + label_colour * (0.8 + 0.4 * g[..., None]) * label_m[..., None]
    paper_colour = np.array([0.72, 0.64, 0.48]) * (0.8 + 0.3 * noise(w, h, 8, 3000 + index))[..., None]
    rgb = rgb * (1 - paper_m[..., None]) + paper_colour * paper_m[..., None]
    # The bands catch the light along their crowns.
    rgb *= (1.0 + band_m * 0.18)[..., None]
    # Gold leaf, worn off where hands have taken the book out by its head.
    wear = noise(w, h, 18, 4000 + index)
    gilt_m = gilt_m * np.clip((wear - 0.25) * 2.2, 0.0, 1.0)
    gold = np.array([0.66, 0.50, 0.24]) * (0.75 + 0.35 * noise(w, h, 30, 4100 + index))[..., None]
    rgb = rgb * (1 - gilt_m[..., None]) + gold * gilt_m[..., None]
    ink_colour = np.array([0.05, 0.04, 0.035])
    rgb = rgb * (1 - ink_m[..., None] * 0.9) + ink_colour * ink_m[..., None] * 0.9

    # Worn at the head and the tail and along both joints, where the leather rubs.
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    joint = np.exp(-((np.minimum(xx, w - 1 - xx)) / 6.0) ** 2)
    ends = np.exp(-((np.minimum(yy, h - 1 - yy)) / 14.0) ** 2)
    scuff = np.clip((joint * 0.6 + ends) * (0.4 + noise(w, h, 24, 5000 + index)), 0.0, 1.0)
    rgb = rgb * (1 - scuff[..., None] * 0.35) + np.array([0.42, 0.34, 0.26]) * scuff[..., None] * 0.35
    # The sun from the window took the colour out of some, from the top down.
    fade = rng.uniform(0.0, 0.35)
    rgb = rgb * (1 - fade) + rgb.mean(axis=-1, keepdims=True) * fade * 1.05

    height = 0.35 + band_m * 0.5 - label_m * 0.05 - gilt_m * 0.08 + g * 0.06 - scuff * 0.04
    rough = np.where(gilt_m > 0.5, 0.38, 0.62 if kind == "leather" else (0.85 if kind == "cloth" else 0.55))
    rough = rough + (noise(w, h, 20, 6000 + index) - 0.5) * 0.12
    return np.clip(rgb, 0, 1), height, gilt_m, np.clip(rough, 0.2, 1.0)


def build_books():
    global SPINES
    SPINES = plan_spines()
    rgb = np.zeros((SIZE, SIZE, 3), dtype=np.float32)
    height = np.full((SIZE, SIZE), 0.35, dtype=np.float32)
    metal = np.zeros((SIZE, SIZE), dtype=np.float32)
    rough = np.full((SIZE, SIZE), 0.8, dtype=np.float32)
    for i, spec in enumerate(SPINES):
        col, row = i % 16, i // 16
        x, y = col * BOOK_CELL_W, row * BOOK_CELL_H
        c, hgt, gm, r = draw_spine(i, spec)
        rgb[y:y + BOOK_CELL_H, x:x + BOOK_CELL_W] = c
        height[y:y + BOOK_CELL_H, x:x + BOOK_CELL_W] = hgt
        metal[y:y + BOOK_CELL_H, x:x + BOOK_CELL_W] = gm
        rough[y:y + BOOK_CELL_H, x:x + BOOK_CELL_W] = r

    # The leaves seen edge on: fine lines along the book's length (across this patch), yellowed,
    # darker towards the outside where the dust and the light got at them.
    px, py, pw, ph = PAGES
    yy, xx = np.mgrid[0:ph, 0:pw].astype(np.float32)
    lines = 0.5 + 0.5 * np.sin(yy * 2.4 + noise(pw, ph, 30, 7001) * 6.0)
    leaves = np.array([0.66, 0.58, 0.43]) * (0.78 + 0.22 * lines)[..., None]
    leaves *= (0.88 + 0.18 * noise(pw, ph, 10, 7002))[..., None]
    # Foxing in specks, not blotches: the edge of a book is a few millimetres wide on screen.
    fox = noise(pw, ph, 160, 7003, octaves=1)
    leaves *= (1.0 - np.clip((fox - 0.8) * 4.0, 0.0, 0.35))[..., None]
    rgb[py:py + ph, px:px + pw] = leaves
    height[py:py + ph, px:px + pw] = 0.3 + lines * 0.2
    rough[py:py + ph, px:px + pw] = 0.9

    # The boards: one plain patch of each binding, with the corners and edges rubbed.
    for k, (_, colour, _, kind) in enumerate(BINDINGS):
        x = COVERS_X + k * COVER_W
        base = np.array(hexrgb(colour), dtype=np.float32) / 255.0
        g = grain(COVER_W, 512, 8000 + k, kind)
        patch = base * (0.82 + 0.36 * g)[..., None]
        yy, xx = np.mgrid[0:512, 0:COVER_W].astype(np.float32)
        edge = np.exp(-((np.minimum(np.minimum(xx, COVER_W - 1 - xx), np.minimum(yy, 511 - yy))) / 8.0) ** 2)
        patch = patch * (1 - edge[..., None] * 0.3) + np.array([0.42, 0.34, 0.26]) * edge[..., None] * 0.3
        rgb[1536:2048, x:x + COVER_W] = patch
        height[1536:2048, x:x + COVER_W] = 0.35 + g * 0.06
        rough[1536:2048, x:x + COVER_W] = 0.62 if kind == "leather" else 0.85

    normal = normal_from_height(height, 6.0)
    arm = np.stack([np.ones_like(rough), rough, metal], axis=-1)
    save_set("library_books", rgb, normal, to_u8(arm))
    # Which binding each spine is, for make_library.py to give each book the boards that go with it.
    with open(os.path.join(TEXTURES, "library_books.json"), "w", encoding="utf-8") as handle:
        json.dump({"bindings": [spec[0] for spec in SPINES]}, handle)
    return rgb


# ---------------------------------------------------------------------------------------------
# Paper.
# ---------------------------------------------------------------------------------------------

def sheet(name, colour):
    _, _, w, h = PAPER[name]
    img = Image.new("RGB", (w * SS, h * SS), hexrgb(colour))
    return img, ImageDraw.Draw(img), w * SS, h * SS


def finish_sheet(img, name, seed, **ageing):
    _, _, w, h = PAPER[name]
    img = img.resize((w, h), Image.LANCZOS)
    rgb = np.asarray(img, dtype=np.float32) / 255.0
    return age_paper(rgb, seed, **ageing)


def gutter(rgb, right):
    w = rgb.shape[1]
    t = np.linspace(0, 1, w, dtype=np.float32)
    if not right:
        t = t[::-1]
    return rgb * np.clip(1.0 - (1.0 - t) ** 8 * 0.55, 0.0, 1.0)[None, :, None]


def make_notebook(right):
    """His notebook: feint-ruled, a margin, a fast hand in black ink; the right page stops partway."""
    name = "notebook_r" if right else "notebook_l"
    img, draw, w, h = sheet(name, "#e9e0c6")
    rng = np.random.default_rng(411 if right else 410)
    rule, red, ink = hexrgb("#9aacbc"), hexrgb("#b0574d"), hexrgb("#16161c")
    for k in range(int((h - 60 * SS) / (17 * SS))):
        y = 60 * SS + k * 17 * SS
        draw.line([(0, y), (w, y)], fill=rule, width=1)
    draw.line([(44 * SS, 0), (44 * SS, h)], fill=red, width=SS)
    lines = 24 if not right else 11
    handwriting(draw, 52 * SS, 74 * SS, w - 16 * SS, 17 * SS, lines, rng, ink, SS + 1, 5.2 * SS, indent=14 * SS)
    # Things underlined and a word boxed: somebody working something out.
    for _ in range(3):
        y = (74 + int(rng.integers(0, lines)) * 17 + 4) * SS
        x = rng.uniform(60, 200) * SS
        draw.line([(x, y), (x + rng.uniform(50, 120) * SS, y - SS)], fill=ink, width=SS)
    if not right:
        x, y = 210 * SS, (74 + 6 * 17 - 12) * SS
        draw.rectangle([x, y, x + 90 * SS, y + 16 * SS], outline=ink, width=SS)
        # Arrows between two notes in the margin.
        draw.line([(20 * SS, 180 * SS), (20 * SS, 290 * SS)], fill=ink, width=SS)
        draw.polygon([(16 * SS, 284 * SS), (24 * SS, 284 * SS), (20 * SS, 294 * SS)], fill=ink)
    else:
        # The last line runs off into a dash and the pen stops.
        y = (74 + lines * 17) * SS
        draw.line([(60 * SS, y), (130 * SS, y + 2 * SS)], fill=ink, width=SS)
    rgb = finish_sheet(img, name, 412 if right else 413, foxing=0.4, damp=0.2, edge=0.5, dust=0.3)
    return gutter(rgb, right)


def make_book_page(right):
    """A printed book, open: a running head, a column of type, a plate on the right-hand page."""
    name = "book_r" if right else "book_l"
    img, draw, w, h = sheet(name, "#e3d6b4")
    rng = np.random.default_rng(421 if right else 420)
    ink = hexrgb("#221c16")
    centred(draw, "CHAPTER" if not right else "", w / 2, 26 * SS, font(FONT_REGULAR, 10), ink, spacing=2 * SS)
    x0, x1 = 34 * SS, w - 30 * SS
    y = 50 * SS
    if right:
        # An engraved plate across the top half: a house among trees, hatched.
        draw.rectangle([x0, y, x1, y + 210 * SS], outline=ink, width=SS * 2)
        cx, base = (x0 + x1) / 2, y + 170 * SS
        draw.rectangle([cx - 70 * SS, base - 70 * SS, cx + 70 * SS, base], outline=ink, width=SS * 2)
        draw.polygon([(cx - 80 * SS, base - 70 * SS), (cx, base - 120 * SS), (cx + 80 * SS, base - 70 * SS)], outline=ink, width=SS * 2)
        for k in range(14):
            draw.line([(x0 + 6 * SS, y + (12 + k * 8) * SS), (x1 - 6 * SS, y + (12 + k * 8) * SS)], fill=ink, width=1)
        for tx in (-120, 115):
            for r in range(3):
                draw.ellipse([cx + (tx - 22 + r * 6) * SS, base - (60 + r * 28) * SS, cx + (tx + 22 - r * 6) * SS, base - (20 + r * 28) * SS], outline=ink, width=SS)
        y += 236 * SS
    else:
        # A drop capital.
        draw.text((x0, y), "T", font=font(FONT_BLACK, 34), fill=ink)
    # Type: lines of short grey words, justified.
    while y < h - 40 * SS:
        cursor = x0 + (44 * SS if y < 110 * SS and not right else 0)
        while cursor < x1 - 8 * SS:
            word = min(rng.uniform(8, 34) * SS, x1 - cursor)
            draw.line([(cursor, y), (cursor + word, y)], fill=hexrgb("#4a423a"), width=int(2.2 * SS))
            cursor += word + 5 * SS
        y += 10 * SS
    figures(draw, w / 2 - 8 * SS, h - 16 * SS, 3, 4 * SS, rng, ink, SS)
    rgb = finish_sheet(img, name, 422 if right else 423, foxing=1.2, damp=0.3, edge=0.6, dust=0.3)
    return gutter(rgb, right)


def make_notes(which):
    """Loose research notes: a numbered list, a diagram of boxes joined by lines, crossings-out."""
    name = "notes_" + which
    img, draw, w, h = sheet(name, "#e7ddc2" if which == "a" else "#ddd3b6")
    rng = np.random.default_rng(431 if which == "a" else 432)
    ink = hexrgb("#1a1a22") if which == "a" else hexrgb("#2b2a3a")
    pencil = hexrgb("#5a5a5e")
    cursive_word(draw, 30 * SS, 44 * SS, 160 * SS, 7 * SS, rng, ink, 2 * SS)
    draw.line([(28 * SS, 52 * SS), (200 * SS, 51 * SS)], fill=ink, width=SS)
    if which == "a":
        for k in range(9):
            y = (86 + k * 34) * SS
            figures(draw, 26 * SS, y, 1, 5 * SS, rng, ink, SS + 1)
            cursive_word(draw, 44 * SS, y, rng.uniform(120, 280) * SS, 5 * SS, rng, ink, SS + 1)
            if rng.random() < 0.3:
                draw.line([(42 * SS, y - 4 * SS), (42 * SS + rng.uniform(100, 200) * SS, y - 5 * SS)], fill=ink, width=SS)
        # Boxes and lines in pencil at the foot: how one thing led to another.
        boxes = [(60, 420), (180, 400), (290, 440), (130, 480)]
        for bx, by in boxes:
            draw.rectangle([(bx - 34) * SS, (by - 14) * SS, (bx + 34) * SS, (by + 14) * SS], outline=pencil, width=SS + 1)
            cursive_word(draw, (bx - 28) * SS, (by + 4) * SS, 50 * SS, 4 * SS, rng, pencil, SS)
        for a, b in ((0, 1), (1, 2), (1, 3), (0, 3)):
            draw.line([(boxes[a][0] * SS, boxes[a][1] * SS), (boxes[b][0] * SS, boxes[b][1] * SS)], fill=pencil, width=SS)
    else:
        handwriting(draw, 30 * SS, 90 * SS, w - 24 * SS, 21 * SS, 17, rng, ink, SS + 1, 5.6 * SS)
        # Struck through, then written again under it.
        for k in (3, 4, 9):
            y = (90 + k * 21 - 4) * SS
            draw.line([(30 * SS, y), (w - 40 * SS, y + 2 * SS)], fill=ink, width=SS + 1)
        draw.ellipse([w - 120 * SS, h - 110 * SS, w - 30 * SS, h - 60 * SS], outline=ink, width=SS + 1)
    return finish_sheet(img, name, 433 if which == "a" else 434, foxing=0.6, damp=0.5, edge=0.7, dust=0.4)


def make_letter():
    """A letter on good paper: an embossed monogram at the head, a date, two sides of a hand."""
    img, draw, w, h = sheet("letter", "#ece4cf")
    rng = np.random.default_rng(441)
    ink = hexrgb("#1d2238")
    draw.ellipse([w / 2 - 22 * SS, 18 * SS, w / 2 + 22 * SS, 62 * SS], outline=hexrgb("#8f8470"), width=SS)
    centred(draw, "H", w / 2, 26 * SS, font(FONT_BOLD, 22), hexrgb("#8f8470"))
    cursive_word(draw, w - 130 * SS, 92 * SS, 96 * SS, 5 * SS, rng, ink, SS + 1)
    cursive_word(draw, 34 * SS, 126 * SS, 80 * SS, 6 * SS, rng, ink, SS + 1)
    handwriting(draw, 34 * SS, 156 * SS, w - 28 * SS, 22 * SS, 14, rng, ink, SS + 1, 5.8 * SS, indent=24 * SS)
    cursive_word(draw, w - 150 * SS, h - 50 * SS, 110 * SS, 7 * SS, rng, ink, SS + 1)
    rgb = finish_sheet(img, "letter", 442, foxing=0.5, damp=0.2, edge=0.5, dust=0.3)
    # The folds: two creases across, darker where they have been opened and closed.
    for f in (1 / 3, 2 / 3):
        y = int(rgb.shape[0] * f)
        rgb[y - 1:y + 2] *= 0.86
    return rgb


def make_envelope():
    """The envelope it came in: an address, a stamp, a postmark across it, torn open along the top."""
    img, draw, w, h = sheet("envelope", "#e0d3b2")
    rng = np.random.default_rng(451)
    ink = hexrgb("#1d2238")
    for k in range(4):
        cursive_word(draw, (150 + k * 12) * SS, (130 + k * 34) * SS, rng.uniform(160, 260) * SS, 6.5 * SS, rng, ink, SS + 1)
    sx, sy = w - 92 * SS, 22 * SS
    draw.rectangle([sx, sy, sx + 60 * SS, sy + 72 * SS], fill=hexrgb("#7a3a30"))
    draw.rectangle([sx + 6 * SS, sy + 6 * SS, sx + 54 * SS, sy + 66 * SS], outline=hexrgb("#d9c9a6"), width=SS)
    draw.ellipse([sx + 16 * SS, sy + 18 * SS, sx + 44 * SS, sy + 52 * SS], outline=hexrgb("#d9c9a6"), width=SS)
    # The postmark: a ring and wavy lines, struck off the stamp onto the paper.
    pm = hexrgb("#34303a")
    cx, cy = sx - 30 * SS, sy + 46 * SS
    draw.ellipse([cx - 34 * SS, cy - 34 * SS, cx + 34 * SS, cy + 34 * SS], outline=pm, width=SS + 1)
    for k in range(4):
        y = cy - 18 * SS + k * 12 * SS
        draw.line([(cx + 36 * SS + i * 8 * SS, y + (4 * SS if i % 2 else 0)) for i in range(10)], fill=pm, width=SS + 1)
    # Torn open along the top edge.
    draw.line([(0, 8 * SS)] + [(x * SS, (8 + rng.uniform(-4, 4)) * SS) for x in range(0, w // SS, 12)], fill=hexrgb("#a89878"), width=3 * SS)
    return finish_sheet(img, "envelope", 452, foxing=0.6, damp=0.4, edge=0.8, dust=0.5)


def make_newspaper():
    """A local paper, folded in half: the masthead, a rule, columns of type, two photographs."""
    img, draw, w, h = sheet("newspaper", "#d8cfb4")
    rng = np.random.default_rng(461)
    ink = hexrgb("#1c1a18")
    centred(draw, "THE COUNTY HERALD", w / 2, 18 * SS, font(FONT_BLACK, 52), ink, spacing=3 * SS)
    draw.line([(24 * SS, 96 * SS), (w - 24 * SS, 96 * SS)], fill=ink, width=3 * SS)
    draw.line([(24 * SS, 104 * SS), (w - 24 * SS, 104 * SS)], fill=ink, width=SS)
    # The date line: printed, too small to read across a desk.
    for x in (40, w // SS - 200):
        draw.line([(x * SS, 100 * SS), ((x + 150) * SS, 100 * SS)], fill=hexrgb("#5a5550"), width=SS)
    cols = 6
    x0, x1 = 24 * SS, w - 24 * SS
    cw = (x1 - x0) / cols
    # A headline across four columns, as heavy bars of type; nothing to read.
    for k in range(2):
        y = (124 + k * 30) * SS
        cursor = x0 + 6 * SS
        while cursor < x0 + cw * 4 - 30 * SS:
            word = rng.uniform(40, 110) * SS
            draw.rectangle([cursor, y, min(cursor + word, x0 + cw * 4 - 10 * SS), y + 20 * SS], fill=hexrgb("#2a2724"))
            cursor += word + 14 * SS
    photos = [(4, 1, 2, 190, 360), (0, 2, 1, 470, 620)]
    for c in range(cols):
        y = (190 if c < 4 else 124) * SS
        while y < h - 20 * SS:
            skip = False
            for pc, pw, _, py0, py1 in photos:
                if pc <= c < pc + pw and py0 * SS <= y <= py1 * SS:
                    skip = True
            if not skip:
                draw.line([(x0 + c * cw + 6 * SS, y), (x0 + (c + 1) * cw - 6 * SS, y)], fill=hexrgb("#3c3833"), width=int(2.4 * SS))
            y += 8 * SS
        if c:
            draw.line([(x0 + c * cw, 120 * SS), (x0 + c * cw, h - 16 * SS)], fill=ink, width=SS)
    for pc, pw, _, py0, py1 in photos:
        bx0, bx1 = x0 + pc * cw + 6 * SS, x0 + (pc + pw) * cw - 6 * SS
        layer = noise(int((bx1 - bx0) / SS) + 1, (py1 - py0) + 1, 4, 470 + pc, octaves=3)
        tone = Image.fromarray((40 + layer * 120).astype(np.uint8), "L").resize((int(bx1 - bx0), int((py1 - py0) * SS)))
        img.paste(Image.merge("RGB", (tone, tone, tone)), (int(bx0), int(py0 * SS)))
    rgb = finish_sheet(img, "newspaper", 462, foxing=0.8, damp=0.5, edge=1.0, dust=0.6)
    # The fold across the middle, where it has been read and put down.
    y = rgb.shape[0] // 2
    rgb[y - 2:y + 3] *= 0.8
    return rgb


def make_calendar():
    """The study's calendar: OCTOBER, as the laundry's, a print of a ship above, engagements written
    into a few of the early days in his hand and nothing after the tenth."""
    img, draw, w, h = sheet("calendar", "#e9dfc4")
    rng = np.random.default_rng(471)
    ink, red, pen = hexrgb("#251f18"), hexrgb("#8e2f24"), hexrgb("#16161c")
    m = 14 * SS
    top = h * 0.42
    draw.rectangle([m, m, w - m, top], outline=ink, width=SS * 2)
    # A sailing ship, engraved: hull, three masts, sails, a hatched sea.
    cx, sea = w / 2, top - 40 * SS
    draw.polygon([(cx - 90 * SS, sea - 10 * SS), (cx + 100 * SS, sea - 10 * SS), (cx + 80 * SS, sea + 12 * SS), (cx - 76 * SS, sea + 12 * SS)], outline=ink, width=SS * 2)
    for k, mx in enumerate((-50, 10, 64)):
        hgt = (150, 170, 130)[k]
        draw.line([(cx + mx * SS, sea - 10 * SS), (cx + mx * SS, sea - hgt * SS)], fill=ink, width=SS * 2)
        for s in range(3):
            y0 = sea - (hgt - 10 - s * 44) * SS
            draw.arc([cx + (mx - 30) * SS, y0, cx + (mx + 30) * SS, y0 + 46 * SS], 200, 340, fill=ink, width=SS * 2)
            draw.line([(cx + (mx - 26) * SS, y0 + 10 * SS), (cx + (mx + 26) * SS, y0 + 10 * SS)], fill=ink, width=SS)
    for k in range(8):
        y = sea + (16 + k * 6) * SS
        draw.line([(m + 8 * SS, y), (w - m - 8 * SS, y)], fill=ink, width=1)
    centred(draw, "OCTOBER", w / 2, top + 12 * SS, font(FONT_BLACK, 32), red, spacing=4 * SS)
    x0, x1 = m, w - m
    y0, y1 = h * 0.53, h - 26 * SS
    cw, ch = (x1 - x0) / 7, (y1 - y0) / 6
    for k, d in enumerate("MTWTFSS"):
        centred(draw, d, x0 + (k + 0.5) * cw, y0 + 4 * SS, font(FONT_BOLD, 13), red if k == 6 else ink)
    first = 2
    for day in range(1, 32):
        slot = first + day - 1
        c, r = slot % 7, min(slot // 7 + 1, 5)
        x, y = x0 + c * cw, y0 + r * ch
        draw.text((x + 6 * SS, y + 3 * SS), str(day), font=font(FONT_REGULAR, 14), fill=red if c == 6 else ink)
        if day <= 10 and rng.random() < 0.45:
            cursive_word(draw, x + 6 * SS, y + ch - 8 * SS, cw - 12 * SS, 2.6 * SS, rng, pen, SS)
    for c in range(8):
        draw.line([(x0 + c * cw, y0 + ch), (x0 + c * cw, y1)], fill=ink, width=SS)
    for r in range(1, 7):
        draw.line([(x0, y0 + r * ch), (x1, y0 + r * ch)], fill=ink, width=SS)
    draw.ellipse([w / 2 - 6 * SS, 2 * SS, w / 2 + 6 * SS, 14 * SS], fill=hexrgb("#1a1612"))
    return finish_sheet(img, "calendar", 472, foxing=1.0, damp=0.6, edge=0.8, dust=0.7)


def make_card():
    """An index card from his drawer: a heading, a rule, a few lines and a reference number."""
    img, draw, w, h = sheet("card", "#e6dcc2")
    rng = np.random.default_rng(481)
    ink, red, rule = hexrgb("#1a1a22"), hexrgb("#a24a40"), hexrgb("#8fa2b4")
    draw.line([(0, 52 * SS), (w, 52 * SS)], fill=red, width=SS)
    for k in range(9):
        draw.line([(0, (80 + k * 26) * SS), (w, (80 + k * 26) * SS)], fill=rule, width=1)
    cursive_word(draw, 24 * SS, 40 * SS, 220 * SS, 7 * SS, rng, ink, SS + 1)
    figures(draw, w - 120 * SS, 40 * SS, 5, 6 * SS, rng, ink, SS + 1)
    handwriting(draw, 24 * SS, 74 * SS, w - 24 * SS, 26 * SS, 6, rng, ink, SS + 1, 5.6 * SS)
    return finish_sheet(img, "card", 482, foxing=0.5, damp=0.2, edge=0.6, dust=0.4)


def make_blotter():
    """Blotting paper off the pad on the desk: green-grey, and printed with years of mirrored
    handwriting, blots and the rings of a cup."""
    img, draw, w, h = sheet("blotter", "#5d6b5a")
    rng = np.random.default_rng(491)
    ink = hexrgb("#2a3140")
    for _ in range(26):
        x, y = rng.uniform(0, w * 0.85), rng.uniform(0, h * 0.9)
        for k in range(int(rng.integers(1, 5))):
            cursive_word(draw, x + w * 0.1, y + k * 18 * SS, rng.uniform(80, 260) * SS, 5 * SS, rng, ink, SS)
    for _ in range(9):
        x, y, r = rng.uniform(0, w), rng.uniform(0, h), rng.uniform(4, 16) * SS
        draw.ellipse([x - r, y - r * 0.8, x + r, y + r * 0.8], fill=hexrgb("#262c38"))
    img = img.transpose(Image.FLIP_LEFT_RIGHT).filter(ImageFilter.GaussianBlur(1.6 * SS))
    draw = ImageDraw.Draw(img)
    for _ in range(3):
        x, y, r = rng.uniform(0.2, 0.8) * w, rng.uniform(0.2, 0.8) * h, 52 * SS
        draw.ellipse([x - r, y - r, x + r, y + r], outline=hexrgb("#4a3a2c"), width=4 * SS)
    return finish_sheet(img, "blotter", 492, foxing=0.2, damp=0.6, edge=0.6, dust=0.5)


def build_paper():
    atlas = np.ones((SIZE, SIZE, 3), dtype=np.float32) * np.array([0.82, 0.78, 0.68], dtype=np.float32)
    makers = {
        "notebook_l": lambda: make_notebook(False), "notebook_r": lambda: make_notebook(True),
        "book_l": lambda: make_book_page(False), "book_r": lambda: make_book_page(True),
        "notes_a": lambda: make_notes("a"), "notes_b": lambda: make_notes("b"),
        "letter": make_letter, "envelope": make_envelope, "newspaper": make_newspaper,
        "calendar": make_calendar, "card": make_card, "blotter": make_blotter,
    }
    for name, make in makers.items():
        x, y, w, h = PAPER[name]
        atlas[y:y + h, x:x + w] = make()
    save_set("library_paper", atlas, rough=0.88)
    return atlas


# ---------------------------------------------------------------------------------------------
# Geography.
# ---------------------------------------------------------------------------------------------

def land_polygons():
    path = os.path.join(REFERENCE, "ne_50m_land.geojson")
    if not os.path.isfile(path):
        os.makedirs(REFERENCE, exist_ok=True)
        urllib.request.urlretrieve(LAND_URL, path)
    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    rings = []
    for feature in data["features"]:
        geometry = feature["geometry"]
        polys = geometry["coordinates"] if geometry["type"] == "MultiPolygon" else [geometry["coordinates"]]
        for poly in polys:
            rings.append(poly[0])
    return rings


def land_mask(width, height, rings):
    """Equirectangular land mask: x = longitude from -180, y = latitude from +90."""
    img = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(img)
    for ring in rings:
        pts = [((lon + 180.0) / 360.0 * width, (90.0 - lat) / 180.0 * height) for lon, lat in ring]
        if len(pts) > 2:
            draw.polygon(pts, fill=255)
    return np.asarray(img, dtype=np.float32) / 255.0


def coast_lines(mask, blur):
    """A band just off the coast, for the water-lining an engraver ruled round every shore."""
    img = Image.fromarray((mask * 255).astype(np.uint8), "L")
    grown = np.asarray(img.filter(ImageFilter.GaussianBlur(blur)), dtype=np.float32) / 255.0
    return np.clip(grown * 3.0, 0.0, 1.0) * (1.0 - mask)


def build_globe(rings):
    """The globe's gores: a faded sea, the land in old wash colours, a graticule, the equator and the
    ecliptic, a cartouche in the South Pacific, and a century of varnish and hands."""
    w, h = SIZE, SIZE // 2
    mask = land_mask(w, h, rings)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    lon = xx / w * 360.0 - 180.0
    lat = 90.0 - yy / h * 180.0
    # The sea a cool grey-green and the land a warm buff, far enough apart that the continents read
    # under the lantern through a century of varnish: at two near buffs the globe was a plain ball.
    sea = np.array(hexrgb("#8e9a8a"), dtype=np.float32) / 255.0
    rgb = np.ones((h, w, 3), dtype=np.float32) * sea * (0.9 + 0.12 * noise(w, h, 8, 501))[..., None]
    # Land: a buff wash laid on by hand, heavier in some places than others. Coloured by region it
    # came out as blotches running straight across the coasts.
    land = np.array(hexrgb("#d6b47a"), dtype=np.float32) / 255.0 * (0.82 + 0.25 * noise(w, h, 9, 502, octaves=3))[..., None]
    land *= (0.92 + 0.1 * noise(w, h, 30, 503))[..., None]
    rgb = rgb * (1 - mask[..., None]) + land * mask[..., None]
    # Water-lining round the coasts, and the coast itself in ink.
    for blur, strength in ((3, 0.25), (7, 0.15), (12, 0.08)):
        band = coast_lines(mask, blur)
        ring = np.exp(-((band - 0.5) ** 2) / 0.02) * (band > 0.05)
        rgb *= (1.0 - ring * strength)[..., None]
    edge = np.asarray(Image.fromarray((mask * 255).astype(np.uint8)).filter(ImageFilter.FIND_EDGES).filter(ImageFilter.MaxFilter(3)),
                      dtype=np.float32) / 255.0
    rgb *= (1.0 - np.clip(edge * 2.0, 0, 1) * 0.75)[..., None]
    # Graticule every fifteen degrees; equator and tropics heavier; the ecliptic as a band.
    line = np.zeros((h, w), dtype=np.float32)
    for l in range(-180, 181, 15):
        line = np.maximum(line, np.exp(-((lon - l) * w / 360.0) ** 2 / 0.8))
    for l in range(-75, 76, 15):
        line = np.maximum(line, np.exp(-((lat - l) * h / 180.0) ** 2 / 0.8))
    for l, s in ((0.0, 1.6), (23.44, 1.0), (-23.44, 1.0)):
        line = np.maximum(line, np.exp(-((lat - l) * h / 180.0) ** 2 / s) * 1.0)
    ecl = 23.44 * np.sin(np.radians(lon))
    band = np.exp(-((lat - ecl) * h / 180.0) ** 2 / 14.0)
    rgb = rgb * (1 - band[..., None] * 0.18) + np.array([0.75, 0.55, 0.3]) * band[..., None] * 0.18
    rgb *= (1.0 - line * 0.28)[..., None]
    # A cartouche in the South Pacific.
    img = Image.fromarray(to_u8(rgb), "RGB")
    draw = ImageDraw.Draw(img)
    cx, cy = int((-130 + 180) / 360 * w), int((90 + 32) / 180 * h)
    draw.ellipse([cx - 120, cy - 60, cx + 120, cy + 60], fill=hexrgb("#d6c9a2"), outline=hexrgb("#3a2e22"), width=3)
    draw.ellipse([cx - 112, cy - 52, cx + 112, cy + 52], outline=hexrgb("#3a2e22"), width=1)
    tf = font(FONT_BOLD, 13)
    tw = draw.textlength("TERRESTRIAL GLOBE", font=tf)
    draw.text((cx - tw / 2, cy - 30), "TERRESTRIAL GLOBE", font=tf, fill=hexrgb("#3a2e22"))
    tf2 = font(FONT_REGULAR, 9)
    tw = draw.textlength("LONDON", font=tf2)
    draw.text((cx - tw / 2, cy + 10), "LONDON", font=tf2, fill=hexrgb("#3a2e22"))
    rgb = np.asarray(img, dtype=np.float32) / 255.0
    # Ocean names in letter-spaced capitals.
    rgb = varnish(rgb, 504, amount=0.7)
    # Handled at the middle latitudes, where a hand spins it: darker and shinier.
    hands = np.exp(-(lat / 35.0) ** 2) * noise(w, h, 6, 505)
    rgb *= (1.0 - hands * 0.18)[..., None]
    sheet_rgb = np.zeros((SIZE, SIZE, 3), dtype=np.float32)
    sheet_rgb[:h] = rgb
    sheet_rgb[h:] = rgb[::-1] * 0.3
    rough = 0.42 + 0.2 * noise(SIZE, SIZE, 10, 506) - hands.mean() * 0.0
    arm = np.stack([np.ones((SIZE, SIZE)), np.clip(rough, 0, 1), np.zeros((SIZE, SIZE))], axis=-1)
    save_set("library_globe", sheet_rgb, None, to_u8(arm))
    return sheet_rgb


def build_maps(rings):
    """Two maps for the frames between the windows: the world in two hemispheres, and a survey of
    the parish with the house in it."""
    out = np.zeros((SIZE, SIZE, 3), dtype=np.float32)
    mask = land_mask(4096, 2048, rings)

    # The world: two orthographic hemispheres, as old atlases drew it.
    w = h = 1024
    img = Image.new("RGB", (w, h), hexrgb("#d9cba6"))
    rgb = np.asarray(img, dtype=np.float32) / 255.0
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    r = 232.0
    for centre_lon, cx in ((-90.0, 258.0), (90.0, 766.0)):
        cy = 470.0
        x = (xx - cx) / r
        y = (cy - yy) / r
        inside = x * x + y * y <= 1.0
        z = np.sqrt(np.clip(1.0 - x * x - y * y, 0.0, 1.0))
        lat = np.degrees(np.arcsin(np.clip(y, -1, 1)))
        lon = centre_lon + np.degrees(np.arctan2(x, z))
        lon = (lon + 180.0) % 360.0 - 180.0
        mx = np.clip(((lon + 180.0) / 360.0 * 4096).astype(int), 0, 4095)
        my = np.clip(((90.0 - lat) / 180.0 * 2048).astype(int), 0, 2047)
        land = mask[my, mx] * inside
        sea = np.array([0.72, 0.74, 0.62]) * (0.92 + 0.1 * noise(w, h, 10, 511))[..., None]
        ground = np.array([0.78, 0.66, 0.48]) * (0.9 + 0.15 * noise(w, h, 20, 512))[..., None]
        disc = sea * (1 - land[..., None]) + ground * land[..., None]
        # Graticule.
        grid = (np.abs(((lon + 7.5) % 15.0) - 7.5) < 0.25) | (np.abs(((lat + 7.5) % 15.0) - 7.5) < 0.25)
        disc *= (1.0 - grid[..., None] * 0.35)
        rgb = np.where(inside[..., None], disc, rgb)
        rim = np.abs(np.sqrt(x * x + y * y) - 1.0) < 0.012
        rgb[rim] = (0.18, 0.14, 0.1)
    img = Image.fromarray(to_u8(rgb), "RGB")
    draw = ImageDraw.Draw(img)
    ink = hexrgb("#2e241a")
    draw.rectangle([18, 18, w - 18, h - 18], outline=ink, width=4)
    draw.rectangle([30, 30, w - 30, h - 30], outline=ink, width=1)
    centred(draw, "ORBIS TERRARUM", w / 2, 760 // 1, font(FONT_BOLD, 26), ink, spacing=8)
    centred(draw, "IN DUO HEMISPHAERIA", w / 2, 830, font(FONT_REGULAR, 14), ink, spacing=6)
    rgb = np.asarray(img, dtype=np.float32) / 255.0
    out[0:1024, 0:1024] = age_paper(rgb, 513, foxing=1.4, damp=0.8, edge=1.0, dust=0.6)

    # The parish: a river, roads, woods, fields, a church, the house, a compass rose.
    rng = np.random.default_rng(521)
    W = 1024 * SS
    img = Image.new("RGB", (W, W), hexrgb("#ddd0ad"))
    draw = ImageDraw.Draw(img)
    ink = hexrgb("#2b2219")
    blue = hexrgb("#4f6670")
    # Field boundaries: a loose lattice of hedgerows.
    for k in range(70):
        x0, y0 = rng.uniform(80, 940) * SS, rng.uniform(140, 900) * SS
        ang = rng.uniform(0, math.pi)
        L = rng.uniform(60, 180) * SS
        draw.line([(x0, y0), (x0 + math.cos(ang) * L, y0 + math.sin(ang) * L)], fill=hexrgb("#6a5d44"), width=SS)
    # The river, winding across.
    pts = []
    y = 260.0
    for x in range(40, 1000, 20):
        y += rng.uniform(-22, 22)
        y = min(max(y, 180), 840)
        pts.append((x * SS, y * SS))
    draw.line(pts, fill=blue, width=6 * SS, joint="curve")
    draw.line([(px, py + 9 * SS) for px, py in pts], fill=blue, width=SS)
    # Roads: double lines.
    for road in ([(60, 700), (300, 640), (520, 560), (760, 520), (980, 470)], [(480, 980), (500, 760), (520, 560), (540, 300), (600, 120)]):
        rp = [(x * SS, y * SS) for x, y in road]
        draw.line(rp, fill=ink, width=3 * SS, joint="curve")
        draw.line([(x + 7 * SS, y + 3 * SS) for x, y in rp], fill=ink, width=SS, joint="curve")
    # Woods: little tree marks in clusters.
    for _ in range(6):
        cx, cy = rng.uniform(120, 900) * SS, rng.uniform(160, 880) * SS
        for _ in range(60):
            tx, ty = cx + rng.normal(0, 40) * SS, cy + rng.normal(0, 30) * SS
            draw.ellipse([tx - 5 * SS, ty - 7 * SS, tx + 5 * SS, ty + 3 * SS], outline=hexrgb("#4a4a30"), width=SS)
            draw.line([(tx, ty + 3 * SS), (tx, ty + 8 * SS)], fill=hexrgb("#4a4a30"), width=SS)
    # The church and the house.
    for (hx, hy), label in (((560, 520), "CHURCH"), ((700, 610), "THE HALL")):
        draw.rectangle([(hx - 14) * SS, (hy - 10) * SS, (hx + 14) * SS, (hy + 10) * SS], fill=ink)
        draw.text(((hx + 20) * SS, (hy - 14) * SS), label, font=font(FONT_REGULAR, 13), fill=ink)
    for (vx, vy), name in (((240, 650), "ASHCOMBE"), ((820, 300), "MILL END"), ((300, 300), "LOWER MARSH")):
        for _ in range(8):
            px, py = vx + rng.uniform(-24, 24), vy + rng.uniform(-14, 14)
            draw.rectangle([(px - 4) * SS, (py - 3) * SS, (px + 4) * SS, (py + 3) * SS], fill=ink)
        draw.text(((vx - 40) * SS, (vy + 20) * SS), name, font=font(FONT_BOLD, 15), fill=ink)
    # Compass rose.
    cx, cy = 880 * SS, 840 * SS
    for k in range(8):
        a = k * math.pi / 4
        L = (60 if k % 2 == 0 else 34) * SS
        tip = (cx + math.sin(a) * L, cy - math.cos(a) * L)
        side1 = (cx + math.sin(a + 0.25) * 10 * SS, cy - math.cos(a + 0.25) * 10 * SS)
        side2 = (cx + math.sin(a - 0.25) * 10 * SS, cy - math.cos(a - 0.25) * 10 * SS)
        draw.polygon([side1, tip, side2, (cx, cy)], fill=ink if k % 2 == 0 else None, outline=ink)
    draw.text((cx - 7 * SS, cy - 92 * SS), "N", font=font(FONT_BOLD, 18), fill=ink)
    # Title cartouche and border.
    draw.rectangle([24 * SS, 24 * SS, W - 24 * SS, W - 24 * SS], outline=ink, width=5 * SS)
    draw.rectangle([36 * SS, 36 * SS, W - 36 * SS, W - 36 * SS], outline=ink, width=SS)
    draw.rectangle([250 * SS, 50 * SS, 774 * SS, 130 * SS], fill=hexrgb("#e2d6b6"), outline=ink, width=2 * SS)
    centred(draw, "A SURVEY OF THE PARISH", 512 * SS, 66 * SS, font(FONT_BOLD, 24), ink, spacing=3 * SS)
    centred(draw, "WITH THE HALL AND ITS GROUNDS", 512 * SS, 102 * SS, font(FONT_REGULAR, 13), ink, spacing=2 * SS)
    img = img.resize((1024, 1024), Image.LANCZOS)
    rgb = np.asarray(img, dtype=np.float32) / 255.0
    out[0:1024, 1024:2048] = age_paper(rgb, 522, foxing=1.2, damp=1.0, edge=1.0, dust=0.6)
    out[1024:] = out[:1024] * 0.25
    save_set("library_maps", out, rough=0.85)
    return out


# ---------------------------------------------------------------------------------------------
# Paintings.
# ---------------------------------------------------------------------------------------------

def soft_shape(w, h, draw_fn, blur):
    img = Image.new("L", (w, h), 0)
    draw_fn(ImageDraw.Draw(img))
    return np.asarray(img.filter(ImageFilter.GaussianBlur(blur)), dtype=np.float32) / 255.0


def paint(rgb, mask, colour, light=None, amount=1.0):
    c = np.array(colour, dtype=np.float32)
    shade = c if light is None else c * light[..., None]
    a = np.clip(mask * amount, 0, 1)[..., None]
    return rgb * (1 - a) + shade * a


def portrait_head(rgb, w, h, cx, cy, rx, ry, skin, hair, hair_shape, seed):
    """A head lit from the upper left, Rembrandt fashion: the face a soft oval, the shadow side
    falling away, eyes in shadow, a line of mouth. Painted soft; nothing sharp in an old oil."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    # The hair first and the face over it: a head of hair is round the face, and painted on top of
    # it the woman's came out as a top hat.
    hair_m = soft_shape(w, h, hair_shape, rx * 0.12)
    hl = 0.7 + 0.4 * noise(w, h, 24, seed + 1)
    rgb = paint(rgb, hair_m, hair, hl)
    face = soft_shape(w, h, lambda d: d.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=255), rx * 0.18)
    light = np.clip(1.15 - ((xx - (cx - rx * 0.45)) / (rx * 2.2)) ** 2 - ((yy - (cy - ry * 0.4)) / (ry * 2.6)) ** 2, 0.25, 1.0)
    light *= 0.9 + 0.1 * noise(w, h, 16, seed)
    rgb = paint(rgb, face, skin, light)
    # The hairline across the top of the forehead.
    fringe = soft_shape(w, h, lambda d: d.chord([cx - rx * 1.02, cy - ry * 1.05, cx + rx * 1.02, cy - ry * 0.25], 180, 360, fill=255), rx * 0.1)
    rgb = paint(rgb, fringe * face, hair, hl)
    for ex in (-0.38, 0.38):
        eye = soft_shape(w, h, lambda d, ex=ex: d.ellipse([cx + ex * rx - rx * 0.24, cy - ry * 0.2, cx + ex * rx + rx * 0.24, cy + ry * 0.02], fill=255), rx * 0.08)
        rgb = paint(rgb, eye * face, (0.10, 0.06, 0.04), None, 0.55)
        pupil = soft_shape(w, h, lambda d, ex=ex: d.ellipse([cx + ex * rx - rx * 0.07, cy - ry * 0.12, cx + ex * rx + rx * 0.07, cy - ry * 0.02], fill=255), rx * 0.03)
        rgb = paint(rgb, pupil * face, (0.04, 0.03, 0.02), None, 0.75)
    nose = soft_shape(w, h, lambda d: d.polygon([(cx + rx * 0.05, cy - ry * 0.05), (cx + rx * 0.18, cy + ry * 0.32), (cx - rx * 0.02, cy + ry * 0.36)], fill=255), rx * 0.06)
    rgb = paint(rgb, nose * face, (0.22, 0.12, 0.08), None, 0.45)
    mouth = soft_shape(w, h, lambda d: d.line([(cx - rx * 0.24, cy + ry * 0.55), (cx + rx * 0.22, cy + ry * 0.54)], fill=255, width=max(2, int(rx * 0.06))), rx * 0.04)
    rgb = paint(rgb, mouth * face, (0.22, 0.08, 0.06), None, 0.7)
    shadow = np.clip((xx - cx) / rx, 0, 1) * face
    rgb = paint(rgb, shadow, (0.12, 0.07, 0.05), None, 0.45)
    return rgb


def make_mother_and_daughter(seed):
    """Over the mantel: the woman seated in dark green, the little girl standing at her shoulder in
    white with a ribbon, a dark curtain and a column behind them."""
    w, h = 512, 640
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    bg = np.array([0.20, 0.14, 0.09]) * np.clip(1.25 - np.sqrt(((xx - 200) / 320) ** 2 + ((yy - 220) / 420) ** 2), 0.3, 1.2)[..., None]
    rgb = bg * (0.85 + 0.25 * noise(w, h, 6, seed))[..., None]
    curtain = soft_shape(w, h, lambda d: d.polygon([(330, 0), (512, 0), (512, 640), (420, 640), (380, 300)], fill=255), 18)
    folds = 0.6 + 0.4 * np.sin(xx / 14.0 + noise(w, h, 4, seed + 2) * 6)
    rgb = paint(rgb, curtain, (0.22, 0.06, 0.05), folds)
    # The woman: dark green silk, shoulders and a lap, her hands folded.
    body = soft_shape(w, h, lambda d: d.polygon([(70, 640), (90, 420), (150, 340), (215, 318), (280, 340), (330, 420), (350, 640)], fill=255), 8)
    sheen = 0.55 + 0.6 * np.clip(1 - ((xx - 170) / 120) ** 2, 0, 1) * (0.7 + 0.3 * noise(w, h, 20, seed + 3))
    rgb = paint(rgb, body, (0.07, 0.16, 0.10), sheen)
    collar = soft_shape(w, h, lambda d: d.polygon([(190, 322), (240, 322), (228, 360), (202, 360)], fill=255), 4)
    rgb = paint(rgb, collar, (0.70, 0.64, 0.52), None, 0.8)
    hands = soft_shape(w, h, lambda d: d.ellipse([170, 520, 260, 560], fill=255), 8)
    rgb = paint(rgb, hands, (0.70, 0.52, 0.40), np.clip(1.1 - (xx - 180) / 200, 0.4, 1.0))
    # Dark hair parted and drawn back over the ears, gathered low at the nape.
    rgb = portrait_head(rgb, w, h, 214, 250, 40, 54, (0.78, 0.60, 0.48), (0.12, 0.07, 0.04),
                        lambda d: (d.ellipse([166, 186, 262, 280], fill=255), d.ellipse([236, 262, 270, 300], fill=255)), seed + 4)
    # The girl at her shoulder, a head lower: white dress, a red ribbon in long fair hair.
    girl = soft_shape(w, h, lambda d: d.polygon([(290, 640), (300, 470), (330, 420), (372, 408), (414, 424), (440, 480), (446, 640)], fill=255), 8)
    rgb = paint(rgb, girl, (0.72, 0.68, 0.58), np.clip(1.1 - (xx - 320) / 180, 0.35, 1.0) * (0.85 + 0.2 * noise(w, h, 18, seed + 5)))
    rgb = portrait_head(rgb, w, h, 368, 360, 30, 40, (0.80, 0.63, 0.52), (0.42, 0.28, 0.14),
                        lambda d: (d.ellipse([332, 314, 404, 380], fill=255), d.polygon([(334, 350), (320, 452), (346, 456), (350, 370)], fill=255),
                                   d.polygon([(402, 350), (416, 448), (392, 452), (388, 370)], fill=255)), seed + 6)
    ribbon = soft_shape(w, h, lambda d: (d.ellipse([388, 318, 410, 334], fill=255), d.ellipse([404, 314, 424, 332], fill=255)), 2)
    rgb = paint(rgb, ribbon, (0.45, 0.06, 0.05), None, 0.9)
    return varnish(np.clip(rgb, 0, 1), seed + 7, amount=1.0)


def make_old_man(seed):
    """Over the arch: an old man in black, a white stock at his throat, white hair and side-whiskers,
    one hand on a book, against nothing but brown dark."""
    w, h = 512, 640
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    bg = np.array([0.16, 0.12, 0.08]) * np.clip(1.2 - np.sqrt(((xx - 190) / 300) ** 2 + ((yy - 200) / 400) ** 2), 0.25, 1.2)[..., None]
    rgb = bg * (0.85 + 0.25 * noise(w, h, 6, seed))[..., None]
    coat = soft_shape(w, h, lambda d: d.polygon([(40, 640), (70, 420), (160, 330), (256, 312), (350, 330), (440, 420), (470, 640)], fill=255), 8)
    rgb = paint(rgb, coat, (0.035, 0.03, 0.03), 0.7 + 0.5 * np.clip(1 - ((xx - 170) / 160) ** 2, 0, 1))
    stock = soft_shape(w, h, lambda d: d.polygon([(220, 312), (292, 312), (280, 380), (256, 400), (232, 380)], fill=255), 5)
    rgb = paint(rgb, stock, (0.75, 0.72, 0.64), np.clip(1.15 - (xx - 220) / 140, 0.5, 1.0))
    book = soft_shape(w, h, lambda d: d.polygon([(110, 560), (230, 540), (240, 600), (120, 620)], fill=255), 4)
    rgb = paint(rgb, book, (0.22, 0.07, 0.05), None, 0.9)
    hand = soft_shape(w, h, lambda d: d.ellipse([150, 520, 220, 566], fill=255), 7)
    rgb = paint(rgb, hand, (0.66, 0.48, 0.38), np.clip(1.1 - (xx - 150) / 160, 0.4, 1.0))
    rgb = portrait_head(rgb, w, h, 256, 230, 52, 68, (0.72, 0.55, 0.45), (0.66, 0.64, 0.60),
                        lambda d: (d.ellipse([196, 154, 316, 250], fill=255), d.polygon([(200, 220), (208, 300), (236, 300), (230, 220)], fill=255),
                                   d.polygon([(312, 220), (304, 300), (276, 300), (282, 220)], fill=255)), seed + 3)
    return varnish(np.clip(rgb, 0, 1), seed + 4, amount=1.2)


def build_paintings():
    out = np.zeros((1024, 1024, 3), dtype=np.float32)
    out[0:640, 0:512] = make_mother_and_daughter(601)
    out[0:640, 512:1024] = make_old_man(611)
    out[640:] = 0.05
    # Oil paint is glossier than anything else on these walls, which is how a painting reads as a
    # painting under a lantern: varnish at about 0.35.
    arm = np.zeros((1024, 1024, 3), dtype=np.float32)
    arm[..., 0] = 1.0
    arm[..., 1] = 0.36 + 0.12 * noise(1024, 1024, 30, 621)
    height = noise(1024, 1024, 120, 622, octaves=2) * 0.5 + crackle(1024, 1024, 623, scale=70.0) * -0.4
    save_set("library_paintings", out, normal_from_height(height, 2.0), to_u8(arm))
    return out


# ---------------------------------------------------------------------------------------------
# The rug.
# ---------------------------------------------------------------------------------------------

def build_rug():
    """A Persian carpet, 400 x 300: a madder-red field with a lattice of small flowers, an indigo
    medallion with ivory pendants, corner pieces, and a broad ivory border between guard stripes.
    Worn to the warp along the way from the arch to the desk and in front of the desk's kneehole;
    faded on the window side; grey with dust everywhere else."""
    W, H = 2000, 1500  # 5 px a centimetre, laid onto a square sheet at the end
    img = Image.new("RGB", (W, H), hexrgb("#6b1f19"))
    draw = ImageDraw.Draw(img)
    rng = np.random.default_rng(701)
    ivory, indigo, red, gold, green, black = (hexrgb(c) for c in ("#c4ae84", "#1d2444", "#7a231b", "#b48a3c", "#3d4b33", "#1a1412"))

    # Borders: outer guard, main border, inner guards.
    def frame(inset, colour, width):
        draw.rectangle([inset, inset, W - inset, H - inset], outline=colour, width=width)

    frame(0, indigo, 40)
    frame(40, ivory, 120)
    frame(160, indigo, 20)
    frame(180, gold, 10)
    # The main border's motifs: rosettes and vine between them, all round.
    def rosette(cx, cy, r, petal, centre):
        for k in range(8):
            a = k * math.pi / 4
            draw.ellipse([cx + math.cos(a) * r * 0.55 - r * 0.32, cy + math.sin(a) * r * 0.55 - r * 0.32,
                          cx + math.cos(a) * r * 0.55 + r * 0.32, cy + math.sin(a) * r * 0.55 + r * 0.32], fill=petal)
        draw.ellipse([cx - r * 0.35, cy - r * 0.35, cx + r * 0.35, cy + r * 0.35], fill=centre)

    for x in range(100, W - 60, 110):
        for y in (100, H - 100):
            rosette(x, y, 34, red if (x // 110) % 2 else indigo, gold)
    for y in range(210, H - 160, 110):
        for x in (100, W - 100):
            rosette(x, y, 34, red if (y // 110) % 2 else indigo, gold)
    # The field: rows of boteh, the bent teardrop, alternate rows turned the other way, each with a
    # dark heart. A lattice of crosses read as graph paper.
    def boteh(cx, cy, s, flip, fill, heart):
        pts = []
        for k in range(24):
            t = k / 23.0 * 2 * math.pi
            r = s * (0.55 + 0.45 * math.cos(t * 0.5) ** 2)
            x = math.cos(t) * r * 0.62
            y = math.sin(t) * r
            # The tip curls over at the top.
            if y < -s * 0.4:
                x += flip * (y + s * 0.4) * -0.6
            pts.append((cx + x * flip, cy + y))
        draw.polygon(pts, fill=fill)
        draw.ellipse([cx - s * 0.18, cy + s * 0.05, cx + s * 0.18, cy + s * 0.4], fill=heart)

    for row, y in enumerate(range(250, H - 220, 64)):
        for col, x in enumerate(range(250 + (32 if row % 2 else 0), W - 230, 64)):
            colour = (ivory, gold, indigo)[(row + col) % 3]
            boteh(x, y, 22, 1 if row % 2 else -1, colour, red if colour != red else indigo)
    # Corner pieces: quarter lozenges in indigo.
    for cx, cy, sx, sy in ((190, 190, 1, 1), (W - 190, 190, -1, 1), (190, H - 190, 1, -1), (W - 190, H - 190, -1, -1)):
        draw.polygon([(cx, cy), (cx + sx * 380, cy), (cx, cy + sy * 300)], fill=indigo)
        draw.polygon([(cx + sx * 40, cy + sy * 30), (cx + sx * 280, cy + sy * 30), (cx + sx * 40, cy + sy * 220)], outline=gold, width=6)
    # The medallion: an indigo lozenge with an ivory centre and two pendants.
    cx, cy = W / 2, H / 2
    draw.polygon([(cx - 420, cy), (cx, cy - 330), (cx + 420, cy), (cx, cy + 330)], fill=indigo)
    draw.polygon([(cx - 400, cy), (cx, cy - 312), (cx + 400, cy), (cx, cy + 312)], outline=gold, width=8)
    draw.ellipse([cx - 170, cy - 140, cx + 170, cy + 140], fill=ivory)
    rosette(cx, cy, 90, red, indigo)
    for sx in (-1, 1):
        draw.polygon([(cx + sx * 420, cy), (cx + sx * 520, cy - 60), (cx + sx * 600, cy), (cx + sx * 520, cy + 60)], fill=ivory)
        rosette(cx + sx * 520, cy, 34, red, indigo)
    # Inside the medallion, small boteh round the centre, and a ring of rosettes.
    for k in range(8):
        a = k * math.pi / 4
        rosette(cx + math.cos(a) * 230, cy + math.sin(a) * 175, 26, (red, gold)[k % 2], ivory)
    for cx2, cy2, sx, sy in ((190, 190, 1, 1), (W - 190, 190, -1, 1), (190, H - 190, 1, -1), (W - 190, H - 190, -1, -1)):
        rosette(cx2 + sx * 110, cy2 + sy * 80, 30, red, gold)
    # Kilim ends: a flat-woven ivory strip at each short end, where the fringe comes out.
    draw.rectangle([0, 0, 24, H], fill=ivory)
    draw.rectangle([W - 24, 0, W, H], fill=ivory)

    # Knotted pile has no hard edges: every outline is a few knots wide.
    img = img.filter(ImageFilter.GaussianBlur(2.2))
    rgb = np.asarray(img, dtype=np.float32) / 255.0
    # Abrash: the dye lots differ, so the field bands across in slightly different reds.
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    abrash = 0.9 + 0.12 * np.sin(xx / 170.0 + noise(W, H, 3, 702) * 3.0)
    rgb *= abrash[..., None]
    # The knots: a fine grid that breaks every edge into steps.
    knots = 0.9 + 0.1 * np.sin(xx * 2.2) * np.sin(yy * 2.2)
    rgb *= knots[..., None]
    rgb *= (0.88 + 0.22 * noise(W, H, 60, 703, octaves=2))[..., None]
    # Wear: the pile gone to the ivory warp along a path from the arch end (x = 0 side is north) to
    # the middle, and in front of where the chair stood (the south long side, y high).
    path = np.exp(-((yy - H * 0.42 - (xx / W) * 120) / 170.0) ** 2) * np.exp(-((xx - W * 0.3) / (W * 0.35)) ** 2)
    chair = np.exp(-(((xx - W * 0.5) / 260.0) ** 2 + ((yy - H * 0.82) / 160.0) ** 2))
    worn = np.clip((path * 0.8 + chair) * (0.3 + noise(W, H, 40, 704)), 0.0, 1.0)
    warp = np.array([0.62, 0.55, 0.42]) * (0.8 + 0.2 * np.sin(xx * 3.1))[..., None]
    rgb = rgb * (1 - worn[..., None] * 0.65) + warp * worn[..., None] * 0.65
    # Faded on the window side (the south long edge), and dust over everything.
    fade = np.clip((yy / H - 0.55) * 1.2, 0.0, 0.4)
    rgb = rgb * (1 - fade[..., None]) + rgb.mean(axis=-1, keepdims=True) * fade[..., None] * 1.15
    dust = noise(W, H, 5, 705) * 0.25 + 0.15
    rgb = rgb * (1 - dust[..., None]) + np.array([0.48, 0.45, 0.40]) * dust[..., None]
    sheet_img = Image.fromarray(to_u8(rgb), "RGB").resize((SIZE, SIZE), Image.LANCZOS)
    rgb = np.asarray(sheet_img, dtype=np.float32) / 255.0
    pile = noise(SIZE, SIZE, 400, 706, octaves=2)
    worn_sq = np.asarray(Image.fromarray((worn * 255).astype(np.uint8)).resize((SIZE, SIZE)), dtype=np.float32) / 255.0
    height = pile * (1.0 - worn_sq * 0.8) + (1.0 - worn_sq) * 0.4
    arm = np.stack([np.ones((SIZE, SIZE)), np.full((SIZE, SIZE), 0.95), np.zeros((SIZE, SIZE))], axis=-1)
    save_set("library_rug", rgb, normal_from_height(height, 3.0), to_u8(arm))
    return rgb


# ---------------------------------------------------------------------------------------------
# The curtains' velvet.
# ---------------------------------------------------------------------------------------------

def tile_noise(n, cutoff, seed, aspect=1.0):
    """Noise that tiles: white noise low-passed in the frequency domain, which is periodic by
    construction. cutoff is in cycles across the sheet; an aspect under one stretches it along v
    (down the sheet, which is down the drop on the generated curtains)."""
    rng = np.random.default_rng(seed)
    white = rng.standard_normal((n, n))
    fy = np.fft.fftfreq(n)[:, None] * n
    fx = np.fft.fftfreq(n)[None, :] * n
    f = np.sqrt(fx ** 2 + (fy / aspect) ** 2)
    out = np.real(np.fft.ifft2(np.fft.fft2(white) * np.exp(-(f / cutoff) ** 2)))
    out -= out.min()
    return (out / out.max()).astype(np.float32)


def build_velvet():
    """Crimson velvet for the library's drapes and its valance, tiling at the drapes' 34cm (so the
    generated curtains' UVs fit it as they are, like the girl's rose print). The pile: a fine grain,
    long soft streaks where it was brushed one way and crushed the other, the odd threadbare patch
    gone to the pale weave, and the colour faded unevenly. Velvet's sheen is a material's job; what
    the photograph-like map can do is the crush, which is most of what makes velvet read as velvet."""
    n = 1024
    pile = tile_noise(n, 180.0, 801)
    crush = tile_noise(n, 5.0, 802, aspect=0.25)
    patches = tile_noise(n, 3.0, 803)
    base = np.array([0.43, 0.035, 0.055], dtype=np.float32)
    light = 0.72 + 0.5 * crush + 0.16 * (pile - 0.5)
    rgb = base * light[..., None]
    # Threadbare where it was handled and where it rubbed the sill: the weave shows, paler and browner.
    bare = np.clip((patches - 0.72) * 5.0, 0.0, 1.0)
    rgb = rgb * (1 - bare[..., None] * 0.6) + np.array([0.30, 0.13, 0.10]) * bare[..., None] * 0.6
    # Faded towards the pink in the sun, by a little, unevenly.
    fade = tile_noise(n, 2.0, 804) * 0.25
    rgb = rgb * (1 - fade[..., None]) + np.array([0.46, 0.16, 0.16]) * fade[..., None]
    height = pile * 0.6 + crush * 0.4 - bare * 0.3
    arm = np.stack([np.ones((n, n)), 0.86 + 0.1 * (1 - crush), np.zeros((n, n))], axis=-1)
    save_set("library_velvet", np.clip(rgb, 0, 1), normal_from_height(height, 2.5), to_u8(arm))
    lin = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
    print("library_velvet linear mean", lin.reshape(-1, 3).mean(axis=0))
    return rgb


def main():
    os.makedirs(PREVIEW, exist_ok=True)
    if "--velvet" in os.sys.argv:
        preview("library_velvet", build_velvet())
        return
    preview("library_velvet", build_velvet())
    os.makedirs(TEXTURES, exist_ok=True)
    rings = land_polygons()
    preview("library_books", build_books())
    preview("library_paper", build_paper())
    preview("library_globe", build_globe(rings))
    preview("library_maps", build_maps(rings))
    preview("library_paintings", build_paintings())
    preview("library_rug", build_rug())
    print("Wrote library_books, library_paper, library_globe, library_maps, library_paintings, library_rug to", TEXTURES)


if __name__ == "__main__":
    main()
