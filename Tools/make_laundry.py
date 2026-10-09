"""
Builds the laundry room's appliances, furniture and household things in Blender, headless, and
exports each as an FBX the art pipeline imports.

    blender --background --factory-startup --python Tools/make_laundry.py [-- --only=a,b]

Nothing free is a 1960s front-loading washing machine, a gas water heater, a fireclay sink, a
clothes airer with shirts on it or a valve radio, and the rest of the house has shown what
primitives do to things like these (a pail built from a cylinder is a drum with a lid). So they are
modelled here: the machines with a real drum behind the porthole, the sink with an inside, cloth
hung over a rail as one folded sheet rather than a heap of slabs, heaps of laundry as one stuffed
skin per garment.

Outputs Art/Source/Models/<name>/<name>.fbx with an empty GENERATED file beside it (re-imported
every run with its own normals), and Workbench previews in Saved/LaundryPreview/.

Frame, as every prop in the house: centimetres, origin on the floor under the middle of the
footprint, front facing local +Y. Exceptions are noted on the model: wall-hung things have their
origin on the wall plane, the things that hang over a rail have theirs on the rail's axis, the
hinged doors theirs on the hinge.

The numbers the C++ also needs (ALaundryActor) are the constants below, and must stay in step with
LaundryActor.h: the machines' portholes and doors, the airer's rails, the window.

Material slots are set by name in C++. Their texel sizes here match the surface each is given
there, whose instances have their tiling at one:
    Paint, Wood   weathered_brown_planks (150cm)     Iron, Zinc   green_metal_rust (110cm)
    Ceramic       cracked_concrete_wall (104cm)      Cloth*, Towel*, Sheet, Cover   rough_linen at 34cm
    Wicker        weathered_brown_planks at 60cm      Label        the laundry_paper atlas, own UVs
Everything else is a flat colour or glass, where the UVs do not matter.
"""

import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blender_kit as K  # noqa: E402
from blender_kit import Part, at, blob, cbox, ellipsoid, lathe, prism, prism_xz, tube  # noqa: E402

PREVIEW_DIR = os.path.join(K.ROOT, "Saved", "LaundryPreview")

FLAT = 100.0
CLOTH = 34.0
SLOTS = {
    "Enamel": ((0.85, 0.83, 0.76, 1.0), FLAT),
    "Chrome": ((0.70, 0.70, 0.72, 1.0), FLAT),
    "Rubber": ((0.05, 0.05, 0.05, 1.0), FLAT),
    "Drum": ((0.35, 0.35, 0.36, 1.0), FLAT),
    "Glass": ((0.55, 0.65, 0.70, 1.0), FLAT),
    "Dial": ((0.85, 0.82, 0.72, 1.0), FLAT),
    "Ink": ((0.03, 0.03, 0.03, 1.0), FLAT),
    "Knob": ((0.10, 0.07, 0.05, 1.0), FLAT),
    "Shadow": ((0.02, 0.02, 0.02, 1.0), FLAT),
    "Paint": ((0.45, 0.46, 0.45, 1.0), 150.0),
    "Wood": ((0.45, 0.33, 0.22, 1.0), 150.0),
    "Iron": ((0.22, 0.16, 0.12, 1.0), 110.0),
    "Zinc": ((0.55, 0.56, 0.56, 1.0), 110.0),
    "Copper": ((0.55, 0.30, 0.18, 1.0), FLAT),
    "Brass": ((0.60, 0.45, 0.20, 1.0), FLAT),
    "Ceramic": ((0.85, 0.83, 0.78, 1.0), 104.0),
    "Cloth": ((0.80, 0.80, 0.78, 1.0), CLOTH),
    "ClothA": ((0.70, 0.78, 0.86, 1.0), CLOTH),
    "ClothB": ((0.86, 0.84, 0.80, 1.0), CLOTH),
    "ClothC": ((0.85, 0.55, 0.62, 1.0), CLOTH),
    "ClothD": ((0.45, 0.47, 0.50, 1.0), CLOTH),
    "TowelA": ((0.80, 0.78, 0.72, 1.0), CLOTH),
    "TowelB": ((0.55, 0.66, 0.70, 1.0), CLOTH),
    "Sheet": ((0.88, 0.86, 0.82, 1.0), CLOTH),
    "Cover": ((0.72, 0.70, 0.62, 1.0), CLOTH),
    "Wicker": ((0.62, 0.50, 0.32, 1.0), 60.0),
    "Label": ((0.85, 0.80, 0.68, 1.0), FLAT),
    "Card": ((0.55, 0.45, 0.32, 1.0), FLAT),
    "Bottle": ((0.25, 0.12, 0.05, 1.0), FLAT),
    "Cap": ((0.60, 0.10, 0.10, 1.0), FLAT),
    "Soap": ((0.80, 0.75, 0.55, 1.0), FLAT),
    "Bakelite": ((0.12, 0.07, 0.04, 1.0), FLAT),
    "Grille": ((0.45, 0.38, 0.26, 1.0), FLAT),
    "Strings": ((0.70, 0.66, 0.56, 1.0), FLAT),
    "Straw": ((0.70, 0.55, 0.30, 1.0), 150.0),
    "ThreadA": ((0.55, 0.08, 0.08, 1.0), FLAT),
    "ThreadB": ((0.08, 0.10, 0.30, 1.0), FLAT),
    "ThreadC": ((0.85, 0.80, 0.70, 1.0), FLAT),
    "Cushion": ((0.60, 0.10, 0.08, 1.0), FLAT),
    "Tin": ((0.30, 0.36, 0.30, 1.0), FLAT),
}

# ---------------------------------------------------------------------------------------------
# Shared with LaundryActor.h.
# ---------------------------------------------------------------------------------------------

# The machines: 62 wide, 60 deep, the body from 2 to 87. The porthole's centre and the door's hinge
# on the machine's front, in its own frame (front +Y, the hinge on the viewer's left, i.e. -X).
MACHINE_W = 62.0
MACHINE_D = 60.0
MACHINE_TOP = 87.0
PORT_Z = 44.0
PORT_R = 18.0
DOOR_R = 21.5
DOOR_HINGE_X = -DOOR_R
DOOR_HINGE_Y = MACHINE_D * 0.5 + 1.2

# The airer: an A-frame 110 long, its top bar at AIRER_TOP on the middle line, its feet at
# +-AIRER_SPLAY, and three rails a side at AIRER_RAILS heights.
AIRER_LENGTH = 110.0
AIRER_TOP = 96.0
AIRER_SPLAY = 30.0
AIRER_RAILS = (34.0, 56.0, 78.0)
AIRER_TUBE = 1.1

# The paper atlas (Tools/make_laundry_art.py): pixel rectangles (x, y, w, h) on a 2048 sheet.
ATLAS = {
    "detergent": (0, 0, 354, 512),
    "softener": (512, 0, 512, 370),
    "spray": (1024, 0, 410, 512),
    "starch": (1536, 0, 354, 512),
    "radio": (0, 512, 512, 200),
    "soap": (512, 512, 512, 256),
}


def atlas_uv(name, tu, tv):
    """A point on an atlas rectangle: tu across from its left, tv up from its bottom."""
    x, y, w, h = ATLAS[name]
    u = (x + tu * w) / 2048.0
    v_img = (y + (1.0 - tv) * h) / 2048.0
    return (u, 1.0 - v_img)


def face_label_uvs(name, x0, x1, z0, z1):
    """A label on a face looking along +Y: left to right is +X (as the viewer sees it), up is +Z."""
    def apply(bm):
        uv = bm.loops.layers.uv.verify()
        for face in bm.faces:
            for loop in face.loops:
                co = loop.vert.co
                tu = min(max((co.x - x0) / (x1 - x0), 0.0), 1.0)
                tv = min(max((co.z - z0) / (z1 - z0), 0.0), 1.0)
                loop[uv].uv = atlas_uv(name, tu, tv)
    return apply


def wrap_label_uvs(name, z0, z1, a0, a1):
    """A label wrapped round an axis up Z, its middle at +Y: the angle runs left to right."""
    def apply(bm):
        uv = bm.loops.layers.uv.verify()
        for face in bm.faces:
            for loop in face.loops:
                co = loop.vert.co
                a = math.atan2(co.x, co.y)
                tu = min(max((a - a0) / (a1 - a0), 0.0), 1.0)
                tv = min(max((co.z - z0) / (z1 - z0), 0.0), 1.0)
                loop[uv].uv = atlas_uv(name, tu, tv)
    return apply


def band(part, r_in, r_out, z0, z1, a0, a1, segments):
    """A closed sector of a thick-walled tube about Z, its middle at +Y: a label on a bottle."""
    bm = part.bm
    rings = []
    for k in range(segments + 1):
        a = a0 + (a1 - a0) * k / segments
        s, c = math.sin(a), math.cos(a)
        rings.append([bm.verts.new((r * s, r * c, z)) for r, z in ((r_out, z0), (r_out, z1), (r_in, z1), (r_in, z0))])
    for a, b in zip(rings, rings[1:]):
        for i in range(4):
            j = (i + 1) % 4
            bm.faces.new((a[i], b[i], b[j], a[j]))
    bm.faces.new(rings[0])
    bm.faces.new(rings[-1][::-1])


def cutter_of(part):
    """A second bmesh on the part, subtracted from it at the bake (blender_kit.bake)."""
    if part.cutter is None:
        part.cutter = bmesh.new()
    holder = Part("Shadow")
    holder.bm = part.cutter
    return holder


def lathe_y(part, profile, centre, sides=40):
    """A turned part whose axis is local +Y (the profile's height runs out of the front)."""
    lathe(part, profile, Matrix.Translation(centre) @ Matrix.Rotation(math.radians(-90.0), 4, "X"), sides=sides)


def drape(part, width, path, thick, seed, wave=0.6, wave_len=9.0, nx=26, fray=0.0):
    """
    A piece of cloth hung over something: a grid along X (width) by a polyline in YZ (path, from
    one hem over the support to the other), given its thickness and solidified into one closed
    shell. The folds are sine waves along X whose phase drifts down the drop, opening towards the
    hems the way cloth does, and nothing at the support, where the weight holds it flat. fray
    shortens the drop at each column by up to that many centimetres, so a hem is not a ruled line.
    """
    rng = random.Random(seed)
    pts = [Vector((0.0, y, z)) for y, z in path]
    # Arc length along the path, and a normal in YZ for each point (pointing out of the fold).
    lengths = [0.0]
    for a, b in zip(pts, pts[1:]):
        lengths.append(lengths[-1] + (b - a).length)
    total = lengths[-1]
    normals = []
    for i in range(len(pts)):
        a = pts[max(i - 1, 0)]
        b = pts[min(i + 1, len(pts) - 1)]
        t = (b - a).normalized()
        normals.append(Vector((0.0, t.z, -t.y)))
    # Where the support is: the highest point on the path. The folds die out towards it.
    top = max(range(len(pts)), key=lambda i: pts[i].z)
    phases = [rng.uniform(0, 2 * math.pi) for _ in range(3)]
    trims = [rng.uniform(0.0, fray) for _ in range(nx + 1)]
    bm = part.bm
    outer = []
    inner = []
    for ix in range(nx + 1):
        x = -width * 0.5 + width * ix / nx
        col_o = []
        col_i = []
        for i, p in enumerate(pts):
            s = abs(lengths[i] - lengths[top])
            from_end = min(lengths[i], total - lengths[i])
            # Fray: pull the hem rows up the drop towards the support.
            q = p
            if fray > 0.0 and from_end < trims[ix]:
                # Slide this point back along the path toward the support by the shortfall.
                j = i + 1 if i < top else i - 1
                q = p.lerp(pts[j], min(1.0, (trims[ix] - from_end) / max((pts[j] - p).length, 1e-3)))
            fade = min(1.0, s / 14.0)
            w = wave * fade * (math.sin(2 * math.pi * x / wave_len + phases[0] + s * 0.06)
                               + 0.5 * math.sin(2 * math.pi * x / (wave_len * 0.47) + phases[1] - s * 0.04))
            n = normals[i]
            base = Vector((x, q.y, q.z)) + n * w
            col_o.append(bm.verts.new(base + n * thick * 0.5))
            col_i.append(bm.verts.new(base - n * thick * 0.5))
        outer.append(col_o)
        inner.append(col_i)
    m = len(pts)
    for ix in range(nx):
        for i in range(m - 1):
            bm.faces.new((outer[ix][i], outer[ix + 1][i], outer[ix + 1][i + 1], outer[ix][i + 1]))
            bm.faces.new((inner[ix][i + 1], inner[ix + 1][i + 1], inner[ix + 1][i], inner[ix][i]))
    for i in range(m - 1):
        bm.faces.new((outer[0][i], outer[0][i + 1], inner[0][i + 1], inner[0][i]))
        bm.faces.new((inner[nx][i], inner[nx][i + 1], outer[nx][i + 1], outer[nx][i]))
    for ix in range(nx):
        bm.faces.new((outer[ix][0], inner[ix][0], inner[ix + 1][0], outer[ix + 1][0]))
        bm.faces.new((outer[ix + 1][m - 1], inner[ix + 1][m - 1], inner[ix][m - 1], outer[ix][m - 1]))


def over_rail(radius, front, back, step=2.0, splay=0.0):
    """A path in YZ over a rail of this radius at the origin: down the front (+Y) to -front, over,
    and down the back to -back. Splay leans the hems out from the vertical, as a heavy cloth does."""
    path = []
    n = max(2, int(front / step))
    for k in range(n, 0, -1):
        z = -front * k / n
        path.append((radius + splay * (-z / front) ** 1.5 * front * 0.1, z))
    for k in range(13):
        a = math.pi * k / 12
        path.append((radius * math.cos(a), radius * math.sin(a)))
    n = max(2, int(back / step))
    for k in range(1, n + 1):
        z = -back * k / n
        path.append((-radius - splay * (-z / back) ** 1.5 * back * 0.1, z))
    return path


def crumple(part, centre, radius, height, seed, base=None, folds=1.3, rings=10, sides=40, ragged=0.22, squash=(1.0, 1.0), yaw=0.0):
    """
    A garment dropped in a heap: one closed skin on a polar grid, a mound that falls to its ragged
    edge, ridged with folds running every way. base(x, y) is what it lies on (the floor of a basket,
    the curve of a drum), zero if not given. Laid over one another in different colours these read
    as a heap of separate clothes, where a heap of metaballs reads as balloons.
    """
    rng = random.Random(seed)
    base = base or (lambda x, y: 0.0)
    waves = [(rng.uniform(0, 2 * math.pi), rng.uniform(4.0, 9.0), rng.uniform(0, 2 * math.pi)) for _ in range(5)]
    edge = [(rng.uniform(0, 2 * math.pi), k + 2) for k in range(4)]
    rot = Matrix.Rotation(math.radians(yaw), 2)

    def outline(a):
        return radius * (1.0 + ragged * sum(math.sin(a * f + ph) / f for ph, f in edge))

    def fold(x, y):
        h = 0.0
        for d, wl, ph in waves:
            u = x * math.cos(d) + y * math.sin(d)
            h += 1.0 - abs(math.sin(math.pi * u / wl + ph))
        return folds * (h / len(waves) - 0.4)

    bm = part.bm
    top = []
    bottom = []
    for i in range(1, rings + 1):
        rho = i / rings
        ring_t = []
        ring_b = []
        for k in range(sides):
            a = 2 * math.pi * k / sides
            r = outline(a) * rho
            local = rot @ Vector((math.cos(a) * r * squash[0], math.sin(a) * r * squash[1]))
            x, y = centre[0] + local.x, centre[1] + local.y
            z0 = base(x, y) + centre[2]
            mound = height * (1.0 - rho ** 2) ** 0.6
            z = z0 + 0.35 + mound + fold(x, y) * (1.0 - rho ** 3)
            ring_t.append(bm.verts.new((x, y, max(z, z0 + 0.3))))
            ring_b.append(bm.verts.new((x, y, z0 + 0.05)))
        top.append(ring_t)
        bottom.append(ring_b)
    zc = base(centre[0], centre[1]) + centre[2]
    pole_t = bm.verts.new((centre[0], centre[1], zc + 0.35 + height + fold(centre[0], centre[1])))
    pole_b = bm.verts.new((centre[0], centre[1], zc + 0.05))
    for k in range(sides):
        j = (k + 1) % sides
        bm.faces.new((pole_t, top[0][k], top[0][j]))
        bm.faces.new((pole_b, bottom[0][j], bottom[0][k]))
        for i in range(rings - 1):
            bm.faces.new((top[i][k], top[i + 1][k], top[i + 1][j], top[i][j]))
            bm.faces.new((bottom[i][j], bottom[i + 1][j], bottom[i + 1][k], bottom[i][k]))
        bm.faces.new((top[-1][k], bottom[-1][k], bottom[-1][j], top[-1][j]))


# ---------------------------------------------------------------------------------------------
# The machines.
# ---------------------------------------------------------------------------------------------

def machine_body(kind):
    """
    The cabinet of a front loader: an enamelled steel box with rounded edges on four feet, the
    porthole's mouth sunk into its front with the drum behind it and the rubber gasket round the
    opening, a fascia across the top front with its dials. Origin on the floor, front +Y.
    """
    half_d = MACHINE_D * 0.5
    body = Part("Enamel", bevel=2.6, segments=4, angle=40.0)
    cbox(body, 0.0, 0.0, (2.0 + MACHINE_TOP) * 0.5, MACHINE_W, MACHINE_D, MACHINE_TOP - 2.0)
    # The drum's mouth is cut back through the front into the cabinet, the drum's own depth.
    cut = cutter_of(body)
    lathe_y(cut, [(0.0, -20.0), (PORT_R + 0.6, -20.0), (PORT_R + 0.6, half_d + 4.0), (0.0, half_d + 4.0)], (0.0, 0.0, PORT_Z), sides=48)
    # The drum: a thin shell inside the cut, open to the front, so the inside of it faces the eye.
    drum = Part("Drum")
    lathe_y(drum, [(0.0, -19.4), (PORT_R + 0.3, -19.4), (PORT_R + 0.3, half_d - 3.0), (PORT_R - 0.1, half_d - 3.0),
                   (PORT_R - 0.1, -19.0), (0.0, -19.0)], (0.0, 0.0, PORT_Z), sides=48)
    # Three lifters inside it, and a ring of holes as dark dots on the back.
    shadow = Part("Shadow")
    for k in range(3):
        a = 2 * math.pi * k / 3 + 0.3
        c, s = math.cos(a), math.sin(a)
        m = at(c * (PORT_R - 1.6), 0.0, PORT_Z + s * (PORT_R - 1.6), pitch=-math.degrees(a))
        cbox(drum, 0.0, 4.0, 0.0, 2.6, 40.0, 2.0, matrix=m)
    for k in range(18):
        a = 2 * math.pi * k / 18
        for r in (5.0, 10.0, 14.0):
            ellipsoid(shadow, (math.cos(a + r) * r, -18.9, PORT_Z + math.sin(a + r) * r), (0.45, 0.1, 0.45), rings=3, sides=6)
    # The gasket: a fat rubber roll round the mouth, its lip curling into the drum.
    rubber = Part("Rubber", smooth=True)
    lathe_y(rubber, [(PORT_R - 1.2, half_d - 5.0), (PORT_R + 0.4, half_d - 5.0), (PORT_R + 1.2, half_d - 1.2),
                     (PORT_R + 0.7, half_d + 0.4), (PORT_R - 0.6, half_d + 0.2), (PORT_R - 1.6, half_d - 2.4)],
            (0.0, 0.0, PORT_Z), sides=48)
    # The chrome bezel the door shuts against.
    chrome = Part("Chrome", bevel=0.3)
    lathe_y(chrome, [(PORT_R + 0.8, half_d - 0.4), (PORT_R + 3.6, half_d - 0.4), (PORT_R + 3.9, half_d + 0.5),
                     (PORT_R + 3.0, half_d + 0.9), (PORT_R + 1.2, half_d + 0.6)], (0.0, 0.0, PORT_Z), sides=48)
    # Hinge knuckles on the left of the mouth, the catch on the right.
    for z in (PORT_Z + 10.0, PORT_Z - 10.0):
        cbox(chrome, DOOR_HINGE_X - 1.0, half_d + 0.6, z, 4.0, 1.6, 4.0)
    cbox(chrome, DOOR_R + 1.0, half_d + 0.5, PORT_Z, 2.0, 1.2, 4.0)

    # The fascia: a strip proud of the front across the top, set back at a slope, with the dials.
    fascia = Part("Enamel", bevel=0.8)
    prism_xz(fascia, [(-29.0, 72.0), (29.0, 72.0), (29.0, 86.5), (-29.0, 86.5)], half_d - 0.5, half_d + 1.2)
    chrome_strip = Part("Chrome")
    cbox(chrome_strip, 0.0, half_d + 1.3, 72.6, 58.0, 0.5, 0.8)
    cbox(chrome_strip, 0.0, half_d + 1.3, 86.0, 58.0, 0.5, 0.8)
    dial = Part("Dial")
    ink = Part("Ink")
    knob = Part("Knob", bevel=0.25)
    if kind == "washer":
        # The programme dial on the right, a temperature knob and two push buttons on the left, and
        # the soap drawer's front at the far left.
        dials = [(16.0, 79.4, 5.6)]
        knobs = [(-4.0, 79.4, 2.6)]
        cbox(chrome, -20.0, half_d + 1.6, 79.4, 11.0, 0.8, 9.0)
        cbox(chrome, -20.0, half_d + 2.2, 79.4, 6.0, 0.8, 1.6)
        for x in (4.0, 8.0):
            cbox(knob, x, half_d + 1.8, 79.4, 2.4, 1.2, 2.4)
    else:
        # The timer: one big dial, a heat switch, and the lint trap's lid on the top.
        dials = [(14.0, 79.4, 6.4)]
        knobs = [(-14.0, 79.4, 2.4), (-6.0, 79.4, 2.4)]
        cbox(chrome, 0.0, -8.0, MACHINE_TOP + 0.3, 30.0, 10.0, 0.6)
        cbox(chrome, 0.0, -8.0, MACHINE_TOP + 0.8, 6.0, 2.0, 0.6)
        # The exhaust grille low on the front, where the warm air came out.
        for k in range(7):
            cbox(shadow, 0.0, half_d + 0.3, 7.0 + k * 1.6, 30.0, 0.6, 0.7)
    for x, z, r in dials:
        lathe_y(dial, [(0.0, half_d + 1.2), (r, half_d + 1.2), (r, half_d + 1.6), (0.0, half_d + 1.6)], (x, 0.0, z), sides=32)
        lathe_y(chrome, [(r, half_d + 1.0), (r + 0.8, half_d + 1.0), (r + 0.8, half_d + 2.0), (r, half_d + 2.0)], (x, 0.0, z), sides=32)
        lathe_y(knob, [(0.0, half_d + 1.6), (r * 0.42, half_d + 1.6), (r * 0.38, half_d + 4.0), (0.0, half_d + 4.2)], (x, 0.0, z), sides=20)
        cbox(knob, x, half_d + 4.0, z + r * 0.12, 0.8, 0.6, r * 0.7)
        for k in range(12):
            a = 2 * math.pi * k / 12
            cbox(ink, 0.0, 0.0, 0.0, 0.35, 0.1, r * 0.18,
                 matrix=at(x + math.sin(a) * r * 0.78, half_d + 1.65, z + math.cos(a) * r * 0.78, pitch=math.degrees(a)))
    for x, z, r in knobs:
        lathe_y(knob, [(0.0, half_d + 1.2), (r, half_d + 1.2), (r * 0.9, half_d + 3.2), (0.0, half_d + 3.4)], (x, 0.0, z), sides=18)
    # The feet: rubber cups, and the makers' badge as a chrome bar under the fascia.
    for sx in (-1, 1):
        for sy in (-1, 1):
            lathe(rubber, [(0.0, 0.0), (2.0, 0.0), (1.8, 2.2), (0.0, 2.4)], at(sx * 26.0, sy * 24.0, 0.0), sides=12)
    cbox(chrome, -18.0, half_d + 0.2, 66.0, 12.0, 0.6, 1.6)
    # The back: the hoses' stubs and the drain hose hook (against the wall, but seen from the side).
    copper = Part("Copper")
    for x in (-20.0, -13.0):
        lathe_y(copper, [(0.0, -half_d - 4.0), (1.1, -half_d - 4.0), (1.1, -half_d + 0.5), (0.0, -half_d + 0.5)], (x, 0.0, 80.0), sides=10)
    return [body, drum, shadow, rubber, chrome, fascia, chrome_strip, dial, ink, knob, copper]


def washer():
    return machine_body("washer")


def dryer():
    return machine_body("dryer")


def machine_door():
    """
    The porthole door: a chrome ring round a bowl of thick glass that bulges back into the drum, a
    handle on its free side. Origin on the hinge axis (vertical), the door shut across +X from it,
    its outer face looking along +Y. A positive yaw swings it open, out of the machine.
    """
    centre = (DOOR_R, 0.0, 0.0)
    chrome = Part("Chrome", bevel=0.4, segments=3)
    lathe_y(chrome, [(PORT_R - 2.0, 0.0), (DOOR_R, 0.0), (DOOR_R + 0.6, 1.4), (DOOR_R - 0.6, 3.6), (PORT_R - 0.8, 4.2),
                     (PORT_R - 2.4, 2.4)], centre, sides=56)
    # The glass: a closed shell, one winding (the cellar's rule), domed back into the drum.
    glass = Part("Glass")
    lathe_y(glass, [(0.0, -9.0), (6.0, -8.6), (11.0, -7.0), (14.6, -4.0), (16.4, -0.5), (16.6, 2.6), (16.0, 3.4),
                    (15.2, 2.4), (14.8, -0.3), (13.2, -3.4), (9.8, -6.0), (5.0, -7.4), (0.0, -7.8)], centre, sides=48)
    # The arm out to the hinge and the handle on the far side.
    cbox(chrome, 1.6, 0.6, 0.0, 3.4, 2.0, 7.0)
    knob = Part("Knob", bevel=0.5)
    cbox(knob, DOOR_R * 2.0 - 0.6, 2.6, 0.0, 3.0, 3.6, 9.0)
    return [chrome, glass, knob]


def wash_load():
    """
    What was in the drum when it stopped: the wash lying in the bottom of the drum the way it fell
    when it stopped turning, stiff now, and one sleeve come over the gasket's lip and hanging down
    the front — the door was opened, and nothing was taken out. In the machine's frame, so it is
    placed with it. Each garment its own piece, in its own colour.
    """
    half_d = MACHINE_D * 0.5
    inner = PORT_R - 0.2

    def drum_floor(x, y):
        # The bottom of the drum: a cylinder about Y through PORT_Z.
        return PORT_Z - math.sqrt(max(inner * inner - x * x, 0.0))
    parts = []
    for slot, centre, radius, height, seed, yaw in (("ClothB", (-3.0, -4.0, 0.0), 13.0, 7.0, 31, 10.0),
                                                     ("ClothA", (5.0, 6.0, 0.6), 11.0, 6.0, 32, -30.0),
                                                     ("ClothD", (-6.0, 14.0, 0.4), 9.0, 4.5, 33, 50.0),
                                                     ("ClothC", (6.0, -10.0, 1.0), 8.0, 5.0, 34, 80.0)):
        piece = Part(slot)
        crumple(piece, centre, radius, height, seed, base=drum_floor, folds=1.4, squash=(0.8, 1.4), yaw=yaw)
        parts.append(piece)
    # The sleeve: out of the heap, over the gasket's lip, down the front of the machine.
    sleeve = Part("ClothA")
    lip = PORT_Z - PORT_R + 0.6
    path = [(half_d - 14.0, lip + 4.0), (half_d - 6.0, lip + 2.6), (half_d - 1.0, lip + 1.4), (half_d + 1.2, lip + 0.8),
            (half_d + 2.6, lip - 1.0), (half_d + 3.2, lip - 6.0), (half_d + 3.4, lip - 12.0), (half_d + 3.3, lip - 18.0)]
    drape(sleeve, 9.0, path, 0.6, 35, wave=0.4, wave_len=5.0, nx=8)
    sleeve.bm.verts.ensure_lookup_table()
    bmesh.ops.transform(sleeve.bm, matrix=Matrix.Translation((-6.0, 0.0, 0.0)), verts=sleeve.bm.verts)
    parts.append(sleeve)
    return parts


# ---------------------------------------------------------------------------------------------
# The water heater, the sink and its taps, the valves, the drain.
# ---------------------------------------------------------------------------------------------

HEATER_R = 27.0
HEATER_TOP = 172.0


def heater():
    """
    A gas water heater of the fifties: a tall enamelled jacket on three legs over the burner, a
    domed top with the flue going up out of it and the two pipes, a thermostat on the front low
    down with its dial, the burner's sight door under it. Origin on the floor at the middle.
    """
    enamel = Part("Enamel", smooth=False)
    lathe(enamel, [(0.0, 22.0), (HEATER_R, 22.0), (HEATER_R + 0.6, 23.0), (HEATER_R + 0.6, HEATER_TOP - 14.0),
                   (HEATER_R, HEATER_TOP - 10.0), (HEATER_R - 4.0, HEATER_TOP - 4.0), (HEATER_R - 12.0, HEATER_TOP - 0.6),
                   (0.0, HEATER_TOP)], Matrix.Identity(4), sides=48)
    iron = Part("Iron")
    # The burner housing under the jacket, a ring of vents round it, and the legs.
    lathe(iron, [(0.0, 12.0), (HEATER_R - 1.0, 12.0), (HEATER_R - 0.4, 13.0), (HEATER_R - 0.4, 22.4), (0.0, 22.4)], Matrix.Identity(4), sides=40)
    shadow = Part("Shadow")
    for k in range(16):
        a = 2 * math.pi * k / 16
        cbox(shadow, 0.0, 0.0, 0.0, 3.0, 0.6, 2.0, matrix=at(math.sin(a) * (HEATER_R - 0.3), math.cos(a) * (HEATER_R - 0.3), 15.0, yaw=-math.degrees(a)))
    for k in range(3):
        a = 2 * math.pi * k / 3 + math.pi / 3
        tube(iron, [(math.sin(a) * (HEATER_R - 4.0), math.cos(a) * (HEATER_R - 4.0), 13.0),
                    (math.sin(a) * (HEATER_R + 1.0), math.cos(a) * (HEATER_R + 1.0), 0.6)], 1.3, sides=8)
        ellipsoid(iron, (math.sin(a) * (HEATER_R + 1.2), math.cos(a) * (HEATER_R + 1.2), 0.6), (2.4, 2.4, 0.6), rings=4, sides=10)
    # The flue: a draught hood on the dome, and the pipe up out of it.
    lathe(iron, [(0.0, HEATER_TOP - 1.0), (9.0, HEATER_TOP - 1.0), (11.0, HEATER_TOP + 4.0), (6.0, HEATER_TOP + 7.0),
                 (5.0, HEATER_TOP + 7.0), (5.0, HEATER_TOP + 40.0), (0.0, HEATER_TOP + 40.0)], Matrix.Identity(4), sides=24)
    # The pipes out of the top: cold in, hot out, each with a union and a stub, copper.
    copper = Part("Copper")
    for x in (-14.0, 14.0):
        z0 = HEATER_TOP - 6.0
        tube(copper, [(x, -4.0, z0 - 2.0), (x, -4.0, HEATER_TOP + 30.0)], 1.4, sides=12)
        lathe(copper, [(0.0, HEATER_TOP + 4.0), (2.2, HEATER_TOP + 4.0), (2.2, HEATER_TOP + 8.0), (0.0, HEATER_TOP + 8.0)],
              at(x, -4.0, 0.0), sides=8)
    # The thermostat on the front, low: a brass body, a dial, the gas cock and the pilot's tube.
    brass = Part("Brass", bevel=0.4)
    cbox(brass, 0.0, HEATER_R + 3.0, 30.0, 14.0, 6.0, 9.0)
    dial = Part("Dial")
    lathe_y(dial, [(0.0, HEATER_R + 6.0), (3.6, HEATER_R + 6.0), (3.6, HEATER_R + 6.4), (0.0, HEATER_R + 6.4)], (0.0, 0.0, 30.0), sides=24)
    knob = Part("Knob")
    lathe_y(knob, [(0.0, HEATER_R + 6.4), (1.6, HEATER_R + 6.4), (1.4, HEATER_R + 8.4), (0.0, HEATER_R + 8.6)], (0.0, 0.0, 30.0), sides=16)
    tube(copper, [(-7.0, HEATER_R + 3.0, 28.0), (-12.0, HEATER_R + 1.0, 24.0), (-14.0, HEATER_R - 0.5, 20.0)], 0.4, sides=6)
    tube(iron, [(7.0, HEATER_R + 3.0, 30.0), (16.0, HEATER_R + 2.0, 30.0), (22.0, HEATER_R - 4.0, 30.0)], 1.0, sides=10)
    # The sight door over the burner, and a maker's plate on the jacket.
    cbox(iron, 0.0, HEATER_R - 0.2, 17.5, 10.0, 1.4, 6.0)
    cbox(shadow, 0.0, HEATER_R + 0.6, 17.5, 4.0, 0.3, 2.0)
    cbox(brass, 0.0, HEATER_R + 0.5, 112.0, 12.0, 0.5, 7.0)
    # A drain cock at the bottom of the tank.
    tube(brass, [(-12.0, HEATER_R - 3.0, 26.0), (-12.0, HEATER_R + 4.0, 26.0)], 0.9, sides=8)
    return [enamel, iron, shadow, copper, brass, dial, knob]


SINK_TOP = 90.0


def sink():
    """
    A deep fireclay sink, the kind a laundry was built round: 76 x 50, 26 deep, glazed, the inside
    with a drain and an overflow slot, on two cast-iron brackets and a pair of iron legs at the
    front; the trap under it going into the floor. Origin on the floor, front +Y.
    """
    w, d, h = 76.0, 50.0, 26.0
    ceramic = Part("Ceramic", bevel=1.6, segments=3, angle=40.0)
    cbox(ceramic, 0.0, 0.0, SINK_TOP - h * 0.5, w, d, h)
    cut = cutter_of(ceramic)
    cbox(cut, 0.0, 0.0, SINK_TOP - h * 0.5 + 3.0, w - 7.0, d - 7.0, h)
    shadow = Part("Shadow")
    lathe(shadow, [(0.0, SINK_TOP - h + 3.05), (2.4, SINK_TOP - h + 3.05), (2.4, SINK_TOP - h + 3.2), (0.0, SINK_TOP - h + 3.2)],
          at(0.0, -4.0, 0.0), sides=20)
    cbox(shadow, 0.0, -d * 0.5 + 3.6, SINK_TOP - 6.0, 9.0, 0.3, 1.4)
    iron = Part("Iron")
    lathe(iron, [(2.4, SINK_TOP - h + 3.1), (3.2, SINK_TOP - h + 3.1), (3.2, SINK_TOP - h + 3.4), (2.4, SINK_TOP - h + 3.4)],
          at(0.0, -4.0, 0.0), sides=20)
    # The brackets: cast iron, a quarter-circle web, bolted to the wall at the back.
    for sx in (-1, 1):
        x = sx * (w * 0.5 - 10.0)
        pts = [(-d * 0.5, SINK_TOP - h - 1.0), (d * 0.5 - 4.0, SINK_TOP - h - 1.0), (d * 0.5 - 4.0, SINK_TOP - h - 4.0)]
        for k in range(1, 9):
            a = math.pi * 0.5 * k / 8
            pts.append((d * 0.5 - 4.0 - (d - 4.0) * (1.0 - math.cos(a)), SINK_TOP - h - 4.0 - 22.0 * math.sin(a)))
        pts.append((-d * 0.5, SINK_TOP - h - 26.0))
        bm = iron.bm
        near = [bm.verts.new((x - 1.0, y, z)) for y, z in pts]
        far = [bm.verts.new((x + 1.0, y, z)) for y, z in pts]
        bm.faces.new(near[::-1])
        bm.faces.new(far)
        for i in range(len(pts)):
            j = (i + 1) % len(pts)
            bm.faces.new((near[i], near[j], far[j], far[i]))
        # And the front legs.
        tube(iron, [(x, d * 0.5 - 6.0, SINK_TOP - h - 1.0), (x, d * 0.5 - 6.0, 2.0)], 1.6, sides=10)
        ellipsoid(iron, (x, d * 0.5 - 6.0, 1.2), (2.6, 2.6, 1.2), rings=4, sides=10)
    # The trap: down from the drain, round the bend, and back into the wall.
    path = [(0.0, -4.0, SINK_TOP - h + 1.0), (0.0, -4.0, 42.0)]
    for k in range(1, 9):
        a = math.pi * k / 8
        path.append((0.0, -4.0 - 5.0 * (1 - math.cos(a)), 42.0 - 5.0 * math.sin(a)))
    path += [(0.0, -14.0, 46.0), (0.0, -d * 0.5 - 2.0, 46.0)]
    tube(iron, path, 2.0, sides=12)
    return [ceramic, shadow, iron]


def taps():
    """
    Two bib taps on the wall over the sink, hot and cold on their pipes, which run up out of sight:
    brass gone brown, crossheads with the porcelain buttons, a rag of a washer drip run under each.
    Origin on the wall plane at the middle between them, the spouts' height; front +Y.
    """
    brass = Part("Brass", bevel=0.3)
    iron = Part("Iron")
    ceramic = Part("Ceramic")
    for x in (-11.0, 11.0):
        # The pipe down the wall into the back of the tap.
        tube(iron, [(x, 3.2, 70.0), (x, 3.2, 9.0), (x, 3.6, 6.0), (x, 6.0, 4.5)], 1.3, sides=10)
        for z in (40.0, 66.0):
            cbox(iron, x, 1.2, z, 5.0, 2.4, 1.6)
        # The body: a boss on the pipe, the spout curving down, the head and the crosshead.
        lathe_y(brass, [(0.0, 5.0), (2.0, 5.0), (2.0, 11.0), (0.0, 11.0)], (x, 0.0, 4.5), sides=16)
        tube(brass, [(x, 10.0, 4.5), (x, 12.6, 3.6), (x, 13.6, 1.0), (x, 13.6, -2.4)], 1.0, sides=10)
        lathe(brass, [(0.0, 5.0), (1.8, 5.0), (1.6, 11.0), (0.6, 12.0), (0.0, 12.2)], at(x, 8.0, 0.0), sides=14)
        for k in range(4):
            a = math.pi * 0.5 * k + math.radians(20.0 if x < 0 else -10.0)
            tube(brass, [(x, 8.0, 12.0), (x + math.cos(a) * 3.4, 8.0 + math.sin(a) * 3.4, 12.0)], 0.45, sides=6)
            ellipsoid(brass, (x + math.cos(a) * 3.5, 8.0 + math.sin(a) * 3.5, 12.0), (0.6, 0.6, 0.6), rings=4, sides=8)
        lathe(ceramic, [(0.0, 12.0), (0.9, 12.0), (0.9, 12.8), (0.0, 13.0)], at(x, 8.0, 0.0), sides=12)
    return [brass, iron, ceramic]


def valve():
    """A gate valve on a pipe running along X: a body, a bonnet, and a handwheel on top, its paint
    gone. Origin on the pipe's axis at the middle."""
    brass = Part("Brass", bevel=0.2)
    lathe(brass, [(0.0, -3.4), (2.0, -3.4), (2.4, -2.2), (2.4, 2.6), (1.4, 3.0), (1.2, 6.0), (0.0, 6.0)], Matrix.Identity(4), sides=16)
    lathe(brass, [(0.0, -3.6), (2.0, -3.6), (2.0, 3.6), (0.0, 3.6)], at(0, 0, 0, pitch=90.0), sides=16)
    iron = Part("Iron")
    ring = [(math.cos(a) * 4.2, math.sin(a) * 4.2, 6.6) for a in [2 * math.pi * k / 24 for k in range(25)]]
    tube(iron, ring, 0.5, sides=8)
    for k in range(4):
        a = 2 * math.pi * k / 4 + 0.4
        tube(iron, [(0.0, 0.0, 6.6), (math.cos(a) * 4.0, math.sin(a) * 4.0, 6.6)], 0.4, sides=6)
    ellipsoid(iron, (0.0, 0.0, 6.8), (0.9, 0.9, 0.6), rings=4, sides=8)
    return [brass, iron]


def drain():
    """A floor drain: a round iron grate with slots, a dark pit under it. Origin on the floor."""
    iron = Part("Iron", bevel=0.2)
    lathe(iron, [(0.0, 0.0), (11.0, 0.0), (11.4, 0.4), (10.6, 0.8), (0.0, 0.8)], Matrix.Identity(4), sides=40)
    cut = cutter_of(iron)
    for k in range(-3, 4):
        cbox(cut, k * 2.6, 0.0, 0.4, 1.1, 2.0 * math.sqrt(max(0.0, 8.6 ** 2 - (k * 2.6) ** 2)), 4.0)
    shadow = Part("Shadow")
    lathe(shadow, [(0.0, 0.05), (10.0, 0.05), (10.0, 0.15), (0.0, 0.15)], Matrix.Identity(4), sides=32)
    return [iron, shadow]


# ---------------------------------------------------------------------------------------------
# Furniture.
# ---------------------------------------------------------------------------------------------

COUNTER_L = 210.0
COUNTER_D = 62.0
COUNTER_TOP = 90.0


def counter():
    """The folding table: a scrubbed deal top two fingers thick on four square legs, aprons, and a
    slatted shelf low down where the baskets went. Origin on the floor, front +Y."""
    top = Part("Wood", bevel=0.8, segments=2)
    cbox(top, 0.0, 0.0, COUNTER_TOP - 2.5, COUNTER_L, COUNTER_D, 5.0)
    frame = Part("Wood", bevel=0.3)
    for sx in (-1, 1):
        for sy in (-1, 1):
            cbox(frame, sx * (COUNTER_L * 0.5 - 6.0), sy * (COUNTER_D * 0.5 - 5.0), (COUNTER_TOP - 5.0) * 0.5, 6.0, 6.0, COUNTER_TOP - 5.0)
    for sy in (-1, 1):
        cbox(frame, 0.0, sy * (COUNTER_D * 0.5 - 4.5), COUNTER_TOP - 10.0, COUNTER_L - 12.0, 2.4, 10.0)
    for sx in (-1, 1):
        cbox(frame, sx * (COUNTER_L * 0.5 - 6.0), 0.0, COUNTER_TOP - 10.0, 2.4, COUNTER_D - 10.0, 10.0)
    slats = Part("Wood", bevel=0.2)
    for k in range(6):
        y = -COUNTER_D * 0.5 + 6.0 + k * (COUNTER_D - 12.0) / 5
        cbox(slats, 0.0, y, 17.0, COUNTER_L - 12.0, 7.0, 2.0)
    for sx in (-1, 1):
        cbox(frame, sx * (COUNTER_L * 0.5 - 6.0), 0.0, 14.5, 4.0, COUNTER_D - 10.0, 3.0)
    return [top, frame, slats]


CABINET_W = 90.0
CABINET_D = 50.0
CABINET_H = 212.0


def cabinet_carcass(open_right):
    """A tall painted cupboard: plinth, two doors with a fielded panel each, a cornice. With
    open_right the right door is left off (it is its own model, hung ajar) and the inside shows:
    four shelves of linen and the things kept with it."""
    paint = Part("Paint", bevel=0.4)
    t = 2.0
    half_w = CABINET_W * 0.5
    half_d = CABINET_D * 0.5
    cbox(paint, -half_w + t * 0.5, 0.0, CABINET_H * 0.5, t, CABINET_D, CABINET_H)
    cbox(paint, half_w - t * 0.5, 0.0, CABINET_H * 0.5, t, CABINET_D, CABINET_H)
    cbox(paint, 0.0, -half_d + 1.0, CABINET_H * 0.5, CABINET_W, 2.0, CABINET_H)
    cbox(paint, 0.0, 0.0, 9.0, CABINET_W, CABINET_D, 2.0)
    cbox(paint, 0.0, 0.0, CABINET_H - 9.0, CABINET_W, CABINET_D, 2.0)
    # Plinth and cornice.
    cbox(paint, 0.0, half_d - 1.5, 4.0, CABINET_W, 3.0, 8.0)
    cornice = Part("Paint", bevel=0.6)
    prism_xz(cornice, [(-half_w - 2.5, CABINET_H - 8.0), (half_w + 2.5, CABINET_H - 8.0), (half_w + 2.5, CABINET_H),
                       (-half_w - 2.5, CABINET_H)], -half_d, half_d + 2.5)
    iron = Part("Iron")
    shadow = Part("Shadow")
    inside = []
    for side in ([-1] if open_right else [-1, 1]):
        door_leaf(side, paint, iron)
    if not open_right:
        # The gap between the two doors.
        cbox(shadow, 0.0, half_d + 1.0, CABINET_H * 0.5, 0.5, 2.2, CABINET_H - 21.0)
    if open_right:
        # The inside: dark, four shelves, and on them what was put away clean.
        dark = Part("Shadow")
        cbox(dark, 0.0, -half_d + 2.05, CABINET_H * 0.5, CABINET_W - 4.0, 0.2, CABINET_H - 20.0)
        sheets = Part("Sheet", bevel=1.4, segments=3, angle=30.0)
        towels = Part("TowelA", bevel=1.8, segments=3, angle=30.0)
        rng = random.Random(8)
        for k, z in enumerate((10.0, 58.0, 106.0, 154.0)):
            if k > 0:
                cbox(paint, 0.0, 0.0, z, CABINET_W - 4.0, CABINET_D - 4.0, 1.8)
            # Folded sheets stacked on the right of the shelf (the side the open door shows).
            top = z + 0.9
            for j in range(rng.randint(3, 5)):
                h = rng.uniform(3.4, 4.6)
                part = sheets if (k + j) % 3 else towels
                cbox(part, 0.0, 0.0, 0.0, 36.0, 34.0, h, matrix=at(18.0 + rng.uniform(-1.5, 1.5), rng.uniform(-2, 2), top + h * 0.5, yaw=rng.uniform(-3, 3)))
                top += h
            # And on the left, behind the shut door, more of the same.
            top = z + 0.9
            for j in range(rng.randint(2, 4)):
                h = rng.uniform(4.0, 5.5)
                cbox(towels, -19.0, rng.uniform(-2, 2), top + h * 0.5, 34.0, 32.0, h)
                top += h
        inside += [dark, sheets, towels]
    return [paint, cornice, iron, shadow] + inside


def door_leaf(side, paint, iron, origin_on_hinge=False):
    """One cupboard door, side -1 the left. Built in the carcass's frame, or, with origin_on_hinge,
    with its hinge on the origin and the leaf running along +X (the right door, hung by C++)."""
    half_w = CABINET_W * 0.5
    half_d = CABINET_D * 0.5
    w = half_w - 0.4
    z0, z1 = 11.0, CABINET_H - 10.0
    if origin_on_hinge:
        # The right door's hinge is at the carcass's right edge; mirrored so the leaf runs along -X
        # from it when shut, i.e. the free edge is at the middle.
        x0, x1 = -w, 0.0
        y = 0.0
    else:
        x0, x1 = (-half_w, -half_w + w) if side < 0 else (half_w - w, half_w)
        y = half_d
    cbox(paint, (x0 + x1) * 0.5, y + 1.0, (z0 + z1) * 0.5, x1 - x0, 2.0, z1 - z0)
    # The fielded panel: a raised field inside a frame of mouldings.
    m = 6.0
    cbox(paint, (x0 + x1) * 0.5, y + 2.4, (z0 + z1) * 0.5, x1 - x0 - 2 * m - 4.0, 0.8, z1 - z0 - 2 * m - 4.0)
    for zz in (z0 + m, z1 - m):
        cbox(paint, (x0 + x1) * 0.5, y + 2.4, zz, x1 - x0 - 2 * m + 1.0, 1.0, 1.2)
    for xx in (x0 + m, x1 - m):
        cbox(paint, xx, y + 2.4, (z0 + z1) * 0.5, 1.2, 1.0, z1 - z0 - 2 * m + 1.0)
    # The knob near the free edge, a keyhole under it, and the hinges at the hinged edge.
    free = x1 - 4.0 if (side < 0 and not origin_on_hinge) else x0 + 4.0
    hinge = x0 if (side < 0 and not origin_on_hinge) else x1
    lathe_y(iron, [(0.0, y + 2.0), (1.0, y + 2.0), (0.7, y + 3.6), (1.3, y + 4.4), (0.0, y + 4.8)], (free, 0.0, 112.0), sides=14)
    cbox(iron, free, y + 2.05, 104.0, 2.0, 0.2, 3.0)
    for zz in (30.0, CABINET_H - 30.0):
        cbox(iron, hinge, y + 1.0, zz, 1.4, 2.4, 8.0)


def cabinet():
    return cabinet_carcass(open_right=False)


def cabinet_open():
    return cabinet_carcass(open_right=True)


def cabinet_door():
    """The open cupboard's right door: hinge on the origin, the leaf along -X from it when shut (so
    a negative yaw swings it out), outer face along +Y."""
    paint = Part("Paint", bevel=0.4)
    iron = Part("Iron")
    door_leaf(1, paint, iron, origin_on_hinge=True)
    return [paint, iron]


SHELVES_W = 100.0
SHELVES_D = 36.0
SHELVES_H = 190.0
SHELVES_Z = (6.0, 50.0, 94.0, 138.0, 182.0)


def shelving():
    """An open shelf unit in plain deal: four posts, five boards, iron angle braces across the back."""
    wood = Part("Wood", bevel=0.3)
    for sx in (-1, 1):
        for sy in (-1, 1):
            cbox(wood, sx * (SHELVES_W * 0.5 - 2.5), sy * (SHELVES_D * 0.5 - 2.5), SHELVES_H * 0.5, 5.0, 5.0, SHELVES_H)
    boards = Part("Wood", bevel=0.4)
    for z in SHELVES_Z:
        cbox(boards, 0.0, 0.0, z, SHELVES_W - 1.0, SHELVES_D, 2.2)
    iron = Part("Iron")
    for a in (1, -1):
        tube(iron, [(-SHELVES_W * 0.5 + 4.0, -SHELVES_D * 0.5 + 0.6, 30.0 if a > 0 else 170.0),
                    (SHELVES_W * 0.5 - 4.0, -SHELVES_D * 0.5 + 0.6, 170.0 if a > 0 else 30.0)], 0.5, sides=6)
    return [wood, boards, iron]


WALL_SHELF_L = 150.0
WALL_SHELF_D = 24.0


def wall_shelf():
    """A shelf on two iron brackets. Origin on the wall plane, at the middle of the board's top."""
    wood = Part("Wood", bevel=0.5)
    cbox(wood, 0.0, WALL_SHELF_D * 0.5, -1.4, WALL_SHELF_L, WALL_SHELF_D, 2.8)
    iron = Part("Iron")
    for x in (-WALL_SHELF_L * 0.5 + 18.0, WALL_SHELF_L * 0.5 - 18.0):
        cbox(iron, x, 0.6, -14.0, 1.2, 1.2, 24.0)
        cbox(iron, x, WALL_SHELF_D * 0.45, -3.4, 1.2, WALL_SHELF_D * 0.9, 1.2)
        tube(iron, [(x, 1.0, -22.0), (x, 8.0, -12.0), (x, 15.0, -4.0)], 0.5, sides=6)
    return [wood, iron]


def stool():
    """A three-legged deal stool, 46 high: a thick round seat, splayed legs, a ring of stretchers."""
    wood = Part("Wood", bevel=0.8)
    lathe(wood, [(0.0, 42.5), (16.0, 42.5), (16.4, 44.0), (16.0, 46.0), (0.0, 46.0)], Matrix.Identity(4), sides=32)
    legs = Part("Wood")
    feet = []
    for k in range(3):
        a = 2 * math.pi * k / 3
        top = (math.cos(a) * 9.0, math.sin(a) * 9.0, 42.6)
        foot = (math.cos(a) * 17.0, math.sin(a) * 17.0, 0.0)
        tube(legs, [foot, top], [1.8, 1.6], sides=10)
        feet.append(foot)
    for k in range(3):
        a = Vector(feet[k]) * 0.62 + Vector((0, 0, 15.0))
        b = Vector(feet[(k + 1) % 3]) * 0.62 + Vector((0, 0, 15.0))
        tube(legs, [tuple(a), tuple(b)], 0.8, sides=8)
    return [wood, legs]


IRONING_L = 122.0


def ironing_board():
    """
    A wooden ironing board folded flat, as it was stood against the wall: the board with its
    pointed nose, the cover laced on over a pad, the legs folded flat under it. Stands on its square
    end with its length up +Z and the cover facing +Y; C++ leans it back against the wall.
    """
    pts = []
    for k in range(13):
        t = k / 12
        z = IRONING_L * 0.62 + (IRONING_L * 0.38) * t
        w = 18.0 * math.cos(t * math.pi * 0.5) ** 0.7
        pts.append((w, z))
    outline = [(-18.0, 0.0), (18.0, 0.0)] + [(x, z) for x, z in pts] + [(-x, z) for x, z in reversed(pts[:-1])]
    wood = Part("Wood", bevel=0.4)
    prism_xz(wood, [(x * 0.97, z) for x, z in outline], -1.0, 0.8)
    cover = Part("Cover", bevel=0.8, segments=3, angle=25.0)
    prism_xz(cover, [(x, z + (1.0 if z < 1.0 else 0.0)) for x, z in outline], 0.8, 2.6)
    iron = Part("Iron")
    for sx in (-1, 1):
        tube(iron, [(sx * 12.0, -2.4, 6.0), (-sx * 9.0, -2.6, 92.0)], 0.9, sides=8)
        tube(iron, [(sx * 13.0, -3.0, 4.0), (sx * 13.0, -3.0, 70.0)], 0.9, sides=8)
    tube(iron, [(-13.0, -3.0, 4.0), (13.0, -3.0, 4.0)], 0.9, sides=8)
    tube(iron, [(-9.0, -2.6, 92.0), (9.0, -2.6, 92.0)], 0.9, sides=8)
    rubber = Part("Rubber")
    for sx in (-1, 1):
        ellipsoid(rubber, (sx * 13.0, -3.0, 3.0), (1.6, 1.6, 2.2), rings=4, sides=8)
    return [wood, cover, iron, rubber]


def flat_iron():
    """An electric iron of the fifties, standing on its sole: a chrome shell over a pointed plate,
    a black handle, the heat dial, the flex. Origin under the middle of the sole, nose toward +Y."""
    outline = [(-5.6, -11.0), (5.6, -11.0), (6.0, -6.0), (5.2, 2.0), (3.2, 8.0), (0.0, 12.0), (-3.2, 8.0), (-5.2, 2.0), (-6.0, -6.0)]
    chrome = Part("Chrome", bevel=0.5, segments=3)
    prism(chrome, outline, 0.0, 1.4)
    shell = Part("Chrome", bevel=1.6, segments=4, angle=25.0)
    prism(shell, [(x * 0.92, y * 0.9 - 0.4) for x, y in outline], 1.4, 6.0)
    bakelite = Part("Bakelite", bevel=0.8, segments=3)
    tube(bakelite, [(0.0, -9.0, 6.0), (0.0, -8.0, 11.5), (0.0, -2.0, 13.2), (0.0, 4.0, 12.0), (0.0, 6.0, 6.0)], 1.5, sides=12)
    lathe(bakelite, [(0.0, 6.0), (2.2, 6.0), (2.0, 7.6), (0.0, 7.8)], at(0.0, 1.0, 0.0), sides=16)
    rubber = Part("Rubber")
    tube(rubber, [(0.0, -11.0, 6.0), (0.0, -15.0, 5.0), (3.0, -22.0, 1.0), (10.0, -28.0, 0.6), (18.0, -26.0, 0.6),
                  (24.0, -32.0, 0.6), (26.0, -42.0, 0.6)], 0.45, sides=8)
    return [chrome, shell, bakelite, rubber]


def airer():
    """
    A folding clothes airer: two frames of painted steel tube hinged at the top, three rails a side,
    the feet capped. Origin on the floor at the middle, its length along X.
    """
    iron = Part("Iron")
    half = AIRER_LENGTH * 0.5
    for sy in (-1, 1):
        for sx in (-1, 1):
            tube(iron, [(sx * half, sy * AIRER_SPLAY, 1.0), (sx * half, 0.0, AIRER_TOP)], AIRER_TUBE, sides=10)
        for z in AIRER_RAILS:
            y = sy * AIRER_SPLAY * (AIRER_TOP - z) / (AIRER_TOP - 1.0)
            tube(iron, [(-half, y, z), (half, y, z)], AIRER_TUBE * 0.8, sides=8)
    tube(iron, [(-half - 2.0, 0.0, AIRER_TOP), (half + 2.0, 0.0, AIRER_TOP)], AIRER_TUBE, sides=10)
    rubber = Part("Rubber")
    for sy in (-1, 1):
        for sx in (-1, 1):
            ellipsoid(rubber, (sx * half, sy * AIRER_SPLAY, 1.2), (1.6, 1.6, 1.4), rings=4, sides=8)
    # The stay that keeps it open, between the two frames at one end.
    tube(iron, [(half - 1.0, -AIRER_SPLAY * 0.55, 42.0), (half - 1.0, AIRER_SPLAY * 0.55, 42.0)], 0.4, sides=6)
    return [iron, rubber]


# ---------------------------------------------------------------------------------------------
# Cloth hung over a rail: origin on the rail's axis, the rail along X.
# ---------------------------------------------------------------------------------------------

SHIRT_HOOK = 7.0


def shirt(scale=1.0, slot="Cloth", seed=5):
    """
    A shirt on a wooden hanger, as it was hung on the airer to dry and never taken down: the body
    hung over the hanger's bar front and back, the sleeves down the sides from the shoulders, a
    little out from the body, the collar standing at the top. Origin on the hanger's bar; the hook
    rises SHIRT_HOOK over it, to hang on a rail.
    """
    s = scale
    wood = Part("Wood")
    tube(wood, [(-21.0 * s, 0.0, -2.6 * s), (-10.0 * s, 0.0, -0.6), (0.0, 0.0, 0.0), (10.0 * s, 0.0, -0.6), (21.0 * s, 0.0, -2.6 * s)],
         0.9, sides=8)
    iron = Part("Iron")
    tube(iron, [(0.0, 0.0, 0.0), (0.0, 0.0, SHIRT_HOOK - 3.0), (0.0, -1.4, SHIRT_HOOK - 0.6), (0.0, 0.2, SHIRT_HOOK + 0.4),
                (0.0, 1.6, SHIRT_HOOK - 1.2), (0.0, 1.4, SHIRT_HOOK - 2.4)], 0.22, sides=6)
    body = Part(slot)
    drape(body, 44.0 * s, over_rail(1.2, 70.0 * s, 72.0 * s, splay=0.15), 0.45, seed, wave=0.7 * s, wave_len=12.0 * s, nx=24, fray=0.0)
    # The shoulders slope: the body's top corners pulled down onto the hanger's arms.
    for v in body.bm.verts:
        if v.co.z > -6.0 * s:
            v.co.z -= (abs(v.co.x) / (22.0 * s)) ** 2 * 3.0 * s
    sleeves = Part(slot)
    for sx in (-1, 1):
        before = len(sleeves.bm.verts)
        path = [(0.6, 0.0), (0.8, -8.0), (1.2, -20.0), (1.4, -34.0), (1.2, -48.0), (1.0, -58.0)]
        drape(sleeves, 12.0 * s, [(y, z * s) for y, z in path], 0.45, seed + sx, wave=0.4, wave_len=6.0, nx=8)
        sleeves.bm.verts.ensure_lookup_table()
        # Out from the shoulder seam, angled away from the body a little, and in front of it.
        bmesh.ops.transform(sleeves.bm, matrix=at(sx * 22.5 * s, 1.4, -3.5 * s, pitch=sx * 7.0), verts=sleeves.bm.verts[before:])
    cuffs = Part(slot)
    for sx in (-1, 1):
        tip = Vector((sx * 22.5 * s, 2.6, -3.5 * s)) + Matrix.Rotation(math.radians(sx * 7.0), 3, "Y") @ Vector((0.0, 0.0, -58.0 * s))
        cbox(cuffs, 0, 0, 0, 12.4 * s, 1.6, 5.0 * s, matrix=at(tip.x, tip.y, tip.z + 2.6 * s, pitch=sx * 7.0))
    collar = Part(slot, bevel=0.3)
    for sx in (-1, 1):
        cbox(collar, 0, 0, 0, 7.0 * s, 0.6, 4.4 * s, matrix=at(sx * 3.4 * s, 2.0, -3.4 * s, yaw=sx * -18.0, pitch=sx * 24.0))
    cbox(collar, 0.0, -0.6, 1.2, 13.0 * s, 2.6, 3.2 * s)
    buttons = Part("Dial", smooth=True)
    for k in range(6):
        ellipsoid(buttons, (0.0, 2.0, -9.0 * s - k * 10.0 * s), (0.55, 0.25, 0.55), rings=3, sides=6)
    placket = Part(slot)
    cbox(placket, 0.0, 1.95, -38.0 * s, 3.0 * s, 0.3, 64.0 * s)
    return [wood, iron, body, sleeves, cuffs, collar, buttons, placket]


def child_top():
    """A girl's blouse, smaller, on her own hanger."""
    return shirt(scale=0.66, slot="ClothC", seed=9)


def towel_hung():
    """A towel thrown over a rail, the two ends uneven."""
    towel = Part("TowelB")
    drape(towel, 52.0, over_rail(1.2, 34.0, 28.0, splay=0.4), 0.9, 12, wave=1.0, wave_len=13.0)
    return [towel]


def pillowcase():
    """A pillowcase pegged on the line by one end and hanging, stiff."""
    case = Part("Sheet")
    drape(case, 48.0, over_rail(0.5, 30.0, 6.0, splay=0.2), 1.0, 13, wave=1.2, wave_len=16.0)
    return [case]


def socks():
    """A pair of a child's socks over a rail: each folded over it, the leg down one side and the foot,
    turned at the heel, down the other."""
    parts = []
    for k, x in enumerate((-4.6, 4.2)):
        sock = Part("ClothB")
        drape(sock, 6.6, over_rail(AIRER_TUBE * 0.8 + 0.4, 15.0 + k * 1.5, 9.0, step=1.5), 0.8, 40 + k, wave=0.25, wave_len=4.0, nx=6)
        sock.bm.verts.ensure_lookup_table()
        bmesh.ops.transform(sock.bm, matrix=Matrix.Translation((x, 0.0, 0.0)), verts=sock.bm.verts)
        # The heel and the toe, rounded: a sock is a tube, not a ribbon.
        heel = Part("ClothB", smooth=True)
        ellipsoid(heel, (x, -AIRER_TUBE - 1.4, -9.6), (3.3, 1.3, 1.6), rings=6, sides=10)
        ellipsoid(heel, (x, AIRER_TUBE + 1.4, -15.6 - k * 1.5), (3.3, 1.3, 1.4), rings=6, sides=10)
        parts += [sock, heel]
    return parts


# ---------------------------------------------------------------------------------------------
# Folded linen, laundry in heaps, baskets.
# ---------------------------------------------------------------------------------------------

def towel_stack():
    """Five towels folded and stacked, not quite square on each other. 40 x 28, about 28 tall."""
    rng = random.Random(14)
    a = Part("TowelA", bevel=2.2, segments=4, angle=25.0)
    b = Part("TowelB", bevel=2.2, segments=4, angle=25.0)
    z = 0.0
    for k in range(5):
        h = rng.uniform(5.0, 6.2)
        cbox(a if k % 2 == 0 else b, 0, 0, 0, 40.0 + rng.uniform(-1.5, 1.5), 28.0 + rng.uniform(-1, 1), h,
             matrix=at(rng.uniform(-1.2, 1.2), rng.uniform(-1.0, 1.0), z + h * 0.5, yaw=rng.uniform(-3.0, 3.0)))
        z += h - 0.4
    return [a, b]


def sheet_stack():
    """Three bed sheets folded into oblongs, the top one turned the other way."""
    rng = random.Random(15)
    sheet = Part("Sheet", bevel=1.4, segments=3, angle=25.0)
    z = 0.0
    for k in range(4):
        h = rng.uniform(3.6, 4.4)
        cbox(sheet, 0, 0, 0, 38.0, 30.0, h, matrix=at(rng.uniform(-1, 1), rng.uniform(-1, 1), z + h * 0.5, yaw=(90.0 if k == 3 else 0.0) + rng.uniform(-2, 2)))
        z += h - 0.3
    return [sheet]


def rag():
    """A cleaning rag dropped in a heap."""
    cloth = Part("ClothD", smooth=True)
    rng = random.Random(16)
    elements = []
    for k in range(9):
        elements.append(((rng.uniform(-9, 9), rng.uniform(-7, 7), 1.2 + rng.uniform(0, 1.6)), (rng.uniform(4, 7), rng.uniform(3, 6), 1.1),
                         at(0, 0, 0, yaw=rng.uniform(0, 180), roll=rng.uniform(-15, 15))))
    blob(cloth, elements, resolution=0.6)
    return [cloth]


BASKET_RX = 30.0
BASKET_RY = 21.0
BASKET_H = 28.0


def basket():
    """
    An oval wicker laundry basket, 60 x 42 x 28: willow stakes standing round a woven base, the
    weavers going in and out of them in rows, a thick rolled rim, a hand-hold at each end.
    """
    wicker = Part("Wicker")
    stakes = 36

    def ell(a, r_scale=1.0, flare=0.0):
        return (math.cos(a) * BASKET_RX * (r_scale + flare), math.sin(a) * BASKET_RY * (r_scale + flare))
    # The base: an oval plate.
    bm = wicker.bm
    n = 48
    bottom = [bm.verts.new((math.cos(2 * math.pi * k / n) * (BASKET_RX - 3.0), math.sin(2 * math.pi * k / n) * (BASKET_RY - 3.0), 0.0)) for k in range(n)]
    top = [bm.verts.new((v.co.x, v.co.y, 1.4)) for v in bottom]
    bm.faces.new(bottom[::-1])
    bm.faces.new(top)
    for k in range(n):
        j = (k + 1) % n
        bm.faces.new((bottom[k], bottom[j], top[j], top[k]))
    # Stakes, flaring out as they rise.
    for k in range(stakes):
        a = 2 * math.pi * k / stakes
        x0, y0 = ell(a, 0.9)
        x1, y1 = ell(a, 1.0)
        tube(wicker, [(x0, y0, 0.6), (x1, y1, BASKET_H)], 0.45, sides=6)
    # Weavers: rows round the side, each in and out of the stakes, leaving the hand-holds open.
    rows = 15
    for r in range(rows):
        z = 2.5 + r * (BASKET_H - 4.5) / (rows - 1)
        f = 0.9 + 0.1 * z / BASKET_H
        path = []
        for k in range(stakes * 4 + 1):
            a = 2 * math.pi * k / (stakes * 4)
            wobble = 0.6 * math.sin(a * stakes * 0.5 * 2 + r * math.pi)
            x, y = ell(a, f)
            nrm = Vector((math.cos(a) / BASKET_RX, math.sin(a) / BASKET_RY, 0.0)).normalized()
            path.append((x + nrm.x * wobble, y + nrm.y * wobble, z))
        # The hand-holds: two gaps in the upper rows at the ends.
        if z > BASKET_H - 10.0:
            seg = []
            segments = []
            for k, p in enumerate(path):
                a = 2 * math.pi * k / (stakes * 4)
                if abs(math.cos(a)) > 0.97:
                    if len(seg) > 1:
                        segments.append(seg)
                    seg = []
                else:
                    seg.append(p)
            if len(seg) > 1:
                segments.append(seg)
            for s in segments:
                tube(wicker, s, 0.75, sides=6)
        else:
            tube(wicker, path, 0.75, sides=6)
    # The rim: a fat roll of willow.
    rim = [(*ell(2 * math.pi * k / 96, 1.0), BASKET_H + 0.4) for k in range(97)]
    tube(wicker, rim, 1.5, sides=10)
    # The hand-hold bars over the gaps.
    for sx in (-1, 1):
        tube(wicker, [(sx * BASKET_RX * 1.0, -6.0, BASKET_H - 9.5), (sx * BASKET_RX * 1.01, 0.0, BASKET_H - 10.2),
                      (sx * BASKET_RX * 1.0, 6.0, BASKET_H - 9.5)], 1.0, sides=8)
    return [wicker]


def basket_clothes():
    """
    What is in the half-full basket: clothes thrown in unfolded, lying in layers in the bottom, a
    shirt's sleeve over the rim at one end and a girl's small dress over the long side. In the
    basket's frame.
    """
    parts = []
    for slot, centre, radius, height, seed, yaw in (("ClothD", (-4.0, 0.0, 1.4), 20.0, 5.0, 51, 0.0),
                                                     ("ClothB", (6.0, -3.0, 4.0), 16.0, 6.0, 52, 30.0),
                                                     ("ClothA", (-10.0, 4.0, 6.5), 12.0, 4.5, 53, -40.0),
                                                     ("ClothC", (12.0, 6.0, 8.0), 9.0, 4.0, 54, 70.0)):
        piece = Part(slot)
        crumple(piece, centre, radius, height, seed, folds=1.6, squash=(1.0, 0.7), yaw=yaw)
        parts.append(piece)
    # The sleeve, out over the rim at the +X end and down the outside.
    sleeve = Part("ClothB")
    rim = BASKET_H + 1.6
    # Over the rim's roll (its axis at BASKET_H + 0.4, 1.5 thick): the apex of the path at 0.
    path = [(-14.0, 13.0), (-8.0, 16.0), (-3.4, 22.0), (-1.6, rim - 0.4), (0.0, rim + 0.8), (1.6, rim - 0.4), (2.4, rim - 6.0),
            (2.6, rim - 13.0), (2.4, rim - 18.0)]
    drape(sleeve, 9.0, path, 0.6, 55, wave=0.4, wave_len=5.0, nx=8)
    sleeve.bm.verts.ensure_lookup_table()
    # The path runs along local Y; turned so it runs out along +X over the end of the oval.
    bmesh.ops.transform(sleeve.bm, matrix=at(BASKET_RX * math.sqrt(1.0 - (4.0 / BASKET_RY) ** 2), 4.0, 0.0, yaw=-90.0), verts=sleeve.bm.verts)
    parts.append(sleeve)
    # The dress over the long side (+Y): bodice in the basket, skirt down the outside.
    dress = Part("ClothC")
    path = [(-12.0, 9.0), (-6.0, 13.0), (-3.4, 21.0), (-1.6, rim - 0.4), (0.0, rim + 0.8), (1.6, rim - 0.4), (2.6, rim - 7.0),
            (3.2, rim - 15.0), (3.6, rim - 22.0)]
    drape(dress, 24.0, path, 0.5, 56, wave=0.9, wave_len=7.0, nx=20, fray=0.0)
    dress.bm.verts.ensure_lookup_table()
    bmesh.ops.transform(dress.bm, matrix=Matrix.Translation((-5.0, BASKET_RY * math.sqrt(1.0 - (5.0 / BASKET_RX) ** 2), 0.0)), verts=dress.bm.verts)
    # The skirt flares: widen it as it falls.
    for v in dress.bm.verts:
        if v.co.z < rim - 2.0 and v.co.y > BASKET_RY + 1.0:
            v.co.x = -5.0 + (v.co.x + 5.0) * (1.0 + (rim - v.co.z) * 0.02)
    parts.append(dress)
    return parts


# ---------------------------------------------------------------------------------------------
# Household things.
# ---------------------------------------------------------------------------------------------

def detergent(name="detergent", w=18.0, d=9.0, h=26.0):
    """A cardboard carton of soap powder, its top flaps open. The front face carries the printed
    label off the atlas; the rest is the card."""
    card = Part("Card", bevel=0.15)
    cbox(card, 0.0, 0.0, h * 0.5, w, d, h)
    for sy in (-1, 1):
        cbox(card, 0, 0, 0, w, 0.2, d * 0.5, matrix=at(0.0, sy * d * 0.5, h, roll=sy * -35.0) @ Matrix.Translation((0.0, 0.0, d * 0.25)))
    shadow = Part("Shadow")
    cbox(shadow, 0.0, 0.0, h - 0.1, w - 0.4, d - 0.4, 0.3)
    label = Part("Label")
    cbox(label, 0.0, d * 0.5 + 0.03, h * 0.5, w - 0.2, 0.06, h - 0.2)
    label.uv_fn = face_label_uvs(name, -w * 0.5, w * 0.5, 0.0, h)
    return [card, shadow, label]


def starch():
    return detergent("starch", 12.0, 6.0, 18.0)


def softener():
    """A tall glass bottle of fabric rinse with a screw cap and a paper label round the front."""
    glass = Part("Bottle")
    profile = [(0.0, 0.0), (4.2, 0.0), (4.6, 0.6), (4.6, 17.0), (4.2, 19.0), (2.6, 21.0), (1.8, 22.0), (1.8, 24.0), (0.0, 24.0)]
    lathe(glass, profile, Matrix.Identity(4), sides=24)
    cap = Part("Cap", bevel=0.2)
    lathe(cap, [(0.0, 23.4), (2.1, 23.4), (2.1, 26.0), (0.0, 26.0)], Matrix.Identity(4), sides=16)
    label = Part("Label")
    span = math.radians(150.0)
    band(label, 4.45, 4.66, 4.0, 15.0, -span / 2, span / 2, 20)
    label.uv_fn = wrap_label_uvs("softener", 4.0, 15.0, -span / 2, span / 2)
    return [glass, cap, label]


def spray():
    """A cleaner's spray bottle: a squat round body, a neck, the trigger head; a label on the front."""
    bottle = Part("Bottle", bevel=0.3)
    lathe(bottle, [(0.0, 0.0), (3.6, 0.0), (3.8, 0.6), (3.8, 15.0), (2.6, 17.4), (1.6, 18.6), (1.6, 19.6), (0.0, 19.6)], Matrix.Identity(4), sides=24)
    head = Part("Cap", bevel=0.4)
    lathe(head, [(0.0, 19.4), (1.9, 19.4), (1.9, 21.6), (0.0, 21.6)], Matrix.Identity(4), sides=14)
    cbox(head, 0.0, 1.0, 23.6, 2.6, 7.6, 3.6)
    cbox(head, 0.0, 4.6, 23.2, 1.4, 1.4, 1.4)
    tube(head, [(0.0, 2.4, 21.6), (0.0, 3.4, 19.0), (0.0, 3.0, 16.0)], 0.7, sides=8)
    label = Part("Label")
    span = math.radians(110.0)
    band(label, 3.78, 3.92, 3.0, 13.0, -span / 2, span / 2, 16)
    label.uv_fn = wrap_label_uvs("spray", 3.0, 13.0, -span / 2, span / 2)
    return [bottle, head, label]


def soap_dish():
    """A glazed soap dish with a worn bar on it, and a wrapped bar beside it. Origin under the dish."""
    ceramic = Part("Ceramic", bevel=0.4)
    lathe(ceramic, [(0.0, 0.0), (5.0, 0.0), (6.2, 0.4), (7.0, 1.8), (6.6, 2.0), (5.4, 0.9), (0.0, 0.8)],
          Matrix.Diagonal((1.35, 1.0, 1.0, 1.0)), sides=32)
    soap = Part("Soap", bevel=0.9, segments=4, angle=20.0)
    cbox(soap, 0.0, 0.0, 2.1, 7.6, 4.8, 2.2)
    wrapped = Part("Card", bevel=0.25)
    cbox(wrapped, 16.0, 1.0, 1.4, 9.0, 6.0, 2.8, matrix=None)
    label = Part("Label")
    cbox(label, 16.0, 1.0, 2.83, 8.8, 5.8, 0.06)
    x0, x1 = 16.0 - 4.4, 16.0 + 4.4

    def top_uvs(bm):
        uv = bm.loops.layers.uv.verify()
        for face in bm.faces:
            for loop in face.loops:
                co = loop.vert.co
                tu = min(max((co.x - x0) / (x1 - x0), 0.0), 1.0)
                tv = min(max((co.y - (1.0 - 2.9)) / 5.8, 0.0), 1.0)
                loop[uv].uv = atlas_uv("soap", tu, tv)
    label.uv_fn = top_uvs
    return [ceramic, soap, wrapped, label]


def peg():
    """A wooden clothes peg, lying flat: two legs, the spring between. 7.4 long, origin at its middle."""
    wood = Part("Wood", bevel=0.12)
    for sy in (-1, 1):
        bm = wood.bm
        pts = [(-3.7, 0.1), (3.7, 0.25), (3.7, 0.55), (0.8, 0.75), (0.0, 0.5), (-1.2, 0.8), (-3.7, 0.55)]
        near = [bm.verts.new((x, sy * y, 0.0)) for x, y in pts]
        far = [bm.verts.new((x, sy * y, 1.0)) for x, y in pts]
        bm.faces.new(near)
        bm.faces.new(far[::-1])
        for i in range(len(pts)):
            j = (i + 1) % len(pts)
            bm.faces.new((near[i], near[j], far[j], far[i]))
    iron = Part("Iron")
    coil = [(-0.4 + 0.8 * k / 30, 0.62 * math.cos(k * 0.9), 0.5 + 0.62 * math.sin(k * 0.9)) for k in range(31)]
    tube(iron, coil, 0.09, sides=5)
    return [wood, iron]


def peg_tin():
    """A round tin of pegs, a dozen standing up in it every way."""
    tin = Part("Tin")
    lathe(tin, [(0.0, 0.0), (6.0, 0.0), (6.2, 0.4), (6.2, 9.0), (6.5, 9.4), (6.0, 9.6), (5.8, 1.0), (0.0, 1.0)], Matrix.Identity(4), sides=28)
    wood = Part("Wood", bevel=0.1)
    iron = Part("Iron")
    rng = random.Random(17)
    for k in range(13):
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(0.0, 3.6)
        tilt = rng.uniform(5, 28)
        m = at(math.cos(a) * r, math.sin(a) * r, 6.0, yaw=math.degrees(a) + rng.uniform(-40, 40)) @ Matrix.Rotation(math.radians(90 - tilt), 4, "Y")
        for sy in (-1, 1):
            cbox(wood, 0.0, sy * 0.35, 0.0, 7.4, 0.55, 1.0, matrix=m)
        cbox(iron, 0.4, 0.0, 0.0, 0.8, 1.4, 1.1, matrix=m)
    return [tin, wood, iron]


def mop_bucket():
    """A galvanised bucket with the mop stood in it: the bucket turned, rolled rim, two beads, the
    bail down on one side; the mop's strings in it and over its rim, the handle leant out to -X."""
    zinc = Part("Zinc")
    h = 30.0
    profile = [(0.0, 0.0), (13.0, 0.0), (13.4, 0.8)]
    for bz in (9.0, 20.0):
        r = 13.4 + 3.2 * bz / h
        profile += [(r, bz - 0.6), (r + 0.4, bz), (r, bz + 0.6)]
    profile += [(16.6, h), (17.2, h + 0.4), (17.0, h + 1.0), (16.4, h + 0.4), (16.2, h - 0.2), (13.0, 1.4), (0.0, 1.2)]
    lathe(zinc, profile, Matrix.Identity(4), sides=40)
    iron = Part("Iron")
    for sy in (-1, 1):
        cbox(iron, 0.0, sy * 16.6, h - 4.0, 2.6, 1.2, 3.2)
    path = []
    for k in range(17):
        t = math.pi * k / 16
        path.append((-17.0 * math.sin(t) * 0.98, 16.6 * math.cos(t), h - 4.0 - 6.0 * math.sin(t)))
    tube(iron, path, 0.35, sides=6)
    wood = Part("Wood")
    tube(wood, [(-3.0, 1.0, 4.0), (-34.0, 3.0, 128.0)], 1.4, sides=10)
    strings = Part("Strings", smooth=True)
    rng = random.Random(18)
    for k in range(44):
        a = rng.uniform(0, 2 * math.pi)
        r0 = rng.uniform(0.5, 3.0)
        start = (-3.0 + math.cos(a) * r0, 1.0 + math.sin(a) * r0, 6.0)
        if k < 14:
            # Over the rim on the +X side and down the outside.
            ang = rng.uniform(-0.8, 0.8)
            rim = (math.cos(ang) * 17.2, math.sin(ang) * 17.2, h + 1.4)
            out = (math.cos(ang) * 19.2, math.sin(ang) * 19.2, h - rng.uniform(4.0, 12.0))
            path = [start, ((start[0] + rim[0]) * 0.5, (start[1] + rim[1]) * 0.5, h - 2.0), rim, out]
        else:
            end = (math.cos(a) * rng.uniform(5.0, 12.0), math.sin(a) * rng.uniform(5.0, 12.0), rng.uniform(1.6, 6.0))
            path = [start, ((start[0] + end[0]) * 0.5, (start[1] + end[1]) * 0.5, 7.0), end]
        tube(strings, K.catmull(path, 4), 0.55, sides=5)
    blob(strings, [((-3.0, 1.0, 6.5), (4.0, 4.0, 3.5), None)], resolution=0.6)
    return [zinc, iron, wood, strings]


def broom():
    """A corn broom standing on its sweeping edge: the straw fan sewn flat, the handle up +Z. C++
    leans it against a wall. Origin under the middle of the sweeping edge."""
    straw = Part("Straw")
    rng = random.Random(19)
    for face in (-1, 1):
        for i in range(30):
            u = (i + 0.5) / 30 * 2 - 1
            top = (face * 1.6, u * 7.0, 32.0)
            bottom = (face * rng.uniform(1.5, 2.6), u * 15.0 + rng.uniform(-0.6, 0.6), rng.uniform(0.0, 1.2))
            tube(straw, [bottom, top], 0.42, sides=5)
    core = Part("Straw")
    prism_xz(core, [(-14.0, 1.0), (14.0, 1.0), (7.0, 32.0), (-7.0, 32.0)], -1.6, 1.6)
    bm = core.bm
    for v in bm.verts:
        v.co = Vector((v.co.y, v.co.x, v.co.z))
    wood = Part("Wood")
    for z in (12.0, 20.0):
        w = 15.0 - (z / 32.0) * 8.0
        cbox(wood, 0.0, 0.0, z, 3.6, w * 2 + 0.6, 0.8)
    tube(wood, [(0.0, 0.0, 28.0), (0.0, 0.0, 142.0)], 1.5, sides=10)
    iron = Part("Iron")
    lathe(iron, [(0.0, 31.0), (2.6, 31.0), (2.2, 35.0), (0.0, 35.0)], Matrix.Identity(4), sides=12)
    return [straw, core, wood, iron]


def gloves():
    """A pair of rubber gloves left in a heap, one inside out at the cuff. Origin on the surface."""
    parts = []
    for k, (cx, cy, yaw) in enumerate(((0.0, 0.0, 20.0), (6.0, 5.0, -50.0))):
        glove = Part("Rubber", smooth=True)
        m = at(cx, cy, 0.0, yaw=yaw)
        elements = []

        def p(x, y, z):
            return tuple(m @ Vector((x, y, z)))
        # Palm, cuff, and four fingers and a thumb lying out of it, slack.
        elements.append((p(0.0, 0.0, 1.4), (4.6, 5.4, 1.2), m))
        for i in range(4):
            elements.append((p(-1.6 + i * 0.3, 7.0 + i * 1.6, 1.0), (3.6, 1.6, 1.0), m))
        for i in range(4):
            x = -3.3 + i * 2.2
            for j in range(3):
                elements.append((p(x + j * (0.2 if k else -0.3), 6.5 + j * 2.4, 0.9), (0.95, 1.4, 0.75), m))
        elements.append((p(5.0, 1.4, 0.9), (1.0, 1.6, 0.8), m))
        elements.append((p(6.6, 3.4, 0.9), (1.0, 1.6, 0.8), m))
        for j in range(3):
            elements.append((p(0.0, -6.0 - j * 3.0, 1.6 + j * 0.2), (4.8 + j * 0.3, 1.8, 1.6), m))
        blob(glove, elements, resolution=0.5)
        parts.append(glove)
    return parts


def sewing_kit():
    """A tin sewing box with its lid thrown back: reels of cotton, a pin cushion, scissors, a
    thimble, a card of buttons. Origin under the middle of the box; the hinge at -Y."""
    tin = Part("Tin")
    w, d, h = 22.0, 14.0, 6.5
    cbox(tin, 0.0, 0.0, 0.3, w, d, 0.6)
    for sx in (-1, 1):
        cbox(tin, sx * (w * 0.5 - 0.2), 0.0, h * 0.5, 0.4, d, h)
    for sy in (-1, 1):
        cbox(tin, 0.0, sy * (d * 0.5 - 0.2), h * 0.5, w, 0.4, h)
    # The lid, open past upright and leaning back on its hinge.
    cbox(tin, 0, 0, 0, w + 0.6, d + 0.6, 1.2, matrix=at(0.0, -d * 0.5, h, roll=105.0) @ Matrix.Translation((0.0, d * 0.5 + 0.3, 0.6)))
    parts = [tin]
    reels = [("ThreadA", -6.5, -2.5), ("ThreadB", -2.0, -2.5), ("ThreadC", 2.5, -2.5), ("ThreadB", 7.0, -3.0)]
    wood = Part("Wood")
    for slot, x, y in reels:
        thread = Part(slot)
        lathe(thread, [(0.0, 0.6), (1.5, 0.6), (1.5, 4.4), (0.0, 4.4)], at(x, y, 0.0), sides=14)
        lathe(wood, [(0.0, 0.5), (1.8, 0.5), (1.8, 1.0), (0.6, 1.0), (0.6, 4.2), (1.8, 4.2), (1.8, 4.7), (0.0, 4.7)], at(x, y, 0.0), sides=14)
        parts.append(thread)
    cushion = Part("Cushion", smooth=True)
    blob(cushion, [((-5.5, 3.5, 2.6), (3.4, 3.4, 2.2), None)], resolution=0.4)
    iron = Part("Iron")
    for k in range(5):
        a = 2 * math.pi * k / 5
        tube(iron, [(-5.5 + math.cos(a) * 1.6, 3.5 + math.sin(a) * 1.6, 3.6), (-5.5 + math.cos(a) * 2.6, 3.5 + math.sin(a) * 2.6, 6.4)], 0.05, sides=4)
    # The scissors lying across the reels, and the thimble.
    for s in (-1, 1):
        tube(iron, [(1.0, 4.0 + s * 0.4, 5.2), (10.0, 2.0 + s * 0.2, 5.0)], [0.35, 0.15], sides=6)
        ring = [(0.0 + math.cos(a) * 1.3, 4.6 + s * 1.6 + math.sin(a) * 1.0, 5.2) for a in [2 * math.pi * k / 14 for k in range(15)]]
        tube(iron, ring, 0.25, sides=5)
    chrome = Part("Chrome")
    lathe(chrome, [(0.0, 0.6), (0.9, 0.6), (0.9, 2.4), (0.6, 2.8), (0.0, 2.9)], at(5.0, 4.0, 0.0), sides=12)
    return parts + [wood, cushion, iron, chrome]


def toolbox():
    """A steel cantilever toolbox, 46 x 20 x 22, painted once, rusting through, its handle down."""
    paint = Part("Paint", bevel=0.4)
    w, d, h = 46.0, 20.0, 18.0
    prism_xz(paint, [(-w * 0.5, 0.0), (w * 0.5, 0.0), (w * 0.5, h), (-w * 0.5, h)], -d * 0.5, d * 0.5)
    lid = Part("Paint", bevel=0.4)
    prism_xz(lid, [(-w * 0.5 - 0.3, h), (w * 0.5 + 0.3, h), (w * 0.5 + 0.3, h + 2.0), (w * 0.5 - 3.0, h + 4.4),
                   (-w * 0.5 + 3.0, h + 4.4), (-w * 0.5 - 0.3, h + 2.0)], -d * 0.5 - 0.3, d * 0.5 + 0.3)
    iron = Part("Iron")
    for sx in (-1, 1):
        cbox(iron, sx * (w * 0.5 + 0.3), 0.0, h - 1.5, 0.6, 4.0, 5.0)
        tube(iron, [(sx * 12.0, 0.0, h + 4.4), (sx * 12.0, 7.6, h + 4.8)], 0.5, sides=6)
    tube(iron, [(-12.0, 7.6, h + 4.8), (12.0, 7.6, h + 4.8)], 0.7, sides=8)
    return [paint, lid, iron]


RADIO_W = 40.0


def radio():
    """
    A mantel radio of the fifties: a walnut cabinet with a rounded top, the speaker cloth behind a
    fretwork of three bars on the left, the tuning scale under glass on the right, two Bakelite
    knobs. Origin on the shelf under the middle; front +Y.
    """
    w, d, h = RADIO_W, 18.0, 25.0
    wood = Part("Wood", bevel=0.6, segments=3)
    pts = [(-w * 0.5, 0.0), (w * 0.5, 0.0), (w * 0.5, h - 6.0)]
    for k in range(1, 8):
        a = math.pi * 0.5 * k / 8
        pts.append((w * 0.5 - 6.0 + 6.0 * math.cos(a), h - 6.0 + 6.0 * math.sin(a)))
    for k in range(0, 8):
        a = math.pi * 0.5 + math.pi * 0.5 * k / 8
        pts.append((-w * 0.5 + 6.0 + 6.0 * math.cos(a), h - 6.0 + 6.0 * math.sin(a)))
    prism_xz(wood, pts, -d * 0.5, d * 0.5)
    cut = cutter_of(wood)
    cbox(cut, -7.0, d * 0.5, 13.0, 20.0, 2.0, 14.0)
    cbox(cut, 12.0, d * 0.5, 15.0, 11.0, 2.0, 6.0)
    grille = Part("Grille")
    cbox(grille, -7.0, d * 0.5 - 0.8, 13.0, 20.4, 0.4, 14.4)
    bars = Part("Wood", bevel=0.2)
    for z in (9.0, 13.0, 17.0):
        cbox(bars, -7.0, d * 0.5 - 0.3, z, 20.4, 0.8, 1.0)
    dial = Part("Label")
    cbox(dial, 12.0, d * 0.5 - 0.5, 15.0, 11.2, 0.1, 6.2)
    dial.uv_fn = face_label_uvs("radio", 12.0 - 5.6, 12.0 + 5.6, 15.0 - 3.1, 15.0 + 3.1)
    ink = Part("Ink")
    cbox(ink, 13.6, d * 0.5 - 0.3, 15.0, 0.25, 0.1, 5.4)
    knob = Part("Bakelite", bevel=0.2)
    for x in (8.0, 16.0):
        lathe_y(knob, [(0.0, d * 0.5), (1.9, d * 0.5), (1.7, d * 0.5 + 2.0), (0.0, d * 0.5 + 2.4)], (x, 0.0, 6.0), sides=18)
    for sx in (-1, 1):
        cbox(knob, sx * (w * 0.5 - 4.0), 0.0, -0.4, 4.0, d - 4.0, 0.8)
    rubber = Part("Rubber")
    tube(rubber, [(w * 0.5 - 6.0, -d * 0.5, 4.0), (w * 0.5 - 6.0, -d * 0.5 - 3.0, 3.0), (w * 0.5 - 4.0, -d * 0.5 - 6.0, 0.4)], 0.3, sides=6)
    return [wood, grille, bars, dial, ink, knob, rubber]


# ---------------------------------------------------------------------------------------------

MODELS = [
    ("laundry_washer", washer), ("laundry_dryer", dryer), ("laundry_machine_door", machine_door), ("laundry_wash_load", wash_load),
    ("laundry_heater", heater), ("laundry_sink", sink), ("laundry_taps", taps), ("laundry_valve", valve), ("laundry_drain", drain),
    ("laundry_counter", counter), ("laundry_cabinet", cabinet), ("laundry_cabinet_open", cabinet_open), ("laundry_cabinet_door", cabinet_door),
    ("laundry_shelving", shelving), ("laundry_wall_shelf", wall_shelf), ("laundry_stool", stool),
    ("laundry_ironing_board", ironing_board), ("laundry_iron", flat_iron), ("laundry_airer", airer),
    ("laundry_shirt", shirt), ("laundry_child_top", child_top), ("laundry_towel_hung", towel_hung), ("laundry_pillowcase", pillowcase),
    ("laundry_socks", socks), ("laundry_towel_stack", towel_stack), ("laundry_sheet_stack", sheet_stack), ("laundry_rag", rag),
    ("laundry_basket", basket), ("laundry_basket_clothes", basket_clothes),
    ("laundry_detergent", detergent), ("laundry_starch", starch), ("laundry_softener", softener), ("laundry_spray", spray),
    ("laundry_soap", soap_dish), ("laundry_peg", peg), ("laundry_peg_tin", peg_tin), ("laundry_mop_bucket", mop_bucket),
    ("laundry_broom", broom), ("laundry_gloves", gloves), ("laundry_sewing_kit", sewing_kit), ("laundry_toolbox", toolbox),
    ("laundry_radio", radio),
]

# Small and repeated: the pegs go in by the dozen.
NANITE = set()

BIG = {"laundry_washer", "laundry_dryer", "laundry_heater", "laundry_sink", "laundry_counter", "laundry_cabinet", "laundry_cabinet_open",
       "laundry_shelving", "laundry_stool", "laundry_ironing_board", "laundry_airer", "laundry_mop_bucket", "laundry_broom"}


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    only = [a.split("=", 1)[1] for a in args if a.startswith("--only=")]
    only = set(only[0].split(",")) if only else None
    baked = []
    for name, make in MODELS:
        if only and name not in only:
            continue
        obj = K.bake(make(), name, SLOTS)
        K.export(obj)
        folder = os.path.join(K.MODEL_DIR, name)
        open(os.path.join(folder, "GENERATED"), "w").close()
        marker = os.path.join(folder, "NANITE")
        if name in NANITE:
            open(marker, "w").close()
        elif os.path.exists(marker):
            os.remove(marker)
        baked.append(obj)
        print("LAUNDRY baked {} ({} verts)".format(name, len(obj.data.vertices)))

    # Previews: the big things in a row, and the small things on a grid.
    big = [o for o in baked if o.name in BIG]
    small = [o for o in baked if o not in big]
    x = 0.0
    for obj in big:
        width = obj.dimensions.x
        obj.location = (x + width * 0.5, 0.0, 0.0)
        x += width + 0.3
    for i, obj in enumerate(small):
        hung = "hung" in obj.name or obj.name in ("laundry_shirt", "laundry_child_top", "laundry_pillowcase", "laundry_socks")
        obj.location = (-1.0 - (i % 6) * 0.7, 3.0 + (i // 6) * 0.7, 0.8 if hung else 0.0)
    K.preview(baked, PREVIEW_DIR, "models", [
        ("big", (x * 0.5, -9.0, 2.4), (x * 0.5, 0.0, 0.8)),
        ("big_close", (x * 0.25, -4.0, 1.6), (x * 0.25, 0.0, 0.7)),
        ("small", (-2.75, 0.4, 2.6), (-2.75, 5.0, 0.2)),
        ("machines", (1.3, -3.4, 1.5), (1.3, 0.0, 0.6)),
        ("end", (x - 1.6, -3.8, 1.6), (x - 1.6, 0.0, 0.7)),
    ])


main()
