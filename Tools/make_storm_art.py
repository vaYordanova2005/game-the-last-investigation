#!/usr/bin/env python3
"""
Bakes everything the storm outside the windows is drawn from.

    python Tools/make_storm_art.py [--only clouds,bolts,moon,foliage,ring]

Into Art/Source/Generated (imported by Tools/build_art.py -ArtStage=storm):

    storm_clouds.png   2048, tileable both ways, linear. Three noise fields the sky material lays
                       out as a cloud deck and drifts: R the big masses, G the fine break-up of their
                       edges, B a second, independent set of masses for the layer behind.
    storm_bolts.png    2048, a 2 x 2 atlas of lightning channels, linear. R is the channel itself, G
                       the glow of the air round it. Every cell's strike starts at (0.5, 0.25) of the
                       cell: cells 0-2 come down out of the cloud to the bottom edge, cell 3 is a
                       crawler that never leaves the cloud. BOLT_ORIGIN is shared with the material.
    storm_moon.png     256, linear grey: the moon's face (maria, a few rayed craters, limb darkening).
                       The disc is cut by the sky material, not by this map.
    forest_foliage.png 2048 RGBA, sRGB: what the trees of Tools/make_forest.py are leafed with.
                       FOLIAGE below gives the cells and is shared with that script.
    forest_ring.png    4096 x 512 RGBA, sRGB, tileable across: the forest's far wall, a run of pines
                       and broadleaves in silhouette, wrapped round the house beyond the modelled trees.

Why baked: a lightning channel is a fractal walk out of one point and a spruce bough is a few
thousand needles on a stem. A generator lays either out in a page; a material graph cannot at all
(the glass crack's lesson, 09-22). Previews of everything go to Saved/StormPreview/.
"""

import argparse
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "Art", "Source", "Generated")
PREVIEW = os.path.join(ROOT, "Saved", "StormPreview")

# Where in a bolt cell the strike starts, as a fraction of the cell (x, y from the top).
# Shared with the sky material in Tools/build_art.py: build_sky_master.
BOLT_ORIGIN = (0.5, 0.25)

# The foliage atlas: name -> (u0, v0, u1, v1), v from the top. Each bough or spray grows from the
# middle of its cell's bottom edge towards the top. Shared with Tools/make_forest.py.
FOLIAGE = {
    "spruce": (0.0, 0.0, 0.5, 0.5),
    "pine": (0.5, 0.0, 1.0, 0.5),
    "leaves": (0.0, 0.5, 0.5, 1.0),
    "twigs": (0.5, 0.5, 1.0, 0.75),
    "bark": (0.5, 0.75, 1.0, 1.0),
}


def save(name, array, mode=None):
    os.makedirs(OUT, exist_ok=True)
    image = Image.fromarray(np.clip(array * 255.0 + 0.5, 0, 255).astype(np.uint8), mode)
    image.save(os.path.join(OUT, name))
    print("wrote", name, image.size)
    return image


def save_preview(name, image):
    os.makedirs(PREVIEW, exist_ok=True)
    image.save(os.path.join(PREVIEW, name))


def tile_fbm(n, beta, seed, low_cut=0.0, stretch=(1.0, 1.0), m=None):
    """Periodic noise with a power-law spectrum, 0..1. low_cut removes features wider than n / low_cut."""
    m = m or n
    rng = np.random.default_rng(seed)
    ky = np.fft.fftfreq(m)[:, None] * m * stretch[1]
    kx = np.fft.fftfreq(n)[None, :] * n * stretch[0]
    k = np.sqrt(kx * kx + ky * ky)
    k[0, 0] = 1.0
    amplitude = k ** (-beta)
    if low_cut > 0.0:
        amplitude *= 1.0 - np.exp(-(k / low_cut) ** 2)
    amplitude[0, 0] = 0.0
    phase = rng.uniform(0.0, 2.0 * math.pi, (m, n))
    field = np.fft.ifft2(amplitude * np.exp(1j * phase)).real
    lo, hi = np.percentile(field, 0.5), np.percentile(field, 99.5)
    return np.clip((field - lo) / (hi - lo), 0.0, 1.0)


# --------------------------------------------------------------------------------------------
# Clouds
# --------------------------------------------------------------------------------------------

def make_clouds():
    n = 2048
    # Billows: the absolute value folds the field into rounded lobes with creases between them,
    # which is the underside of a cumulus deck; plain fbm is smoke.
    big = tile_fbm(n, 2.5, 11, low_cut=3.0)
    lobes = 1.0 - np.abs(tile_fbm(n, 2.3, 12, low_cut=6.0) * 2.0 - 1.0)
    masses = np.clip(big * 0.72 + lobes * 0.38 - 0.05, 0.0, 1.0)
    detail = tile_fbm(n, 1.9, 13, low_cut=14.0)
    behind = np.clip(tile_fbm(n, 2.6, 14, low_cut=2.0) * 0.8 + (1.0 - np.abs(tile_fbm(n, 2.2, 15, low_cut=5.0) * 2.0 - 1.0)) * 0.3, 0.0, 1.0)
    image = save("storm_clouds.png", np.dstack([masses, detail, behind]), "RGB")

    # A flat look at the deck as the material shades it: thin edges bright, bellies dark.
    thickness = np.clip(masses * 1.5 + (detail - 0.5) * 0.3 - 0.25, 0.0, 1.0)
    lit = np.exp(-3.2 * thickness)[..., None] * np.array([0.55, 0.66, 0.9]) + (1.0 - np.exp(-3.2 * thickness))[..., None] * np.array([0.05, 0.06, 0.09])
    save_preview("clouds_shaded.png", Image.fromarray((np.clip(lit, 0, 1) ** (1 / 2.2) * 255).astype(np.uint8)).resize((1024, 1024)))
    return image


# --------------------------------------------------------------------------------------------
# Lightning
# --------------------------------------------------------------------------------------------

def fractal_path(rng, a, b, roughness=0.2, depth=7):
    """Midpoint displacement between two points: jagged at every scale, as a channel is."""
    points = [np.array(a, dtype=np.float64), np.array(b, dtype=np.float64)]
    for level in range(depth):
        out = [points[0]]
        for p, q in zip(points, points[1:]):
            d = q - p
            length = np.hypot(*d)
            normal = np.array([-d[1], d[0]]) / max(length, 1e-6)
            # Mostly small kinks and now and then a big one: a channel steps, it does not wave.
            kick = rng.normal(0.0, roughness) * length * (1.9 if rng.random() < 0.18 else 0.8)
            out.append((p + q) * 0.5 + normal * kick)
            out.append(q)
        points = out
    return points


def draw_channel(layers, points, width, brightness, taper=0.45):
    """One channel into the core layer; width in final pixels at the head, tapering to the tail."""
    core, scale = layers
    draw = ImageDraw.Draw(core)
    count = len(points) - 1
    for i in range(count):
        t = i / max(count - 1, 1)
        w = width * (1.0 - t * (1.0 - taper)) * scale
        value = int(255 * min(1.0, brightness * (1.0 - 0.35 * t)))
        p, q = points[i], points[i + 1]
        box = [p[0] * scale, p[1] * scale, q[0] * scale, q[1] * scale]
        # Screen, not overwrite: a faint branch crossing the trunk must not cut a notch in it.
        draw.line(box, fill=value, width=max(1, int(round(w))))


def grow_bolt(rng, layers, start, heading, length, width, brightness, depth, cell):
    """A channel from `start` along `heading` (radians, 0 = down), and the branches off it."""
    end = (start[0] + math.sin(heading) * length, start[1] + math.cos(heading) * length)
    points = fractal_path(rng, start, end, roughness=0.17 if depth == 0 else 0.22, depth=7 if length > cell * 0.25 else 5)
    draw_channel(layers, points, width, brightness)
    if depth >= 3:
        return
    # Branches leave on the downhill side at twenty to fifty degrees, most of them from the upper
    # two thirds; each is thinner and dimmer and dies well short of where the trunk goes.
    count = rng.integers(3, 7) if depth == 0 else rng.integers(0, 4 - depth)
    for _ in range(count):
        at = rng.uniform(0.08, 0.8)
        origin = points[int(at * (len(points) - 1))]
        turn = math.radians(rng.uniform(18.0, 52.0)) * (1 if rng.random() < 0.5 else -1)
        grow_bolt(rng, layers, origin, heading + turn, length * (1.0 - at) * rng.uniform(0.35, 0.8),
                  max(1.0, width * 0.5), brightness * rng.uniform(0.3, 0.6), depth + 1, cell)


def make_bolts():
    cell = 1024
    scale = 2  # supersampling
    atlas = np.zeros((cell * 2, cell * 2, 3), dtype=np.float32)
    origin = (BOLT_ORIGIN[0] * cell, BOLT_ORIGIN[1] * cell)
    for index in range(4):
        rng = np.random.default_rng(700 + index * 31)
        core = Image.new("L", (cell * scale, cell * scale), 0)
        layers = (core, scale)
        if index < 3:
            # Cloud to ground: off the vertical by a few degrees, right down through the bottom edge.
            lean = math.radians(rng.uniform(-14.0, 14.0))
            grow_bolt(rng, layers, origin, lean, (cell - origin[1]) * 1.06 / math.cos(lean), 3.2, 1.0, 0, cell)
            # And what the strike leaves in the cloud over it: a few short feeders going up and out.
            for _ in range(int(rng.integers(2, 5))):
                grow_bolt(rng, layers, origin, math.radians(rng.uniform(110.0, 250.0)), cell * rng.uniform(0.08, 0.2),
                          1.4, rng.uniform(0.2, 0.4), 2, cell)
        else:
            # A crawler: long arms running out sideways under the cloud base, forking as they go.
            for arm in range(5):
                heading = math.radians(rng.choice([80.0, 100.0, -80.0, -100.0, 60.0, -65.0]) + rng.uniform(-14.0, 14.0))
                grow_bolt(rng, layers, origin, heading, cell * rng.uniform(0.28, 0.46), 2.2, rng.uniform(0.55, 0.95), 1, cell)
        core = core.resize((cell, cell), Image.LANCZOS)
        core_f = np.asarray(core, dtype=np.float32) / 255.0
        # The glow is the channel blurred at three scales: tight round the core, and a wide breath
        # of lit rain. Weighted by the core's own brightness, so faint branches glow faintly.
        glow = np.zeros_like(core_f)
        for radius, weight in ((3, 0.9), (12, 0.6), (40, 0.5)):
            glow += np.asarray(core.filter(ImageFilter.GaussianBlur(radius)), dtype=np.float32) / 255.0 * weight * radius * 0.45
        glow = np.clip(glow, 0.0, 1.0)
        # Nothing may touch the sides or the top of a cell: the next cell is the next strike.
        yy, xx = np.mgrid[0:cell, 0:cell].astype(np.float32) / cell
        edge = np.clip(np.minimum(np.minimum(xx, 1.0 - xx), yy) / 0.04, 0.0, 1.0)
        y0, x0 = (index // 2) * cell, (index % 2) * cell
        atlas[y0:y0 + cell, x0:x0 + cell, 0] = core_f * edge
        atlas[y0:y0 + cell, x0:x0 + cell, 1] = glow * edge
    image = save("storm_bolts.png", atlas, "RGB")
    look = np.clip(atlas[..., 0:1] * np.array([1.0, 1.0, 1.0]) + atlas[..., 1:2] * np.array([0.35, 0.45, 0.8]), 0, 1)
    save_preview("bolts.png", Image.fromarray((look * 255).astype(np.uint8)).resize((1024, 1024), Image.LANCZOS))
    return image


# --------------------------------------------------------------------------------------------
# Moon
# --------------------------------------------------------------------------------------------

def make_moon():
    n = 256
    rng = np.random.default_rng(5)
    yy, xx = (np.mgrid[0:n, 0:n].astype(np.float32) + 0.5) / n * 2.0 - 1.0
    r = np.sqrt(xx * xx + yy * yy)
    low = tile_fbm(n, 2.3, 21, low_cut=1.5)
    mid = tile_fbm(n, 1.6, 22, low_cut=5.0)
    # Maria: dark plains gathered on one side of the face, as the near side's are.
    bias = np.clip(0.55 - (xx * 0.35 - yy * 0.5), 0.0, 1.0)
    seas = np.clip((low * 0.7 + mid * 0.3) * 1.35 * bias - 0.28, 0.0, 1.0)
    seas = np.clip(seas * 3.0, 0.0, 1.0)
    face = 0.86 - 0.42 * seas + (mid - 0.5) * 0.14
    # A few bright rayed craters.
    for _ in range(5):
        cx, cy = rng.uniform(-0.6, 0.6, 2)
        d = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
        angle = np.arctan2(yy - cy, xx - cx)
        rays = 0.5 + 0.5 * np.cos(angle * rng.integers(7, 12) + rng.uniform(0, 6))
        face += 0.22 * np.exp(-(d / 0.035) ** 2) + 0.09 * rays * np.exp(-d / 0.16)
    face *= 0.72 + 0.28 * np.sqrt(np.clip(1.0 - r * r, 0.0, 1.0))  # limb darkening
    image = save("storm_moon.png", np.clip(face, 0, 1), "L")
    save_preview("moon.png", image)
    return image


# --------------------------------------------------------------------------------------------
# Foliage
# --------------------------------------------------------------------------------------------

class Canvas:
    """An RGBA picture drawn supersampled; colours are sRGB 0..255 tuples."""

    def __init__(self, width, height, ground, scale=2):
        self.scale = scale
        self.size = (width, height)
        self.rgb = Image.new("RGB", (width * scale, height * scale), ground)
        self.alpha = Image.new("L", (width * scale, height * scale), 0)
        self.draw_rgb = ImageDraw.Draw(self.rgb)
        self.draw_alpha = ImageDraw.Draw(self.alpha)

    def line(self, p, q, colour, width):
        s = self.scale
        box = [p[0] * s, p[1] * s, q[0] * s, q[1] * s]
        w = max(1, int(round(width * s)))
        self.draw_rgb.line(box, fill=colour, width=w)
        self.draw_alpha.line(box, fill=255, width=w)

    def polygon(self, points, colour):
        s = self.scale
        pts = [(x * s, y * s) for x, y in points]
        self.draw_rgb.polygon(pts, fill=colour)
        self.draw_alpha.polygon(pts, fill=255)

    def result(self):
        rgb = np.asarray(self.rgb.resize(self.size, Image.LANCZOS), dtype=np.float32) / 255.0
        alpha = np.asarray(self.alpha.resize(self.size, Image.LANCZOS), dtype=np.float32) / 255.0
        return np.dstack([rgb, alpha])


def shade(rng, base, spread=0.25):
    k = 1.0 + rng.uniform(-spread, spread)
    return tuple(int(np.clip(c * k + rng.uniform(-6, 6), 0, 255)) for c in base)


NEEDLE = (52, 84, 50)
PINE_NEEDLE = (62, 92, 58)
LEAF = (70, 98, 44)
TWIG = (58, 46, 36)
GROUND = (46, 66, 40)  # what shows under the alpha's edge when a mip blurs it: leaf colour, not black


def needles_along(canvas, rng, p, q, length, colour, step=2.2, width=1.3, angle=52.0):
    """Short needles down both sides of a twig, raked towards its tip."""
    d = np.array(q) - np.array(p)
    span = np.hypot(*d)
    if span < 1e-3:
        return
    heading = math.atan2(d[1], d[0])
    for i in range(int(span / step)):
        t = (i + rng.uniform(0, 1)) * step / span
        at = np.array(p) + d * min(t, 1.0)
        for side in (-1, 1):
            a = heading + side * math.radians(angle + rng.uniform(-14, 14))
            l = length * rng.uniform(0.7, 1.15)
            canvas.line(at, (at[0] + math.cos(a) * l, at[1] + math.sin(a) * l), shade(rng, colour), width)


def spruce_bough(canvas, rng, x0, y0, size):
    """A spruce bough from above: a stem, side twigs raked forward and shortening to the tip."""
    base = np.array([x0 + size * 0.5, y0 + size * 0.985])
    tip = np.array([x0 + size * (0.5 + rng.uniform(-0.03, 0.03)), y0 + size * 0.03])
    stem = fractal_path(rng, base, tip, roughness=0.035, depth=4)
    count = 34
    for i in range(count):
        t = (i + 0.5) / count
        at = np.array(stem[int(t * (len(stem) - 1))])
        # Widest a third of the way up, narrowing to the tip and to the base.
        reach = size * 0.44 * (math.sin(min(t / 0.33, 1.0) * math.pi * 0.5) ** 0.7) * (1.0 - t) ** 0.62 * rng.uniform(0.75, 1.05) + size * 0.02
        for side in (-1, 1):
            if rng.random() < 0.06:
                continue  # a twig the bough has lost
            a = -math.pi / 2 + side * math.radians(58 + rng.uniform(-9, 9) - 18 * t)
            end = at + np.array([math.cos(a), math.sin(a)]) * reach
            twig = fractal_path(rng, at, end, roughness=0.05, depth=3)
            for p, q in zip(twig, twig[1:]):
                canvas.line(p, q, shade(rng, TWIG, 0.1), 2.2)
            # Twiglets off the twig, then needles over all of it.
            for j in range(int(reach / (size * 0.035))):
                u = (j + 0.6) / max(reach / (size * 0.035), 1)
                o = np.array(twig[int(u * (len(twig) - 1))])
                for s2 in (-1, 1):
                    b = a + s2 * math.radians(42 + rng.uniform(-8, 8))
                    e2 = o + np.array([math.cos(b), math.sin(b)]) * reach * 0.3 * (1.0 - u * 0.6)
                    canvas.line(o, e2, shade(rng, TWIG, 0.1), 1.4)
                    needles_along(canvas, rng, o, e2, size * 0.014, NEEDLE)
            for p, q in zip(twig, twig[1:]):
                needles_along(canvas, rng, p, q, size * 0.016, NEEDLE)
    for p, q in zip(stem, stem[1:]):
        canvas.line(p, q, TWIG, 5.0)
        needles_along(canvas, rng, p, q, size * 0.018, NEEDLE)


def pine_spray(canvas, rng, x0, y0, size):
    """Scots pine: bare forking twigs, each ending in bottle-brush tufts of long needles."""
    def tuft(at, heading, radius):
        for _ in range(110):
            a = heading + rng.normal(0.0, 0.85)
            l = radius * rng.uniform(0.55, 1.0)
            canvas.line(at, (at[0] + math.cos(a) * l, at[1] + math.sin(a) * l), shade(rng, PINE_NEEDLE), 1.5)

    def twig(at, heading, length, depth):
        end = at + np.array([math.cos(heading), math.sin(heading)]) * length
        path = fractal_path(rng, at, end, roughness=0.07, depth=3)
        for p, q in zip(path, path[1:]):
            canvas.line(p, q, shade(rng, TWIG, 0.1), 5.5 - depth * 1.3)
        if depth >= 2:
            tuft(end, heading, size * 0.085)
            tuft(np.array(path[len(path) * 2 // 3]), heading, size * 0.07)
            return
        for _ in range(int(rng.integers(2, 4))):
            origin = np.array(path[int(rng.uniform(0.45, 1.0) * (len(path) - 1))])
            twig(origin, heading + math.radians(rng.uniform(-48, 48)), length * rng.uniform(0.5, 0.72), depth + 1)
        tuft(end, heading, size * 0.08)

    base = np.array([x0 + size * 0.5, y0 + size * 0.985])
    for lean in (-34, -12, 10, 33):
        twig(base, -math.pi / 2 + math.radians(lean + rng.uniform(-6, 6)), size * rng.uniform(0.36, 0.46), 0)


def leaf(canvas, rng, at, heading, length, colour):
    """One pointed leaf as a six-sided blade, with a pale midrib."""
    width = length * rng.uniform(0.36, 0.5)
    f = np.array([math.cos(heading), math.sin(heading)])
    s = np.array([-f[1], f[0]])
    pts = [at, at + f * length * 0.3 + s * width * 0.5, at + f * length * 0.7 + s * width * 0.36,
           at + f * length, at + f * length * 0.7 - s * width * 0.36, at + f * length * 0.3 - s * width * 0.5]
    tone = shade(rng, colour, 0.3)
    canvas.polygon([tuple(p) for p in pts], tone)
    canvas.line(at, at + f * length * 0.9, tuple(min(255, int(c * 1.25)) for c in tone), 0.8)


def leaf_spray(canvas, rng, x0, y0, width, height, leaves_per_node, spread=62):
    """A broadleaf branch end: forking twigs, leaves at every node, gaps left between."""
    def twig(at, heading, length, depth):
        end = at + np.array([math.cos(heading), math.sin(heading)]) * length
        path = fractal_path(rng, at, end, roughness=0.09, depth=3)
        for p, q in zip(path, path[1:]):
            canvas.line(p, q, shade(rng, TWIG, 0.12), max(1.2, 6.0 - depth * 1.6))
        nodes = 3 if depth < 2 else 5
        for i in range(nodes):
            o = np.array(path[int((i + 1) / nodes * (len(path) - 1))])
            for _ in range(leaves_per_node if depth >= 1 else 0):
                leaf(canvas, rng, o, heading + math.radians(rng.uniform(-95, 95)), height * rng.uniform(0.045, 0.075), LEAF)
        if depth >= 3:
            return
        for _ in range(int(rng.integers(2, 4))):
            origin = np.array(path[int(rng.uniform(0.3, 1.0) * (len(path) - 1))])
            twig(origin, heading + math.radians(rng.uniform(-spread, spread)), length * rng.uniform(0.5, 0.75), depth + 1)

    base = np.array([x0 + width * 0.5, y0 + height * 0.985])
    wide = width > height * 1.2
    fan = (-66, -40, -14, 14, 40, 66) if wide else (-30, -10, 10, 30)
    for lean in fan:
        twig(base, -math.pi / 2 + math.radians(lean + rng.uniform(-6, 6)), height * (rng.uniform(0.3, 0.4) if wide else rng.uniform(0.24, 0.3)), 0)


def bark(width, height):
    # Fissures run up the trunk: the noise is stretched along V and ridged.
    ridges = np.abs(tile_fbm(width, 1.5, 31, low_cut=10.0, stretch=(0.12, 1.0), m=height) * 2.0 - 1.0)
    plates = tile_fbm(width, 1.3, 32, low_cut=4.0, stretch=(0.3, 1.0), m=height)
    value = np.clip(0.35 + ridges * 0.75 + (plates - 0.5) * 0.5, 0.05, 1.0)
    colour = value[..., None] * np.array([0.36, 0.31, 0.26])
    return np.dstack([colour, np.ones((height, width), dtype=np.float32)])


def fix_edges(rgba, ground):
    """Leaf colour under every transparent pixel, so no mip carries the background into the edge."""
    out = rgba.copy()
    g = np.array(ground, dtype=np.float32) / 255.0
    a = out[..., 3:4]
    out[..., :3] = out[..., :3] * np.clip(a * 4.0, 0, 1) + g * (1.0 - np.clip(a * 4.0, 0, 1))
    return out


def make_foliage():
    n = 2048
    half = n // 2
    atlas = np.zeros((n, n, 4), dtype=np.float32)
    atlas[..., :3] = np.array(GROUND, dtype=np.float32) / 255.0

    def cell(name):
        u0, v0, u1, v1 = FOLIAGE[name]
        return int(u0 * n), int(v0 * n), int((u1 - u0) * n), int((v1 - v0) * n)

    jobs = (
        ("spruce", lambda c, r, w, h: spruce_bough(c, r, 0, 0, w)),
        ("pine", lambda c, r, w, h: pine_spray(c, r, 0, 0, w)),
        ("leaves", lambda c, r, w, h: leaf_spray(c, r, 0, 0, w, h, 5, spread=46)),
        ("twigs", lambda c, r, w, h: leaf_spray(c, r, 0, 0, w, h, 2, spread=50)),
    )
    for seed, (name, job) in enumerate(jobs):
        x, y, w, h = cell(name)
        canvas = Canvas(w, h, GROUND)
        job(canvas, np.random.default_rng(900 + seed), w, h)
        picture = fix_edges(canvas.result(), GROUND)
        # A clear margin: a card's UVs stop short of the cell, and nothing may bleed across.
        margin = 6
        picture[:margin, :, 3] = 0
        picture[:, :margin, 3] = 0
        picture[:, -margin:, 3] = 0
        atlas[y:y + h, x:x + w] = picture
    x, y, w, h = cell("bark")
    atlas[y:y + h, x:x + w] = bark(w, h)
    image = save("forest_foliage.png", atlas, "RGBA")
    backdrop = Image.new("RGBA", image.size, (150, 165, 190, 255))
    backdrop.alpha_composite(image)
    save_preview("foliage.png", backdrop.resize((1024, 1024), Image.LANCZOS))
    return image


# --------------------------------------------------------------------------------------------
# The far wall of the forest
# --------------------------------------------------------------------------------------------

def silhouette_conifer(canvas, rng, x, ground, height, colour):
    """A spruce side on: a trunk, and tier on tier of boughs that droop and lift at the tip."""
    top = ground - height
    canvas.line((x, ground), (x, top + height * 0.45), shade(rng, (30, 26, 22), 0.1), max(1.5, height * 0.018))
    canvas.line((x, top + height * 0.45), (x, top + height * 0.04), shade(rng, (30, 26, 22), 0.1), max(1.0, height * 0.007))
    tiers = int(height / 7.5)
    lean = rng.uniform(-0.04, 0.04)
    for i in range(tiers):
        t = (i + rng.uniform(0, 0.6)) / tiers          # 0 at the top
        y = top + height * (0.03 + 0.9 * t)
        reach = height * 0.2 * (t ** 0.75) * rng.uniform(0.7, 1.1) + height * 0.012
        if t > 0.86:
            reach *= 0.55                               # the lowest boughs are dying back
        for side in (-1, 1):
            if rng.random() < 0.08:
                continue
            droop = reach * rng.uniform(0.25, 0.5)
            pts = []
            for k in range(7):
                u = k / 6.0
                pts.append((x + lean * (ground - y) + side * reach * u, y + droop * math.sin(u * math.pi * 0.85) * 1.0 - droop * 0.35 * u * u))
            tone = shade(rng, colour, 0.22)
            for p, q in zip(pts, pts[1:]):
                canvas.line(p, q, tone, max(1.0, height * 0.006))
            # Hanging twiglets: what makes the tier ragged underneath.
            for k in range(int(reach / 1.6)):
                u = rng.uniform(0.1, 1.0)
                px = x + lean * (ground - y) + side * reach * u
                py = y + droop * math.sin(u * math.pi * 0.85) - droop * 0.35 * u * u
                l = rng.uniform(1.5, 5.5) * (1.0 - 0.4 * u) * height / 300.0 * 2.2
                canvas.line((px, py - 1), (px + rng.uniform(-1.2, 1.2), py + l), shade(rng, colour, 0.25), 1.6)


def silhouette_broadleaf(canvas, rng, x, ground, height, colour):
    """An oak or a beech side on: a forking skeleton, and leaf masses with sky between them."""
    def clump(cx, cy, radius):
        for _ in range(int(radius * radius * 0.55)):
            a = rng.uniform(0, 2 * math.pi)
            r = radius * math.sqrt(rng.uniform(0, 1))
            px, py = cx + math.cos(a) * r * 1.25, cy + math.sin(a) * r * 0.8
            s = rng.uniform(0.9, 2.2)
            canvas.polygon([(px - s, py), (px, py - s * 0.7), (px + s, py), (px, py + s * 0.7)], shade(rng, colour, 0.28))

    def limb(at, heading, length, width, depth):
        end = (at[0] + math.cos(heading) * length, at[1] + math.sin(heading) * length)
        path = fractal_path(rng, at, end, roughness=0.08, depth=3)
        for p, q in zip(path, path[1:]):
            canvas.line(p, q, shade(rng, (30, 26, 22), 0.1), max(1.0, width))
        if depth >= 3 or length < height * 0.07:
            clump(end[0], end[1], height * rng.uniform(0.05, 0.085))
            return
        for _ in range(int(rng.integers(2, 4))):
            origin = path[int(rng.uniform(0.5, 1.0) * (len(path) - 1))]
            limb(origin, heading + math.radians(rng.uniform(-50, 50)), length * rng.uniform(0.55, 0.78), width * 0.6, depth + 1)
        if depth >= 1:
            clump(end[0], end[1], height * rng.uniform(0.04, 0.07))

    limb((x, ground), -math.pi / 2 + rng.uniform(-0.06, 0.06), height * rng.uniform(0.36, 0.46), height * 0.03, 0)


def make_ring():
    width, height = 4096, 512
    canvas = Canvas(width, height, (20, 28, 20), scale=2)
    rng = np.random.default_rng(4242)
    ground = height - 2

    def plant(draw, x, h, colour):
        # Drawn three times, a strip's width apart, so whatever overhangs an end comes in at the other.
        state = rng.bit_generator.state
        for shift in (-width, 0, width):
            rng.bit_generator.state = state
            draw(canvas, rng, x + shift, ground, h, colour)

    # Two ranks: a darker, taller one behind and a nearer one in front of it.
    for rank, (count, low, high, tone) in enumerate(((46, 300, 500, 0.72), (40, 210, 400, 1.0))):
        for i in range(count):
            x = (i + rng.uniform(0.1, 0.9)) * width / count
            h = rng.uniform(low, high)
            if rng.random() < 0.6:
                plant(silhouette_conifer, x, h, tuple(int(c * tone) for c in (30, 46, 30)))
            else:
                plant(silhouette_broadleaf, x, h * 0.82, tuple(int(c * tone) for c in (38, 52, 28)))
    # Undergrowth: the foot of the wood is solid.
    for i in range(1400):
        x = rng.uniform(0, width)
        h = rng.uniform(8, 46)
        for shift in (-width, 0, width):
            canvas.polygon([(x + shift - h * 0.7, ground + 2), (x + shift, ground - h), (x + shift + h * 0.7, ground + 2)], shade(rng, (22, 32, 20), 0.2))
    picture = fix_edges(canvas.result(), (24, 34, 22))
    picture[-3:, :, 3] = 1.0
    image = save("forest_ring.png", picture, "RGBA")
    backdrop = Image.new("RGBA", image.size, (120, 140, 175, 255))
    backdrop.alpha_composite(image)
    save_preview("ring.png", backdrop.resize((2048, 256), Image.LANCZOS))
    return image


STAGES = {"clouds": make_clouds, "bolts": make_bolts, "moon": make_moon, "foliage": make_foliage, "ring": make_ring}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--only", default="")
    args = parser.parse_args()
    wanted = [s.strip() for s in args.only.split(",") if s.strip()] or list(STAGES)
    for name in wanted:
        STAGES[name]()


if __name__ == "__main__":
    main()
