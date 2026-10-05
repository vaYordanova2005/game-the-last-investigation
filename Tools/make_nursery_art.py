#!/usr/bin/env python3
"""
Bakes the girl's bedroom's printed and drawn surfaces into texture sets the art pipeline imports.

    python Tools/make_nursery_art.py

Three sets, each written as <set>_Diffuse / _nor_dx / _arm into Art/Source/Textures beside the
downloaded library, so Tools/build_art.py picks them up like any other surface:

    nursery_wallpaper   pale pink paper with a cream stripe and a rosebud sprig between, printed
                        over decrepit_wallpaper's own wear and grime so it ages like the rest of
                        the house; its normal and ARM are that photograph's.
    floral_fabric       a small rose-and-leaf print on a pink ground, woven into rough_linen's
                        threads; for the curtains and the bedding. Linen's normal and ARM.
    child_drawings      an atlas of eight drawings on paper, four across and two down: the flowers,
                        the house, the cat, the hearts, the rainbow, the family, the sketch of this
                        house she never finished, and the sea. A flat normal, matt paper ARM.

Why baked: a printed pattern and a child's crayon drawing are both *pictures*, and neither the
photographed library nor a material graph has them. The decrepit wallpaper photograph is a plain
worn grey-brown with no print at all, and tinting it pink gives a pink stain, not a wallpaper.
Generated here in plain Python (numpy + Pillow, free) the way Tools/make_glass_crack.py bakes the
fracture, with a preview, so the look is judged in seconds rather than after an import.

Needs the downloaded library first (python Tools/fetch_assets.py), since two of the three sets are
built on top of photographs from it.
"""

import math
import os
import shutil

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXTURES = os.path.join(ROOT, "Art", "Source", "Textures")
PREVIEW = os.path.join(ROOT, "Saved", "NurseryPreview")

SIZE = 2048


def srgb_to_linear(a):
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(a):
    a = np.clip(a, 0.0, 1.0)
    return np.where(a <= 0.0031308, a * 12.92, 1.055 * np.power(a, 1.0 / 2.4) - 0.055)


def load_linear(name):
    image = Image.open(os.path.join(TEXTURES, name)).convert("RGB")
    if image.size != (SIZE, SIZE):
        image = image.resize((SIZE, SIZE), Image.LANCZOS)
    return srgb_to_linear(np.asarray(image, dtype=np.float32) / 255.0)


def save_srgb(linear, name):
    out = (linear_to_srgb(linear) * 255.0 + 0.5).astype(np.uint8)
    Image.fromarray(out, "RGB").save(os.path.join(TEXTURES, name))


def detail_of(linear, blur=48):
    """A photograph's luminance divided by its own blurred mean: its wear, without its colour."""
    lum = linear @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    image = Image.fromarray(np.clip(lum * 255.0 / max(lum.max(), 1e-4), 0, 255).astype(np.uint8), "L")
    smooth = np.asarray(image.filter(ImageFilter.GaussianBlur(blur)), dtype=np.float32) / 255.0 * max(lum.max(), 1e-4)
    return lum / np.maximum(smooth, 1e-3)


def hex_linear(code):
    rgb = np.array([int(code[i:i + 2], 16) for i in (1, 3, 5)], dtype=np.float32) / 255.0
    return srgb_to_linear(rgb)


def stamp_tiled(layer, draw_fn, positions):
    """Draws a motif at each position and its wrapped copies, so the print repeats seamlessly."""
    for x, y in positions:
        for dx in (-SIZE, 0, SIZE):
            for dy in (-SIZE, 0, SIZE):
                draw_fn(layer, x + dx, y + dy)


# ---------------------------------------------------------------------------------------------
# The wallpaper.
# ---------------------------------------------------------------------------------------------

def rosebud(draw, x, y, scale, rot):
    """A rosebud sprig: a curled stem, two leaves and a bud, in four flat colours of print."""
    def p(u, v):
        c, s = math.cos(rot), math.sin(rot)
        return (x + (u * c - v * s) * scale, y + (u * s + v * c) * scale)

    stem = [p(0.0, 14.0), p(1.5, 6.0), p(0.5, -2.0), p(-0.5, -8.0)]
    draw.line(stem, fill=3, width=max(1, int(scale * 1.4)))
    for side in (-1, 1):
        leaf = [p(0.6, 4.0), p(side * 5.5, 1.5), p(side * 8.0, -1.0), p(side * 4.0, 3.5)]
        draw.polygon(leaf, fill=3)
    bud = [p(-3.6, -8.0), p(-3.0, -13.0), p(0.0, -15.5), p(3.0, -13.0), p(3.6, -8.0), p(0.0, -6.0)]
    draw.polygon(bud, fill=1)
    draw.polygon([p(-1.6, -9.0), p(0.0, -14.0), p(1.6, -9.0)], fill=2)
    draw.polygon([p(-2.4, -7.5), p(0.0, -5.5), p(2.4, -7.5), p(0.0, -8.6)], fill=3)


def build_wallpaper():
    base = load_linear("decrepit_wallpaper_Diffuse.jpg")
    detail = detail_of(base)

    # The print, as an index map: 0 ground, 1 rose, 2 rose shade, 3 leaf, 4 stripe.
    index = Image.new("L", (SIZE, SIZE), 0)
    draw = ImageDraw.Draw(index)
    # A pair of narrow cream stripes every 256 px (20cm on the wall at 160cm a repeat).
    for x0 in range(0, SIZE, 256):
        draw.rectangle([x0, 0, x0 + 9, SIZE], fill=4)
        draw.rectangle([x0 + 16, 0, x0 + 19, SIZE], fill=4)
    # The sprigs between, in a half drop, each turned its own way.
    rng = np.random.default_rng(1104)
    positions = []
    for col in range(8):
        for row in range(8):
            positions.append((col * 256 + 138, row * 256 + (128 if col % 2 else 0) + 70))
    for x, y in positions:
        rot = rng.uniform(-0.5, 0.5)
        for dx in (-SIZE, 0, SIZE):
            for dy in (-SIZE, 0, SIZE):
                rosebud(draw, x + dx, y + dy, 4.4, rot)
    index = np.asarray(index.filter(ImageFilter.ModeFilter(3)))

    # Colours as printed, already faded: the pink is going to the paper's own ivory and the greens
    # to grey. The tint in C++ darkens the whole sheet to the room's value; what is fixed here is
    # the hue relation between ground and print.
    palette = {
        0: hex_linear("#e2b9b4"),  # ground: shell pink
        1: hex_linear("#c4706f"),  # rose
        2: hex_linear("#a85257"),  # rose shade
        3: hex_linear("#94a088"),  # leaf
        4: hex_linear("#ecd9c4"),  # stripe: cream
    }
    colour = np.zeros((SIZE, SIZE, 3), dtype=np.float32)
    for key, value in palette.items():
        colour[index == key] = value

    # Sun and damp take the print first: where the old paper is darkest the print is gone to it.
    soft = np.asarray(Image.fromarray(np.clip(detail * 120, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(12)), dtype=np.float32) / 120.0
    fade = np.clip((soft - 0.55) / 0.6, 0.0, 1.0)[..., None]
    ivory = hex_linear("#d9c9b0")
    colour = colour * fade + (colour * 0.35 + ivory * 0.65) * (1.0 - fade)
    # The photograph's wear at half strength: at full strength its streaks read as bark.
    out = colour * np.clip(np.power(np.clip(detail, 0.05, 3.0), 0.5), 0.35, 1.3)[..., None]

    save_srgb(out, "nursery_wallpaper_Diffuse.png")
    shutil.copyfile(os.path.join(TEXTURES, "decrepit_wallpaper_nor_dx.jpg"), os.path.join(TEXTURES, "nursery_wallpaper_nor_dx.jpg"))
    shutil.copyfile(os.path.join(TEXTURES, "decrepit_wallpaper_arm.jpg"), os.path.join(TEXTURES, "nursery_wallpaper_arm.jpg"))
    return out


# ---------------------------------------------------------------------------------------------
# The fabric.
# ---------------------------------------------------------------------------------------------

def flower(draw, x, y, r, rot, petal, centre):
    for k in range(5):
        a = rot + k * 2.0 * math.pi / 5.0
        cx, cy = x + math.cos(a) * r * 0.62, y + math.sin(a) * r * 0.62
        draw.ellipse([cx - r * 0.5, cy - r * 0.5, cx + r * 0.5, cy + r * 0.5], fill=petal)
    draw.ellipse([x - r * 0.3, y - r * 0.3, x + r * 0.3, y + r * 0.3], fill=centre)


def build_fabric():
    linen = load_linear("rough_linen_Diffuse.jpg")
    weave = detail_of(linen, blur=6)

    index = Image.new("L", (SIZE, SIZE), 0)
    draw = ImageDraw.Draw(index)
    rng = np.random.default_rng(611)
    step = 256
    for col in range(SIZE // step):
        for row in range(SIZE // step):
            x = col * step + step * 0.5 + rng.uniform(-30, 30)
            y = row * step + (step * 0.5 if col % 2 else 0) + rng.uniform(-30, 30)
            rot = rng.uniform(0, math.tau)
            for dx in (-SIZE, 0, SIZE):
                for dy in (-SIZE, 0, SIZE):
                    # Two leaves under each flower, then the flower, then a tiny bud beside it.
                    for side in (-1, 1):
                        a = rot + side * 1.9
                        lx, ly = x + dx + math.cos(a) * 30, y + dy + math.sin(a) * 30
                        draw.ellipse([lx - 15, ly - 7, lx + 15, ly + 7], fill=3)
                    flower(draw, x + dx, y + dy, 30, rot, 1, 2)
                    bx, by = x + dx + math.cos(rot + 0.7) * 58, y + dy + math.sin(rot + 0.7) * 58
                    draw.ellipse([bx - 9, by - 9, bx + 9, by + 9], fill=1)
    index = np.asarray(index)

    palette = {
        0: hex_linear("#e4b4b8"),  # ground
        1: hex_linear("#f3e6dc"),  # petals: cream
        2: hex_linear("#c97e88"),  # centres
        3: hex_linear("#a6af96"),  # leaves
    }
    colour = np.zeros((SIZE, SIZE, 3), dtype=np.float32)
    for key, value in palette.items():
        colour[index == key] = value
    colour = np.asarray(Image.fromarray((linear_to_srgb(colour) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2)), dtype=np.float32) / 255.0
    colour = srgb_to_linear(colour)
    out = colour * np.clip(weave, 0.4, 1.5)[..., None]

    save_srgb(out, "floral_fabric_Diffuse.png")
    shutil.copyfile(os.path.join(TEXTURES, "rough_linen_nor_dx.jpg"), os.path.join(TEXTURES, "floral_fabric_nor_dx.jpg"))
    shutil.copyfile(os.path.join(TEXTURES, "rough_linen_arm.jpg"), os.path.join(TEXTURES, "floral_fabric_arm.jpg"))
    return out


# ---------------------------------------------------------------------------------------------
# The drawings. Eight cells of 512 x 1024 on a 2048 x 2048 sheet, four across and two down. Each
# is drawn on a 512 x 1024 canvas, scaled to fit a portrait A4 page of 512 x 724 (its true
# 1:1.414), and the page sits at the top of its cell. C++ samples a cell with TilingXY
# (0.25, PAGE_H / 2048) and UVOffset (col / 4, row / 2), on a quad 21 x 29.7 cm.
# ---------------------------------------------------------------------------------------------

CELL_W = SIZE // 4
# Every crayon line and every colouring-in stroke is this much heavier than first drawn: at the
# first weights the drawings on the wall read as blank paper from across the room under a lantern.
WEIGHT = 1.7
CELL_H = SIZE // 2
PAGE_H = int(round(CELL_W * math.sqrt(2.0)))


class Sheet:
    """One drawing: crayon layers composited over paper."""

    def __init__(self, seed, paper="#efe6d2"):
        self.rng = np.random.default_rng(seed)
        self.paper = hex_linear(paper)
        self.layers = []  # (mask image, colour, waxy)

    def layer(self, colour, waxy=True):
        image = Image.new("L", (CELL_W, CELL_H), 0)
        self.layers.append((image, hex_linear(colour), waxy))
        return ImageDraw.Draw(image)

    def jitter(self, points, amount=3.0):
        return [(x + self.rng.normal(0, amount), y + self.rng.normal(0, amount)) for x, y in points]

    def stroke(self, draw, points, width=7, passes=3, amount=2.2):
        """A crayon line: the same path gone over two or three times, never quite in the same place."""
        for _ in range(passes):
            draw.line(self.jitter(points, amount), fill=255, width=int(round(width * WEIGHT)), joint="curve")

    def scribble_fill(self, draw, polygon, spacing=9, width=6, angle=None):
        """Colouring in: back-and-forth strokes across a shape, clipped to it, missing the edges."""
        mask = Image.new("L", (CELL_W, CELL_H), 0)
        ImageDraw.Draw(mask).polygon(polygon, fill=255)
        hatch = Image.new("L", (CELL_W, CELL_H), 0)
        hd = ImageDraw.Draw(hatch)
        a = angle if angle is not None else self.rng.uniform(-0.9, 0.9)
        c, s = math.cos(a), math.sin(a)
        xs = [p[0] for p in polygon]
        ys = [p[1] for p in polygon]
        cx, cy = sum(xs) / len(xs), sum(ys) / len(ys)
        reach = max(max(xs) - min(xs), max(ys) - min(ys))
        t = -reach
        while t < reach:
            p0 = (cx + c * t - s * reach, cy + s * t + c * reach)
            p1 = (cx + c * t + s * reach, cy + s * t - c * reach)
            hd.line(self.jitter([p0, p1], 2.0), fill=int(self.rng.uniform(200, 255)), width=int(round(width * WEIGHT)))
            t += spacing * self.rng.uniform(0.75, 1.15)
        # A child colours over the line in places and short of it in others.
        mask = mask.filter(ImageFilter.GaussianBlur(3)).point(lambda v: 255 if v > self.rng.uniform(70, 160) else 0)
        draw.bitmap((0, 0), Image.fromarray(np.minimum(np.asarray(hatch), np.asarray(mask)).astype(np.uint8)), fill=255)

    def ellipse_path(self, cx, cy, rx, ry, n=36, start=0.0, end=math.tau):
        return [(cx + math.cos(start + (end - start) * k / n) * rx, cy + math.sin(start + (end - start) * k / n) * ry) for k in range(n + 1)]

    def render(self, age=1.0):
        h, w = CELL_H, CELL_W
        # Paper grain, which is what makes wax read as wax: the colour skips the pits.
        grain = self.rng.random((h, w)).astype(np.float32)
        grain = np.asarray(Image.fromarray((grain * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.9)), dtype=np.float32) / 255.0
        colour = np.ones((h, w, 3), dtype=np.float32) * self.paper
        for image, tint, waxy in self.layers:
            mask = np.asarray(image.filter(ImageFilter.GaussianBlur(0.8)), dtype=np.float32) / 255.0
            if waxy:
                mask = mask * np.clip((grain - 0.12) * 2.6, 0.0, 1.0)
            else:
                mask = mask * (0.55 + 0.45 * grain)
            # Faded: the colours are going to the paper.
            tint = tint * (1.0 - 0.35 * age) + self.paper * 0.35 * age * 0.6
            colour = colour * (1.0 - mask[..., None]) + (colour * tint / np.maximum(self.paper, 1e-3)) * mask[..., None] * 0.5 + tint * mask[..., None] * 0.5
        # Onto the page: the drawing scaled to fit its height, centred, paper either side of it.
        fit_w = int(round(w * PAGE_H / h))
        drawn = Image.fromarray((linear_to_srgb(colour) * 255).astype(np.uint8)).resize((fit_w, PAGE_H), Image.LANCZOS)
        page = Image.new("RGB", (w, PAGE_H), tuple(int(v) for v in (linear_to_srgb(self.paper) * 255)))
        page.paste(drawn, ((w - fit_w) // 2, 0))
        colour = srgb_to_linear(np.asarray(page, dtype=np.float32) / 255.0)
        h = PAGE_H
        # Yellowed towards the edges and spotted with foxing.
        yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
        edge = np.minimum(np.minimum(xx, w - 1 - xx) / w, np.minimum(yy, h - 1 - yy) / h)
        yellow = np.clip(1.0 - edge * 9.0, 0.0, 1.0)[..., None]
        colour = colour * (1.0 - 0.18 * yellow) + hex_linear("#b69a6c") * 0.18 * yellow
        for _ in range(int(14 * age)):
            fx, fy, fr = self.rng.uniform(0, w), self.rng.uniform(0, h), self.rng.uniform(3, 14)
            d = np.sqrt((xx - fx) ** 2 + (yy - fy) ** 2)
            spot = np.clip(1.0 - d / fr, 0.0, 1.0)[..., None] * self.rng.uniform(0.15, 0.4)
            colour = colour * (1.0 - spot) + hex_linear("#9a7748") * spot
        return colour


def drawing_flowers():
    s = Sheet(1)
    grass = s.layer("#4f9a3a")
    for x in range(10, CELL_W, 14):
        s.stroke(grass, [(x, 1010), (x + s.rng.uniform(-8, 8), 900 + s.rng.uniform(-20, 20))], width=6, passes=1)
    stems = s.layer("#3d8a37")
    heads = [("#e8414f", 130, 560), ("#f1a82b", 260, 500), ("#9a52c9", 390, 590)]
    for _, x, y in heads:
        s.stroke(stems, [(x, y + 40), (x + 6, y + 200), (x - 4, 960)], width=8)
        s.scribble_fill(stems, [(x, y + 260), (x - 60, y + 210), (x - 70, y + 240)], spacing=7)
        s.scribble_fill(stems, [(x, y + 320), (x + 60, y + 270), (x + 70, y + 300)], spacing=7)
    for colour, x, y in heads:
        petals = s.layer(colour)
        for k in range(6):
            a = k * math.tau / 6
            px, py = x + math.cos(a) * 46, y + math.sin(a) * 46
            s.stroke(petals, s.ellipse_path(px, py, 34, 34, 20), width=6)
            s.scribble_fill(petals, s.ellipse_path(px, py, 30, 30, 12)[:-1], spacing=8)
        middle = s.layer("#f5d23a")
        s.scribble_fill(middle, s.ellipse_path(x, y, 26, 26, 14)[:-1], spacing=6)
    sun = s.layer("#f6c21c")
    s.scribble_fill(sun, s.ellipse_path(450, 90, 60, 60, 18)[:-1], spacing=7)
    for k in range(9):
        a = math.pi * 0.5 + k * math.pi / 8
        s.stroke(sun, [(450 + math.cos(a) * 80, 90 + math.sin(a) * 80), (450 + math.cos(a) * 130, 90 + math.sin(a) * 130)], width=7)
    return s


def drawing_house():
    s = Sheet(2)
    sky = s.layer("#6aa5e0")
    s.stroke(sky, [(0, 140), (CELL_W, 130)], width=40, passes=2, amount=6)
    ground = s.layer("#56a03e")
    s.scribble_fill(ground, [(0, 860), (CELL_W, 840), (CELL_W, 1020), (0, 1020)], spacing=10)
    walls = s.layer("#d9573f")
    house = [(90, 560), (340, 560), (340, 860), (90, 860)]
    s.stroke(walls, house + [house[0]], width=7)
    s.scribble_fill(walls, house, spacing=14, width=5)
    roof = s.layer("#4a3a8a")
    s.stroke(roof, [(70, 565), (215, 400), (360, 565), (70, 565)], width=8)
    s.scribble_fill(roof, [(80, 560), (215, 410), (350, 560)], spacing=9)
    s.stroke(roof, [(290, 470), (290, 400), (320, 400), (320, 500)], width=7)
    smoke = s.layer("#8c8c8c", waxy=False)
    s.stroke(smoke, [(305, 380), (330, 330), (300, 290), (335, 240), (310, 200)], width=6, passes=2)
    trim = s.layer("#3b3029")
    for wx, wy in ((150, 620), (280, 620)):
        s.stroke(trim, [(wx - 30, wy - 30), (wx + 30, wy - 30), (wx + 30, wy + 30), (wx - 30, wy + 30), (wx - 30, wy - 30)], width=6)
        s.stroke(trim, [(wx, wy - 30), (wx, wy + 30)], width=4, passes=1)
        s.stroke(trim, [(wx - 30, wy), (wx + 30, wy)], width=4, passes=1)
    s.stroke(trim, [(190, 860), (190, 740), (240, 740), (240, 860)], width=7)
    door = s.layer("#a8642f")
    s.scribble_fill(door, [(192, 856), (192, 744), (238, 744), (238, 856)], spacing=6)
    tree = s.layer("#6b4424")
    s.scribble_fill(tree, [(420, 860), (420, 640), (450, 640), (450, 860)], spacing=6)
    leaves = s.layer("#3f8f3a")
    s.scribble_fill(leaves, s.ellipse_path(435, 580, 70, 90, 24)[:-1], spacing=8)
    sun = s.layer("#f6c21c")
    s.scribble_fill(sun, s.ellipse_path(80, 230, 50, 50, 18)[:-1], spacing=7)
    return s


def drawing_cat():
    s = Sheet(3)
    body = s.layer("#e08a2c")
    s.stroke(body, s.ellipse_path(256, 720, 150, 170), width=8)
    s.scribble_fill(body, s.ellipse_path(256, 720, 140, 160, 30)[:-1], spacing=12, width=6)
    s.stroke(body, s.ellipse_path(256, 440, 120, 105), width=8)
    s.scribble_fill(body, s.ellipse_path(256, 440, 112, 98, 30)[:-1], spacing=12, width=6)
    for side in (-1, 1):
        ear = [(256 + side * 40, 360), (256 + side * 105, 270), (256 + side * 110, 400)]
        s.stroke(body, ear + [ear[0]], width=7)
        s.scribble_fill(body, ear, spacing=8)
    s.stroke(body, [(400, 800), (470, 740), (470, 620), (430, 560)], width=16)
    lines = s.layer("#2b2420")
    for side in (-1, 1):
        s.stroke(lines, s.ellipse_path(256 + side * 45, 420, 16, 22, 14), width=6)
        for k in (-1, 0, 1):
            s.stroke(lines, [(256 + side * 40, 475 + k * 4), (256 + side * 150, 455 + k * 26)], width=4, passes=1)
    s.stroke(lines, [(240, 460), (272, 460), (256, 478), (240, 460)], width=5)
    s.stroke(lines, [(256, 478), (240, 500), (225, 495)], width=4, passes=1)
    s.stroke(lines, [(256, 478), (272, 500), (288, 495)], width=4, passes=1)
    pink = s.layer("#e98aa5")
    s.scribble_fill(pink, [(244, 462), (268, 462), (256, 474)], spacing=4)
    return s


def drawing_hearts():
    s = Sheet(4)
    colours = ["#e23b4e", "#f07aa0", "#b33a8c", "#e23b4e", "#ff9d7a", "#c9478f", "#e86a7d"]
    for i in range(9):
        cx = s.rng.uniform(90, CELL_W - 90)
        cy = 120 + i * 100 + s.rng.uniform(-20, 20)
        size = s.rng.uniform(45, 90)
        pts = []
        for k in range(40):
            t = k / 40 * math.tau
            x = 16 * math.sin(t) ** 3
            y = -(13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t))
            pts.append((cx + x * size / 16, cy + y * size / 16))
        layer = s.layer(colours[i % len(colours)])
        s.stroke(layer, pts + [pts[0]], width=7)
        if i % 3 != 2:
            s.scribble_fill(layer, pts, spacing=9)
    return s


def drawing_rainbow():
    s = Sheet(5)
    bands = ["#e23b3b", "#f28b2a", "#f2d42a", "#4fa43b", "#3b7fd0", "#7a4bb5"]
    for i, colour in enumerate(bands):
        layer = s.layer(colour)
        r = 230 - i * 26
        s.stroke(layer, s.ellipse_path(256, 620, r, r * 1.05, 40, math.pi, math.tau), width=24, passes=2, amount=3)
    clouds = s.layer("#9cb3c9", waxy=False)
    for cx in (60, 452):
        for dx, dy, r in ((-30, 0, 40), (10, -20, 46), (45, 4, 38)):
            s.stroke(clouds, s.ellipse_path(cx + dx, 620 + dy, r, r * 0.8, 20), width=6)
    rain = s.layer("#5d8fd0")
    for k in range(18):
        x = s.rng.uniform(20, CELL_W - 20)
        y = s.rng.uniform(700, 980)
        s.stroke(rain, [(x, y), (x - 6, y + 26)], width=5, passes=1)
    return s


def drawing_family():
    s = Sheet(6)
    ground = s.layer("#56a03e")
    s.stroke(ground, [(0, 930), (CELL_W, 920)], width=26, passes=2, amount=5)
    ink = s.layer("#2b2420")
    # Daddy, tall; Mummy with long hair and a triangle dress; and her, in the middle, holding both
    # their hands. Stick figures, the way she drew them when she was small.
    figures = [(110, 360, 1.18, False), (256, 560, 0.82, True), (400, 410, 1.0, True)]
    for x, top, k, dress in figures:
        head = 46 * k
        s.stroke(ink, s.ellipse_path(x, top + head, head, head, 22), width=6)
        neck = top + head * 2
        s.stroke(ink, [(x, neck), (x, neck + 170 * k)], width=6)
        s.stroke(ink, [(x, neck + 170 * k), (x - 50 * k, 915)], width=6)
        s.stroke(ink, [(x, neck + 170 * k), (x + 50 * k, 915)], width=6)
        s.stroke(ink, [(x - 70, neck + 60 * k), (x + 70, neck + 60 * k)], width=6)
        s.stroke(ink, [(x - 16 * k, top + head * 0.9), (x - 10 * k, top + head * 0.9)], width=6, passes=1)
        s.stroke(ink, [(x + 10 * k, top + head * 0.9), (x + 16 * k, top + head * 0.9)], width=6, passes=1)
        s.stroke(ink, s.ellipse_path(x, top + head * 1.1, head * 0.5, head * 0.45, 12, 0.3, math.pi - 0.3), width=5, passes=1)
        if dress:
            frock = s.layer("#e2508a" if k < 0.9 else "#4f86d6")
            tri = [(x, neck + 10), (x - 70 * k, neck + 190 * k), (x + 70 * k, neck + 190 * k)]
            s.stroke(frock, tri + [tri[0]], width=7)
            s.scribble_fill(frock, tri, spacing=10)
            hair = s.layer("#8a5a2b" if k > 0.9 else "#e0b33a")
            for side in (-1, 1):
                s.stroke(hair, [(x + side * head * 0.8, top + head * 0.5), (x + side * head * 1.15, top + head * 2.6)], width=10)
            s.stroke(hair, s.ellipse_path(x, top + head, head * 1.02, head * 1.02, 16, math.pi * 1.05, math.tau - 0.05), width=12)
        else:
            shirt = s.layer("#3f8f5a")
            body = [(x - 30, neck + 10), (x + 30, neck + 10), (x + 30, neck + 150 * k), (x - 30, neck + 150 * k)]
            s.scribble_fill(shirt, body, spacing=9)
    sun = s.layer("#f6c21c")
    s.scribble_fill(sun, s.ellipse_path(440, 110, 55, 55, 18)[:-1], spacing=7)
    hearts = s.layer("#e23b4e")
    for hx, hy in ((180, 260), (330, 300)):
        pts = []
        for k in range(30):
            t = k / 30 * math.tau
            pts.append((hx + 16 * math.sin(t) ** 3 * 2.2, hy - (13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)) * 2.2))
        s.scribble_fill(hearts, pts, spacing=6)
    return s


def drawing_sketch():
    """The one on the desk: this house, in pencil, from the lawn. Twelve, not six: perspective,
    construction lines, hatching — and only the left half finished."""
    s = Sheet(7, paper="#f3ecdc")
    pencil = s.layer("#4a4744", waxy=False)
    faint = s.layer("#8d8a86", waxy=False)
    # Construction: a horizon and two vanishing lines, light.
    s.stroke(faint, [(0, 700), (CELL_W, 690)], width=2, passes=1, amount=0.6)
    s.stroke(faint, [(60, 420), (500, 520)], width=2, passes=1, amount=0.6)
    s.stroke(faint, [(60, 780), (500, 735)], width=2, passes=1, amount=0.6)
    # The house: a gable front, the long side running away.
    outline = [(60, 780), (60, 430), (190, 330), (320, 460), (320, 770)]
    s.stroke(pencil, outline, width=3, passes=2, amount=0.8)
    s.stroke(pencil, [(60, 430), (320, 460)], width=3, passes=1, amount=0.8)
    s.stroke(pencil, [(190, 330), (470, 410), (480, 500)], width=3, passes=2, amount=0.8)
    s.stroke(faint, [(320, 770), (470, 740), (480, 500)], width=2, passes=1, amount=0.8)
    for wx in (95, 205):
        for wy in (500, 630):
            s.stroke(pencil, [(wx, wy), (wx + 55, wy + 3), (wx + 55, wy + 85), (wx, wy + 82), (wx, wy)], width=3, passes=1, amount=0.6)
            s.stroke(pencil, [(wx + 27, wy), (wx + 27, wy + 84)], width=2, passes=1, amount=0.5)
    # The hatching, on the left half only: the shadow side of the gable and the roof. It stops.
    for t in range(0, 230, 7):
        s.stroke(pencil, [(62 + t * 0.45, 440 + t * 1.4), (62 + t * 0.45 + 40, 440 + t * 1.4 - 60)], width=2, passes=1, amount=0.5)
    for t in range(0, 120, 6):
        s.stroke(pencil, [(70 + t, 425 - t * 0.75), (70 + t + 30, 425 - t * 0.75 + 12)], width=2, passes=1, amount=0.5)
    # A tree beside it, the trunk drawn and the crown only blocked in.
    s.stroke(pencil, [(400, 790), (405, 650), (395, 600)], width=4, passes=2, amount=0.8)
    s.stroke(faint, s.ellipse_path(405, 560, 80, 70, 24), width=2, passes=1, amount=2.0)
    # And an eraser smudge where something was tried and taken out.
    smudge = s.layer("#b9b3aa", waxy=False)
    s.stroke(smudge, [(330, 230), (380, 240), (420, 225)], width=36, passes=1, amount=4)
    return s


def drawing_sea():
    s = Sheet(8)
    sky = s.layer("#86b7e6")
    s.scribble_fill(sky, [(0, 0), (CELL_W, 0), (CELL_W, 560), (0, 580)], spacing=16, width=5, angle=1.47)
    sea = s.layer("#2f6fb8")
    s.scribble_fill(sea, [(0, 580), (CELL_W, 560), (CELL_W, 860), (0, 880)], spacing=8, angle=1.52)
    for k in range(7):
        y = 600 + k * 38
        s.stroke(sea, [(x, y + 10 * math.sin(x * 0.05 + k)) for x in range(0, CELL_W + 1, 16)], width=4, passes=1)
    sand = s.layer("#e7c26b")
    s.scribble_fill(sand, [(0, 880), (CELL_W, 860), (CELL_W, 1020), (0, 1020)], spacing=8)
    boat = s.layer("#c93a3a")
    hull = [(180, 590), (360, 590), (330, 640), (210, 640)]
    s.stroke(boat, hull + [hull[0]], width=7)
    s.scribble_fill(boat, hull, spacing=6)
    sail = s.layer("#f3dd7a")
    s.scribble_fill(sail, [(272, 580), (272, 405), (346, 568)], spacing=7)
    ink = s.layer("#3a3330")
    s.stroke(ink, [(270, 590), (270, 400), (350, 570), (270, 575)], width=5)
    for gx, gy in ((110, 230), (160, 200), (390, 260)):
        s.stroke(ink, [(gx - 22, gy), (gx, gy + 12), (gx + 22, gy)], width=4, passes=1)
    sun = s.layer("#f6c21c")
    s.scribble_fill(sun, s.ellipse_path(400, 110, 55, 55, 18)[:-1], spacing=7)
    return s


def build_drawings():
    sheets = [drawing_flowers, drawing_house, drawing_cat, drawing_hearts,
              drawing_rainbow, drawing_family, drawing_sketch, drawing_sea]
    atlas = np.ones((SIZE, SIZE, 3), dtype=np.float32) * hex_linear("#efe6d2")
    for i, make in enumerate(sheets):
        colour = make().render(age=0.0 if make is drawing_sketch else 0.5)
        col, row = i % 4, i // 4
        atlas[row * CELL_H:row * CELL_H + PAGE_H, col * CELL_W:(col + 1) * CELL_W] = colour
    save_srgb(atlas, "child_drawings_Diffuse.png")
    flat = np.zeros((64, 64, 3), dtype=np.uint8)
    flat[...] = (128, 128, 255)
    Image.fromarray(flat, "RGB").save(os.path.join(TEXTURES, "child_drawings_nor_dx.png"))
    arm = np.zeros((64, 64, 3), dtype=np.uint8)
    arm[...] = (255, 222, 0)  # AO 1, roughness ~0.87, no metal: matt cartridge paper
    Image.fromarray(arm, "RGB").save(os.path.join(TEXTURES, "child_drawings_arm.png"))
    return atlas


def main():
    os.makedirs(PREVIEW, exist_ok=True)
    wallpaper = build_wallpaper()
    fabric = build_fabric()
    drawings = build_drawings()
    # Previews in display space, with the C++ tints roughly applied so the value is the room's.
    for name, image, tint in (("wallpaper", wallpaper, 1.0), ("fabric", fabric, 1.0), ("drawings", drawings, 1.0)):
        out = (linear_to_srgb(image * tint) * 255).astype(np.uint8)
        Image.fromarray(out, "RGB").resize((1024, 1024), Image.LANCZOS).save(os.path.join(PREVIEW, name + ".png"))
    print("Wrote nursery_wallpaper, floral_fabric, child_drawings to", TEXTURES)


if __name__ == "__main__":
    main()
