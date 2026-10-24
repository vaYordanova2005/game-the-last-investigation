#!/usr/bin/env python3
"""
Bakes the music room's printed, painted, bound and woven surfaces into texture sets the art
pipeline imports.

    python Tools/make_music_art.py [--velvet]

Sets written as <set>_Diffuse / _nor_dx / _arm into Art/Source/Textures:

    music_velvet     the curtains' emerald velvet, tiling at the drapes' 34cm (as library_velvet).
                     `--velvet` bakes it alone.
    music_damask     the silk damask on the walls over the panelling: a faded old-gold ground with
                     the pattern woven in satin, one repeat every 64cm, half-dropped.
    music_paper      a 2048 atlas (PAPER below, shared with make_music_room.py and AMusicRoomActor):
                     two printed pages of a string quartet, two pages of manuscript in a hand (the
                     second stops partway down), the open orchestral score on the iron stand, the
                     music notebook, the concert programme outside and in, three certificates, three
                     photographs, and the paper cover a score is sold in.
    music_books      the spines of the music on the display shelf: 32 thin paper-bound volumes of
                     32 x 512 along the top of a 1024 sheet, the leaves and eight plain covers under
                     them (BOOKS below). music_books.json beside it gives each spine its cover.
    music_paintings  three pictures on a 1024 sheet: the woman at her cello, a river at dusk, a
                     violin and a score on a table.

Why baked, as for the library: a page of music, a spine and a photograph are pictures, and neither
the photographed library nor a material graph has them.

The only typeface is Cinzel (SIL Open Font License, Content/Fonts/OFL.txt). The composers on the
spines are real and long out of copyright; every other name is invented. The handwriting is a
cursive *hand*, not text (make_cellar_art.py's helpers): the clue writing is the user's to do once
the game is built (the 09-30 decision), so the titles of the manuscript, the programme's items and
the names on the certificates are a hand or bars of type, and nothing in this file says anything.
"""

import json
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from make_cellar_art import (FONT_BLACK, FONT_BOLD, FONT_REGULAR, SS, TEXTURES, age_paper, centred, cursive_word, font,
                             handwriting, hexrgb, noise)
from make_library_art import (crackle, normal_from_height, paint, portrait_head, save_set, soft_shape, tile_noise, to_u8,
                              varnish)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PREVIEW = os.path.join(ROOT, "Saved", "MusicPreview")
SIZE = 2048

# ---------------------------------------------------------------------------------------------
# Shared with Tools/make_music_room.py and MusicRoomActor.cpp.
# ---------------------------------------------------------------------------------------------

# music_paper, in pixels (x, y, w, h).
PAPER = {
    "score_a": (0, 0, 362, 512),
    "score_b": (512, 0, 362, 512),
    "manuscript_a": (1024, 0, 362, 512),
    "manuscript_b": (1536, 0, 362, 512),
    "book_l": (0, 512, 362, 512),
    "book_r": (512, 512, 362, 512),
    "notebook_l": (1024, 512, 362, 512),
    "notebook_r": (1536, 512, 362, 512),
    "programme": (0, 1024, 362, 512),
    "programme_in": (512, 1024, 362, 512),
    "certificate_a": (1024, 1024, 512, 362),
    "certificate_b": (1536, 1024, 362, 512),
    "photo_a": (0, 1536, 256, 320),
    "photo_b": (256, 1536, 256, 320),
    "photo_c": (512, 1536, 320, 256),
    "certificate_c": (1024, 1536, 512, 362),
    "cover": (1536, 1536, 362, 512),
}

# music_books: 32 spines of 32 x 512 along the top, the leaves and eight covers under them.
BOOK_SHEET = 1024
SPINE_W = 32
SPINE_H = 512
LEAVES = (0, 512, 512, 256)
COVERS_X = 512
COVER_W = 64

# music_paintings, in pixels (x, y, w, h).
PAINTINGS = {
    "cellist": (0, 0, 512, 640),
    "river": (512, 0, 512, 384),
    "still_life": (512, 384, 512, 384),
}

# ---------------------------------------------------------------------------------------------
# Small helpers.
# ---------------------------------------------------------------------------------------------


def preview(name, rgb, size=1024):
    img = Image.fromarray(to_u8(rgb), "RGB")
    img.thumbnail((size, size), Image.LANCZOS)
    img.save(os.path.join(PREVIEW, name + ".png"))


def linear_mean(rgb):
    lin = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
    return lin.reshape(-1, 3).mean(axis=0)


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


def smooth_path(points, passes=2):
    for _ in range(passes):
        points = [points[0]] + [((points[i - 1][0] + 2 * points[i][0] + points[i + 1][0]) / 4,
                                 (points[i - 1][1] + 2 * points[i][1] + points[i + 1][1]) / 4)
                                for i in range(1, len(points) - 1)] + [points[-1]]
    return points


# ---------------------------------------------------------------------------------------------
# Music: staves, clefs, notes. Everything in staff space: x along the staff, y down from its top
# line, measured in gaps (the distance between two lines).
# ---------------------------------------------------------------------------------------------

TREBLE = [(0.25, 3.0), (-0.25, 2.65), (-0.05, 2.0), (0.75, 2.1), (1.05, 2.95), (0.55, 3.85), (-0.45, 3.85), (-1.05, 3.0),
          (-0.75, 1.85), (0.35, 0.85), (0.85, -0.35), (0.55, -1.35), (0.1, -0.95), (-0.05, 0.4), (0.2, 3.0), (0.45, 5.0),
          (0.15, 5.6), (-0.4, 5.45), (-0.35, 5.0)]
BASS = [(-0.55, 1.05), (-0.3, 0.3), (0.35, 0.05), (0.95, 0.55), (0.95, 1.55), (0.35, 2.6), (-0.7, 3.5)]


class Hand:
    """How a page of music is drawn: crisp engraving, or a pen with a wander in it."""

    def __init__(self, rng, ink, wobble=0.0, width=1.0):
        self.rng = rng
        self.ink = ink
        self.wobble = wobble
        self.width = width

    def j(self, gap):
        return self.rng.normal(0.0, self.wobble * gap) if self.wobble else 0.0


def draw_staff(draw, x0, x1, y, gap, colour, width, hand=None):
    for k in range(5):
        yy = y + k * gap
        if hand and hand.wobble:
            pts = [(x0 + (x1 - x0) * t, yy + hand.j(gap) * 0.25) for t in np.linspace(0, 1, 9)]
            draw.line(smooth_path(pts), fill=colour, width=width)
        else:
            draw.line([(x0, yy), (x1, yy)], fill=colour, width=width)


def draw_glyph(draw, path, x, y, gap, colour, width, hand=None, scale=1.0):
    pts = [(x + px * gap * scale + (hand.j(gap) * 0.1 if hand else 0), y + py * gap * scale) for px, py in path]
    draw.line(smooth_path(pts, 3), fill=colour, width=width, joint="curve")


def draw_clef(draw, kind, x, y, gap, colour, width, hand=None):
    if kind == "treble":
        draw_glyph(draw, TREBLE, x, y, gap, colour, int(width * 1.3), hand)
        draw.ellipse([x - 0.65 * gap, y + 5.0 * gap, x - 0.05 * gap, y + 5.6 * gap], fill=colour)
    elif kind == "bass":
        draw_glyph(draw, BASS, x, y, gap, colour, int(width * 1.6), hand)
        draw.ellipse([x - 0.8 * gap, y + 0.75 * gap, x - 0.25 * gap, y + 1.3 * gap], fill=colour)
        for dy in (0.55, 1.45):
            draw.ellipse([x + 1.3 * gap, y + (dy - 0.2) * gap, x + 1.7 * gap, y + (dy + 0.2) * gap], fill=colour)
    else:
        # The alto clef: two bars and the two brackets that meet on the middle line.
        draw.rectangle([x - 0.9 * gap, y - 0.05 * gap, x - 0.45 * gap, y + 4.05 * gap], fill=colour)
        draw.line([(x - 0.2 * gap, y), (x - 0.2 * gap, y + 4 * gap)], fill=colour, width=width)
        for top in (True, False):
            y0, y1 = (y - 0.1 * gap, y + 2.0 * gap) if top else (y + 2.0 * gap, y + 4.1 * gap)
            draw.arc([x - 0.4 * gap, y0, x + 1.2 * gap, y1], 270 if top else 0, 90 if top else 270, fill=colour, width=int(width * 1.4))


def draw_key(draw, x, y, gap, colour, width, count):
    """A key signature of sharps: two uprights and two thick slanted bars each."""
    places = [0.0, 1.5, -0.5, 1.0, 2.5]
    for k in range(count):
        cx = x + k * 0.9 * gap
        cy = y + places[k] * gap
        for dx in (-0.18, 0.18):
            draw.line([(cx + dx * gap, cy - 1.0 * gap), (cx + dx * gap, cy + 1.0 * gap)], fill=colour, width=width)
        for dy in (-0.35, 0.35):
            draw.line([(cx - 0.4 * gap, cy + (dy + 0.12) * gap), (cx + 0.4 * gap, cy + (dy - 0.12) * gap)], fill=colour, width=int(width * 2.0))


def draw_time(draw, x, y, gap, colour, top, bottom):
    f = font(FONT_BLACK, gap * 1.7 / SS)
    draw.text((x, y - 0.35 * gap), top, font=f, fill=colour)
    draw.text((x, y + 1.65 * gap), bottom, font=f, fill=colour)


def draw_note(draw, x, y, gap, step, value, colour, width, hand=None, stem_up=None):
    """One note. step: half-gaps down from the top line (0 = top line, 8 = bottom line). value: 1
    whole, 2 half, 4 quarter, 8 eighth (the eighth's flag is drawn here; beams by the caller).
    Returns the end of its stem."""
    cy = y + step * gap * 0.5 + (hand.j(gap) if hand else 0.0)
    cx = x + (hand.j(gap) if hand else 0.0)
    rx, ry = 0.62 * gap, 0.44 * gap
    # Ledger lines above and below the staff.
    for ledger in range(-2, step - 1, -2):
        draw.line([(x - rx * 1.6, y + ledger * gap * 0.5), (x + rx * 1.6, y + ledger * gap * 0.5)], fill=colour, width=width)
    for ledger in range(10, step + 1, 2):
        draw.line([(x - rx * 1.6, y + ledger * gap * 0.5), (x + rx * 1.6, y + ledger * gap * 0.5)], fill=colour, width=width)
    pts = [(cx + math.cos(a) * rx * math.cos(-0.35) - math.sin(a) * ry * math.sin(-0.35),
            cy + math.cos(a) * rx * math.sin(-0.35) + math.sin(a) * ry * math.cos(-0.35)) for a in np.linspace(0, 2 * math.pi, 16, endpoint=False)]
    if value >= 4:
        draw.polygon(pts, fill=colour)
    else:
        draw.polygon(pts, outline=colour, width=int(width * 1.4))
    if value == 1:
        return None
    up = (step >= 4) if stem_up is None else stem_up
    sx = cx + (rx * 0.92 if up else -rx * 0.92)
    sy0 = cy - ry * 0.3 if up else cy + ry * 0.3
    sy1 = sy0 - 3.4 * gap if up else sy0 + 3.4 * gap
    draw.line([(sx, sy0), (sx, sy1)], fill=colour, width=width)
    if value == 8 and stem_up is None:
        d = 1 if up else -1
        draw.line([(sx, sy1), (sx + 0.7 * gap, sy1 + d * 1.2 * gap), (sx + 0.55 * gap, sy1 + d * 2.0 * gap)], fill=colour, width=int(width * 1.5))
    return (sx, sy1)


def draw_rest(draw, x, y, gap, value, colour, width):
    if value == 4:
        pts = [(x - 0.2 * gap, y + 1.0 * gap), (x + 0.35 * gap, y + 1.7 * gap), (x - 0.15 * gap, y + 2.3 * gap), (x + 0.35 * gap, y + 3.0 * gap),
               (x - 0.25 * gap, y + 2.85 * gap), (x + 0.05 * gap, y + 3.6 * gap)]
        draw.line(pts, fill=colour, width=int(width * 1.8), joint="curve")
    elif value == 2:
        draw.rectangle([x - 0.5 * gap, y + 1.55 * gap, x + 0.5 * gap, y + 2.0 * gap], fill=colour)
    else:
        draw.rectangle([x - 0.5 * gap, y + 1.0 * gap, x + 0.5 * gap, y + 1.45 * gap], fill=colour)


def draw_bars(draw, x0, x1, y, gap, rng, colour, width, bars, hand=None, density=1.0, stop_at=None, slurs=True):
    """Fills a staff from x0 to x1 with `bars` bars of a melody that walks by step and leap.
    stop_at: a fraction of the way along after which nothing is written (the unfinished page)."""
    bar_w = (x1 - x0) / bars
    step = int(rng.integers(2, 7))
    for b in range(bars):
        bx = x0 + b * bar_w
        if stop_at is not None and (b + 0.0) / bars >= stop_at:
            break
        beats = 0.0
        slots = []
        while beats < 4.0 - 1e-6:
            choice = rng.random()
            if density < 1.0 and rng.random() > density:
                value, length = (2, 2.0) if beats <= 2.0 else (4, 1.0)
                slots.append(("rest", value, beats, length))
                beats += length
                continue
            if choice < 0.18 and beats <= 2.0:
                value, length = 2, 2.0
            elif choice < 0.6 or beats > 3.0:
                value, length = 4, 1.0
            else:
                value, length = 8, 1.0
            slots.append(("note", value, beats, length))
            beats += length
        for kind, value, start, length in slots:
            nx = bx + bar_w * (0.12 + 0.8 * start / 4.0)
            if kind == "rest":
                draw_rest(draw, nx, y, gap, value, colour, width)
                continue
            if value == 8:
                # A beamed pair in the beat.
                a = step
                step = int(np.clip(step + rng.integers(-2, 3), -3, 11))
                b2 = step
                up = (a + b2) * 0.5 >= 4
                e1 = draw_note(draw, nx, y, gap, a, 4, colour, width, hand, stem_up=up)
                e2 = draw_note(draw, nx + bar_w * 0.1, y, gap, b2, 4, colour, width, hand, stem_up=up)
                if e1 and e2:
                    draw.line([e1, e2], fill=colour, width=int(width * 2.6))
            else:
                draw_note(draw, nx, y, gap, step, value, colour, width, hand)
            leap = rng.random() < 0.2
            step = int(np.clip(step + (rng.integers(-4, 5) if leap else rng.integers(-2, 3)), -3, 11))
        if slurs and rng.random() < 0.35:
            sx = bx + bar_w * 0.1
            draw.arc([sx, y - 2.2 * gap, sx + bar_w * 0.7, y - 0.6 * gap], 200, 340, fill=colour, width=width)
        draw.line([(bx + bar_w, y), (bx + bar_w, y + 4 * gap)], fill=colour, width=width)


def dynamic(draw, x, y, gap, mark, colour):
    draw.text((x, y), mark, font=font(FONT_BOLD, gap * 1.3 / SS), fill=colour)


# ---------------------------------------------------------------------------------------------
# The paper.
# ---------------------------------------------------------------------------------------------

def make_score_page(name, seed, first):
    """A printed page of a string quartet: four staves to a system, braced, three systems."""
    img, draw, w, h = sheet(name, "#e6dcc0")
    rng = np.random.default_rng(seed)
    ink = hexrgb("#1c1814")
    x0, x1 = 46 * SS, w - 22 * SS
    gap = 4.4 * SS
    y = 40 * SS
    if first:
        centred(draw, "QUARTET", w / 2, 16 * SS, font(FONT_BLACK, 15), ink, spacing=3 * SS)
        centred(draw, "IN G MAJOR", w / 2, 38 * SS, font(FONT_REGULAR, 9), ink, spacing=2 * SS)
        draw.text((w - 90 * SS, 50 * SS), "W. A. MOZART", font=font(FONT_REGULAR, 7), fill=ink)
        y = 74 * SS
    else:
        draw.text((w / 2 - 6 * SS, 14 * SS), str(int(rng.integers(4, 20))), font=font(FONT_REGULAR, 8), fill=ink)
        y = 34 * SS
    clefs = ["treble", "treble", "alto", "bass"]
    labels = ["VL. I", "VL. II", "VLA.", "VC."]
    system_h = 4 * 4 * gap + 3 * 3.0 * gap
    while y + system_h < h - 18 * SS:
        top = y
        for k in range(4):
            sy = top + k * (4 * gap + 3.0 * gap)
            draw_staff(draw, x0, x1, sy, gap, ink, SS)
            draw_clef(draw, clefs[k], x0 + 7 * SS, sy, gap, ink, SS)
            draw_key(draw, x0 + 16 * SS, sy + (0.0 if clefs[k] != "bass" else 1.0 * gap) + (0.5 * gap if clefs[k] == "alto" else 0.0), gap, ink, SS, 1)
            if first and top == 74 * SS:
                draw_time(draw, x0 + 22 * SS, sy, gap, ink, "3", "4")
                draw.text((8 * SS, sy + 1.2 * gap), labels[k], font=font(FONT_REGULAR, 6), fill=ink)
            draw_bars(draw, x0 + 32 * SS, x1, sy, gap, rng, ink, SS, 4, density=0.75 if k else 0.95)
            if rng.random() < 0.3:
                dynamic(draw, x0 + rng.uniform(40, 200) * SS, sy + 4.6 * gap, gap, ["p", "f", "mf", "pp"][int(rng.integers(0, 4))], ink)
        # The system's bracket down the left and the line through all four staves.
        bottom = top + 3 * (4 * gap + 3.0 * gap) + 4 * gap
        draw.line([(x0, top), (x0, bottom)], fill=ink, width=SS)
        draw.rectangle([x0 - 5 * SS, top - SS, x0 - 3 * SS, bottom + SS], fill=ink)
        y = bottom + 5.5 * gap
    return finish_sheet(img, name, seed + 1, foxing=0.7, damp=0.3, edge=0.6, dust=0.5)


def make_manuscript(name, seed, unfinished):
    """Manuscript paper with its printed staves, written on in ink. The second page stops partway
    down: two bars, a bar half written, a note crossed out, then nothing on the staves below."""
    img, draw, w, h = sheet(name, "#e9e1c8")
    rng = np.random.default_rng(seed)
    ruled = hexrgb("#7d7468")
    ink = hexrgb("#1a1c2a")
    pencil = hexrgb("#6a6a6c")
    hand = Hand(rng, ink, wobble=0.12)
    x0, x1 = 24 * SS, w - 18 * SS
    gap = 4.0 * SS
    staves = 12
    y_top = 46 * SS
    pitch = (h - y_top - 20 * SS) / staves
    # A title and a tempo in the hand at the head of the first page; the second has none.
    if not unfinished:
        cursive_word(draw, w * 0.3, 26 * SS, w * 0.4, 6 * SS, rng, ink, SS + 1)
        cursive_word(draw, x0, 40 * SS, 60 * SS, 3.5 * SS, rng, ink, SS)
    else:
        cursive_word(draw, x0 + 8 * SS, 30 * SS, 70 * SS, 3.8 * SS, rng, pencil, SS)
    written = staves if not unfinished else 5
    for k in range(staves):
        sy = y_top + k * pitch
        draw_staff(draw, x0, x1, sy, gap, ruled, max(1, SS - 1))
        if k >= written:
            continue
        clef = "treble" if k % 2 == 0 else "bass"
        draw_clef(draw, clef, x0 + 6 * SS, sy, gap, ink, SS, hand)
        stop = None
        if unfinished and k == written - 1:
            stop = 0.55
        draw_bars(draw, x0 + 22 * SS, x1, sy, gap, rng, ink, SS + 1, 4, hand=hand, stop_at=stop)
        if rng.random() < 0.25:
            # A bar scratched out and written again over it.
            bx = x0 + 22 * SS + (x1 - x0 - 22 * SS) * rng.integers(0, 3) / 4.0
            for d in range(4):
                draw.line([(bx + d * 6 * SS, sy - gap), (bx + 30 * SS + d * 6 * SS, sy + 5 * gap)], fill=ink, width=SS)
    if unfinished:
        # The last note, crossed through, and a question mark in pencil over the staff after it.
        sy = y_top + (written - 1) * pitch
        nx = x0 + 22 * SS + (x1 - x0 - 22 * SS) * 0.62
        draw_note(draw, nx, sy, gap, 3, 4, ink, SS + 1, hand)
        draw.line([(nx - 6 * SS, sy + 5 * gap), (nx + 7 * SS, sy - gap)], fill=ink, width=SS + 1)
        cursive_word(draw, nx + 24 * SS, sy - 1.5 * gap, 40 * SS, 3.0 * SS, rng, pencil, SS)
    else:
        # Fingerings and bowings in pencil over a few bars.
        for _ in range(14):
            sx = rng.uniform(x0 + 30 * SS, x1 - 10 * SS)
            sy = y_top + int(rng.integers(0, staves)) * pitch - 2.2 * gap
            draw.text((sx, sy), str(int(rng.integers(1, 5))), font=font(FONT_REGULAR, 5), fill=pencil)
    return finish_sheet(img, name, seed + 1, foxing=0.9, damp=0.5, edge=0.8, dust=0.6)


def make_orchestral_page(right):
    """The open score on the iron stand: an orchestral page, ten staves at a time and small."""
    name = "book_r" if right else "book_l"
    img, draw, w, h = sheet(name, "#e2d6b6")
    rng = np.random.default_rng(530 if right else 531)
    ink = hexrgb("#1e1a15")
    x0, x1 = 30 * SS, w - 26 * SS
    gap = 2.2 * SS
    draw.text((w / 2 - 6 * SS, 10 * SS), str(37 + int(right)), font=font(FONT_REGULAR, 7), fill=ink)
    y = 28 * SS
    clefs = ["treble"] * 6 + ["alto", "bass", "bass", "treble"]
    while y + 10 * 7.5 * gap < h - 10 * SS:
        top = y
        for k in range(10):
            sy = top + k * 7.0 * gap
            draw_staff(draw, x0, x1, sy, gap, ink, max(1, SS - 1))
            draw_clef(draw, clefs[k], x0 + 4 * SS, sy, gap, ink, max(1, SS - 1))
            draw_bars(draw, x0 + 12 * SS, x1, sy, gap, rng, ink, max(1, SS - 1), 3, density=0.6, slurs=False)
        draw.line([(x0, top), (x0, top + 9 * 7.0 * gap + 4 * gap)], fill=ink, width=SS)
        y = top + 10 * 7.0 * gap + 4 * gap
    rgb = finish_sheet(img, name, 532 if right else 533, foxing=1.0, damp=0.3, edge=0.6, dust=0.5)
    return gutter(rgb, right)


def make_music_notebook(right):
    """Her music notebook: half the page staves with exercises in pencil, half lines of the hand,
    things circled and arrows to them; the right page stops partway."""
    name = "notebook_r" if right else "notebook_l"
    img, draw, w, h = sheet(name, "#e8dfc6")
    rng = np.random.default_rng(540 if right else 541)
    ruled = hexrgb("#8d8478")
    ink = hexrgb("#20243a")
    pencil = hexrgb("#5e5e62")
    hand = Hand(rng, pencil, wobble=0.15)
    x0, x1 = 22 * SS, w - 18 * SS
    gap = 3.4 * SS
    y = 30 * SS
    for k in range(4 if not right else 3):
        draw_staff(draw, x0, x1, y, gap, ruled, max(1, SS - 1))
        draw_clef(draw, "treble" if (k + right) % 2 else "bass", x0 + 5 * SS, y, gap, pencil, SS, hand)
        draw_bars(draw, x0 + 18 * SS, x1, y, gap, rng, pencil, SS, 3, hand=hand, density=0.85, stop_at=0.7 if (right and k == 2) else None)
        y += 9 * gap
    lines = 12 if not right else 5
    handwriting(draw, x0, y + 14 * SS, x1, 17 * SS, lines, rng, ink, SS + 1, 4.8 * SS)
    for _ in range(3):
        cx = rng.uniform(x0 + 30 * SS, x1 - 40 * SS)
        cy = 30 * SS + int(rng.integers(0, 3)) * 9 * gap + 2 * gap
        draw.ellipse([cx - 16 * SS, cy - 9 * SS, cx + 16 * SS, cy + 9 * SS], outline=ink, width=SS)
        draw.line([(cx + 16 * SS, cy + 6 * SS), (cx + 40 * SS, y + 10 * SS)], fill=ink, width=SS)
    rgb = finish_sheet(img, name, 542 if right else 543, foxing=0.4, damp=0.25, edge=0.5, dust=0.4)
    return gutter(rgb, right)


def ornament_border(draw, w, h, m, colour, width):
    draw.rectangle([m, m, w - m, h - m], outline=colour, width=width * 2)
    draw.rectangle([m + 6 * SS, m + 6 * SS, w - m - 6 * SS, h - m - 6 * SS], outline=colour, width=width)
    for cx, cy in ((m, m), (w - m, m), (m, h - m), (w - m, h - m)):
        r = 9 * SS
        draw.ellipse([cx - r, cy - r, cx + r, cy + r], outline=colour, width=width)
        draw.ellipse([cx - r * 0.4, cy - r * 0.4, cx + r * 0.4, cy + r * 0.4], fill=colour)


def draw_lyre(draw, cx, cy, s, colour, width):
    """A lyre: two horns curving up and out, a yoke, the strings, a little base."""
    for side in (-1, 1):
        pts = [(cx + side * 6 * s, cy + 30 * s), (cx + side * 22 * s, cy + 18 * s), (cx + side * 24 * s, cy - 2 * s), (cx + side * 14 * s, cy - 22 * s),
               (cx + side * 18 * s, cy - 34 * s), (cx + side * 26 * s, cy - 36 * s)]
        draw.line(smooth_path(pts, 3), fill=colour, width=width * 2, joint="curve")
    draw.line([(cx - 16 * s, cy - 24 * s), (cx + 16 * s, cy - 24 * s)], fill=colour, width=width * 2)
    for k in range(5):
        x = cx - 8 * s + k * 4 * s
        draw.line([(x, cy - 24 * s), (x, cy + 26 * s)], fill=colour, width=max(1, width - 1))
    draw.rectangle([cx - 10 * s, cy + 26 * s, cx + 10 * s, cy + 32 * s], fill=colour)


def make_programme(inside):
    """The concert programme, a folded card: outside a border, a lyre and the occasion; inside the
    order of the pieces as lines of type with the players' names in the hand beside them."""
    name = "programme_in" if inside else "programme"
    img, draw, w, h = sheet(name, "#e4d9bc" if not inside else "#e8ddc2")
    rng = np.random.default_rng(560 + inside)
    ink, accent = hexrgb("#221b15"), hexrgb("#4a5a3a")
    ornament_border(draw, w, h, 14 * SS, accent, SS)
    if not inside:
        centred(draw, "A RECITAL", w / 2, 70 * SS, font(FONT_BLACK, 22), ink, spacing=3 * SS)
        centred(draw, "OF MUSIC", w / 2, 104 * SS, font(FONT_BOLD, 14), ink, spacing=4 * SS)
        draw.line([(w * 0.3, 136 * SS), (w * 0.7, 136 * SS)], fill=accent, width=SS)
        draw_lyre(draw, w / 2, 230 * SS, 1.6 * SS, accent, SS)
        centred(draw, "STRINGS", w / 2, 320 * SS, font(FONT_BOLD, 12), ink, spacing=5 * SS)
        # The date and the place as rules of small type: the clue writing is the user's.
        for k, lw in enumerate((0.5, 0.36)):
            y = (372 + k * 20) * SS
            draw.line([(w * (0.5 - lw / 2), y), (w * (0.5 + lw / 2), y)], fill=ink, width=int(2.4 * SS))
        centred(draw, "PROGRAMME", w / 2, h - 70 * SS, font(FONT_REGULAR, 10), ink, spacing=3 * SS)
    else:
        centred(draw, "PROGRAMME", w / 2, 34 * SS, font(FONT_BOLD, 13), ink, spacing=3 * SS)
        y = 80 * SS
        for item in range(7):
            # A composer in capitals and the piece as a rule, the players in a hand under them.
            draw.text((34 * SS, y), ["BACH", "MOZART", "HAYDN", "SCHUBERT", "ELGAR", "BRAHMS", "DVORAK"][item], font=font(FONT_BOLD, 8), fill=ink)
            draw.line([(110 * SS, y + 7 * SS), (110 * SS + rng.uniform(80, 180) * SS, y + 7 * SS)], fill=ink, width=int(2.2 * SS))
            cursive_word(draw, 60 * SS, y + 26 * SS, rng.uniform(80, 170) * SS, 3.4 * SS, rng, hexrgb("#3a3a4a"), SS)
            y += 52 * SS
        draw_lyre(draw, w / 2, h - 50 * SS, 0.7 * SS, accent, SS)
    return finish_sheet(img, name, 562 + inside, foxing=0.8, damp=0.4, edge=0.7, dust=0.9)


def make_certificate(name, seed, heading, sub, landscape):
    """An award, framed on the east wall: a border, a heading in capitals, her name in the hand on a
    rule, a red seal with its ribbons, two signatures."""
    img, draw, w, h = sheet(name, "#ebe0c2")
    rng = np.random.default_rng(seed)
    ink, accent, red = hexrgb("#231c14"), hexrgb("#6e5a26"), hexrgb("#7a1e18")
    ornament_border(draw, w, h, 12 * SS, accent, SS)
    top = 46 * SS if landscape else 70 * SS
    centred(draw, heading, w / 2, top, font(FONT_BLACK, 20 if landscape else 18), ink, spacing=3 * SS)
    centred(draw, sub, w / 2, top + 36 * SS, font(FONT_BOLD, 10), accent, spacing=4 * SS)
    centred(draw, "AWARDED TO", w / 2, top + 66 * SS, font(FONT_REGULAR, 7), ink, spacing=2 * SS)
    y = top + 112 * SS
    cursive_word(draw, w * 0.3, y - 4 * SS, w * 0.4, 7 * SS, rng, hexrgb("#1c2038"), SS + 1)
    draw.line([(w * 0.22, y + 6 * SS), (w * 0.78, y + 6 * SS)], fill=ink, width=SS)
    for k in range(2):
        yy = y + (30 + k * 16) * SS
        draw.line([(w * 0.25, yy), (w * 0.75, yy)], fill=hexrgb("#5a5048"), width=int(1.8 * SS))
    # Seal: a red disc with a scalloped edge and two ribbon tails.
    sx, sy = (w * 0.5, h - 60 * SS) if not landscape else (w * 0.5, h - 54 * SS)
    for side in (-1, 1):
        draw.polygon([(sx + side * 6 * SS, sy), (sx + side * 22 * SS, sy + 46 * SS), (sx + side * 12 * SS, sy + 40 * SS), (sx + side * 2 * SS, sy + 48 * SS)], fill=red)
    pts = [(sx + math.cos(a) * (24 + 2.5 * (k % 2)) * SS, sy + math.sin(a) * (24 + 2.5 * (k % 2)) * SS)
           for k, a in enumerate(np.linspace(0, 2 * math.pi, 28, endpoint=False))]
    draw.polygon(pts, fill=red)
    draw.ellipse([sx - 15 * SS, sy - 15 * SS, sx + 15 * SS, sy + 15 * SS], outline=hexrgb("#a8473a"), width=SS)
    for side in (-1, 1):
        x = w * 0.5 + side * w * 0.3
        cursive_word(draw, x - 40 * SS, sy + 4 * SS, 80 * SS, 4 * SS, rng, ink, SS)
        draw.line([(x - 46 * SS, sy + 12 * SS), (x + 46 * SS, sy + 12 * SS)], fill=ink, width=SS)
    return finish_sheet(img, name, seed + 1, foxing=0.6, damp=0.3, edge=0.5, dust=0.3)


def make_cover():
    """The paper cover a quartet's parts are sold in: an ochre card, a printed panel and a title."""
    img, draw, w, h = sheet("cover", "#b9934a")
    ink = hexrgb("#2a2014")
    draw.rectangle([22 * SS, 22 * SS, w - 22 * SS, h - 22 * SS], outline=ink, width=SS * 2)
    draw.rectangle([44 * SS, 90 * SS, w - 44 * SS, 260 * SS], fill=hexrgb("#d9c48e"), outline=ink, width=SS)
    centred(draw, "STRING", w / 2, 116 * SS, font(FONT_BLACK, 20), ink, spacing=3 * SS)
    centred(draw, "QUARTETS", w / 2, 150 * SS, font(FONT_BLACK, 20), ink, spacing=3 * SS)
    centred(draw, "HAYDN", w / 2, 200 * SS, font(FONT_BOLD, 13), ink, spacing=4 * SS)
    centred(draw, "VOL. II", w / 2, 226 * SS, font(FONT_REGULAR, 9), ink, spacing=2 * SS)
    centred(draw, "EDITION LORELEI", w / 2, h - 60 * SS, font(FONT_REGULAR, 8), ink, spacing=2 * SS)
    return finish_sheet(img, "cover", 571, foxing=0.5, damp=0.4, edge=0.9, dust=0.7)


# ---------------------------------------------------------------------------------------------
# Photographs: colour prints from a chemist's, gone the way they go — the blues first, a cast to
# magenta, the blacks lifted, a white border, a crease across one.
# ---------------------------------------------------------------------------------------------

def photo_finish(rgb, seed, crease=False):
    h, w, _ = rgb.shape
    rng = np.random.default_rng(seed)
    out = rgb * np.array([1.04, 0.92, 0.80]) + np.array([0.06, 0.03, 0.05])
    out = out * 0.82 + 0.08
    out *= (0.94 + 0.12 * noise(w, h, 90, seed, octaves=2))[..., None]
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    d = np.sqrt(((xx - w / 2) / (w / 2)) ** 2 + ((yy - h / 2) / (h / 2)) ** 2)
    out *= (1.0 - np.clip(d - 0.7, 0, 1) * 0.5)[..., None]
    if crease:
        y0 = rng.uniform(0.3, 0.7) * h
        line = np.exp(-((yy - y0 - (xx - w / 2) * 0.12) / 1.6) ** 2)
        out = out * (1 - line[..., None] * 0.35) + 0.8 * line[..., None] * 0.35
    border = 10
    frame = np.ones((h + border * 2, w + border * 2, 3), dtype=np.float32) * np.array([0.86, 0.84, 0.78])
    frame[border:border + h, border:border + w] = np.clip(out, 0, 1)
    img = Image.fromarray(to_u8(frame), "RGB").resize((w, h), Image.LANCZOS)
    return age_paper(np.asarray(img, dtype=np.float32) / 255.0, seed + 1, foxing=0.2, damp=0.1, edge=0.3, dust=0.4)


def violin_outline(cx, top, length, upper=0.47, lower=0.58, waist=0.33):
    """The outline of a fiddle's body as polygon points, from its length: two bouts and the waist."""
    pts = []
    for k in range(64):
        t = k / 63
        z = top + t * length
        # Width as a sum of the two bouts and the waist between them.
        bout_u = upper * math.exp(-((t - 0.24) / 0.2) ** 2)
        bout_l = lower * math.exp(-((t - 0.73) / 0.24) ** 2)
        width = max(bout_u, bout_l, waist * math.exp(-((t - 0.48) / 0.35) ** 2)) * length * 0.5
        width *= math.sin(math.pi * min(1.0, max(0.0, t)) ) ** 0.35
        pts.append((cx + width, z))
    return pts + [(2 * cx - x, z) for x, z in reversed(pts)]


def draw_cello(rgb, w, h, cx, top, length, seed, angle=0.0):
    """A cello in paint: the varnished body with its two f-holes, the neck and the scroll above."""
    outline = violin_outline(cx, top, length)
    ca, sa = math.cos(angle), math.sin(angle)
    rot = lambda p: (cx + (p[0] - cx) * ca - (p[1] - top) * sa, top + (p[0] - cx) * sa + (p[1] - top) * ca)
    body = soft_shape(w, h, lambda d: d.polygon([rot(p) for p in outline], fill=255), 3)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    light = np.clip(1.25 - ((xx - cx + length * 0.12) / (length * 0.5)) ** 2 - ((yy - top - length * 0.35) / (length * 0.7)) ** 2, 0.25, 1.1)
    rgb = paint(rgb, body, (0.36, 0.12, 0.04), light * (0.85 + 0.25 * noise(w, h, 30, seed)))
    for side in (-1, 1):
        f = soft_shape(w, h, lambda d, side=side: d.line([rot((cx + side * length * 0.09, top + length * 0.38)), rot((cx + side * length * 0.11, top + length * 0.5)),
                                                          rot((cx + side * length * 0.08, top + length * 0.62))], fill=255, width=max(2, int(length * 0.015))), 1)
        rgb = paint(rgb, f, (0.03, 0.015, 0.01), None, 0.9)
    neck = soft_shape(w, h, lambda d: d.line([rot((cx, top + length * 0.55)), rot((cx, top - length * 0.42))], fill=255, width=max(3, int(length * 0.05))), 2)
    rgb = paint(rgb, neck, (0.05, 0.035, 0.03), None, 0.95)
    scroll = soft_shape(w, h, lambda d: d.ellipse([*rot((cx - length * 0.04, top - length * 0.5)), *rot((cx + length * 0.05, top - length * 0.4))], fill=255), 2)
    rgb = paint(rgb, scroll, (0.30, 0.11, 0.04), None, 0.95)
    for k in (-1, 1):
        s = soft_shape(w, h, lambda d, k=k: d.line([rot((cx + k * length * 0.012, top - length * 0.4)), rot((cx + k * length * 0.03, top + length * 0.85))], fill=255, width=1), 0.6)
        rgb = paint(rgb, s, (0.7, 0.62, 0.48), None, 0.5)
    return rgb


def make_photo_a():
    """The woman at her cello in this room, the green curtains behind her: a print from the album."""
    w, h = 256, 320
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    rgb = np.ones((h, w, 3), dtype=np.float32) * np.array([0.30, 0.24, 0.16])
    curtain = soft_shape(w, h, lambda d: d.rectangle([0, 0, 70, h], fill=255), 6)
    rgb = paint(rgb, curtain, (0.06, 0.24, 0.14), 0.6 + 0.4 * np.sin(xx / 6.0) ** 2)
    window = soft_shape(w, h, lambda d: d.rectangle([80, 10, 190, 170], fill=255), 4)
    rgb = paint(rgb, window, (0.75, 0.78, 0.74), None, 0.85)
    body = soft_shape(w, h, lambda d: d.polygon([(70, h), (86, 190), (120, 150), (160, 150), (190, 196), (200, h)], fill=255), 4)
    rgb = paint(rgb, body, (0.12, 0.10, 0.12), 0.7 + 0.4 * np.clip(1 - ((xx - 120) / 80) ** 2, 0, 1))
    rgb = portrait_head(rgb, w, h, 138, 116, 20, 26, (0.84, 0.64, 0.52), (0.16, 0.09, 0.05),
                        lambda d: (d.ellipse([114, 82, 164, 134], fill=255), d.ellipse([150, 120, 170, 150], fill=255)), 581)
    rgb = draw_cello(rgb, w, h, 118, 196, 150, 582, angle=0.12)
    arm = soft_shape(w, h, lambda d: d.line([(160, 170), (180, 230), (150, 250)], fill=255, width=12), 3)
    rgb = paint(rgb, arm, (0.80, 0.62, 0.50), None, 0.9)
    return photo_finish(rgb, 583)


def make_photo_b():
    """The girl, younger, with the violin under her chin and her mother bending to fix her bow arm."""
    w, h = 256, 320
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    rgb = np.ones((h, w, 3), dtype=np.float32) * np.array([0.52, 0.46, 0.36]) * (0.8 + 0.3 * (yy / h))[..., None]
    mother = soft_shape(w, h, lambda d: d.polygon([(130, h), (140, 150), (175, 112), (215, 118), (240, 170), (250, h)], fill=255), 4)
    rgb = paint(rgb, mother, (0.20, 0.30, 0.42), 0.7 + 0.3 * noise(w, h, 10, 590))
    rgb = portrait_head(rgb, w, h, 196, 86, 18, 24, (0.84, 0.64, 0.52), (0.16, 0.09, 0.05),
                        lambda d: (d.ellipse([174, 56, 220, 104], fill=255),), 591)
    girl = soft_shape(w, h, lambda d: d.polygon([(40, h), (50, 200), (78, 166), (118, 168), (138, 206), (140, h)], fill=255), 4)
    rgb = paint(rgb, girl, (0.82, 0.48, 0.56), 0.75 + 0.3 * noise(w, h, 12, 592))
    rgb = portrait_head(rgb, w, h, 96, 136, 18, 23, (0.88, 0.70, 0.58), (0.52, 0.36, 0.18),
                        lambda d: (d.ellipse([74, 108, 120, 156], fill=255), d.polygon([(76, 140), (66, 200), (86, 202), (90, 150)], fill=255)), 593)
    fiddle = soft_shape(w, h, lambda d: d.polygon([(70, 172), (128, 150), (136, 166), (78, 190)], fill=255), 2)
    rgb = paint(rgb, fiddle, (0.40, 0.15, 0.05), None, 0.95)
    bow = soft_shape(w, h, lambda d: d.line([(110, 210), (230, 130)], fill=255, width=2), 0.8)
    rgb = paint(rgb, bow, (0.12, 0.07, 0.04), None, 0.9)
    return photo_finish(rgb, 594, crease=True)


def make_photo_c():
    """The two of them on a stage after a concert, taking a bow, the lights in the dark behind."""
    w, h = 320, 256
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    rgb = np.ones((h, w, 3), dtype=np.float32) * np.array([0.05, 0.04, 0.05])
    for lx in (40, 120, 200, 280):
        glow = np.exp(-(((xx - lx) / 18) ** 2 + ((yy - 20) / 10) ** 2))
        rgb += glow[..., None] * np.array([0.9, 0.75, 0.5])
    stage = soft_shape(w, h, lambda d: d.rectangle([0, 200, w, h], fill=255), 3)
    rgb = paint(rgb, stage, (0.30, 0.18, 0.10), None, 1.0)
    for x0, colour, hair, girl in ((70, (0.12, 0.12, 0.14), (0.16, 0.09, 0.05), False), (190, (0.86, 0.84, 0.80), (0.52, 0.36, 0.18), True)):
        s = 0.8 if girl else 1.0
        body = soft_shape(w, h, lambda d, x0=x0, s=s: d.polygon([(x0 - 26 * s, 206), (x0 - 18 * s, 120), (x0 + 10 * s, 96), (x0 + 34 * s, 104), (x0 + 30 * s, 206)], fill=255), 3)
        rgb = paint(rgb, body, colour, 0.7 + 0.4 * np.clip(1 - ((xx - x0) / 40) ** 2, 0, 1))
        rgb = portrait_head(rgb, w, h, x0 + 22 * s, 100 - 8 * s, 13 * s, 15 * s, (0.86, 0.66, 0.54), hair,
                            lambda d, x0=x0, s=s: (d.ellipse([x0 + 8 * s, 80 - 8 * s, x0 + 38 * s, 112 - 8 * s], fill=255),), 600 + x0)
    return photo_finish(rgb, 601)


def build_paper():
    out = np.zeros((SIZE, SIZE, 3), dtype=np.float32)
    out[...] = np.array([0.80, 0.75, 0.62])
    makers = {
        "score_a": lambda: make_score_page("score_a", 501, True),
        "score_b": lambda: make_score_page("score_b", 503, False),
        "manuscript_a": lambda: make_manuscript("manuscript_a", 511, False),
        "manuscript_b": lambda: make_manuscript("manuscript_b", 513, True),
        "book_l": lambda: make_orchestral_page(False),
        "book_r": lambda: make_orchestral_page(True),
        "notebook_l": lambda: make_music_notebook(False),
        "notebook_r": lambda: make_music_notebook(True),
        "programme": lambda: make_programme(False),
        "programme_in": lambda: make_programme(True),
        "certificate_a": lambda: make_certificate("certificate_a", 551, "CERTIFICATE", "OF MERIT", True),
        "certificate_b": lambda: make_certificate("certificate_b", 553, "FIRST PRIZE", "JUNIOR STRINGS", False),
        "certificate_c": lambda: make_certificate("certificate_c", 555, "DIPLOMA", "VIOLONCELLO", True),
        "photo_a": make_photo_a,
        "photo_b": make_photo_b,
        "photo_c": make_photo_c,
        "cover": make_cover,
    }
    for name, make in makers.items():
        x, y, w, h = PAPER[name]
        out[y:y + h, x:x + w] = make()
    # Paper is matte; the photographs have a little of their gloss left.
    arm = np.zeros((SIZE, SIZE, 3), dtype=np.float32)
    arm[..., 0] = 1.0
    arm[..., 1] = 0.86
    for name in ("photo_a", "photo_b", "photo_c"):
        x, y, w, h = PAPER[name]
        arm[y:y + h, x:x + w, 1] = 0.42
    height = noise(SIZE, SIZE, 200, 509, octaves=2) * 0.4
    save_set("music_paper", out, normal_from_height(height, 1.2), to_u8(arm))
    return out


# ---------------------------------------------------------------------------------------------
# The music on the shelf: thin volumes in paper, in the colours the publishers sold them in.
# ---------------------------------------------------------------------------------------------

# Binding colours (sRGB): green, blue, ochre, cream, red-brown, black cloth, grey-green, maroon.
BINDINGS = ["#3d5a3c", "#3a4f68", "#b08a36", "#c9b98f", "#6a2c20", "#1e1c1a", "#5d6352", "#4e1a1e"]
SPINE_TITLES = [
    ("BACH", 0), ("MOZART", 2), ("BEETHOVEN", 1), ("HAYDN", 0), ("SCHUBERT", 3), ("BRAHMS", 1), ("DVORAK", 4), ("ELGAR", 6),
    ("VIVALDI", 2), ("POPPER", 3), ("KREUTZER", 0), ("SCALES", 5), ("DUETS", 7), ("SCHUMANN", 1), ("HANDEL", 6), ("CORELLI", 3),
    ("BOCCHERINI", 2), ("TARTINI", 4), ("STUDIES", 0), ("CHOPIN", 1), ("FAURE", 3), ("GRIEG", 7), ("MENDELSSOHN", 0), ("PURCELL", 6),
    ("ETUDES", 2), ("SONATAS", 5), ("TRIOS", 4), ("QUARTETS", 0), ("CONCERTI", 1), ("EXERCISES", 3), ("SUITES", 7), ("ALBUM", 2),
]


def draw_music_spine(index, title, binding):
    w, h = SPINE_W, SPINE_H
    W, H = w * SS * 2, h * SS * 2
    rng = np.random.default_rng(700 + index)
    base = np.array(hexrgb(BINDINGS[binding]), dtype=np.float32) / 255.0
    base = base * rng.uniform(0.85, 1.12)
    rgb = np.ones((h, w, 3), dtype=np.float32) * base * (0.85 + 0.3 * noise(w, h, 30, 710 + index, octaves=2))[..., None]
    # The title runs down the spine, top to bottom, on a pale printed band for the light papers and
    # straight onto the colour for the dark ones.
    label = Image.new("L", (H, W), 0)
    dl = ImageDraw.Draw(label)
    size = 34 if len(title) <= 7 else (28 if len(title) <= 9 else 23)
    f = font(FONT_BOLD, size)
    tw = dl.textlength(title, font=f)
    dl.text((H * 0.2, W * 0.5 - size * SS * 0.62), title, font=f, fill=255)
    dl.line([(H * 0.2 - 30 * SS, W * 0.5), (H * 0.2 - 12 * SS, W * 0.5)], fill=255, width=3 * SS)
    dl.line([(H * 0.2 + tw + 12 * SS, W * 0.5), (H * 0.2 + tw + 30 * SS, W * 0.5)], fill=255, width=3 * SS)
    text = label.rotate(-90, expand=True).resize((w, h), Image.LANCZOS)
    t = np.asarray(text, dtype=np.float32) / 255.0
    dark = base.mean() < 0.35
    ink = np.array([0.86, 0.80, 0.62]) if dark else np.array([0.08, 0.06, 0.05])
    rgb = rgb * (1 - t[..., None]) + ink * t[..., None]
    # Rubbed at the head and the tail and along the joints, and a little faded from the top.
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    scuff = np.clip(np.exp(-(np.minimum(yy, h - 1 - yy) / 10.0) ** 2) + np.exp(-(np.minimum(xx, w - 1 - xx) / 2.0) ** 2) * 0.4, 0, 1)
    scuff *= 0.4 + noise(w, h, 20, 720 + index)
    rgb = rgb * (1 - scuff[..., None] * 0.35) + np.array([0.5, 0.44, 0.34]) * scuff[..., None] * 0.35
    fade = rng.uniform(0.0, 0.3)
    rgb = rgb * (1 - fade) + rgb.mean(axis=-1, keepdims=True) * fade
    return np.clip(rgb, 0, 1)


def build_books():
    out = np.zeros((BOOK_SHEET, BOOK_SHEET, 3), dtype=np.float32)
    out[...] = 0.3
    for k, (title, binding) in enumerate(SPINE_TITLES):
        out[0:SPINE_H, k * SPINE_W:(k + 1) * SPINE_W] = draw_music_spine(k, title, binding)
    # The leaves: cream edges with the lines of the pages and dust in them.
    x, y, w, h = LEAVES
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    lines = 0.85 + 0.15 * np.sin(yy * 2.3 + noise(w, h, 8, 731) * 8.0)
    out[y:y + h, x:x + w] = np.array([0.74, 0.68, 0.54]) * (lines * (0.8 + 0.3 * noise(w, h, 20, 732)))[..., None]
    # The covers: a plain patch of each binding's paper.
    for k, colour in enumerate(BINDINGS):
        base = np.array(hexrgb(colour), dtype=np.float32) / 255.0
        patch = base * (0.85 + 0.3 * noise(COVER_W, 256, 12, 740 + k, octaves=3))[..., None]
        out[512:768, COVERS_X + k * COVER_W:COVERS_X + (k + 1) * COVER_W] = patch
    out[768:] = 0.2
    arm = np.zeros((BOOK_SHEET, BOOK_SHEET, 3), dtype=np.float32)
    arm[..., 0] = 1.0
    arm[..., 1] = 0.82
    height = noise(BOOK_SHEET, BOOK_SHEET, 160, 750, octaves=2) * 0.5
    save_set("music_books", np.clip(out, 0, 1), normal_from_height(height, 1.0), to_u8(arm))
    with open(os.path.join(TEXTURES, "music_books.json"), "w", encoding="utf-8") as handle:
        json.dump({"bindings": [b for _, b in SPINE_TITLES]}, handle)
    return out


# ---------------------------------------------------------------------------------------------
# The paintings.
# ---------------------------------------------------------------------------------------------

def make_cellist(seed):
    """The woman at her cello, in oils: seated in dark green, the instrument between her knees, the
    bow drawn across it, a window and a curtain behind her. The library's portrait is the same woman."""
    w, h = 512, 640
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    bg = np.array([0.18, 0.13, 0.08]) * np.clip(1.25 - np.sqrt(((xx - 260) / 320) ** 2 + ((yy - 220) / 420) ** 2), 0.3, 1.2)[..., None]
    rgb = bg * (0.85 + 0.25 * noise(w, h, 6, seed))[..., None]
    window = soft_shape(w, h, lambda d: d.rectangle([40, 40, 180, 330], fill=255), 14)
    rgb = paint(rgb, window, (0.36, 0.38, 0.40), 0.8 + 0.3 * noise(w, h, 5, seed + 1))
    curtain = soft_shape(w, h, lambda d: d.polygon([(0, 0), (70, 0), (90, 640), (0, 640)], fill=255), 16)
    rgb = paint(rgb, curtain, (0.05, 0.18, 0.10), 0.6 + 0.4 * np.sin(xx / 11.0 + noise(w, h, 4, seed + 2) * 5) ** 2)
    body = soft_shape(w, h, lambda d: d.polygon([(130, 640), (160, 400), (220, 330), (280, 316), (340, 340), (380, 420), (420, 640)], fill=255), 8)
    sheen = 0.55 + 0.6 * np.clip(1 - ((xx - 250) / 140) ** 2, 0, 1) * (0.7 + 0.3 * noise(w, h, 20, seed + 3))
    rgb = paint(rgb, body, (0.06, 0.15, 0.09), sheen)
    rgb = portrait_head(rgb, w, h, 282, 252, 38, 50, (0.78, 0.60, 0.48), (0.12, 0.07, 0.04),
                        lambda d: (d.ellipse([238, 192, 326, 284], fill=255), d.ellipse([300, 266, 334, 304], fill=255)), seed + 4)
    rgb = draw_cello(rgb, w, h, 250, 420, 230, seed + 5, angle=0.1)
    arm = soft_shape(w, h, lambda d: d.line([(350, 380), (380, 470), (330, 520)], fill=255, width=26), 6)
    rgb = paint(rgb, arm, (0.048, 0.12, 0.072), None, 1.0)
    hand = soft_shape(w, h, lambda d: d.ellipse([310, 506, 350, 536], fill=255), 5)
    rgb = paint(rgb, hand, (0.72, 0.54, 0.42), None, 0.95)
    bow = soft_shape(w, h, lambda d: d.line([(120, 600), (440, 500)], fill=255, width=3), 1)
    rgb = paint(rgb, bow, (0.15, 0.08, 0.04), None, 0.9)
    return varnish(np.clip(rgb, 0, 1), seed + 6, amount=1.0)


def make_river(seed):
    """A river at dusk between dark trees, the last light on the water and a spire far off."""
    w, h = 512, 384
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    sky = np.array([0.62, 0.48, 0.30]) * (1 - yy / h * 0.6)[..., None] + np.array([0.20, 0.22, 0.28]) * (yy / h * 0.6)[..., None]
    rgb = sky * (0.9 + 0.15 * noise(w, h, 4, seed))[..., None]
    clouds = np.clip((noise(w, h, 5, seed + 1) - 0.5) * 3.0, 0, 1) * np.clip(1 - yy / (h * 0.45), 0, 1)
    rgb = paint(rgb, clouds, (0.42, 0.34, 0.26), None, 0.6)
    horizon = 230
    land = soft_shape(w, h, lambda d: d.polygon([(0, horizon - 30), (120, horizon - 50), (220, horizon - 20), (330, horizon - 40), (512, horizon - 60), (512, h), (0, h)], fill=255), 4)
    rgb = paint(rgb, land, (0.10, 0.10, 0.06), 0.8 + 0.3 * noise(w, h, 16, seed + 2))
    river = soft_shape(w, h, lambda d: d.polygon([(236, horizon), (262, horizon), (300, 270), (250, 310), (380, h), (90, h), (190, 300), (220, 262)], fill=255), 4)
    streaks = np.clip(noise(w, h, 3, seed + 3) * 0.5 + np.asarray(Image.fromarray((noise(64, h, 30, seed + 5) * 255).astype(np.uint8)).resize((w, h)), dtype=np.float32) / 255.0 * 0.8, 0, 1)
    rgb = paint(rgb, river, (0.58, 0.50, 0.38), 0.55 + 0.6 * streaks * (1 - (yy - horizon) / (h - horizon) * 0.5))
    for k, (tx, th) in enumerate(((40, 150), (90, 120), (380, 170), (450, 140), (500, 160))):
        tree = soft_shape(w, h, lambda d, tx=tx, th=th: d.ellipse([tx - 40, horizon - th, tx + 40, horizon + 10], fill=255), 6)
        rgb = paint(rgb, tree, (0.06, 0.07, 0.04), 0.7 + 0.4 * noise(w, h, 24, seed + 10 + k))
    spire = soft_shape(w, h, lambda d: d.polygon([(250, horizon - 30), (255, horizon - 80), (260, horizon - 30)], fill=255), 1.5)
    rgb = paint(rgb, spire, (0.16, 0.14, 0.12), None, 0.9)
    return varnish(np.clip(rgb, 0, 1), seed + 4, amount=1.1)


def make_still_life(seed):
    """A violin on a draped table with an open score and a jug of roses, against the dark."""
    w, h = 512, 384
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    rgb = np.array([0.12, 0.09, 0.06]) * np.clip(1.2 - np.sqrt(((xx - 200) / 300) ** 2 + ((yy - 160) / 260) ** 2), 0.3, 1.2)[..., None]
    rgb = rgb * (0.85 + 0.25 * noise(w, h, 6, seed))[..., None]
    table = soft_shape(w, h, lambda d: d.polygon([(0, 250), (512, 230), (512, 384), (0, 384)], fill=255), 4)
    rgb = paint(rgb, table, (0.28, 0.10, 0.06), 0.6 + 0.5 * np.sin(xx / 20.0 + noise(w, h, 4, seed + 1) * 4) ** 2)
    score = soft_shape(w, h, lambda d: d.polygon([(250, 240), (400, 226), (420, 270), (262, 286)], fill=255), 2)
    rgb = paint(rgb, score, (0.72, 0.66, 0.52), np.clip(1.2 - (xx - 250) / 300, 0.6, 1.1))
    for k in range(5):
        line = soft_shape(w, h, lambda d, k=k: d.line([(262, 248 + k * 6), (405, 234 + k * 6)], fill=255, width=1), 0.5)
        rgb = paint(rgb, line, (0.2, 0.16, 0.12), None, 0.6)
    jug = soft_shape(w, h, lambda d: d.ellipse([360, 120, 450, 250], fill=255), 4)
    rgb = paint(rgb, jug, (0.40, 0.36, 0.30), np.clip(1.25 - (xx - 360) / 100, 0.4, 1.1))
    for k in range(7):
        cx, cy = 380 + (k % 4) * 18, 100 + (k // 4) * 18 - (k % 2) * 6
        rose = soft_shape(w, h, lambda d, cx=cx, cy=cy: d.ellipse([cx - 12, cy - 11, cx + 12, cy + 11], fill=255), 3)
        rgb = paint(rgb, rose, (0.55, 0.10, 0.08), 0.6 + 0.5 * noise(w, h, 40, seed + 20 + k))
    outline = [(x, y) for x, y in violin_outline(0.0, 0.0, 200.0)]
    ang = math.radians(-78)
    pts = [(130 + x * math.cos(ang) - y * math.sin(ang), 300 + x * math.sin(ang) + y * math.cos(ang)) for x, y in outline]
    body = soft_shape(w, h, lambda d: d.polygon(pts, fill=255), 2)
    rgb = paint(rgb, body, (0.46, 0.17, 0.05), np.clip(1.2 - ((yy - 260) / 60) ** 2 * 0.3, 0.5, 1.1))
    neck = soft_shape(w, h, lambda d: d.line([(130 + 200 * math.cos(ang) * 0.95, 300 + 200 * math.sin(ang) * 0.95 + 0), (130 + 300 * math.cos(ang), 300 + 300 * math.sin(ang))],
                                             fill=255, width=7), 1.5)
    rgb = paint(rgb, neck, (0.05, 0.035, 0.03), None, 0.9)
    return varnish(np.clip(rgb, 0, 1), seed + 5, amount=1.0)


def build_paintings():
    out = np.zeros((1024, 1024, 3), dtype=np.float32)
    out[...] = 0.05
    for name, make, seed in (("cellist", make_cellist, 801), ("river", make_river, 811), ("still_life", make_still_life, 821)):
        x, y, w, h = PAINTINGS[name]
        out[y:y + h, x:x + w] = make(seed)
    arm = np.zeros((1024, 1024, 3), dtype=np.float32)
    arm[..., 0] = 1.0
    arm[..., 1] = 0.36 + 0.12 * noise(1024, 1024, 30, 831)
    height = noise(1024, 1024, 120, 832, octaves=2) * 0.5 + crackle(1024, 1024, 833, scale=70.0) * -0.4
    save_set("music_paintings", out, normal_from_height(height, 2.0), to_u8(arm))
    return out


# ---------------------------------------------------------------------------------------------
# The velvet and the damask.
# ---------------------------------------------------------------------------------------------

def build_velvet():
    """Emerald velvet for the music room's drapes, tiling at the drapes' 34cm: library_velvet's
    pile and crush in green, faded unevenly towards a grey olive where the sun had it, and gone to
    the pale weave where it was handled."""
    n = 1024
    pile = tile_noise(n, 180.0, 901)
    crush = tile_noise(n, 5.0, 902, aspect=0.25)
    patches = tile_noise(n, 3.0, 903)
    base = np.array([0.035, 0.30, 0.17], dtype=np.float32)
    light = 0.72 + 0.5 * crush + 0.16 * (pile - 0.5)
    rgb = base * light[..., None]
    bare = np.clip((patches - 0.72) * 5.0, 0.0, 1.0)
    rgb = rgb * (1 - bare[..., None] * 0.6) + np.array([0.26, 0.24, 0.16]) * bare[..., None] * 0.6
    fade = tile_noise(n, 2.0, 904) * 0.3
    rgb = rgb * (1 - fade[..., None]) + np.array([0.24, 0.30, 0.22]) * fade[..., None]
    height = pile * 0.6 + crush * 0.4 - bare * 0.3
    arm = np.stack([np.ones((n, n)), 0.86 + 0.1 * (1 - crush), np.zeros((n, n))], axis=-1)
    save_set("music_velvet", np.clip(rgb, 0, 1), normal_from_height(height, 2.5), to_u8(arm))
    print("music_velvet linear mean", linear_mean(np.clip(rgb, 0, 1)))
    return rgb


def damask_motif(draw, cx, cy, s, fill):
    """One motif of the damask, symmetric about its upright: a palmette rising from a vase of
    acanthus, two pairs of curling leaves cut at their edges, scrolls at the foot, a pomegranate
    head opening at the crown. About two thirds of its cell."""
    def ellipse(x, y, rx, ry, angle):
        pts = []
        for a in np.linspace(0, 2 * math.pi, 32, endpoint=False):
            # A leaf: an ellipse with its edge cut into lobes.
            lobe = 1.0 + 0.12 * math.sin(a * 7.0)
            ex, ey = math.cos(a) * rx * lobe, math.sin(a) * ry * lobe
            pts.append((x + ex * math.cos(angle) - ey * math.sin(angle), y + ex * math.sin(angle) + ey * math.cos(angle)))
        draw.polygon(pts, fill=fill)

    def curl(x0, y0, side, r0, turns, width):
        pts = []
        for k in range(40):
            a = k / 39 * math.pi * 2.0 * turns
            r = r0 * (1.0 - k / 46)
            pts.append((x0 + side * math.cos(a) * r, y0 - math.sin(a) * r))
        draw.line(pts, fill=fill, width=int(width), joint="curve")

    draw.line([(cx, cy + 170 * s), (cx, cy - 120 * s)], fill=fill, width=int(10 * s))
    # The vase of leaves at the foot.
    draw.polygon([(cx - 46 * s, cy + 150 * s), (cx + 46 * s, cy + 150 * s), (cx + 26 * s, cy + 196 * s), (cx - 26 * s, cy + 196 * s)], fill=fill)
    for side in (-1, 1):
        ellipse(cx + side * 70 * s, cy + 78 * s, 74 * s, 22 * s, side * -0.5)
        ellipse(cx + side * 58 * s, cy - 6 * s, 62 * s, 19 * s, side * -0.8)
        ellipse(cx + side * 34 * s, cy - 62 * s, 40 * s, 13 * s, side * -1.05)
        curl(cx + side * 52 * s, cy + 150 * s, side, 46 * s, 0.85, 11 * s)
        curl(cx + side * 120 * s, cy + 40 * s, side, 30 * s, 0.75, 8 * s)
        # The palmette's fronds fanning out round its head.
        for k in range(5):
            a = math.radians(10 + k * 19)
            ellipse(cx + side * math.sin(a) * 70 * s, cy - 150 * s - math.cos(a) * 58 * s, 36 * s, 10 * s, side * (math.pi / 2 - a))
    # The pomegranate: a round head, a crown of seeds, split open down the middle.
    draw.ellipse([cx - 40 * s, cy - 196 * s, cx + 40 * s, cy - 116 * s], fill=fill)
    for k in range(7):
        a = math.radians(-90 + (k - 3) * 22)
        x, y = cx + math.cos(a) * 52 * s, cy - 156 * s + math.sin(a) * 52 * s
        draw.ellipse([x - 9 * s, y - 9 * s, x + 9 * s, y + 9 * s], fill=fill)


def ogee(draw, p0, p1, width, fill, bulge):
    """One reach of the ogee lattice: an S from one meeting point of the cells to the next."""
    (x0, y0), (x1, y1) = p0, p1
    dx, dy = x1 - x0, y1 - y0
    length = math.hypot(dx, dy)
    nx, ny = -dy / length, dx / length
    pts = [(x0 + dx * t + nx * math.sin(2 * math.pi * t) * bulge, y0 + dy * t + ny * math.sin(2 * math.pi * t) * bulge) for t in np.linspace(0, 1, 48)]
    draw.line(pts, fill=fill, width=int(width), joint="curve")


def build_damask():
    """The silk on the walls: one repeat every 64cm, the motif in the middle and a half-drop of it at
    the corners, each in its cell of an ogee lattice with a small flower where the cells meet; woven
    as satin on a matte ground of the same faded gold. Drawn on a sheet three repeats across and
    cropped to the middle one, so it tiles; the wear is periodic noise."""
    n = 1024
    big = Image.new("L", (n * 3, n * 3), 0)
    draw = ImageDraw.Draw(big)
    s = n / 470.0
    half = n * 0.5
    for gx in range(-1, 5):
        for gy in range(-1, 5):
            for ox, oy in ((half, half), (0.0, 0.0)):
                cx, cy = gx * n + ox, gy * n + oy
                damask_motif(draw, cx, cy, s * (1.0 if ox else 0.92), 255)
                # The cell round it: four reaches from its top and bottom points to its sides.
                top, bottom = (cx, cy - half), (cx, cy + half)
                left, right = (cx - half, cy), (cx + half, cy)
                for a, b in ((top, right), (right, bottom), (bottom, left), (left, top)):
                    ogee(draw, a, b, 9 * s, 255, 20 * s)
                for px, py in (top, left):
                    for k in range(5):
                        ang = k / 5 * 2 * math.pi
                        fx, fy = px + math.cos(ang) * 14 * s, py + math.sin(ang) * 14 * s
                        draw.ellipse([fx - 9 * s, fy - 9 * s, fx + 9 * s, fy + 9 * s], fill=255)
    mask = np.asarray(big.crop((n, n, 2 * n, 2 * n)).filter(ImageFilter.GaussianBlur(1.5)), dtype=np.float32) / 255.0
    # The weave: a fine twill in the ground, the satin's float running the other way in the motif.
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)
    twill = 0.5 + 0.5 * np.sin((xx + yy) * (2 * math.pi / 6.0))
    satin = 0.5 + 0.5 * np.sin(yy * (2 * math.pi / 4.0))
    # Tone on tone, as damask is: the pattern is in the weave and the sheen, the colour barely moves.
    # At half again the ground's value it read as a printed wallpaper across the room.
    ground = np.array([0.31, 0.25, 0.14], dtype=np.float32)
    motif = np.array([0.37, 0.30, 0.17], dtype=np.float32)
    rgb = ground * (0.92 + 0.08 * twill)[..., None] * (1 - mask[..., None]) + motif * (0.94 + 0.06 * satin)[..., None] * mask[..., None]
    # Faded where the light fell, darkened where it did not, and a few tide-marks of damp.
    fade = tile_noise(n, 2.5, 911)
    rgb = rgb * (0.82 + 0.3 * fade)[..., None]
    damp = np.clip((tile_noise(n, 4.0, 912) - 0.68) * 4.0, 0, 1)
    rgb = rgb * (1 - damp[..., None] * 0.35) + np.array([0.24, 0.18, 0.10]) * damp[..., None] * 0.35
    grime = tile_noise(n, 60.0, 913)
    rgb *= (0.9 + 0.2 * grime)[..., None]
    height = mask * 0.5 + twill * 0.08 * (1 - mask) + satin * 0.05 * mask
    # The satin floats are glossier than the ground, but sixty years of dust have taken most of it:
    # at 0.56 the motif mirrored the cold windows and the walls came out patterned in blue.
    rough = 0.9 - mask * 0.14 + (grime - 0.5) * 0.08
    arm = np.stack([np.ones((n, n)), rough, np.zeros((n, n))], axis=-1)
    save_set("music_damask", np.clip(rgb, 0, 1), normal_from_height(height, 3.0), to_u8(arm))
    print("music_damask linear mean", linear_mean(np.clip(rgb, 0, 1)))
    return rgb


def main():
    os.makedirs(PREVIEW, exist_ok=True)
    if "--velvet" in os.sys.argv:
        preview("music_velvet", build_velvet())
        return
    os.makedirs(TEXTURES, exist_ok=True)
    preview("music_velvet", build_velvet())
    preview("music_damask", build_damask())
    preview("music_paper", build_paper())
    preview("music_books", build_books())
    preview("music_paintings", build_paintings())
    print("Wrote music_velvet, music_damask, music_paper, music_books, music_paintings to", TEXTURES)


if __name__ == "__main__":
    main()
