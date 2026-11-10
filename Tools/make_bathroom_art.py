#!/usr/bin/env python3
"""
Bakes the bathroom's two tiled surfaces into texture sets the art pipeline imports.

    python Tools/make_bathroom_art.py

Writes into Art/Source/Textures (_Diffuse / _nor_dx / _arm):

    bathroom_tiles   the walls' bevelled white tiles, 15 x 7.5 laid in running bond, 4 across and 8
                     up: one repeat every TILES_CM (BathroomActor.h: TileSheetCm). Glazed, the glaze
                     crazed and gone the colour of old teeth, the grout grey and darker where the
                     water ran, a hairline crack through the odd one. Every edge is a cushion bevel
                     in the normals, so the lantern runs a highlight round each tile.
    bathroom_mosaic  the floor: a hexagon mosaic, tesserae three centimetres across, white with
                     black seven-hex flowers on a staggered lattice, the Victorian bathroom floor.
                     It repeats every MOSAIC_W x MOSAIC_H cm (not square: a hexagon grid's periods
                     across and along are 1.5 and sqrt(3) times its radius), so the actor writes the
                     two repeat counts out itself. The grout dirty, a few tesserae lifted out to the
                     bed, the white ones yellowed unevenly.

Both tile seamlessly: every noise is the periodic kind (tile_noise), and every pattern repeats
exactly on the sheet. Linear means are printed so the actor's tints can be worked back from them,
as everywhere in the house. Previews go to Saved/BathroomPreview/.
"""

import math
import os

import numpy as np
from PIL import Image

from make_cellar_art import TEXTURES
from make_library_art import normal_from_height, save_set, tile_noise, to_u8

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PREVIEW = os.path.join(ROOT, "Saved", "BathroomPreview")
SIZE = 1024

TILES_CM = 60.0
HEX_R = 1.5
MOSAIC_COLS = 24
MOSAIC_ROWS = 20
MOSAIC_W = 1.5 * HEX_R * MOSAIC_COLS                 # 54.0
MOSAIC_H = math.sqrt(3.0) * HEX_R * MOSAIC_ROWS      # 51.96


def linear_mean(rgb):
    lin = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
    return lin.reshape(-1, 3).mean(axis=0)


def contour(n, cutoff, seed, sharp):
    """The thin contour of a signed periodic noise field: a crazing line, a hairline crack."""
    field = tile_noise(n, cutoff, seed)
    return np.clip(1.0 - np.abs(field - 0.5) * sharp, 0.0, 1.0)


def build_tiles():
    n = SIZE
    tw, th = n // 4, n // 8          # 256 x 128 px: 15 x 7.5 cm
    yy, xx = np.mgrid[0:n, 0:n]
    row = yy // th
    shift = (row % 2) * (tw // 2)
    xs = (xx + shift) % n
    col = xs // tw
    u = xs - col * tw
    v = yy - row * th
    edge = np.minimum(np.minimum(u, tw - 1 - u), np.minimum(v, th - 1 - v)).astype(np.float32)
    tile_id = (row * 4 + col).astype(np.int64)
    rng = np.random.default_rng(1201)
    count = 4 * 8
    tint = rng.uniform(0.96, 1.02, count)
    yellow = rng.uniform(0.0, 1.0, count) ** 4 * 0.45
    tilt_x = rng.uniform(-1.0, 1.0, count)
    tilt_y = rng.uniform(-1.0, 1.0, count)

    joint = 1.7
    grout = edge < joint
    bevel = np.clip((edge - joint) / 9.0, 0.0, 1.0)
    bevel = bevel * bevel * (3.0 - 2.0 * bevel)
    # A tile is a cushion: rounded down to the joint, a little domed, and set a hair off true.
    du = (u - tw * 0.5) / (tw * 0.5)
    dv = (v - th * 0.5) / (th * 0.5)
    dome = 1.0 - 0.15 * (du * du + dv * dv)
    tilt = 0.04 * (tilt_x[tile_id] * du + tilt_y[tile_id] * dv)
    height = np.where(grout, 0.0, 0.55 + 0.4 * bevel * dome + tilt * bevel)

    glaze = np.array([0.87, 0.86, 0.82], dtype=np.float32)
    old = np.array([0.80, 0.74, 0.60], dtype=np.float32)
    col_rgb = glaze[None, None, :] * tint[tile_id][..., None]
    col_rgb = col_rgb * (1 - 0.5 * yellow[tile_id][..., None]) + old[None, None, :] * (0.5 * yellow[tile_id][..., None])
    # Crazing: a fine network in the glaze, the dirt in it.
    craze = np.maximum(contour(n, 40.0, 1202, 70.0), contour(n, 75.0, 1203, 90.0))
    col_rgb *= (1.0 - 0.07 * craze)[..., None]
    # Grime: in the bevels, heavier towards the bottom of each tile (water runs down), and in clouds.
    cloud = tile_noise(n, 5.0, 1204)
    low = np.clip(v / th, 0, 1) ** 3
    grime = np.clip((1.0 - bevel) * 0.55 + low * 0.25 + (cloud - 0.5) * 0.3, 0, 1)
    col_rgb = col_rgb * (1 - 0.32 * grime[..., None]) + np.array([0.40, 0.35, 0.27]) * (0.32 * grime[..., None])
    # A few tiles with a hairline crack.
    cracked = rng.uniform(0, 1, count) < 0.12
    crack = contour(n, 4.0, 1205, 400.0) * cracked[tile_id]
    col_rgb *= (1.0 - 0.55 * crack)[..., None]
    height -= 0.25 * crack
    # The grout: grey, darker and browner where it is lowest on the wall.
    grout_rgb = np.array([0.44, 0.42, 0.38]) * (0.75 + 0.35 * tile_noise(n, 30.0, 1206))[..., None]
    rgb = np.where(grout[..., None], grout_rgb, col_rgb)

    rough = np.where(grout, 0.92, 0.22 + 0.12 * grime + 0.1 * craze + 0.08 * tile_noise(n, 20.0, 1207))
    ao = np.where(grout, 0.55, 0.75 + 0.25 * bevel)
    arm = np.stack([ao, rough, np.zeros_like(rough)], axis=-1)
    save_set("bathroom_tiles", np.clip(rgb, 0, 1), normal_from_height(height, 6.0), to_u8(arm))
    return np.clip(rgb, 0, 1)


def build_mosaic():
    n = SIZE
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float64)
    x = (xx + 0.5) / n * MOSAIC_W
    y = (yy + 0.5) / n * MOSAIC_H
    r = HEX_R
    # Flat-topped hexagons: axial coordinates, cube-rounded to the nearest centre.
    q = (2.0 / 3.0 * x) / r
    s = (-1.0 / 3.0 * x + math.sqrt(3.0) / 3.0 * y) / r
    cx, cz = q, s
    cy = -cx - cz
    rx, ry, rz = np.round(cx), np.round(cy), np.round(cz)
    dx, dy, dz = np.abs(rx - cx), np.abs(ry - cy), np.abs(rz - cz)
    fix_x = (dx > dy) & (dx > dz)
    fix_y = ~fix_x & (dy > dz)
    rx = np.where(fix_x, -ry - rz, rx)
    ry = np.where(fix_y, -rx - rz, ry)
    rz = np.where(~fix_x & ~fix_y, -rx - ry, rz)
    qi = rx.astype(np.int64)
    ri = rz.astype(np.int64)
    centre_x = r * 1.5 * qi
    centre_y = r * math.sqrt(3.0) * (ri + qi / 2.0)
    px, py = x - centre_x, y - centre_y
    c30 = math.cos(math.radians(30.0))
    reach = np.maximum(np.abs(py), np.maximum(np.abs(px * c30 + py * 0.5), np.abs(px * c30 - py * 0.5)))
    edge = (r * c30 - reach)                       # cm to the hexagon's edge
    col = np.mod(qi, MOSAIC_COLS)
    row = np.mod(ri + np.floor_divide(qi - np.mod(qi, 2), 2), MOSAIC_ROWS)
    hex_id = row * MOSAIC_COLS + col

    # The flowers: a black hexagon and its six neighbours, on a staggered lattice.
    centres = []
    for i in range(0, MOSAIC_COLS, 12):
        for j in range(0, MOSAIC_ROWS, 10):
            centres.append((i, j))
            centres.append(((i + 6) % MOSAIC_COLS, (j + 5) % MOSAIC_ROWS))
    hcx = r * 1.5 * col
    hcy = r * math.sqrt(3.0) * (row + 0.5 * (col % 2))
    black = np.zeros(hcx.shape, dtype=bool)
    for i, j in centres:
        fx = r * 1.5 * i
        fy = r * math.sqrt(3.0) * (j + 0.5 * (i % 2))
        ddx = np.abs(hcx - fx)
        ddx = np.minimum(ddx, MOSAIC_W - ddx)
        ddy = np.abs(hcy - fy)
        ddy = np.minimum(ddy, MOSAIC_H - ddy)
        black |= np.hypot(ddx, ddy) < r * math.sqrt(3.0) * 1.08

    rng = np.random.default_rng(1301)
    count = MOSAIC_COLS * MOSAIC_ROWS
    shade = rng.uniform(0.9, 1.04, count)
    yellow = rng.uniform(0, 1, count) ** 2
    missing = rng.uniform(0, 1, count) < 0.012
    white = np.array([0.84, 0.83, 0.79])
    ivory = np.array([0.78, 0.72, 0.58])
    ink = np.array([0.055, 0.055, 0.06])
    base = white[None, None, :] * shade[hex_id][..., None]
    base = base * (1 - 0.45 * yellow[hex_id][..., None]) + ivory * (0.45 * yellow[hex_id][..., None])
    base = np.where(black[..., None], ink * shade[hex_id][..., None] * 1.1, base)

    joint = 0.08
    grout = edge < joint
    bevel = np.clip((edge - joint) / 0.25, 0, 1)
    height = np.where(grout, 0.0, 0.5 + 0.45 * bevel)
    cloud = tile_noise(n, 4.0, 1302)
    dirt = np.clip((cloud - 0.35) * 1.4, 0, 1)
    grout_rgb = np.array([0.36, 0.33, 0.28]) * (0.6 + 0.5 * tile_noise(n, 40.0, 1303))[..., None] * (1 - 0.35 * dirt[..., None])
    rgb = np.where(grout[..., None], grout_rgb, base)
    # A film of dirt over the whole floor, in clouds; heavier on the white than it shows on the black.
    rgb = rgb * (1 - 0.28 * dirt[..., None]) + np.array([0.33, 0.29, 0.22]) * (0.28 * dirt[..., None])
    # Lifted tesserae: the grey bed they were set in, sunk.
    bed = np.array([0.30, 0.29, 0.26]) * (0.8 + 0.3 * tile_noise(n, 60.0, 1304))[..., None]
    gone = missing[hex_id]
    rgb = np.where(gone[..., None], bed, rgb)
    height = np.where(gone, -0.3, height)

    rough = np.where(grout | gone, 0.95, 0.42 + 0.2 * dirt)
    ao = np.where(grout | gone, 0.6, 0.8 + 0.2 * bevel)
    arm = np.stack([ao, rough, np.zeros_like(rough)], axis=-1)
    save_set("bathroom_mosaic", np.clip(rgb, 0, 1), normal_from_height(height.astype(np.float32), 5.0), to_u8(arm))
    return np.clip(rgb, 0, 1)


def main():
    os.makedirs(PREVIEW, exist_ok=True)
    os.makedirs(TEXTURES, exist_ok=True)
    for name, make in (("bathroom_tiles", build_tiles), ("bathroom_mosaic", build_mosaic)):
        rgb = make()
        Image.fromarray(to_u8(rgb), "RGB").save(os.path.join(PREVIEW, name + ".png"))
        tiled = np.tile(rgb, (2, 2, 1))
        img = Image.fromarray(to_u8(tiled), "RGB")
        img.thumbnail((1024, 1024), Image.LANCZOS)
        img.save(os.path.join(PREVIEW, name + "_2x2.png"))
        print("{}: linear mean {}".format(name, np.round(linear_mean(rgb), 3)))
    print("Mosaic repeats every {:.2f} x {:.2f} cm".format(MOSAIC_W, MOSAIC_H))


if __name__ == "__main__":
    main()
