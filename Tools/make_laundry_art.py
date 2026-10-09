#!/usr/bin/env python3
"""
Bakes the laundry room's printed and written paper into a texture set the art pipeline imports.

    python Tools/make_laundry_art.py

One set, laundry_paper, written as _Diffuse / _nor_dx / _arm into Art/Source/Textures: a 2048 sheet
of rectangles (pixel x, y, w, h), which Tools/make_laundry.py (ATLAS) and ALaundryActor share:

    detergent  (   0,    0, 354, 512)  the front of a carton of soap powder
    softener   ( 512,    0, 512, 370)  a fabric rinse's label, wrapped round 150 degrees of a bottle
    spray      (1024,    0, 410, 512)  a household cleaner's label
    starch     (1536,    0, 354, 512)  a smaller carton: laundry starch
    radio      (   0,  512, 512, 200)  the radio's tuning scale
    soap       ( 512,  512, 512, 256)  the paper round a bar of soap
    calendar   (1024,  512, 366, 512)  a month on the wall: a print above, the days below
    note_a     (1536,  512, 342, 512)  a note on a sheet off a pad
    schedule   (   0, 1024, 724, 512)  the week's laundry, ruled up by hand
    note_b     (1024, 1024, 342, 512)  a list, ticked off partway

The handwriting is a cursive *hand*, not text, as in the wine cellar (make_cellar_art.py, whose
helpers this uses): nothing written here says anything, because the clue writing is the user's to
do when the game is built (the 09-30 decision). Printed things say what printed things say — a
maker's name, a month, the days — in Cinzel (SIL OFL), the house's one typeface. The makers'
names are invented.
"""

import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from make_cellar_art import (FONT_BLACK, FONT_BOLD, FONT_REGULAR, SS, TEXTURES, age_paper, centred, cursive_word, figures,
                             font, handwriting, hexrgb)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PREVIEW = os.path.join(ROOT, "Saved", "LaundryPreview")
SIZE = 2048

RECTS = {
    "detergent": (0, 0, 354, 512),
    "softener": (512, 0, 512, 370),
    "spray": (1024, 0, 410, 512),
    "starch": (1536, 0, 354, 512),
    "radio": (0, 512, 512, 200),
    "soap": (512, 512, 512, 256),
    "calendar": (1024, 512, 366, 512),
    "note_a": (1536, 512, 342, 512),
    "schedule": (0, 1024, 724, 512),
    "note_b": (1024, 1024, 342, 512),
}


def canvas(name, colour):
    w, h = RECTS[name][2], RECTS[name][3]
    img = Image.new("RGB", (w * SS, h * SS), hexrgb(colour))
    return img, ImageDraw.Draw(img), w * SS, h * SS


def finish(img, name, seed, foxing=0.6, damp=0.4, edge=0.8, dust=0.6, blur=0.4):
    w, h = RECTS[name][2], RECTS[name][3]
    img = img.resize((w, h), Image.LANCZOS)
    if blur:
        img = img.filter(ImageFilter.GaussianBlur(blur))
    rgb = np.asarray(img, dtype=np.float32) / 255.0
    return age_paper(rgb, seed, foxing=foxing, damp=damp, edge=edge, dust=dust)


def sunburst(draw, cx, cy, r0, r1, rays, colour, width):
    for k in range(rays):
        a = 2 * math.pi * k / rays
        draw.line([(cx + math.cos(a) * r0, cy + math.sin(a) * r0), (cx + math.cos(a) * r1, cy + math.sin(a) * r1)], fill=colour, width=width)


# ---------------------------------------------------------------------------------------------
# Packaging.
# ---------------------------------------------------------------------------------------------

def make_detergent():
    """Soap powder: a blue carton, a white sunburst, the maker's name across a red band."""
    img, draw, w, h = canvas("detergent", "#2c5a8c")
    cream = hexrgb("#f1e8d2")
    red = hexrgb("#b23a2e")
    navy = hexrgb("#16253d")
    sunburst(draw, w / 2, h * 0.36, 30 * SS, 150 * SS, 36, hexrgb("#5d86b2"), 5 * SS)
    draw.ellipse([w / 2 - 70 * SS, h * 0.36 - 70 * SS, w / 2 + 70 * SS, h * 0.36 + 70 * SS], fill=cream)
    centred(draw, "NEW", w / 2, h * 0.36 - 44 * SS, font(FONT_BOLD, 22), red, spacing=3 * SS)
    centred(draw, "WHITER", w / 2, h * 0.36 - 14 * SS, font(FONT_BLACK, 24), navy, spacing=2 * SS)
    centred(draw, "WASH", w / 2, h * 0.36 + 14 * SS, font(FONT_BLACK, 24), navy, spacing=2 * SS)
    draw.rectangle([0, h * 0.62, w, h * 0.80], fill=red)
    centred(draw, "WHITECLIFF", w / 2, h * 0.64, font(FONT_BLACK, 40), cream, spacing=2 * SS)
    centred(draw, "SOAP POWDER", w / 2, h * 0.835, font(FONT_BOLD, 24), cream, spacing=4 * SS)
    centred(draw, "FOR THE FAMILY WASH", w / 2, h * 0.905, font(FONT_REGULAR, 15), cream, spacing=2 * SS)
    draw.rectangle([10 * SS, 10 * SS, w - 10 * SS, h - 10 * SS], outline=cream, width=3 * SS)
    return finish(img, "detergent", 61, foxing=0.3, damp=0.8, edge=1.0, dust=0.9)


def make_starch():
    """Laundry starch: a lilac carton with a collar and cuffs drawn on it."""
    img, draw, w, h = canvas("starch", "#7d6c93")
    cream = hexrgb("#efe6d4")
    ink = hexrgb("#2b2234")
    draw.rectangle([0, 0, w, h * 0.22], fill=cream)
    centred(draw, "SILVERSHEEN", w / 2, h * 0.05, font(FONT_BLACK, 30), ink, spacing=SS)
    centred(draw, "LAUNDRY STARCH", w / 2, h * 0.14, font(FONT_BOLD, 18), ink, spacing=3 * SS)
    # A shirt collar, drawn in white line.
    cx, cy = w / 2, h * 0.52
    draw.polygon([(cx - 80 * SS, cy - 40 * SS), (cx, cy + 30 * SS), (cx - 20 * SS, cy - 60 * SS)], outline=cream, width=4 * SS)
    draw.polygon([(cx + 80 * SS, cy - 40 * SS), (cx, cy + 30 * SS), (cx + 20 * SS, cy - 60 * SS)], outline=cream, width=4 * SS)
    draw.arc([cx - 90 * SS, cy - 90 * SS, cx + 90 * SS, cy + 50 * SS], 200, 340, fill=cream, width=4 * SS)
    centred(draw, "CRISP AS NEW", w / 2, h * 0.78, font(FONT_BOLD, 20), cream, spacing=3 * SS)
    centred(draw, "COLLARS  CUFFS  LINEN", w / 2, h * 0.87, font(FONT_REGULAR, 13), cream, spacing=2 * SS)
    return finish(img, "starch", 62, foxing=0.4, damp=0.6, edge=1.0, dust=0.9)


def make_softener():
    """A fabric rinse: cream paper, a green border, a spray of flowers, the maker's name."""
    img, draw, w, h = canvas("softener", "#ece2c6")
    green = hexrgb("#3f6a46")
    rose = hexrgb("#b2546a")
    ink = hexrgb("#24301f")
    draw.rectangle([12 * SS, 12 * SS, w - 12 * SS, h - 12 * SS], outline=green, width=6 * SS)
    draw.rectangle([24 * SS, 24 * SS, w - 24 * SS, h - 24 * SS], outline=green, width=2 * SS)
    for k, (dx, dy) in enumerate(((-40, 0), (0, -20), (40, 4), (-18, 26), (22, 30))):
        cx, cy = w / 2 + dx * SS, 110 * SS + dy * SS
        for p in range(5):
            a = 2 * math.pi * p / 5 + k
            draw.ellipse([cx + math.cos(a) * 9 * SS - 9 * SS, cy + math.sin(a) * 9 * SS - 9 * SS,
                          cx + math.cos(a) * 9 * SS + 9 * SS, cy + math.sin(a) * 9 * SS + 9 * SS], fill=rose)
        draw.ellipse([cx - 5 * SS, cy - 5 * SS, cx + 5 * SS, cy + 5 * SS], fill=hexrgb("#e8c46a"))
    for side in (-1, 1):
        draw.ellipse([w / 2 + side * 80 * SS - 30 * SS, 120 * SS, w / 2 + side * 80 * SS + 30 * SS, 140 * SS], fill=green)
    centred(draw, "MEADOWSOFT", w / 2, 180 * SS, font(FONT_BLACK, 46), ink, spacing=2 * SS)
    centred(draw, "FABRIC RINSE", w / 2, 246 * SS, font(FONT_BOLD, 22), green, spacing=5 * SS)
    centred(draw, "SOFT AS SPRING", w / 2, 290 * SS, font(FONT_REGULAR, 16), ink, spacing=3 * SS)
    return finish(img, "softener", 63, foxing=0.8, damp=0.9, edge=0.9, dust=0.8)


def make_spray():
    """A household cleaner: yellow, a red diamond, the name, a few lines of small print."""
    img, draw, w, h = canvas("spray", "#d7b23a")
    red = hexrgb("#a52f24")
    ink = hexrgb("#2a1c10")
    cream = hexrgb("#f3e9cf")
    draw.polygon([(w / 2, 40 * SS), (w - 50 * SS, 170 * SS), (w / 2, 300 * SS), (50 * SS, 170 * SS)], fill=red)
    centred(draw, "CLEARWELL", w / 2, 140 * SS, font(FONT_BLACK, 34), cream, spacing=SS)
    centred(draw, "HOUSEHOLD", w / 2, 330 * SS, font(FONT_BOLD, 26), ink, spacing=3 * SS)
    centred(draw, "CLEANER", w / 2, 368 * SS, font(FONT_BOLD, 26), ink, spacing=3 * SS)
    for k in range(4):
        y = 420 * SS + k * 16 * SS
        draw.line([(70 * SS, y), (w - 70 * SS - (k == 3) * 100 * SS, y)], fill=ink, width=3 * SS)
    return finish(img, "spray", 64, foxing=0.3, damp=0.7, edge=1.0, dust=1.0)


def make_soap():
    """The paper round a bar of soap: a band of brown on cream, the name in a cartouche."""
    img, draw, w, h = canvas("soap", "#e9dcbc")
    brown = hexrgb("#6a4126")
    draw.rectangle([0, 0, w, 44 * SS], fill=brown)
    draw.rectangle([0, h - 44 * SS, w, h], fill=brown)
    draw.rounded_rectangle([60 * SS, 70 * SS, w - 60 * SS, h - 70 * SS], radius=30 * SS, outline=brown, width=4 * SS)
    centred(draw, "PURE PALE", w / 2, 86 * SS, font(FONT_BLACK, 36), brown, spacing=3 * SS)
    centred(draw, "SOAP", w / 2, 134 * SS, font(FONT_BOLD, 26), brown, spacing=8 * SS)
    return finish(img, "soap", 65, foxing=0.6, damp=0.5, edge=1.0, dust=0.8)


def make_radio():
    """The tuning scale: wavebands across a cream glass, ticks and the figures of the dial."""
    img, draw, w, h = canvas("radio", "#e3d4ad")
    ink = hexrgb("#2b2014")
    red = hexrgb("#8f2f22")
    for row, (y, label) in enumerate(((60, "LONG"), (110, "MEDIUM"), (160, "SHORT"))):
        yy = y * SS
        draw.line([(110 * SS, yy), (w - 30 * SS, yy)], fill=ink, width=2 * SS)
        draw.text((18 * SS, yy - 14 * SS), label, font=font(FONT_BOLD, 14), fill=red)
        for k in range(21):
            x = 110 * SS + k * (w - 140 * SS) / 20
            draw.line([(x, yy - (10 if k % 5 == 0 else 5) * SS), (x, yy)], fill=ink, width=SS + (k % 5 == 0))
        for k in range(5):
            x = 110 * SS + k * (w - 140 * SS) / 4
            draw.text((x - 10 * SS, yy - 34 * SS), str((row + 1) * 50 + k * 25 * (row + 1)), font=font(FONT_REGULAR, 12), fill=ink)
    return finish(img, "radio", 66, foxing=0.3, damp=0.2, edge=0.5, dust=0.7)


# ---------------------------------------------------------------------------------------------
# Paper on the wall and on the table.
# ---------------------------------------------------------------------------------------------

def draw_lighthouse(draw, x0, y0, x1, y1, colours):
    """The month's picture: a lighthouse on a headland, the sea, a gull or two. Printed in four
    flat inks, the way a calendar from the coalman was."""
    sky, sea, land, ink = colours
    draw.rectangle([x0, y0, x1, y1], fill=sky)
    horizon = y0 + (y1 - y0) * 0.58
    draw.rectangle([x0, horizon, x1, y1], fill=sea)
    w = x1 - x0
    head = [(x0, horizon - 30 * SS), (x0 + w * 0.35, horizon - 46 * SS), (x0 + w * 0.55, horizon - 20 * SS),
            (x0 + w * 0.62, y1), (x0, y1)]
    draw.polygon(head, fill=land)
    tx = x0 + w * 0.22
    ty = horizon - 44 * SS
    draw.polygon([(tx - 12 * SS, ty), (tx + 12 * SS, ty), (tx + 8 * SS, ty - 90 * SS), (tx - 8 * SS, ty - 90 * SS)], fill=hexrgb("#efe6d2"))
    for k in range(3):
        yy = ty - 20 * SS - k * 26 * SS
        draw.rectangle([tx - 11 * SS + k * SS, yy - 8 * SS, tx + 11 * SS - k * SS, yy], fill=hexrgb("#a83a2c"))
    draw.rectangle([tx - 10 * SS, ty - 106 * SS, tx + 10 * SS, ty - 90 * SS], fill=ink)
    draw.polygon([(tx - 12 * SS, ty - 106 * SS), (tx, ty - 118 * SS), (tx + 12 * SS, ty - 106 * SS)], fill=hexrgb("#a83a2c"))
    for k in range(10):
        yy = horizon + (k + 1) * (y1 - horizon) / 11
        draw.line([(x0 + w * 0.6, yy), (x1 - 10 * SS, yy)], fill=hexrgb("#e8dcc4"), width=SS)
    for gx, gy in ((0.7, 0.2), (0.82, 0.3)):
        x, y = x0 + w * gx, y0 + (y1 - y0) * gy
        draw.arc([x - 14 * SS, y - 6 * SS, x, y + 6 * SS], 200, 340, fill=ink, width=2 * SS)
        draw.arc([x, y - 6 * SS, x + 14 * SS, y + 6 * SS], 200, 340, fill=ink, width=2 * SS)


def make_calendar():
    """
    One month on the wall: a picture above, the days below in a grid. The days are crossed off in
    pencil up to the tenth, as she did every morning; after that, nothing.
    """
    img, draw, w, h = canvas("calendar", "#ebe1c7")
    ink = hexrgb("#2a2219")
    red = hexrgb("#9a3326")
    pencil = hexrgb("#4a4a4c")
    m = 14 * SS
    draw_lighthouse(draw, m, m, w - m, h * 0.44, (hexrgb("#9eb3bd"), hexrgb("#3e6476"), hexrgb("#5c6b3a"), ink))
    draw.rectangle([m, m, w - m, h * 0.44], outline=ink, width=SS * 2)
    centred(draw, "OCTOBER", w / 2, h * 0.455, font(FONT_BLACK, 34), red, spacing=4 * SS)
    x0, x1 = m, w - m
    y0, y1 = h * 0.56, h - 30 * SS
    cw, ch = (x1 - x0) / 7, (y1 - y0) / 6
    for k, d in enumerate("MTWTFSS"):
        centred(draw, d, x0 + (k + 0.5) * cw, y0 + 4 * SS, font(FONT_BOLD, 14), red if k == 6 else ink)
    first = 2  # the first falls on a Wednesday
    rng = np.random.default_rng(71)
    for day in range(1, 32):
        slot = first + day - 1
        c, r = slot % 7, slot // 7 + 1
        if r > 5:
            r, c = 5, c  # the last days share the bottom row's cells, as calendars did
        x, y = x0 + c * cw, y0 + r * ch
        draw.text((x + 8 * SS, y + 4 * SS), str(day), font=font(FONT_REGULAR, 17), fill=red if c == 6 else ink)
        if day <= 10:
            j = rng.uniform(-3, 3, 4) * SS
            draw.line([(x + 6 * SS + j[0], y + 6 * SS), (x + cw - 6 * SS, y + ch - 4 * SS + j[1])], fill=pencil, width=2 * SS)
            draw.line([(x + cw - 8 * SS + j[2], y + 6 * SS), (x + 8 * SS, y + ch - 6 * SS + j[3])], fill=pencil, width=2 * SS)
    for c in range(8):
        draw.line([(x0 + c * cw, y0 + ch), (x0 + c * cw, y1)], fill=ink, width=SS)
    for r in range(1, 7):
        draw.line([(x0, y0 + r * ch), (x1, y0 + r * ch)], fill=ink, width=SS)
    # The hole it hangs by.
    draw.ellipse([w / 2 - 6 * SS, 2 * SS, w / 2 + 6 * SS, 14 * SS], fill=hexrgb("#1a1612"))
    return finish(img, "calendar", 72, foxing=1.0, damp=0.9, edge=0.9, dust=0.8)


def make_schedule():
    """
    The week's laundry, ruled up by hand on a sheet of foolscap turned sideways: the days across the
    top, the jobs down the side, a tick or a word in the squares. Pencil rules, ink writing.
    """
    img, draw, w, h = canvas("schedule", "#e8dfc8")
    rng = np.random.default_rng(81)
    ink = hexrgb("#1f2a44")
    pencil = hexrgb("#55575c")
    # A heading, written and underlined twice.
    cursive_word(draw, w * 0.34, 46 * SS, w * 0.32, 8 * SS, rng, ink, 2 * SS)
    draw.line([(w * 0.33, 54 * SS), (w * 0.68, 53 * SS)], fill=ink, width=SS + 1)
    draw.line([(w * 0.34, 58 * SS), (w * 0.67, 58 * SS)], fill=ink, width=SS + 1)
    x0, x1 = 24 * SS, w - 20 * SS
    y0, y1 = 76 * SS, h - 22 * SS
    first = 150 * SS
    cols = 7
    cw = (x1 - x0 - first) / cols
    rows = 8
    rh = (y1 - y0) / rows
    for c in range(cols + 1):
        x = x0 + first + c * cw
        draw.line([(x + rng.uniform(-1, 1) * SS, y0), (x + rng.uniform(-1, 1) * SS, y1)], fill=pencil, width=SS)
    draw.line([(x0, y0), (x0, y1)], fill=pencil, width=SS)
    for r in range(rows + 1):
        y = y0 + r * rh
        draw.line([(x0, y + rng.uniform(-1, 1) * SS), (x1, y + rng.uniform(-1, 1) * SS)], fill=pencil, width=SS)
    # The days across the top, the jobs down the side.
    for c in range(cols):
        cursive_word(draw, x0 + first + c * cw + 8 * SS, y0 + rh * 0.7, cw * rng.uniform(0.45, 0.7), 5.5 * SS, rng, ink, SS + 1)
    for r in range(1, rows):
        cursive_word(draw, x0 + 8 * SS, y0 + r * rh + rh * 0.7, first * rng.uniform(0.55, 0.85), 5.5 * SS, rng, ink, SS + 1)
        for c in range(cols):
            cx, cy = x0 + first + c * cw, y0 + r * rh
            pick = rng.random()
            if pick < 0.35:
                # A tick.
                draw.line([(cx + cw * 0.35, cy + rh * 0.55), (cx + cw * 0.45, cy + rh * 0.75), (cx + cw * 0.68, cy + rh * 0.28)],
                          fill=ink, width=SS + 1)
            elif pick < 0.55:
                cursive_word(draw, cx + 6 * SS, cy + rh * 0.68, cw * rng.uniform(0.4, 0.75), 4.6 * SS, rng, ink, SS + 1)
    # Something added later in pencil down the margin, with an arrow.
    cursive_word(draw, x1 - 150 * SS, y1 + 14 * SS, 120 * SS, 4.4 * SS, rng, pencil, SS + 1)
    return finish(img, "schedule", 82, foxing=0.7, damp=0.5, edge=0.8, dust=0.6)


def make_note(name, seed, ticks):
    """A sheet off a pad, a red rule down the margin; a note, or with ticks a list done partway."""
    img, draw, w, h = canvas(name, "#ece4cc")
    rng = np.random.default_rng(seed)
    blue = hexrgb("#9fb0c4")
    red = hexrgb("#b0574c")
    ink = hexrgb("#1c2236") if not ticks else hexrgb("#2d2119")
    pitch = 26 * SS
    for k in range(2, int(h / pitch)):
        draw.line([(0, k * pitch), (w, k * pitch)], fill=blue, width=SS)
    draw.line([(44 * SS, 0), (44 * SS, h)], fill=red, width=SS)
    # The torn top edge off the pad.
    for x in range(0, w, 6 * SS):
        draw.rectangle([x, 0, x + 3 * SS, rng.uniform(2, 7) * SS], fill=hexrgb("#d8ccae"))
    if ticks:
        n = 9
        done = 6
        for k in range(n):
            y = (3 + k) * pitch - 6 * SS
            cursive_word(draw, 58 * SS, y, rng.uniform(0.35, 0.75) * (w - 80 * SS), 5.4 * SS, rng, ink, SS + 1)
            if k < done:
                draw.line([(16 * SS, y - 8 * SS), (24 * SS, y), (38 * SS, y - 18 * SS)], fill=ink, width=SS + 1)
    else:
        cursive_word(draw, 58 * SS, 2 * pitch - 6 * SS, 90 * SS, 5.6 * SS, rng, ink, SS + 1)
        handwriting(draw, 58 * SS, 4 * pitch - 6 * SS, w - 20 * SS, pitch, 9, rng, ink, SS + 1, 5.4 * SS, indent=16 * SS)
        # Signed off with an initial, underlined.
        cursive_word(draw, w - 110 * SS, 15 * pitch - 6 * SS, 50 * SS, 6.4 * SS, rng, ink, SS + 1)
    return finish(img, name, seed, foxing=0.6, damp=0.4 if ticks else 0.7, edge=0.6, dust=0.5, blur=0.3)


def main():
    os.makedirs(PREVIEW, exist_ok=True)
    atlas = np.ones((SIZE, SIZE, 3), dtype=np.float32) * np.array([0.80, 0.76, 0.66], dtype=np.float32)
    makers = {
        "detergent": make_detergent, "softener": make_softener, "spray": make_spray, "starch": make_starch,
        "radio": make_radio, "soap": make_soap, "calendar": make_calendar, "schedule": make_schedule,
        "note_a": lambda: make_note("note_a", 91, False), "note_b": lambda: make_note("note_b", 92, True),
    }
    for name, make in makers.items():
        x, y, w, h = RECTS[name]
        atlas[y:y + h, x:x + w] = make()
    Image.fromarray((np.clip(atlas, 0, 1) * 255 + 0.5).astype(np.uint8), "RGB").save(os.path.join(TEXTURES, "laundry_paper_Diffuse.png"))
    flat = np.zeros((64, 64, 3), dtype=np.uint8)
    flat[...] = (128, 128, 255)
    Image.fromarray(flat, "RGB").save(os.path.join(TEXTURES, "laundry_paper_nor_dx.png"))
    arm = np.zeros((64, 64, 3), dtype=np.uint8)
    arm[...] = (255, 225, 0)  # AO 1, roughness ~0.88, no metal: paper and card
    Image.fromarray(arm, "RGB").save(os.path.join(TEXTURES, "laundry_paper_arm.png"))
    Image.fromarray((np.clip(atlas, 0, 1) * 255).astype(np.uint8), "RGB").resize((1024, 1024), Image.LANCZOS).save(os.path.join(PREVIEW, "laundry_paper.png"))
    print("Wrote laundry_paper to", TEXTURES)


if __name__ == "__main__":
    main()
