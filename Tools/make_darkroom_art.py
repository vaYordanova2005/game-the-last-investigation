#!/usr/bin/env python3
"""
Bakes the darkroom's prints, film and labels into one texture set the art pipeline imports.

    python Tools/make_darkroom_art.py

Writes darkroom_paper (_Diffuse / _nor_dx / _arm) into Art/Source/Textures: a 2048 atlas of the
rectangles below (pixel x, y, w, h), shared with Tools/make_darkroom.py (ATLAS) and
DarkroomActor.cpp (DarkroomRects):

    wedding   the same wedding photograph as on her nightstand upstairs: he printed it here
    family    the same beach photograph as under the bedroom's television
    violin    the girl with her violin, at a music stand
    house     this house on a summer afternoon, the lawn mown
    dark_a    a print left too long under the enlarger: nearly black, two figures under an umbrella
              only just there in it
    dark_b    another, black through
    road      the print in the fixer: a wet road at night, two lamps far off, rain. Pale, as a print
              is that was pulled out of the developer too soon
    contact   a contact sheet: thirty frames, the family's summer, some ringed in grease pencil
    negs      a strip of colour negative: orange base, six frames, sprocket holes
    dev, stop, fix   the chemical bottles' labels (ARGENTA, an invented maker)
    timer     the darkroom timer's dial, sixty seconds round

Printed things say what printed things say (a maker, a chemical, the seconds); nothing written says
anything (the 09-30 decision). Faces small and soft, as in the bedroom's photographs.
"""

import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from make_bedroom_art import PICTURES as BEDROOM_PICTURES, make_family, make_wedding, photo_finish
from make_cellar_art import FONT_BOLD, FONT_REGULAR, SS, TEXTURES, age_paper, centred, font, noise
from make_library_art import normal_from_height, paint, portrait_head, save_set, soft_shape, to_u8

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PREVIEW = os.path.join(ROOT, "Saved", "DarkroomPreview")
SIZE = 2048

# Must stay in step with make_darkroom.py (ATLAS) and DarkroomActor.cpp (DarkroomRects).
RECTS = {
    "wedding": (0, 0, 256, 320),
    "family": (272, 0, 320, 256),
    "violin": (608, 0, 256, 320),
    "house": (880, 0, 320, 256),
    "dark_a": (1216, 0, 256, 320),
    "dark_b": (1488, 0, 320, 256),
    "road": (0, 336, 320, 256),
    "contact": (336, 336, 512, 640),
    "negs": (864, 336, 1024, 128),
    "dev": (864, 480, 256, 320),
    "stop": (1136, 480, 256, 320),
    "fix": (1408, 480, 256, 320),
    "timer": (1680, 480, 256, 256),
    "filmbox": (0, 992, 256, 384),
    "canister": (272, 992, 256, 96),
}


def grid(name):
    w, h = RECTS[name][2:]
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    return w, h, yy, xx


def make_violin():
    """The girl at a music stand, the violin up under her chin, a window behind. Portrait."""
    w, h, yy, xx = grid("violin")
    rgb = np.array([0.70, 0.64, 0.55]) * (0.9 + 0.2 * noise(w, h, 30, 801))[..., None]
    window = soft_shape(w, h, lambda d: d.rectangle([150, 30, 240, 180], fill=255), 3)
    rgb = paint(rgb, window, (0.92, 0.90, 0.84), None, 0.9)
    floor = soft_shape(w, h, lambda d: d.rectangle([0, 250, w, h], fill=255), 2)
    rgb = paint(rgb, floor, (0.42, 0.30, 0.20), 0.85 + 0.15 * np.sin(xx / 6.0) ** 2)
    stand = soft_shape(w, h, lambda d: (d.line([(200, 160), (200, 290)], fill=255, width=4), d.rectangle([168, 140, 232, 170], fill=255)), 1.5)
    rgb = paint(rgb, stand, (0.10, 0.09, 0.08), None, 0.95)
    dress = soft_shape(w, h, lambda d: d.polygon([(70, 300), (80, 190), (100, 160), (140, 160), (160, 190), (170, 300)], fill=255), 3)
    rgb = paint(rgb, dress, (0.36, 0.42, 0.60), 0.75 + 0.3 * np.clip(1 - ((xx - 120) / 50) ** 2, 0, 1))
    violin = soft_shape(w, h, lambda d: d.polygon([(132, 150), (178, 168), (172, 182), (126, 164)], fill=255), 1.5)
    rgb = paint(rgb, violin, (0.40, 0.18, 0.06), None, 0.95)
    bow = soft_shape(w, h, lambda d: d.line([(100, 200), (190, 130)], fill=255, width=2), 1)
    rgb = paint(rgb, bow, (0.85, 0.80, 0.70), None, 0.8)
    rgb = portrait_head(rgb, w, h, 120, 134, 15, 18, (0.88, 0.70, 0.58), (0.52, 0.36, 0.18),
                        lambda d: (d.ellipse([102, 110, 138, 150], fill=255), d.ellipse([126, 128, 142, 176], fill=255)), 803)
    return photo_finish(rgb, 805)


def make_house():
    """The house in summer, from the lawn: the grey façade, its windows, the trees either side."""
    w, h, yy, xx = grid("house")
    rgb = np.array([0.60, 0.70, 0.82]) * (1 - yy / h * 0.2)[..., None]
    trees = soft_shape(w, h, lambda d: (d.ellipse([-40, 40, 90, 200], fill=255), d.ellipse([240, 30, 370, 210], fill=255)), 6)
    rgb = paint(rgb, trees, (0.16, 0.26, 0.12), 0.8 + 0.4 * noise(w, h, 10, 811))
    house = soft_shape(w, h, lambda d: (d.rectangle([70, 80, 250, 200], fill=255), d.polygon([(60, 82), (160, 30), (260, 82)], fill=255)), 1.5)
    rgb = paint(rgb, house, (0.56, 0.54, 0.50), 0.9 + 0.1 * noise(w, h, 20, 813))
    for cx in (100, 140, 180, 220):
        for cy in (100, 150):
            win = soft_shape(w, h, lambda d, cx=cx, cy=cy: d.rectangle([cx - 8, cy - 14, cx + 8, cy + 14], fill=255), 1)
            rgb = paint(rgb, win, (0.12, 0.13, 0.15), None, 0.95)
    lawn = soft_shape(w, h, lambda d: d.rectangle([0, 200, w, h], fill=255), 2)
    rgb = paint(rgb, lawn, (0.30, 0.44, 0.18), 0.85 + 0.25 * noise(w, h, 6, 815))
    return photo_finish(rgb, 817)


def overexposed(rgb, seed, amount):
    """A print left under the lamp: the blacks gone solid, the highlights grey, a ghost of the
    picture left in the middle tones."""
    lum = rgb.mean(axis=-1, keepdims=True)
    dark = np.clip(lum - amount, 0.0, 1.0) * 0.35 + 0.02
    h, w, _ = rgb.shape
    dark *= (0.85 + 0.3 * noise(w, h, 40, seed))[..., None]
    return np.concatenate([dark * 1.05, dark, dark * 0.95], axis=-1)


def make_dark_a():
    """Two figures under an umbrella on a pavement, printed far too dark: there, if you look."""
    w, h, yy, xx = grid("dark_a")
    rgb = np.array([0.80, 0.80, 0.80]) * np.ones((h, w, 1))
    umbrella = soft_shape(w, h, lambda d: d.chord([60, 80, 200, 170], 180, 360, fill=255), 2)
    rgb = paint(rgb, umbrella, (0.45, 0.45, 0.45))
    woman = soft_shape(w, h, lambda d: (d.polygon([(110, 300), (114, 150), (150, 150), (156, 300)], fill=255), d.ellipse([118, 116, 146, 148], fill=255)), 3)
    rgb = paint(rgb, woman, (0.55, 0.55, 0.55))
    girl = soft_shape(w, h, lambda d: (d.polygon([(80, 300), (84, 210), (108, 210), (112, 300)], fill=255), d.ellipse([84, 184, 106, 208], fill=255)), 3)
    rgb = paint(rgb, girl, (0.58, 0.58, 0.58))
    return overexposed(rgb, 821, 0.38)


def make_dark_b():
    w, h, yy, xx = grid("dark_b")
    rgb = np.array([0.6, 0.6, 0.6]) * (0.9 + 0.2 * noise(w, h, 50, 825))[..., None]
    return overexposed(rgb, 827, 0.55)


def make_road():
    """A wet road at night, from a car: the verge, the crown of the road shining, two lamps far off
    and the rain across them. Underdeveloped: grey where it should be black, the lamps hardly white."""
    w, h, yy, xx = grid("road")
    rgb = np.ones((h, w, 3), dtype=np.float32) * 0.18
    road = soft_shape(w, h, lambda d: d.polygon([(0, h), (130, 120), (190, 120), (w, h)], fill=255), 3)
    rgb = paint(rgb, road, (0.30, 0.30, 0.31), 0.8 + 0.4 * np.clip((yy - 120) / 140, 0, 1))
    sheen = soft_shape(w, h, lambda d: d.polygon([(150, 125), (170, 125), (200, h), (120, h)], fill=255), 8)
    rgb = paint(rgb, sheen, (0.62, 0.62, 0.62), None, 0.6)
    for cx in (150, 172):
        lamp = soft_shape(w, h, lambda d, cx=cx: d.ellipse([cx - 5, 112, cx + 5, 122], fill=255), 2)
        glow = soft_shape(w, h, lambda d, cx=cx: d.ellipse([cx - 22, 96, cx + 22, 138], fill=255), 12)
        rgb = paint(rgb, glow, (0.70, 0.70, 0.68), None, 0.5)
        rgb = paint(rgb, lamp, (0.95, 0.95, 0.92))
    rain = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(rain)
    rng = np.random.default_rng(831)
    for _ in range(260):
        x, y = rng.uniform(0, w), rng.uniform(0, h)
        d.line([(x, y), (x - 3, y + rng.uniform(8, 18))], fill=int(rng.uniform(60, 160)), width=1)
    rgb = paint(rgb, np.asarray(rain, dtype=np.float32) / 255.0, (0.75, 0.75, 0.75), None, 0.5)
    # Pulled too soon: the whole print lifted towards the paper's grey, and uneven where the
    # developer did not reach.
    rgb = rgb * 0.55 + 0.32
    rgb *= (0.9 + 0.2 * noise(w, h, 60, 833))[..., None]
    gray = rgb.mean(axis=-1, keepdims=True)
    return np.clip(np.concatenate([gray, gray, gray * 0.97], axis=-1), 0, 1)


def thumbnails():
    """Thirty small frames for the contact sheet: the family's prints, cropped and repeated."""
    w_p, h_p = BEDROOM_PICTURES["family"][2:]
    sources = [make_family(), make_wedding(), make_violin(), make_house()]
    frames = []
    rng = np.random.default_rng(841)
    for k in range(30):
        src = sources[k % 4]
        sh, sw, _ = src.shape
        cw, ch = int(sw * rng.uniform(0.6, 0.9)), int(sh * rng.uniform(0.6, 0.9))
        x0, y0 = int(rng.uniform(0, sw - cw)), int(rng.uniform(0, sh - ch))
        crop = src[y0:y0 + ch, x0:x0 + cw]
        img = Image.fromarray(to_u8(crop)).resize((70, 52), Image.LANCZOS)
        frames.append(np.asarray(img, dtype=np.float32) / 255.0)
    return frames


def make_contact():
    """Six strips of five frames in black film rebate on a white sheet, three frames ringed."""
    w, h, yy, xx = grid("contact")
    rgb = np.ones((h, w, 3), dtype=np.float32) * np.array([0.88, 0.87, 0.84])
    frames = thumbnails()
    ring = Image.new("L", (w, h), 0)
    rd = ImageDraw.Draw(ring)
    for row in range(6):
        y = 40 + row * 98
        rgb[y - 10:y + 66, 20:w - 20] = 0.04
        for col in range(5):
            x = 34 + col * 92
            gray = frames[row * 5 + col].mean(axis=-1, keepdims=True)
            rgb[y:y + 52, x:x + 70] = np.concatenate([gray, gray, gray], axis=-1) * 0.85 + 0.04
            if (row, col) in ((0, 2), (2, 4), (4, 1)):
                rd.ellipse([x - 8, y - 8, x + 78, y + 60], outline=255, width=3)
    rgb = paint(rgb, np.asarray(ring.filter(ImageFilter.GaussianBlur(0.8)), dtype=np.float32) / 255.0, (0.70, 0.12, 0.08))
    return age_paper(rgb, 843, foxing=0.3, damp=0.3, edge=0.5, dust=0.5)


def make_negs():
    """A strip of colour negative: the orange mask, six frames of the summer inverted, holes."""
    w, h, yy, xx = grid("negs")
    base = np.array([0.72, 0.42, 0.22])
    rgb = np.ones((h, w, 3), dtype=np.float32) * base
    frames = thumbnails()
    for k in range(6):
        x = 20 + k * 166
        img = Image.fromarray(to_u8(frames[k * 3])).resize((150, 84), Image.LANCZOS)
        f = np.asarray(img, dtype=np.float32) / 255.0
        rgb[22:106, x:x + 150] = (1.0 - f) * base * 1.1 + base * 0.15
    for x in range(8, w, 24):
        for y in (4, h - 16):
            rgb[y:y + 12, x:x + 12] = 0.05
    rgb *= (0.92 + 0.12 * noise(w, h, 30, 851))[..., None]
    return np.clip(rgb, 0, 1)


def make_label(name, title, seed):
    """A chemical bottle's paper label: the maker, the chemical, a rule, a dilution in figures."""
    w, h, yy, xx = grid(name)
    img = Image.new("RGB", (w * SS, h * SS), (226, 214, 186))
    d = ImageDraw.Draw(img)
    d.rectangle([10 * SS, 10 * SS, (w - 10) * SS, (h - 10) * SS], outline=(30, 26, 22), width=3 * SS)
    centred(d, "ARGENTA", w * SS / 2, 34 * SS, font(FONT_BOLD, 30), (120, 28, 22), spacing=4 * SS)
    d.line([(30 * SS, 96 * SS), ((w - 30) * SS, 96 * SS)], fill=(30, 26, 22), width=2 * SS)
    lines = title.split("\n")
    for k, line in enumerate(lines):
        centred(d, line, w * SS / 2, (130 + k * 46) * SS, font(FONT_BOLD, 30), (28, 24, 20))
    centred(d, "PHOTOGRAPHIC", w * SS / 2, 236 * SS, font(FONT_REGULAR, 20), (28, 24, 20), spacing=3 * SS)
    centred(d, "1 + 9", w * SS / 2, 268 * SS, font(FONT_REGULAR, 22), (28, 24, 20))
    img = img.resize((w, h), Image.LANCZOS)
    rgb = np.asarray(img, dtype=np.float32) / 255.0
    # Chemical runs down the label, and the stain where a wet thumb held it.
    run = soft_shape(w, h, lambda dd: dd.rectangle([int(w * 0.6), 0, int(w * 0.68), h], fill=255), 6)
    rgb = paint(rgb, run * (0.5 + 0.5 * noise(w, h, 20, seed)), (0.55, 0.45, 0.28), None, 0.6)
    return age_paper(rgb, seed, foxing=0.8, damp=0.8, edge=0.9, dust=0.7)


def make_timer():
    """A darkroom timer's face: sixty seconds round, every fifth figured, on a black dial."""
    w, h, yy, xx = grid("timer")
    img = Image.new("RGB", (w * SS, h * SS), (18, 18, 18))
    d = ImageDraw.Draw(img)
    cx, cy, r = w * SS / 2, h * SS / 2, w * SS * 0.46
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(200, 196, 180), width=2 * SS)
    for k in range(60):
        a = math.radians(k * 6 - 90)
        r0 = r * (0.80 if k % 5 == 0 else 0.88)
        d.line([(cx + math.cos(a) * r0, cy + math.sin(a) * r0), (cx + math.cos(a) * r * 0.95, cy + math.sin(a) * r * 0.95)],
               fill=(200, 196, 180), width=(3 if k % 5 == 0 else 1) * SS)
        if k % 5 == 0:
            text = str(k)
            fnt = font(FONT_BOLD, 20)
            tw = d.textlength(text, font=fnt)
            d.text((cx + math.cos(a) * r * 0.64 - tw / 2, cy + math.sin(a) * r * 0.64 - 12 * SS), text, font=fnt, fill=(200, 196, 180))
    centred(d, "ARGENTA", cx, cy + r * 0.25, font(FONT_REGULAR, 14), (150, 40, 30), spacing=2 * SS)
    img = img.resize((w, h), Image.LANCZOS)
    return np.asarray(img, dtype=np.float32) / 255.0


def make_filmbox():
    """The front of a box of roll film: a yellow carton, a red band, the maker and the exposures."""
    w, h, yy, xx = grid("filmbox")
    img = Image.new("RGB", (w * SS, h * SS), (214, 160, 20))
    d = ImageDraw.Draw(img)
    d.rectangle([0, int(h * 0.55) * SS, w * SS, int(h * 0.78) * SS], fill=(160, 30, 22))
    centred(d, "ARGENTA", w * SS / 2, 40 * SS, font(FONT_BOLD, 34), (30, 20, 12), spacing=3 * SS)
    centred(d, "FILM", w * SS / 2, 100 * SS, font(FONT_BOLD, 52), (30, 20, 12))
    centred(d, "36 EXP", w * SS / 2, int(h * 0.60) * SS, font(FONT_BOLD, 34), (240, 220, 180))
    centred(d, "ISO 400", w * SS / 2, int(h * 0.84) * SS, font(FONT_REGULAR, 26), (30, 20, 12))
    img = img.resize((w, h), Image.LANCZOS)
    rgb = np.asarray(img, dtype=np.float32) / 255.0
    return age_paper(rgb, 881, foxing=0.3, damp=0.4, edge=0.8, dust=0.6)


def make_canister():
    """The label round a 35mm cassette: yellow and black, the maker along it."""
    w, h, yy, xx = grid("canister")
    img = Image.new("RGB", (w * SS, h * SS), (210, 156, 22))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, int(w * 0.28) * SS, h * SS], fill=(24, 20, 16))
    centred(d, "ARGENTA 400", w * SS * 0.62, 30 * SS, font(FONT_BOLD, 26), (30, 20, 12))
    img = img.resize((w, h), Image.LANCZOS)
    return np.asarray(img, dtype=np.float32) / 255.0


def main():
    os.makedirs(PREVIEW, exist_ok=True)
    atlas = np.ones((SIZE, SIZE, 3), dtype=np.float32) * np.array([0.80, 0.78, 0.72])
    makers = {
        "wedding": make_wedding, "family": make_family, "violin": make_violin, "house": make_house,
        "dark_a": make_dark_a, "dark_b": make_dark_b, "road": make_road, "contact": make_contact, "negs": make_negs,
        "dev": lambda: make_label("dev", "DEVELOPER", 861), "stop": lambda: make_label("stop", "STOP\nBATH", 862),
        "fix": lambda: make_label("fix", "FIXER", 863), "timer": make_timer,
        "filmbox": make_filmbox, "canister": make_canister,
    }
    for name, make in makers.items():
        x, y, w, h = RECTS[name]
        tile = make()
        # The bedroom's prints come back with their white border: fit them to the rectangle.
        if tile.shape[0] != h or tile.shape[1] != w:
            tile = np.asarray(Image.fromarray(to_u8(tile)).resize((w, h), Image.LANCZOS), dtype=np.float32) / 255.0
        atlas[y:y + h, x:x + w] = tile
    # Prints are glossy paper, labels matt, film a little shiny.
    arm = np.zeros((SIZE, SIZE, 3), dtype=np.float32)
    arm[..., 0] = 1.0
    arm[..., 1] = 0.85
    for name in ("wedding", "family", "violin", "house", "dark_a", "dark_b", "road", "negs"):
        x, y, w, h = RECTS[name]
        arm[y:y + h, x:x + w, 1] = 0.4
    height = noise(SIZE, SIZE, 200, 871, octaves=2) * 0.2
    save_set("darkroom_paper", atlas, normal_from_height(height, 1.0), to_u8(arm))
    Image.fromarray(to_u8(atlas), "RGB").resize((1024, 1024), Image.LANCZOS).save(os.path.join(PREVIEW, "darkroom_paper.png"))
    print("Wrote darkroom_paper to", TEXTURES)


if __name__ == "__main__":
    main()
