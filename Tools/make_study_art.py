#!/usr/bin/env python3
"""
Bakes the study's pictures and its carpet into texture sets the art pipeline imports.

    python Tools/make_study_art.py

Writes into Art/Source/Textures (_Diffuse / _nor_dx / _arm):

    study_pictures  a 2048 atlas (PICTURES below, shared with Tools/make_study.py, whose frames,
                    boards and folders carry UVs into it):
        board     the case board on the west wall, 200 x 125: cork, and pinned to it a road map with
                  a ring drawn round one bend, photographs (a wet road at night, a car, this house,
                  a bridge, a man in a hat, two figures under an umbrella), cuttings from a paper,
                  index cards in his hand, and red thread from pin to pin into the ring.
        portrait  over the desk, behind his chair: a man in a black suit and a black hat, a burgundy
                  tie, a gold ring on the hand on the cane; the brim's shadow over his eyes, so the
                  face is a jaw and a mouth and nothing above them.
        map       the city map framed on the east wall: streets, a river, a park, the railway.
        folder    the front of a case file, buff card, a typed label and a red stamp.
        photo_a/b two prints loose on the desk, from the board's set.
    study_rug       the carpet under the desk, 400 x 280: a burgundy field, a black border with a gold
                    key fret, a gold medallion. Worn in front of the desk and dusty everywhere.

Nothing here can be read (the 09-30 decision): headlines and labels are bars of type, notes are a
hand. A preview of each goes to Saved/StudyPreview/.
"""

import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from make_cellar_art import SS, TEXTURES, age_paper, handwriting, hexrgb, noise
from make_library_art import crackle, normal_from_height, paint, portrait_head, save_set, soft_shape, to_u8, varnish

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PREVIEW = os.path.join(ROOT, "Saved", "StudyPreview")
SIZE = 2048

# name -> (x, y, w, h) in pixels of the 2048 atlas. Must stay in step with make_study.py: PICTURES.
PICTURES = {
    "board": (0, 0, 1280, 800),
    "portrait": (1280, 0, 512, 640),
    "map": (1280, 660, 768, 512),
    "folder": (0, 820, 384, 288),
    "photo_a": (420, 820, 192, 144),
    "photo_b": (640, 820, 192, 144),
}


def linear_mean(rgb):
    lin = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
    return lin.reshape(-1, 3).mean(axis=0)


def preview(name, rgb, size=1024):
    os.makedirs(PREVIEW, exist_ok=True)
    img = Image.fromarray(to_u8(rgb), "RGB")
    img.thumbnail((size, size), Image.LANCZOS)
    img.save(os.path.join(PREVIEW, name + ".png"))


# ---------------------------------------------------------------------------------------------
# Photographs: small black-and-white prints, each a scene in a few soft shapes.
# ---------------------------------------------------------------------------------------------

def print_border(grey, seed, border=6):
    """A gelatin print: the picture, a white border, a little silvering and a crease."""
    h, w = grey.shape
    rgb = np.repeat(grey[..., None], 3, axis=-1) * np.array([1.0, 0.98, 0.93])
    rgb *= (0.92 + 0.12 * noise(w, h, 30, seed, octaves=2))[..., None]
    out = np.ones((h + border * 2, w + border * 2, 3), dtype=np.float32) * np.array([0.86, 0.84, 0.78])
    out[border:border + h, border:border + w] = rgb
    return age_paper(out, seed + 1, foxing=0.6, damp=0.0, edge=0.6, dust=0.3)


def scene(kind, w, h, seed):
    """One photograph, greyscale in [0, 1]."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    rng = np.random.default_rng(seed)
    g = np.full((h, w), 0.12, dtype=np.float32)
    if kind == "road":
        # A wet road at night running away to a bend, two headlights' streaks on the tarmac, rain.
        sky = np.clip(0.22 - yy / h * 0.15, 0.05, 0.3)
        g = sky + 0.05 * noise(w, h, 6, seed)
        road = soft_shape(w, h, lambda d: d.polygon([(w * 0.15, h), (w * 0.85, h), (w * 0.56, h * 0.48), (w * 0.5, h * 0.48)], fill=255), 2)
        g = g * (1 - road) + road * (0.08 + 0.25 * np.exp(-((xx - w * 0.5) / (w * 0.08)) ** 2) * (yy / h))
        trees = soft_shape(w, h, lambda d: d.polygon([(0, h * 0.5), (w * 0.3, h * 0.3), (w * 0.45, h * 0.47), (w * 0.62, h * 0.32), (w, h * 0.45), (w, h * 0.55), (0, h * 0.6)], fill=255), 3)
        g = g * (1 - trees) + trees * 0.04
        for lx in (0.42, 0.58):
            streak = np.exp(-((xx - w * lx - (yy - h) * (lx - 0.5) * 0.6) / 2.0) ** 2) * np.clip((yy - h * 0.55) / (h * 0.45), 0, 1)
            g += streak * 0.5
        rain = (noise(w, h, 60, seed + 3, octaves=1) > 0.86).astype(np.float32)
        g += np.asarray(Image.fromarray((rain * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.6)), dtype=np.float32) / 255.0 * 0.15
    elif kind == "car":
        # A big saloon side-on in a garage's light, its front wing stove in.
        g = 0.35 + 0.15 * (yy / h) + 0.05 * noise(w, h, 5, seed)
        body = soft_shape(w, h, lambda d: (d.rounded_rectangle([w * 0.1, h * 0.45, w * 0.92, h * 0.75], radius=h * 0.08, fill=255),
                                             d.polygon([(w * 0.3, h * 0.46), (w * 0.38, h * 0.28), (w * 0.68, h * 0.28), (w * 0.78, h * 0.46)], fill=255)), 2)
        g = g * (1 - body) + body * (0.07 + 0.06 * np.clip(1 - (yy - h * 0.45) / (h * 0.3), 0, 1))
        glass = soft_shape(w, h, lambda d: d.polygon([(w * 0.36, h * 0.44), (w * 0.41, h * 0.31), (w * 0.66, h * 0.31), (w * 0.73, h * 0.44)], fill=255), 2)
        g = g * (1 - glass) + glass * 0.22
        for wx in (0.25, 0.78):
            wheel = soft_shape(w, h, lambda d, wx=wx: d.ellipse([w * wx - h * 0.12, h * 0.62, w * wx + h * 0.12, h * 0.86], fill=255), 1.5)
            g = g * (1 - wheel) + wheel * 0.03
        dent = soft_shape(w, h, lambda d: d.polygon([(w * 0.84, h * 0.48), (w * 0.93, h * 0.5), (w * 0.9, h * 0.66), (w * 0.8, h * 0.6)], fill=255), 3)
        g = g * (1 - dent * 0.6) + dent * 0.6 * 0.2
    elif kind == "house":
        # This house from the drive at dusk: the tall front, its gables, a lit window, bare trees.
        g = 0.42 - 0.2 * (yy / h) + 0.05 * noise(w, h, 5, seed)
        mass = soft_shape(w, h, lambda d: d.polygon([(w * 0.12, h * 0.9), (w * 0.12, h * 0.42), (w * 0.28, h * 0.2), (w * 0.44, h * 0.42), (w * 0.56, h * 0.42),
                                                     (w * 0.7, h * 0.16), (w * 0.86, h * 0.42), (w * 0.86, h * 0.9)], fill=255), 1.5)
        g = g * (1 - mass) + mass * 0.08
        for wx in (0.2, 0.3, 0.62, 0.72, 0.78):
            for wy in (0.5, 0.7):
                win = soft_shape(w, h, lambda d, wx=wx, wy=wy: d.rectangle([w * wx, h * wy, w * wx + w * 0.04, h * wy + h * 0.1], fill=255), 1)
                g = g * (1 - win) + win * (0.5 if (wx, wy) == (0.62, 0.5) else 0.15)
        ground = np.clip((yy - h * 0.88) / (h * 0.12), 0, 1)
        g = g * (1 - ground) + ground * 0.1
    elif kind == "bridge":
        # A stone bridge over a river in fog, the parapet running off out of the frame.
        g = 0.55 - 0.15 * (yy / h) + 0.06 * noise(w, h, 4, seed)
        span = soft_shape(w, h, lambda d: (d.rectangle([0, h * 0.4, w, h * 0.5], fill=255),
                                             d.chord([w * 0.15, h * 0.45, w * 0.55, h * 0.95], 180, 360, fill=0)), 2)
        g = g * (1 - span) + span * 0.18
        water = np.clip((yy - h * 0.7) / (h * 0.3), 0, 1)
        g = g * (1 - water) + water * (0.3 + 0.1 * np.sin(xx / 3.0 + yy / 1.5))
    elif kind == "man":
        # A man in a long coat and a hat under a street lamp, walking away.
        g = 0.1 + 0.4 * np.exp(-(((xx - w * 0.45) / (w * 0.35)) ** 2 + ((yy - h * 0.1) / (h * 0.6)) ** 2))
        coat = soft_shape(w, h, lambda d: (d.polygon([(w * 0.42, h * 0.35), (w * 0.55, h * 0.35), (w * 0.6, h * 0.9), (w * 0.37, h * 0.9)], fill=255),
                                             d.ellipse([w * 0.44, h * 0.2, w * 0.53, h * 0.34], fill=255),
                                             d.ellipse([w * 0.39, h * 0.2, w * 0.58, h * 0.25], fill=255)), 1.5)
        g = g * (1 - coat) + coat * 0.03
        lamp = soft_shape(w, h, lambda d: d.rectangle([w * 0.2, h * 0.05, w * 0.22, h], fill=255), 1)
        g = g * (1 - lamp) + lamp * 0.05
    elif kind == "figures":
        # Two figures at a kerb in the rain under one umbrella, a woman and a child, from behind.
        g = 0.25 + 0.1 * noise(w, h, 4, seed) + 0.1 * (yy / h)
        woman = soft_shape(w, h, lambda d: (d.polygon([(w * 0.4, h * 0.38), (w * 0.52, h * 0.38), (w * 0.56, h * 0.88), (w * 0.37, h * 0.88)], fill=255),
                                              d.ellipse([w * 0.42, h * 0.26, w * 0.5, h * 0.38], fill=255)), 2)
        child = soft_shape(w, h, lambda d: (d.polygon([(w * 0.58, h * 0.6), (w * 0.65, h * 0.6), (w * 0.67, h * 0.88), (w * 0.57, h * 0.88)], fill=255),
                                              d.ellipse([w * 0.585, h * 0.52, w * 0.64, h * 0.61], fill=255)), 2)
        brolly = soft_shape(w, h, lambda d: d.chord([w * 0.3, h * 0.12, w * 0.7, h * 0.4], 180, 360, fill=255), 2)
        for m in (woman, child, brolly):
            g = g * (1 - m) + m * 0.05
        rain = (noise(w, h, 50, seed + 4, octaves=1) > 0.85).astype(np.float32)
        g += rain * 0.08
        g = np.asarray(Image.fromarray(to_u8(g)).filter(ImageFilter.GaussianBlur(1.2)), dtype=np.float32) / 255.0
    g += (rng.random((h, w)).astype(np.float32) - 0.5) * 0.05
    return np.clip(g, 0, 1)


def clipping(w, h, seed):
    """A cutting from a newspaper: a headline in bars, columns of type, sometimes a photograph."""
    rng = np.random.default_rng(seed)
    img = Image.new("RGB", (w * SS, h * SS), hexrgb("#cfc5a8"))
    d = ImageDraw.Draw(img)
    ink = hexrgb("#24211d")
    y = 8 * SS
    cursor = 8 * SS
    while cursor < (w - 20) * SS:
        word = rng.uniform(18, 46) * SS
        d.rectangle([cursor, y, min(cursor + word, (w - 8) * SS), y + 9 * SS], fill=ink)
        cursor += word + 6 * SS
    cols = 2 if w > 110 else 1
    cw = (w - 16) * SS / cols
    for c in range(cols):
        yy = 26 * SS
        while yy < (h - 6) * SS:
            d.line([(8 * SS + c * cw + 2 * SS, yy), (8 * SS + (c + 1) * cw - 4 * SS, yy)], fill=hexrgb("#4a463f"), width=int(1.4 * SS))
            yy += 4 * SS
    img = img.resize((w, h), Image.LANCZOS)
    return age_paper(np.asarray(img, dtype=np.float32) / 255.0, seed + 1, foxing=0.8, damp=0.3, edge=1.0, dust=0.4)


def index_card(w, h, seed):
    rng = np.random.default_rng(seed)
    img = Image.new("RGB", (w * SS, h * SS), hexrgb("#d6ceb6"))
    d = ImageDraw.Draw(img)
    d.line([(0, 12 * SS), (w * SS, 12 * SS)], fill=hexrgb("#9a4a44"), width=SS)
    for k in range(1, h // 8):
        d.line([(0, (12 + k * 8) * SS), (w * SS, (12 + k * 8) * SS)], fill=hexrgb("#8ea2b0"), width=1)
    handwriting(d, 6 * SS, 10 * SS, (w - 6) * SS, 8 * SS, max(2, (h - 16) // 8), rng, hexrgb("#1f2433"), int(1.3 * SS), 2.2 * SS)
    img = img.resize((w, h), Image.LANCZOS)
    return age_paper(np.asarray(img, dtype=np.float32) / 255.0, seed + 1, foxing=0.6, damp=0.2, edge=0.8, dust=0.3)


def road_map(w, h, seed):
    """A road map torn from a book: lanes, a river, woods in hatching, and a ring round one bend."""
    rng = np.random.default_rng(seed)
    img = Image.new("RGB", (w * SS, h * SS), hexrgb("#d9cfb0"))
    d = ImageDraw.Draw(img)
    for _ in range(9):
        cx, cy = rng.uniform(0, w) * SS, rng.uniform(0, h) * SS
        r = rng.uniform(14, 40) * SS
        d.ellipse([cx - r, cy - r * 0.7, cx + r, cy + r * 0.7], fill=hexrgb("#b9c09a"))
    river = [(0, h * 0.7 * SS)]
    for k in range(1, 9):
        river.append((w * SS * k / 8, (h * 0.7 + math.sin(k * 1.3) * h * 0.12) * SS))
    d.line(river, fill=hexrgb("#7f9bb0"), width=5 * SS, joint="curve")
    for k in range(6):
        pts = [(rng.uniform(0, w) * SS, 0)]
        for t in range(1, 7):
            pts.append((pts[-1][0] + rng.uniform(-30, 30) * SS, h * SS * t / 6))
        d.line(pts, fill=hexrgb("#7a6c55"), width=SS, joint="curve")
    main = [(0, h * 0.3 * SS), (w * 0.3 * SS, h * 0.34 * SS), (w * 0.52 * SS, h * 0.5 * SS), (w * 0.6 * SS, h * 0.38 * SS), (w * SS, h * 0.42 * SS)]
    d.line(main, fill=hexrgb("#a1302a"), width=4 * SS, joint="curve")
    d.line(main, fill=hexrgb("#d9a24a"), width=2 * SS, joint="curve")
    cx, cy = w * 0.52 * SS, h * 0.5 * SS
    for k in range(3):
        r = (16 + k * 1.5) * SS
        d.ellipse([cx - r, cy - r * 0.8, cx + r, cy + r * 0.8], outline=hexrgb("#b0201c"), width=int(1.6 * SS))
    img = img.resize((w, h), Image.LANCZOS)
    rgb = age_paper(np.asarray(img, dtype=np.float32) / 255.0, seed + 1, foxing=0.8, damp=0.6, edge=1.0, dust=0.4)
    # Folded in quarters before it was pinned up.
    rgb[:, w // 2 - 1:w // 2 + 1] *= 0.82
    rgb[h // 2 - 1:h // 2 + 1] *= 0.82
    return rgb


def build_board():
    """The case board: everything laid onto cork at its place, the pins, then the thread."""
    W, H = PICTURES["board"][2], PICTURES["board"][3]
    cork = noise(W, H, 220, 801, octaves=2)
    grains = noise(W, H, 600, 802, octaves=1)
    rgb = np.array([0.42, 0.28, 0.16]) * (0.75 + 0.35 * cork)[..., None]
    rgb *= (0.85 + 0.25 * (grains > 0.6))[..., None]
    height = cork * 0.6 + grains * 0.4
    shadow_mask = np.zeros((H, W), dtype=np.float32)

    pins = []

    def lay(picture, x, y, angle):
        """Pastes a picture turned by angle, with a soft shadow under it, and returns its pin."""
        ph, pw, _ = picture.shape
        img = Image.fromarray(to_u8(picture), "RGB").convert("RGBA")
        img = img.rotate(angle, resample=Image.BICUBIC, expand=True)
        alpha = np.asarray(img.split()[3], dtype=np.float32) / 255.0
        col = np.asarray(img.convert("RGB"), dtype=np.float32) / 255.0
        h2, w2 = alpha.shape
        x0, y0 = int(x - w2 / 2), int(y - h2 / 2)
        sx0, sy0 = max(0, x0 + 4), max(0, y0 + 5)
        sh = np.asarray(Image.fromarray((alpha * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(5)), dtype=np.float32) / 255.0
        hh, ww = min(H - sy0, h2), min(W - sx0, w2)
        shadow_mask[sy0:sy0 + hh, sx0:sx0 + ww] = np.maximum(shadow_mask[sy0:sy0 + hh, sx0:sx0 + ww], sh[:hh, :ww])
        hh, ww = min(H - y0, h2), min(W - x0, w2)
        a = alpha[:hh, :ww, None]
        nonlocal rgb
        rgb[y0:y0 + hh, x0:x0 + ww] *= 1.0 - shadow_mask[y0:y0 + hh, x0:x0 + ww, None] * 0.0
        rgb[y0:y0 + hh, x0:x0 + ww] = rgb[y0:y0 + hh, x0:x0 + ww] * (1 - a) + col[:hh, :ww] * a
        height[y0:y0 + hh, x0:x0 + ww] = height[y0:y0 + hh, x0:x0 + ww] * (1 - a[..., 0]) + 0.9 * a[..., 0]
        pin = (x + math.sin(math.radians(angle)) * ph * 0.42, y - math.cos(math.radians(angle)) * ph * 0.42)
        pins.append(pin)
        return pin

    # The shadows go down first, under everything: worked out per piece and darkened at the end.
    rng = np.random.default_rng(803)
    centre = lay(road_map(400, 300, 810), 640, 420, -2.0)
    ring = (640 - 400 * 0.5 + 400 * 0.52, 420 - 300 * 0.5 + 300 * 0.5)
    photos = [("road", 190, 170, 4.0), ("car", 1090, 160, -5.0), ("house", 210, 560, -3.0), ("bridge", 1080, 590, 6.0),
              ("man", 400, 120, -8.0), ("figures", 900, 360, 3.0)]
    photo_pins = []
    for k, (kind, x, y, angle) in enumerate(photos):
        pw, ph = (150, 112) if kind not in ("man", "figures") else (100, 132)
        photo_pins.append(lay(print_border(scene(kind, pw, ph, 820 + k), 830 + k), x, y, angle))
    clip_pins = [lay(clipping(int(rng.uniform(110, 170)), int(rng.uniform(70, 130)), 840 + k), x, y, rng.uniform(-6, 6))
                 for k, (x, y) in enumerate(((420, 690), (860, 120), (1150, 380), (110, 360)))]
    card_pins = [lay(index_card(110, 70, 850 + k), x, y, rng.uniform(-7, 7)) for k, (x, y) in enumerate(((640, 140), (380, 330), (880, 700), (1160, 740), (90, 740)))]

    rgb *= (1.0 - shadow_mask * 0.45)[..., None]

    # The thread: from each photograph's pin and a few cards' to the ring on the map, pulled taut but
    # not straight (it sags a little between pins), with a pin at both ends.
    layer = Image.new("L", (W * SS, H * SS), 0)
    d = ImageDraw.Draw(layer)
    ends = photo_pins + card_pins[:3] + clip_pins[:2]
    for k, (px, py) in enumerate(ends):
        tx, ty = ring if k % 3 else centre
        mx, my = (px + tx) * 0.5, (py + ty) * 0.5 + 6
        pts = [(px + (mx - px) * t * 2 if t < 0.5 else mx + (tx - mx) * (t - 0.5) * 2,
                py + (my - py) * t * 2 if t < 0.5 else my + (ty - my) * (t - 0.5) * 2) for t in np.linspace(0, 1, 9)]
        d.line([(x * SS, y * SS) for x, y in pts], fill=255, width=int(2.0 * SS), joint="curve")
    d.line([(photo_pins[5][0] * SS, photo_pins[5][1] * SS), (photo_pins[0][0] * SS, photo_pins[0][1] * SS)], fill=255, width=int(2.0 * SS))
    thread = np.asarray(layer.resize((W, H), Image.LANCZOS), dtype=np.float32) / 255.0
    thread_shadow = np.roll(np.roll(thread, 3, axis=0), 2, axis=1)
    rgb *= (1.0 - thread_shadow * 0.4)[..., None]
    rgb = paint(rgb, thread, (0.42, 0.04, 0.04), 0.8 + 0.3 * noise(W, H, 200, 860, octaves=1))
    height = np.maximum(height, thread * 1.0)
    # The pins: brass heads with a red one here and there, a highlight on the upper left.
    pin_layer = Image.new("L", (W, H), 0)
    red_layer = Image.new("L", (W, H), 0)
    pd, rd = ImageDraw.Draw(pin_layer), ImageDraw.Draw(red_layer)
    for k, (px, py) in enumerate(pins + [ring, centre]):
        (rd if k % 4 == 0 else pd).ellipse([px - 6, py - 6, px + 6, py + 6], fill=255)
    pm = np.asarray(pin_layer.filter(ImageFilter.GaussianBlur(0.8)), dtype=np.float32) / 255.0
    rm = np.asarray(red_layer.filter(ImageFilter.GaussianBlur(0.8)), dtype=np.float32) / 255.0
    rgb = paint(rgb, np.roll(np.roll(pm + rm, 4, axis=0), 3, axis=1), (0.05, 0.03, 0.02), None, 0.6)
    rgb = paint(rgb, pm, (0.62, 0.45, 0.16), None, 1.0)
    rgb = paint(rgb, rm, (0.55, 0.06, 0.05), None, 1.0)
    height = np.maximum(height, (pm + rm) * 1.2)
    # Dust over the whole board, thicker towards the top where it settles on the pinned edges.
    dust = noise(W, H, 4, 870) * 0.22 + 0.08
    rgb = rgb * (1 - dust[..., None]) + np.array([0.5, 0.48, 0.44]) * dust[..., None]
    return np.clip(rgb, 0, 1), height


# ---------------------------------------------------------------------------------------------
# The portrait behind the desk.
# ---------------------------------------------------------------------------------------------

def make_portrait(seed):
    """A man in a black three-piece suit and a black homburg, seated, a cane under his hand with a
    gold ring on the finger; a burgundy tie on a white shirt; a burgundy curtain falling behind him.
    The brim's shadow is over his eyes: lit from above, what is left of the face is a jaw and a
    mouth. Who he is the painter did not say."""
    w, h = 512, 640
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    bg = np.array([0.10, 0.03, 0.03]) * np.clip(1.3 - np.sqrt(((xx - 250) / 300) ** 2 + ((yy - 200) / 380) ** 2), 0.25, 1.3)[..., None]
    rgb = bg * (0.85 + 0.25 * noise(w, h, 6, seed))[..., None]
    curtain = soft_shape(w, h, lambda d: d.polygon([(0, 0), (150, 0), (110, 330), (140, 640), (0, 640)], fill=255), 18)
    folds = 0.55 + 0.5 * np.sin(xx / 11.0 + noise(w, h, 4, seed + 2) * 6)
    rgb = paint(rgb, curtain, (0.26, 0.04, 0.05), folds)
    # The suit: broad shoulders, the lapels a shade lighter where the light catches the silk facing.
    coat = soft_shape(w, h, lambda d: d.polygon([(50, 640), (70, 420), (160, 330), (256, 316), (352, 330), (442, 420), (462, 640)], fill=255), 8)
    rgb = paint(rgb, coat, (0.03, 0.028, 0.03), 0.7 + 0.6 * np.clip(1 - ((xx - 200) / 170) ** 2, 0, 1))
    lapels = soft_shape(w, h, lambda d: (d.polygon([(214, 320), (256, 470), (190, 360)], fill=255), d.polygon([(298, 320), (256, 470), (322, 360)], fill=255)), 4)
    rgb = paint(rgb, lapels, (0.07, 0.065, 0.07), 0.8 + 0.4 * np.clip(1 - (xx - 210) / 120, 0, 1))
    shirt = soft_shape(w, h, lambda d: d.polygon([(226, 318), (286, 318), (256, 460)], fill=255), 4)
    rgb = paint(rgb, shirt, (0.74, 0.71, 0.64), np.clip(1.2 - (xx - 226) / 100, 0.5, 1.0))
    tie = soft_shape(w, h, lambda d: d.polygon([(248, 330), (264, 330), (270, 440), (256, 458), (242, 440)], fill=255), 2)
    rgb = paint(rgb, tie, (0.36, 0.04, 0.06), 0.7 + 0.4 * noise(w, h, 30, seed + 5))
    # The hand on the cane's gold knob, a ring on it.
    cane = soft_shape(w, h, lambda d: d.line([(148, 560), (120, 640)], fill=255, width=9), 2)
    rgb = paint(rgb, cane, (0.05, 0.03, 0.02), None, 1.0)
    knob = soft_shape(w, h, lambda d: d.ellipse([136, 538, 168, 566], fill=255), 2)
    rgb = paint(rgb, knob, (0.62, 0.44, 0.15), np.clip(1.2 - (xx - 136) / 40, 0.5, 1.2))
    hand = soft_shape(w, h, lambda d: d.ellipse([128, 500, 200, 548], fill=255), 6)
    rgb = paint(rgb, hand, (0.62, 0.44, 0.34), np.clip(1.1 - (xx - 130) / 120, 0.4, 1.0))
    ring = soft_shape(w, h, lambda d: d.ellipse([172, 518, 186, 530], fill=255), 1)
    rgb = paint(rgb, ring, (0.75, 0.55, 0.18), None, 1.0)
    # The head, then the hat over it, then the brim's shadow down to the top lip.
    rgb = portrait_head(rgb, w, h, 256, 236, 48, 64, (0.66, 0.50, 0.40), (0.05, 0.04, 0.035),
                        lambda d: d.ellipse([206, 168, 306, 240], fill=255), seed + 3)
    brim_shadow = soft_shape(w, h, lambda d: d.rectangle([196, 160, 316, 250], fill=255), 10)
    rgb = paint(rgb, brim_shadow, (0.025, 0.018, 0.015), None, 0.93)
    hat = soft_shape(w, h, lambda d: (d.rounded_rectangle([210, 96, 302, 178], radius=24, fill=255), d.ellipse([172, 162, 340, 194], fill=255)), 3)
    rgb = paint(rgb, hat, (0.025, 0.022, 0.024), 0.8 + 0.6 * np.clip(1 - ((xx - 230) / 60) ** 2, 0, 1) * np.clip(1 - (yy - 100) / 90, 0, 1))
    band = soft_shape(w, h, lambda d: d.rectangle([212, 152, 300, 166], fill=255), 2)
    rgb = paint(rgb, band, (0.12, 0.03, 0.035), None, 0.9)
    return varnish(np.clip(rgb, 0, 1), seed + 7, amount=1.1)


def make_city_map(seed):
    """A city map in a gold frame, the kind a man who knows a city hangs: a river through it, a park,
    the railway, the street grid going off true where the old town is, wards edged in red."""
    w, h = PICTURES["map"][2], PICTURES["map"][3]
    rng = np.random.default_rng(seed)
    img = Image.new("RGB", (w * SS, h * SS), hexrgb("#d4c6a2"))
    d = ImageDraw.Draw(img)
    s = SS
    d.rectangle([10 * s, 10 * s, (w - 10) * s, (h - 10) * s], outline=hexrgb("#3a2c22"), width=3 * s)
    d.rectangle([16 * s, 16 * s, (w - 16) * s, (h - 16) * s], outline=hexrgb("#3a2c22"), width=s)
    # The blocks, a grid laid at two angles.
    for angle, (cx, cy), extent in ((8, (w * 0.3, h * 0.55), 200), (-22, (w * 0.7, h * 0.45), 220)):
        a = math.radians(angle)
        ca, sa = math.cos(a), math.sin(a)
        for i in range(-10, 11):
            for j in range(-10, 11):
                bx, by = i * 22, j * 16
                if abs(bx) > extent or abs(by) > extent * 0.7 or rng.random() < 0.08:
                    continue
                pts = []
                for ox, oy in ((2, 2), (19, 2), (19, 13), (2, 13)):
                    x = cx + (bx + ox) * ca - (by + oy) * sa
                    y = cy + (bx + ox) * sa + (by + oy) * ca
                    pts.append((x * s, y * s))
                if all(20 * s < px < (w - 20) * s and 20 * s < py < (h - 20) * s for px, py in pts):
                    d.polygon(pts, fill=hexrgb("#c2a983" if rng.random() < 0.85 else "#b08f6a"))
    river = [(20 * s, h * 0.8 * s)]
    for k in range(1, 11):
        river.append(((20 + (w - 40) * k / 10) * s, (h * 0.8 - math.sin(k * 0.7) * h * 0.25 - k * 8) * s))
    d.line(river, fill=hexrgb("#8aa1ad"), width=14 * s, joint="curve")
    d.line(river, fill=hexrgb("#6f8995"), width=2 * s, joint="curve")
    d.ellipse([w * 0.12 * s, h * 0.15 * s, w * 0.32 * s, h * 0.38 * s], fill=hexrgb("#a6ad84"))
    rail = [(20 * s, h * 0.25 * s), (w * 0.5 * s, h * 0.3 * s), ((w - 20) * s, h * 0.18 * s)]
    d.line(rail, fill=hexrgb("#2b2621"), width=3 * s, joint="curve")
    for k in range(40):
        t = k / 40
        x = (20 + (w - 40) * t) * s
        y = (h * 0.25 + (0.05 if t < 0.5 else 0.05 - (t - 0.5) * 0.24) * h * 2 * min(t, 1 - t) * 0 + (h * 0.05 * t * 2 if t < 0.5 else h * 0.05 - h * 0.12 * (t - 0.5) * 2)) * s
        d.line([(x, y - 4 * s), (x, y + 4 * s)], fill=hexrgb("#2b2621"), width=s)
    for k in range(5):
        cx, cy = rng.uniform(0.2, 0.8) * w * s, rng.uniform(0.2, 0.8) * h * s
        r = rng.uniform(40, 90) * s
        d.ellipse([cx - r, cy - r * 0.7, cx + r, cy + r * 0.7], outline=hexrgb("#9e3a2e"), width=2 * s)
    # The cartouche at the top: a title in bars of type.
    d.rectangle([w * 0.32 * s, 26 * s, w * 0.68 * s, 58 * s], fill=hexrgb("#e2d6b6"), outline=hexrgb("#3a2c22"), width=s)
    cursor = w * 0.35 * s
    while cursor < w * 0.64 * s:
        word = rng.uniform(20, 46) * s
        d.rectangle([cursor, 36 * s, min(cursor + word, w * 0.65 * s), 48 * s], fill=hexrgb("#2b2621"))
        cursor += word + 8 * s
    img = img.resize((w, h), Image.LANCZOS)
    return age_paper(np.asarray(img, dtype=np.float32) / 255.0, seed + 1, foxing=1.0, damp=0.5, edge=1.0, dust=0.5)


def make_folder(seed):
    """A case file's front: buff card, the tab along the top, a typed label, a red stamp gone pink."""
    w, h = PICTURES["folder"][2], PICTURES["folder"][3]
    rng = np.random.default_rng(seed)
    img = Image.new("RGB", (w * SS, h * SS), hexrgb("#b89c6a"))
    d = ImageDraw.Draw(img)
    s = SS
    d.rectangle([24 * s, 30 * s, 190 * s, 62 * s], fill=hexrgb("#d9cfb7"), outline=hexrgb("#6d5a3b"), width=s)
    cursor = 32 * s
    for k in range(2):
        cursor = 32 * s
        while cursor < 180 * s:
            word = rng.uniform(12, 34) * s
            d.rectangle([cursor, (38 + k * 12) * s, min(cursor + word, 182 * s), (44 + k * 12) * s], fill=hexrgb("#2b2621"))
            cursor += word + 6 * s
    stamp = Image.new("L", (w * s, h * s), 0)
    sd = ImageDraw.Draw(stamp)
    sd.rectangle([200 * s, 150 * s, 350 * s, 196 * s], outline=255, width=4 * s)
    cursor = 212 * s
    while cursor < 336 * s:
        word = rng.uniform(16, 40) * s
        sd.rectangle([cursor, 164 * s, min(cursor + word, 338 * s), 182 * s], fill=255)
        cursor += word + 8 * s
    stamp = stamp.rotate(-8, resample=Image.BICUBIC, center=(275 * s, 173 * s))
    sm = np.asarray(stamp.resize((w, h), Image.LANCZOS), dtype=np.float32) / 255.0 * (0.5 + 0.5 * noise(w, h, 30, seed + 2))
    img = img.resize((w, h), Image.LANCZOS)
    rgb = np.asarray(img, dtype=np.float32) / 255.0
    rgb = paint(rgb, sm, (0.62, 0.18, 0.16), None, 0.8)
    # Coffee: a ring where a cup stood on it.
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    ringd = np.sqrt((xx - 110) ** 2 + (yy - 200) ** 2)
    rgb = paint(rgb, np.exp(-((ringd - 34) / 2.0) ** 2) * 0.7, (0.35, 0.22, 0.10), None, 0.6)
    return age_paper(rgb, seed + 3, foxing=0.6, damp=0.3, edge=1.0, dust=0.4)


def build_pictures():
    out = np.zeros((SIZE, SIZE, 3), dtype=np.float32) + 0.05
    height = np.zeros((SIZE, SIZE), dtype=np.float32)
    rough = np.full((SIZE, SIZE), 0.85, dtype=np.float32)

    def put(name, rgb, h=None, r=None):
        x, y, w, hh = PICTURES[name]
        rgb = np.asarray(Image.fromarray(to_u8(rgb)).resize((w, hh), Image.LANCZOS), dtype=np.float32) / 255.0
        out[y:y + hh, x:x + w] = rgb
        if h is not None:
            height[y:y + hh, x:x + w] = h
        if r is not None:
            rough[y:y + hh, x:x + w] = r

    board, board_h = build_board()
    put("board", board, board_h * 0.6, 0.92)
    put("portrait", make_portrait(901), noise(512, 640, 120, 902, octaves=2) * 0.5 - crackle(512, 640, 903, 70.0) * 0.4, 0.36)
    put("map", make_city_map(911), None, 0.8)
    put("folder", make_folder(921), None, 0.9)
    put("photo_a", print_border(scene("road", 180, 132, 931), 932), None, 0.5)
    put("photo_b", print_border(scene("figures", 180, 132, 933), 934), None, 0.5)

    arm = np.stack([np.ones_like(rough), rough, np.zeros_like(rough)], axis=-1)
    save_set("study_pictures", out, normal_from_height(height, 2.0), to_u8(arm))
    preview("study_pictures", out, 2048)
    print("study_pictures linear mean", linear_mean(out))


# ---------------------------------------------------------------------------------------------
# The carpet.
# ---------------------------------------------------------------------------------------------

def build_rug():
    """400 x 280: a burgundy field with a small diaper of gold dots, a black border carrying a gold
    key fret between gold rules, a gold-edged black medallion with a burgundy heart. Worn to the
    warp where the guest chairs' feet and the desk chair's castors go, dusty all over."""
    W, H = 2000, 1400
    burgundy, black, gold, deep = (hexrgb(c) for c in ("#5a0d16", "#0d0a0a", "#a9802f", "#3a070d"))
    img = Image.new("RGB", (W, H), burgundy)
    d = ImageDraw.Draw(img)

    d.rectangle([0, 0, W, H], outline=black, width=150)
    for inset, wid in ((18, 8), (132, 8)):
        d.rectangle([inset, inset, W - inset, H - inset], outline=gold, width=wid)
    d.rectangle([150, 150, W - 150, H - 150], outline=deep, width=14)

    # The Greek key round the border: a run of hooked spirals, one module every 56px.
    def key(x, y, horizontal, flip):
        u = 9.0
        pts = [(0, 0), (6, 0), (6, 6), (2, 6), (2, 3), (4, 3)]
        path = []
        for px, py in pts:
            py = -py if flip else py
            path.append((x + px * u, y + py * u) if horizontal else (x + py * u, y + px * u))
        d.line(path, fill=gold, width=7)

    for x in range(40, W - 90, 60):
        key(x, 48, True, False)
        key(x, H - 48, True, True)
    for y in range(110, H - 150, 60):
        key(48, y, False, False)
        key(W - 48, y, False, True)

    # The field's diaper: small gold lozenges on a staggered grid.
    for row, y in enumerate(range(200, H - 180, 46)):
        for x in range(200 + (23 if row % 2 else 0), W - 180, 46):
            d.polygon([(x, y - 6), (x + 5, y), (x, y + 6), (x - 5, y)], fill=gold)
    # The medallion.
    cx, cy = W / 2, H / 2
    d.ellipse([cx - 380, cy - 260, cx + 380, cy + 260], fill=black)
    d.ellipse([cx - 370, cy - 250, cx + 370, cy + 250], outline=gold, width=8)
    d.ellipse([cx - 220, cy - 150, cx + 220, cy + 150], fill=deep)
    d.ellipse([cx - 210, cy - 140, cx + 210, cy + 140], outline=gold, width=5)
    for k in range(16):
        a = k * math.pi / 8
        x, y = cx + math.cos(a) * 300, cy + math.sin(a) * 205
        d.ellipse([x - 16, y - 16, x + 16, y + 16], fill=gold)
    for k in range(8):
        a = k * math.pi / 4
        d.polygon([(cx, cy), (cx + math.cos(a - 0.12) * 130, cy + math.sin(a - 0.12) * 90),
                   (cx + math.cos(a) * 160, cy + math.sin(a) * 110), (cx + math.cos(a + 0.12) * 130, cy + math.sin(a + 0.12) * 90)], fill=burgundy)
    d.ellipse([cx - 40, cy - 30, cx + 40, cy + 30], fill=gold)

    img = img.filter(ImageFilter.GaussianBlur(2.0))
    rgb = np.asarray(img, dtype=np.float32) / 255.0
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    rgb *= (0.9 + 0.1 * np.sin(xx * 2.2) * np.sin(yy * 2.2))[..., None]
    rgb *= (0.86 + 0.24 * noise(W, H, 60, 951, octaves=2))[..., None]
    # Wear: where the guests sat in front of the desk (low y is the desk's side once laid: the rug's
    # local +Y runs from the desk towards the door), and under the desk chair.
    guests = np.exp(-(((xx - W * 0.5) / 420.0) ** 2 + ((yy - H * 0.78) / 160.0) ** 2))
    castors = np.exp(-(((xx - W * 0.5) / 220.0) ** 2 + ((yy - H * 0.12) / 110.0) ** 2))
    worn = np.clip((guests * 0.7 + castors) * (0.3 + noise(W, H, 40, 952)), 0.0, 1.0)
    warp = np.array([0.55, 0.48, 0.38]) * (0.8 + 0.2 * np.sin(xx * 3.1))[..., None]
    rgb = rgb * (1 - worn[..., None] * 0.6) + warp * worn[..., None] * 0.6
    dust = noise(W, H, 5, 953) * 0.22 + 0.12
    rgb = rgb * (1 - dust[..., None]) + np.array([0.46, 0.43, 0.39]) * dust[..., None]
    n = 1024
    rgb = np.asarray(Image.fromarray(to_u8(rgb), "RGB").resize((n, n), Image.LANCZOS), dtype=np.float32) / 255.0
    worn_sq = np.asarray(Image.fromarray((worn * 255).astype(np.uint8)).resize((n, n)), dtype=np.float32) / 255.0
    height = noise(n, n, 400, 954, octaves=2) * (1.0 - worn_sq * 0.8) + (1.0 - worn_sq) * 0.4
    arm = np.stack([np.ones((n, n)), np.full((n, n), 0.95), np.zeros((n, n))], axis=-1)
    save_set("study_rug", rgb, normal_from_height(height, 3.0), to_u8(arm))
    preview("study_rug", rgb)
    print("study_rug linear mean", linear_mean(rgb))


def main():
    build_pictures()
    build_rug()


if __name__ == "__main__":
    main()
