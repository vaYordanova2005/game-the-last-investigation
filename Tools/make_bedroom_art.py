#!/usr/bin/env python3
"""
Bakes the master bedroom's pictures into one texture set the art pipeline imports.

    python Tools/make_bedroom_art.py

Writes into Art/Source/Textures bedroom_pictures (_Diffuse / _nor_dx / _arm), a 1024 atlas (PICTURES
below, shared with Tools/make_bedroom.py, whose frames carry UVs into it) of

    artwork   the modern canvas over the bed: soft sweeps of stone, sand and charcoal on an off-white
              ground, a thread of old gold through them. Bought for the renovation, to go with the
              white room, and the only thing in the house that was never meant to look old.
    wedding   the wedding photograph on her nightstand: the two of them on the steps of a church.
    family    the family photograph on the cabinet under the television: the three of them on a beach.

and bedroom_paint: the room's white paint, tileable, one repeat every 120cm (MasterBedroomActor.cpp:
BedroomPaint). Painted plaster that was smooth when it was done: a faint roller stipple, the
slightest unevenness in the colour, and nothing coarse. Poly Haven's ceiling_interior was tried
first and is a knock-down stucco: across a wall at that size it came out as heavy blobs.

Faces are painted soft and small, as in the music room's photographs: who they are is the story's
to say, not the picture's. Nothing here is text (the 09-30 decision).

A preview of the atlas goes to Saved/BedroomPreview/bedroom_pictures.png.
"""

import math
import os

import numpy as np
from PIL import Image

from make_cellar_art import TEXTURES, age_paper, noise
from make_library_art import normal_from_height, paint, portrait_head, save_set, soft_shape, tile_noise, to_u8

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PREVIEW = os.path.join(ROOT, "Saved", "BedroomPreview")
SIZE = 1024

# name -> (x, y, w, h) in pixels. Must stay in step with make_bedroom.py: PICTURES.
PICTURES = {
    "artwork": (0, 0, 1024, 658),
    "wedding": (0, 680, 256, 320),
    "family": (280, 680, 320, 256),
}


def photo_finish(rgb, seed):
    """A colour print a decade or two old: warm, a little faded, a soft vignette, a white border.
    Kept in a frame behind glass, so not foxed like the cellar's paper; only dulled."""
    h, w, _ = rgb.shape
    out = rgb * np.array([1.03, 0.97, 0.88]) + np.array([0.03, 0.025, 0.03])
    out = out * 0.88 + 0.05
    out *= (0.95 + 0.1 * noise(w, h, 80, seed, octaves=2))[..., None]
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    d = np.sqrt(((xx - w / 2) / (w / 2)) ** 2 + ((yy - h / 2) / (h / 2)) ** 2)
    out *= (1.0 - np.clip(d - 0.75, 0, 1) * 0.45)[..., None]
    border = 8
    frame = np.ones((h + border * 2, w + border * 2, 3), dtype=np.float32) * np.array([0.90, 0.88, 0.84])
    frame[border:border + h, border:border + w] = np.clip(out, 0, 1)
    img = Image.fromarray(to_u8(frame), "RGB").resize((w, h), Image.LANCZOS)
    return age_paper(np.asarray(img, dtype=np.float32) / 255.0, seed + 1, foxing=0.0, damp=0.0, edge=0.15, dust=0.25)


def make_artwork():
    """Large soft sweeps of colour across a canvas, the way a decorator's abstract is painted: a
    broad stone-grey arc, a sand band under it, one charcoal stroke, a fine line of gold leaf."""
    x0, y0, w, h = PICTURES["artwork"]
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    ground = np.array([0.86, 0.84, 0.79])
    rgb = np.ones((h, w, 3), dtype=np.float32) * ground
    # The canvas weave under everything.
    weave = 0.97 + 0.03 * np.sin(xx * 1.9) * np.sin(yy * 1.9)
    rgb *= weave[..., None]

    def sweep(cx, cy, rx, ry, thick, colour, blur, seed, amount=0.92, a0=200, a1=340):
        mask = soft_shape(w, h, lambda d: d.arc([cx - rx, cy - ry, cx + rx, cy + ry], a0, a1, fill=255, width=thick), blur)
        # Dry-brush: the stroke breaks up along its length.
        # Bristle streaks run along the stroke: noise stretched along it reads as a brush; a regular
        # sine across it read as a ruled hatch.
        streak = np.asarray(Image.fromarray(to_u8(noise(w // 8, h, 90, seed + 2, octaves=2)[..., None].repeat(3, -1))).resize((w, h), Image.BICUBIC),
                            dtype=np.float32)[..., 0] / 255.0
        drag = np.clip(0.45 + 0.6 * noise(w, h, 30, seed, octaves=3) + 0.5 * (streak - 0.5), 0, 1)
        return paint(rgb, mask * drag, colour, 0.9 + 0.15 * noise(w, h, 12, seed + 1), amount)

    rgb = sweep(520, 760, 640, 560, 210, (0.62, 0.61, 0.58), 26, 11)
    rgb = sweep(430, 880, 760, 520, 120, (0.76, 0.68, 0.55), 20, 13, 0.85)
    rgb = sweep(640, 640, 520, 420, 46, (0.16, 0.15, 0.15), 6, 17, 0.9, 210, 300)
    rgb = sweep(300, 520, 360, 260, 18, (0.30, 0.29, 0.28), 4, 19, 0.7, 250, 330)
    # A small wash of rust where the sweeps cross.
    wash = soft_shape(w, h, lambda d: d.ellipse([690, 250, 860, 380], fill=255), 30)
    rgb = paint(rgb, wash * noise(w, h, 18, 23), (0.55, 0.36, 0.24), None, 0.55)
    # The gold: a thin uneven line, laid last.
    gold = soft_shape(w, h, lambda d: d.arc([120, 180, 980, 900], 205, 318, fill=255, width=5), 1.2)
    rgb = paint(rgb, gold * np.clip(noise(w, h, 60, 29) * 1.6, 0, 1), (0.72, 0.56, 0.26), None, 0.95)
    # The painted edge wraps round the stretcher: darker at the border.
    edge = np.clip(1.0 - np.minimum(np.minimum(xx, w - 1 - xx), np.minimum(yy, h - 1 - yy)) / 10.0, 0, 1)
    rgb = rgb * (1 - edge[..., None] * 0.18)
    return np.clip(rgb, 0, 1)


def make_wedding():
    """The two of them at the top of a church's steps, the arch of the door behind: him in a dark
    suit, her in white with a veil and a posy of pale roses. Portrait, 4 x 5."""
    w, h = PICTURES["wedding"][2:]
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    stone = np.array([0.58, 0.55, 0.50]) * (0.85 + 0.3 * noise(w, h, 14, 701))[..., None]
    rgb = stone.copy()
    door = soft_shape(w, h, lambda d: (d.rectangle([60, 70, 196, h], fill=255), d.ellipse([60, 10, 196, 140], fill=255)), 3)
    rgb = paint(rgb, door, (0.16, 0.11, 0.08), 0.8 + 0.3 * np.sin(xx / 5.0) ** 2)
    steps = soft_shape(w, h, lambda d: d.rectangle([0, 268, w, h], fill=255), 2)
    rgb = paint(rgb, steps, (0.66, 0.63, 0.58), 0.85 + 0.15 * np.sin(yy / 3.0))
    # Him, on the left: dark suit, white shirt at the collar.
    suit = soft_shape(w, h, lambda d: d.polygon([(34, h), (40, 170), (62, 140), (96, 134), (126, 146), (138, 200), (134, h)], fill=255), 3)
    rgb = paint(rgb, suit, (0.06, 0.06, 0.08), 0.7 + 0.4 * np.clip(1 - ((xx - 80) / 60) ** 2, 0, 1))
    shirt = soft_shape(w, h, lambda d: d.polygon([(80, 142), (94, 142), (88, 190)], fill=255), 1.5)
    rgb = paint(rgb, shirt, (0.92, 0.92, 0.90), None, 0.95)
    rgb = portrait_head(rgb, w, h, 86, 112, 15, 19, (0.82, 0.62, 0.50), (0.12, 0.08, 0.05),
                        lambda d: (d.ellipse([70, 88, 104, 120], fill=255),), 703)
    # Her, on the right: the dress to the steps, the veil falling behind.
    veil = soft_shape(w, h, lambda d: d.polygon([(150, 96), (196, 92), (230, 260), (150, 280)], fill=255), 8)
    rgb = paint(rgb, veil, (0.93, 0.92, 0.90), None, 0.45)
    dress = soft_shape(w, h, lambda d: d.polygon([(120, h), (136, 220), (146, 150), (164, 136), (190, 140), (206, 220), (228, h)], fill=255), 3)
    rgb = paint(rgb, dress, (0.94, 0.93, 0.90), 0.8 + 0.25 * np.clip(1 - ((xx - 160) / 60) ** 2, 0, 1))
    rgb = portrait_head(rgb, w, h, 172, 116, 13, 17, (0.86, 0.66, 0.54), (0.36, 0.22, 0.12),
                        lambda d: (d.ellipse([156, 94, 190, 128], fill=255), d.ellipse([180, 112, 196, 150], fill=255)), 705)
    posy = soft_shape(w, h, lambda d: d.ellipse([128, 196, 158, 222], fill=255), 2)
    rgb = paint(rgb, posy * (0.7 + 0.3 * noise(w, h, 40, 707)), (0.88, 0.76, 0.72), None, 0.95)
    return photo_finish(rgb, 709)


def make_family():
    """The three of them on a beach at the end of a summer day: the man and the woman either side
    of the little girl, a hand each, the sea behind. Landscape, 5 x 4."""
    w, h = PICTURES["family"][2:]
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    sky = np.array([0.62, 0.70, 0.80]) * (1 - yy / h * 0.25)[..., None] + np.array([0.20, 0.10, 0.02]) * (yy / h)[..., None]
    rgb = sky.copy()
    sea = soft_shape(w, h, lambda d: d.rectangle([0, 110, w, 160], fill=255), 1.5)
    rgb = paint(rgb, sea, (0.24, 0.38, 0.46), 0.85 + 0.2 * np.sin(xx / 4.0 + yy) ** 2)
    sand = soft_shape(w, h, lambda d: d.rectangle([0, 158, w, h], fill=255), 2)
    rgb = paint(rgb, sand, (0.78, 0.68, 0.52), 0.85 + 0.2 * noise(w, h, 30, 711))
    surf = soft_shape(w, h, lambda d: d.line([(0, 160), (w, 156)], fill=255, width=4), 2)
    rgb = paint(rgb, surf, (0.92, 0.92, 0.90), None, 0.7)
    figures = (
        # x, shoulder y, scale, clothes, skin, hair, girl
        (96, 104, 1.0, (0.20, 0.28, 0.40), (0.80, 0.60, 0.48), (0.12, 0.08, 0.05), False),
        (226, 112, 0.92, (0.86, 0.80, 0.70), (0.86, 0.66, 0.54), (0.36, 0.22, 0.12), False),
        (160, 150, 0.6, (0.86, 0.54, 0.60), (0.88, 0.70, 0.58), (0.52, 0.36, 0.18), True),
    )
    for x, sy, s, clothes, skin, hair, girl in figures:
        body = soft_shape(w, h, lambda d, x=x, sy=sy, s=s: d.polygon(
            [(x - 24 * s, 236), (x - 22 * s, sy + 6 * s), (x - 10 * s, sy - 4 * s), (x + 10 * s, sy - 4 * s), (x + 22 * s, sy + 6 * s), (x + 24 * s, 236)], fill=255), 2)
        rgb = paint(rgb, body, clothes, 0.75 + 0.3 * np.clip(1 - ((xx - x) / (30 * s)) ** 2, 0, 1))
        hair_shape = ((lambda d, x=x, sy=sy, s=s: (d.ellipse([x - 13 * s, sy - 44 * s, x + 13 * s, sy - 14 * s], fill=255),
                                                    d.ellipse([x + 4 * s, sy - 30 * s, x + 16 * s, sy - 4 * s], fill=255)))
                      if girl or s < 0.95 else
                      (lambda d, x=x, sy=sy, s=s: (d.ellipse([x - 12 * s, sy - 42 * s, x + 12 * s, sy - 18 * s], fill=255),)))
        rgb = portrait_head(rgb, w, h, x, sy - 26 * s, 10 * s, 13 * s, skin, hair, hair_shape, 720 + x)
    # Their hands joined across the girl.
    for a, b in (((118, 170), (146, 186)), ((174, 186), (204, 174))):
        arm = soft_shape(w, h, lambda d, a=a, b=b: d.line([a, b], fill=255, width=5), 1.5)
        rgb = paint(rgb, arm, (0.82, 0.62, 0.50), None, 0.9)
    return photo_finish(rgb, 731)


def build_pictures():
    out = np.ones((SIZE, SIZE, 3), dtype=np.float32) * np.array([0.86, 0.84, 0.80])
    for name, make in (("artwork", make_artwork), ("wedding", make_wedding), ("family", make_family)):
        x, y, w, h = PICTURES[name]
        out[y:y + h, x:x + w] = make()
    # Canvas is matt; the prints keep a little of their gloss under the glass.
    arm = np.zeros((SIZE, SIZE, 3), dtype=np.float32)
    arm[..., 0] = 1.0
    arm[..., 1] = 0.88
    for name in ("wedding", "family"):
        x, y, w, h = PICTURES[name]
        arm[y:y + h, x:x + w, 1] = 0.45
    height = noise(SIZE, SIZE, 260, 741, octaves=2) * 0.3
    save_set("bedroom_pictures", out, normal_from_height(height, 1.0), to_u8(arm))
    return out


def build_paint():
    """White paint on plaster, tiling: an even off-white with the faintest cloud in it, and a fine
    roller stipple in the normals. Linear mean about 0.6, so the actor's tints work like the
    photographs' do."""
    n = 1024
    cloud = tile_noise(n, 3.0, 901)
    mottle = tile_noise(n, 14.0, 902)
    stipple = tile_noise(n, 260.0, 903)
    grain = tile_noise(n, 90.0, 904)
    base = np.array([0.83, 0.82, 0.79], dtype=np.float32)
    shade = 0.96 + 0.05 * (cloud - 0.5) + 0.025 * (mottle - 0.5) + 0.02 * (stipple - 0.5)
    rgb = np.clip(base[None, None, :] * shade[..., None], 0, 1)
    height = stipple * 0.55 + grain * 0.3 + mottle * 0.15
    arm = np.zeros((n, n, 3), dtype=np.float32)
    arm[..., 0] = 1.0
    arm[..., 1] = 0.72 + 0.08 * (grain - 0.5)
    save_set("bedroom_paint", rgb, normal_from_height(height, 0.9), to_u8(arm))
    return rgb


def main():
    os.makedirs(PREVIEW, exist_ok=True)
    os.makedirs(TEXTURES, exist_ok=True)
    out = build_pictures()
    Image.fromarray(to_u8(out), "RGB").save(os.path.join(PREVIEW, "bedroom_pictures.png"))
    Image.fromarray(to_u8(build_paint()), "RGB").save(os.path.join(PREVIEW, "bedroom_paint.png"))
    print("Wrote bedroom_pictures, bedroom_paint to", TEXTURES)


if __name__ == "__main__":
    main()
