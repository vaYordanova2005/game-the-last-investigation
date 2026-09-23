#!/usr/bin/env python3
"""
Downloads the room's art from Poly Haven into Art/Source/.

Everything here is CC0 — public domain, free for commercial use, no account and no purchase —
which is what keeps the project's zero-production-cost rule intact. See Art/CREDITS.md.

    python Tools/fetch_assets.py            # fetch anything missing
    python Tools/fetch_assets.py --list     # show what the manifest wants, download nothing

Resumable: any file already on disk with a non-zero size is skipped, so an interrupted run just
continues. The downloads are gitignored — the imported .uasset in Content/ is what ships — so this
script is how a fresh clone gets the source art back.
"""

import argparse
import json
import os
import struct
import zlib
import sys
import time
import urllib.request

API = "https://api.polyhaven.com"
# Poly Haven rejects urllib's default Python user-agent with a 403, so every request identifies
# itself. Nothing sneaky — it is just asking to be treated as an ordinary client.
USER_AGENT = "TheLastInvestigation-assetfetch/1.0 (+github hobby game project)"
RESOLUTION = "2k"  # 4k quadruples the download and the VRAM for detail no one sees in a dark room

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXTURE_DIR = os.path.join(ROOT, "Art", "Source", "Textures")
MODEL_DIR = os.path.join(ROOT, "Art", "Source", "Models")

# Poly Haven texture id -> what it dresses in Room01.
TEXTURES = {
    "old_wooden_floor_02": "floorboards",
    "decrepit_wallpaper": "wallpaper over the plaster",
    # The wall itself. clay_plaster was here and was a mistake: it is a smooth, evenly troweled
    # modern finish, and photographed flat it is very nearly a single brown colour. On a wall it
    # read as no texture at all, which is precisely what it is. These three are damaged surfaces —
    # the damage is in the photograph rather than painted on top of it in geometry.
    "cracked_concrete_wall": "the plaster itself: flaked, cracked, filthy",
    "damaged_plaster": "brick and render behind the plaster, where it has come away",
    "plastered_stone_wall": "damp: the dark bloom that spreads from corners and the ceiling line",
    "ceiling_interior": "ceiling",
    "weathered_brown_planks": "door, beams, rough carpentry",
    "raw_plank_wall": "skirting and loose boards",
    "rough_linen": "curtains and cloth",
    "green_metal_rust": "iron: lock, hinges, tools",
}

# The three maps the master material wants. nor_dx because UE expects DirectX-convention normals;
# arm packs AO/Roughness/Metallic into one texture, which is the UE convention too.
TEXTURE_MAPS = ["Diffuse", "nor_dx", "arm"]

# Poly Haven model id -> what it becomes in the room.
MODELS = {
    "vintage_cabinet_01": "the chest of drawers against the wall",
    "wooden_bookshelf_worn": "the leaning bookcase",
    "book_encyclopedia_set_01": "books still on the shelf",
    "decorative_book_set_01": "books spilled on the floor",
    "WoodenChair_01": "the overturned chair",
    "WoodenTable_01": "the table under the window",
    "wall_clock": "the stopped clock",
    "hanging_picture_frame_01": "the picture frame",
    "Lantern_01": "the detective's lantern",
    "wooden_crate_01": "debris",
    # The bedroom corner. The room is where the detective wakes up, and until now he woke up on
    # bare boards in a room with no reason to be slept in; a bed is what makes the house a house
    # somebody lived in rather than a set of walls with furniture against them.
    "GothicBed_01": "the four-poster in the corner",
    "ArmChair_01": "the armchair beside it",
    "ClassicNightstand_01": "the nightstand",
    "GothicCabinet_01": "the press in the corner past the head of the bed",
}


def open_url(url, timeout):
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    return urllib.request.urlopen(request, timeout=timeout)


def fetch_json(url, attempts=4):
    for attempt in range(attempts):
        try:
            with open_url(url, 30) as response:
                return json.loads(response.read().decode("utf-8"))
        except Exception as error:
            if attempt == attempts - 1:
                raise
            print("    retrying {} ({})".format(url, error))
            time.sleep(2 * (attempt + 1))
    return None


def download(url, destination, attempts=4):
    """Returns True if the file was fetched now, False if it was already present."""
    if os.path.exists(destination) and os.path.getsize(destination) > 0:
        return False

    os.makedirs(os.path.dirname(destination), exist_ok=True)
    temporary = destination + ".part"

    for attempt in range(attempts):
        try:
            with open_url(url, 180) as response, open(temporary, "wb") as out:
                while True:
                    chunk = response.read(1 << 16)
                    if not chunk:
                        break
                    out.write(chunk)
            os.replace(temporary, destination)
            return True
        except Exception as error:
            print("    retrying {} ({})".format(os.path.basename(destination), error))
            time.sleep(2 * (attempt + 1))

    raise RuntimeError("failed to download " + url)


def fetch_textures(list_only):
    print("== Textures ({}) ==".format(len(TEXTURES)))
    for asset_id, role in TEXTURES.items():
        print("  {} — {}".format(asset_id, role))
        if list_only:
            continue

        files = fetch_json("{}/files/{}".format(API, asset_id))
        for map_name in TEXTURE_MAPS:
            entry = files.get(map_name)
            if not entry or RESOLUTION not in entry:
                print("    ! {} has no {} map".format(asset_id, map_name))
                continue

            formats = entry[RESOLUTION]
            # jpg over png: a third of the size, and the difference is invisible once the texture
            # has been block-compressed by the engine anyway.
            chosen = formats.get("jpg") or formats.get("png")
            destination = os.path.join(TEXTURE_DIR, "{}_{}.jpg".format(asset_id, map_name))
            if download(chosen["url"], destination):
                print("    + {}_{}".format(asset_id, map_name))


def fetch_models(list_only):
    print("== Models ({}) ==".format(len(MODELS)))
    for asset_id, role in MODELS.items():
        print("  {} — {}".format(asset_id, role))
        if list_only:
            continue

        files = fetch_json("{}/files/{}".format(API, asset_id))
        entry = files.get("fbx", {}).get(RESOLUTION, {}).get("fbx")
        if not entry:
            print("    ! {} has no {} fbx".format(asset_id, RESOLUTION))
            continue

        folder = os.path.join(MODEL_DIR, asset_id)
        if download(entry["url"], os.path.join(folder, os.path.basename(entry["url"]))):
            print("    + {} mesh".format(asset_id))

        # The mesh's own textures travel alongside it, under a relative path the fbx refers to.
        for relative_path, info in entry.get("include", {}).items():
            if download(info["url"], os.path.join(folder, relative_path.replace("/", os.sep))):
                print("    + {}".format(relative_path))


def write_neutral_arm():
    """An 8x8 flat AO/Roughness/Metallic map, written rather than downloaded.

    Poly Haven ships models with roughness and metallic as separate greyscales and no packed ARM,
    so a prop slot almost never has one — and a sampler with nothing in it does not render as
    nothing, it renders with the master material's default, which is one of our wall sets. Every
    prop in the room was taking its roughness and its ambient occlusion from a photograph of
    cracked concrete. This is what they get instead: white AO, mid roughness, no metal.

    Art/Source is gitignored, so it has to be produced here alongside the downloads rather than
    committed, or a fresh clone silently goes back to wearing the wall.
    """
    path = os.path.join(TEXTURE_DIR, "neutral_arm.png")
    if os.path.isfile(path):
        return

    # A minimal PNG: one IHDR, one IDAT, one IEND. Not worth a dependency for 64 pixels.
    size = 8
    pixel = b"\xff\x8c\x00"  # R = AO 1.0, G = roughness 0.55, B = metallic 0
    raw = b"".join(b"\x00" + pixel * size for _ in range(size))  # filter byte 0 per scanline

    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))

    if not os.path.isdir(TEXTURE_DIR):
        os.makedirs(TEXTURE_DIR)
    with open(path, "wb") as handle:
        handle.write(png)
    print("  + neutral_arm.png")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--list", action="store_true", help="show the manifest without downloading")
    args = parser.parse_args()

    fetch_textures(args.list)
    fetch_models(args.list)
    if not args.list:
        write_neutral_arm()

    if not args.list:
        print("Done. Import with Tools/build_art.py via the pythonscript commandlet.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
