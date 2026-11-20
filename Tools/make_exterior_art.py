"""
Bakes the ivy on the house's walls: an atlas of four cards, each a run of stem with ivy leaves on
it, with a cutout mask, written as a surface set (exterior_ivy_Diffuse / _nor_dx / _arm / _opacity)
into Art/Source/Textures, so build_art.py hangs it off the masked, two-sided master.

    python Tools/make_exterior_art.py

Common ivy (Hedera helix) on an old wall: dark, leathery, glossy leaves three to five lobed, pale
veins, the young leaves a lighter green, here and there one gone yellow or brown, on a woody stem
that clings flat to the brick. Each card is 512 px for 50cm of wall (AMansionExteriorActor:
IvyCardSize), so a leaf is five to ten centimetres across. A lit preview is written beside it.

numpy and Pillow only.
"""

import math
import os
import random

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXTURES = os.path.join(ROOT, "Art", "Source", "Textures")
PREVIEW = os.path.join(ROOT, "Saved", "ExteriorPreview")

SIZE = 1024
CELL = 512
SS = 3  # supersampling


def leaf_outline(cx, cy, r, angle, lobes, rng):
    """The outline of an ivy leaf: a heart-shaped base at the stalk, three or five pointed lobes."""
    pts = []
    n = 90
    # Lobe directions relative to the leaf's axis (0 = tip).
    if lobes == 5:
        dirs = [(0.0, 1.0), (0.85, 0.72), (-0.85, 0.72), (1.75, 0.52), (-1.75, 0.52)]
    else:
        dirs = [(0.0, 1.0), (1.0, 0.70), (-1.0, 0.70)]
    jitter = [rng.uniform(0.92, 1.08) for _ in dirs]
    for k in range(n):
        t = -math.pi + 2.0 * math.pi * k / n
        # A broad leaf with shallow, pointed lobes: a wide base shape, the lobes standing off it.
        rr = 0.55
        for (d, w), j in zip(dirs, jitter):
            diff = math.atan2(math.sin(t - d), math.cos(t - d))
            rr = max(rr, w * j * (0.55 + 0.45 * max(0.0, math.cos(diff * 1.25)) ** 3.0))
        # The notch where the stalk joins.
        back = math.atan2(math.sin(t - math.pi), math.cos(t - math.pi))
        rr *= 1.0 - 0.22 * max(0.0, math.cos(back * 2.5)) ** 8
        a = t + angle
        pts.append((cx + math.sin(a) * rr * r, cy - math.cos(a) * rr * r))
    return pts


def draw_card(rng, img, height, ox, oy, dense):
    """One card: stems wandering up through the cell, leaves off them on stalks. A dense card is a
    mat of leaves over the whole cell, for the thick of a patch; the others are trailing runs, for
    its edges."""
    d = ImageDraw.Draw(img)
    hd = ImageDraw.Draw(height)
    s = SS
    # The stem: from the bottom of the cell up, wandering, with a side shoot.
    stems = []
    x = ox + CELL * rng.uniform(0.35, 0.65)
    y = oy + CELL * 0.98
    path = [(x, y)]
    while y > oy + CELL * 0.05:
        x += rng.uniform(-18, 18)
        x = min(max(x, ox + CELL * 0.15), ox + CELL * 0.85)
        y -= rng.uniform(26, 40)
        path.append((x, y))
    stems.append(path)
    if dense:
        for _ in range(2):
            x = ox + CELL * rng.uniform(0.15, 0.85)
            y = oy + CELL * 0.98
            extra = [(x, y)]
            while y > oy + CELL * 0.05:
                x += rng.uniform(-22, 22)
                x = min(max(x, ox + CELL * 0.1), ox + CELL * 0.9)
                y -= rng.uniform(26, 40)
                extra.append((x, y))
            stems.append(extra)
    for _ in range(2):
        i = rng.randrange(2, len(path) - 2)
        bx, by = path[i]
        side = rng.choice((-1, 1))
        branch = [(bx, by)]
        for _ in range(rng.randint(3, 6)):
            bx += side * rng.uniform(18, 36)
            by -= rng.uniform(10, 30)
            bx = min(max(bx, ox + CELL * 0.06), ox + CELL * 0.94)
            branch.append((bx, by))
        stems.append(branch)
    for path in stems:
        pts = [(px * s, py * s) for px, py in path]
        d.line(pts, fill=(58, 44, 30, 255), width=4 * s, joint="curve")
        hd.line(pts, fill=150, width=4 * s, joint="curve")
        # Rootlets along the stem, clinging.
        for (ax, ay), (bx, by) in zip(path, path[1:]):
            for k in range(3):
                t = rng.random()
                px, py = ax + (bx - ax) * t, ay + (by - ay) * t
                for _ in range(2):
                    dx, dy = rng.uniform(-6, 6), rng.uniform(-3, 5)
                    d.line([(px * s, py * s), ((px + dx) * s, (py + dy) * s)], fill=(70, 58, 44, 255), width=s)

    # Leaves: on stalks off the stems, the bigger and darker the older, back to front.
    leaves = []
    for path in stems:
        for (ax, ay), (bx, by) in zip(path, path[1:]):
            for _ in range(rng.randint(2, 4) if dense else rng.randint(1, 3)):
                t = rng.random()
                px, py = ax + (bx - ax) * t, ay + (by - ay) * t
                ang = rng.uniform(-1.4, 1.4)
                reach = rng.uniform(10, 26)
                lx = px + math.sin(ang) * reach
                ly = py - math.cos(ang) * reach * 0.6
                leaves.append((px, py, lx, ly))
    rng.shuffle(leaves)
    for px, py, lx, ly in leaves:
        r = rng.uniform(24, 44)
        lx = min(max(lx, ox + r + 4), ox + CELL - r - 4)
        ly = min(max(ly, oy + r + 4), oy + CELL - r - 4)
        lobes = rng.choice((3, 5, 5))
        ang = math.atan2(lx - px, -(ly - py)) + rng.uniform(-0.5, 0.5)
        cx = lx + math.sin(ang) * r * 0.25
        cy = ly - math.cos(ang) * r * 0.25
        outline = leaf_outline(cx, cy, r, ang, lobes, rng)
        kind = rng.random()
        if kind < 0.06:
            base = np.array([95, 72, 28])     # gone yellow
        elif kind < 0.10:
            base = np.array([70, 45, 26])     # dead, brown
        elif kind < 0.30:
            base = np.array([52, 74, 34])     # young
        else:
            base = np.array([28, 46, 24])     # old, dark
        base = np.clip(base * rng.uniform(0.85, 1.12), 0, 255).astype(int)
        # Stalk.
        d.line([(px * s, py * s), ((cx - math.sin(ang) * r * 0.55) * s, (cy + math.cos(ang) * r * 0.55) * s)], fill=(62, 60, 34, 255), width=2 * s)
        poly = [(x * s, y * s) for x, y in outline]
        d.polygon(poly, fill=tuple(base) + (255,))
        hd.polygon(poly, fill=200)
        # Shading: the half away from the light a little darker, the edge darker still.
        half = [(cx * s, cy * s)] + [p for p, (x, y) in zip(poly, outline) if (x - cx) * 0.7 + (y - cy) * 0.7 > 0]
        if len(half) > 3:
            d.polygon(half, fill=tuple(np.clip(base * 0.82, 0, 255).astype(int)) + (255,))
        # Veins: from the stalk's end out towards each lobe tip.
        root = (cx - math.sin(ang) * r * 0.3, cy + math.cos(ang) * r * 0.3)
        vein = tuple(np.clip(base * 1.55 + 18, 0, 255).astype(int)) + (255,)
        tips = sorted(outline, key=lambda p: -math.hypot(p[0] - cx, p[1] - cy))[:1]
        for k, (dx, w) in enumerate([(0.0, 1.0), (0.9, 0.7), (-0.9, 0.7), (1.8, 0.5), (-1.8, 0.5)][:lobes]):
            a = ang + dx
            ex = cx + math.sin(a) * r * w * 0.85
            ey = cy - math.cos(a) * r * w * 0.85
            d.line([(root[0] * s, root[1] * s), (ex * s, ey * s)], fill=vein, width=max(1, int(s * (1.4 if k == 0 else 1.0))))
            hd.line([(root[0] * s, root[1] * s), (ex * s, ey * s)], fill=170, width=s)


def main():
    rng = random.Random(1104)
    img = Image.new("RGBA", (SIZE * SS, SIZE * SS), (0, 0, 0, 0))
    height = Image.new("L", (SIZE * SS, SIZE * SS), 0)
    for cy in range(2):
        for cx in range(2):
            draw_card(rng, img, height, cx * CELL, cy * CELL, dense=(cy == 0))
    img = img.resize((SIZE, SIZE), Image.LANCZOS)
    height = height.resize((SIZE, SIZE), Image.LANCZOS).filter(ImageFilter.GaussianBlur(1.2))

    rgba = np.asarray(img).astype(np.float32)
    alpha = rgba[..., 3] / 255.0
    # Colour bleeds out under the cutout, so the mips do not pull black into the leaves' edges.
    colour = rgba[..., :3]
    filled = Image.fromarray(colour.astype(np.uint8), "RGB")
    mask = Image.fromarray((alpha > 0.02).astype(np.uint8) * 255, "L")
    for _ in range(6):
        grown = filled.filter(ImageFilter.MaxFilter(5))
        filled = Image.composite(filled, grown, mask)
        mask = mask.filter(ImageFilter.MaxFilter(5))
    os.makedirs(TEXTURES, exist_ok=True)
    filled.save(os.path.join(TEXTURES, "exterior_ivy_Diffuse.png"))
    Image.fromarray((np.clip(alpha, 0, 1) * 255).astype(np.uint8), "L").convert("RGB").save(os.path.join(TEXTURES, "exterior_ivy_opacity.png"))

    h = np.asarray(height).astype(np.float32) / 255.0
    gy, gx = np.gradient(h)
    nx, ny = -gx * 6.0, gy * 6.0
    nz = np.ones_like(nx)
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    normal = np.stack([nx / ln, -ny / ln, nz / ln], axis=-1)  # DirectX: green down
    Image.fromarray(((normal * 0.5 + 0.5) * 255).astype(np.uint8), "RGB").save(os.path.join(TEXTURES, "exterior_ivy_nor_dx.png"))

    arm = np.zeros((SIZE, SIZE, 3), np.uint8)
    arm[..., 0] = 255
    arm[..., 1] = 205  # leathery, but a wet sky in it turned every card white at 120
    Image.fromarray(arm, "RGB").save(os.path.join(TEXTURES, "exterior_ivy_arm.png"))

    os.makedirs(PREVIEW, exist_ok=True)
    back = Image.new("RGB", (SIZE, SIZE), (60, 36, 28))
    back.paste(filled, (0, 0), Image.fromarray((alpha * 255).astype(np.uint8), "L"))
    back.save(os.path.join(PREVIEW, "ivy_preview.png"))
    print("ivy written")
    rain()


GENERATED = os.path.join(ROOT, "Art", "Source", "Generated")


def rain():
    """
    The rain for a camera outside (AMansionExteriorActor), three greyscale masks in the red channel,
    written into Art/Source/Generated for build_art.py's rain stage:

      rain_streak.png  one drop as the eye sees it falling at night: a soft, thin streak, brightest
                       a third of the way down, tapering to nothing at both ends. Laid on a card
                       several times wider than the streak, so its edge is never the card's.
      rain_sheet.png   tileable: hundreds of thin streaks of every length and brightness, for the
                       far curtains of rain that panned down give a downpour its density.
      rain_splash.png  a crown thrown up where a drop lands, seen from the side: a soft fan of spray.

    A lit preview of each is written to Saved/ExteriorPreview.
    """
    rng = np.random.default_rng(1104)
    os.makedirs(GENERATED, exist_ok=True)
    os.makedirs(PREVIEW, exist_ok=True)

    # One streak: 64 wide, 512 long.
    w, h = 64, 512
    x = (np.arange(w) - (w - 1) / 2.0) / (w / 2.0)
    y = np.linspace(0.0, 1.0, h)
    across = np.exp(-(x / 0.16) ** 2)
    along = np.clip(np.sin(np.pi * y) ** 0.8 * (1.0 - 0.35 * y), 0.0, 1.0)
    flicker = 1.0 + 0.12 * np.sin(y * 37.0) * np.sin(y * 11.0)
    streak = np.outer(along * flicker, across)
    streak /= streak.max()
    Image.fromarray((streak * 255).astype(np.uint8), "L").convert("RGB").save(os.path.join(GENERATED, "rain_streak.png"))

    # The sheet: tileable 1024 square, streaks wrapped round both edges.
    n = 1024
    sheet = np.zeros((n, n), np.float32)
    for _ in range(1400):
        cx = rng.uniform(0, n)
        cy = rng.uniform(0, n)
        length = rng.uniform(30, 160)
        bright = rng.uniform(0.15, 1.0) ** 1.6
        width = rng.uniform(0.5, 1.3)
        ys = np.arange(int(cy - length / 2), int(cy + length / 2))
        t = (ys - (cy - length / 2)) / length
        env = np.sin(np.pi * np.clip(t, 0, 1)) ** 0.8 * bright
        for dx in range(-2, 3):
            col = int(round(cx)) + dx
            fall = np.exp(-((col - cx) / width) ** 2)
            sheet[ys % n, col % n] += env * fall
    sheet = np.clip(sheet / np.percentile(sheet[sheet > 0], 99.5), 0.0, 1.0)
    Image.fromarray((sheet * 255).astype(np.uint8), "L").convert("RGB").save(os.path.join(GENERATED, "rain_sheet.png"))

    # The splash: 128 square, a soft fan of droplets over a low ring.
    m = 128
    yy, xx = np.mgrid[0:m, 0:m].astype(np.float32)
    u = (xx - m / 2) / (m / 2)
    v = (m - 1 - yy) / m
    crown = np.exp(-((np.abs(u) - 0.55) / 0.18) ** 2) * np.exp(-(v / 0.18) ** 2)
    splash = crown * 0.7
    for _ in range(26):
        a = rng.uniform(-1.2, 1.2)
        r = rng.uniform(0.25, 0.9)
        px, py = np.sin(a) * r, np.cos(a) * r * 0.8
        splash += 0.8 * np.exp(-(((u - px) / 0.045) ** 2 + ((v - py) / 0.05) ** 2))
    splash = np.clip(splash / splash.max(), 0.0, 1.0)
    Image.fromarray((splash * 255).astype(np.uint8), "L").convert("RGB").save(os.path.join(GENERATED, "rain_splash.png"))

    # Preview: the sheet and a few streaks over the night.
    back = np.full((n, n, 3), (10, 12, 18), np.float32)
    back += sheet[..., None] * np.array([150, 160, 180]) * 0.6
    Image.fromarray(np.clip(back, 0, 255).astype(np.uint8), "RGB").save(os.path.join(PREVIEW, "rain_sheet_preview.png"))
    print("rain written")


if __name__ == "__main__":
    main()
