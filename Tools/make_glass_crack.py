"""
Bakes the fracture maps for the one struck pane in Room01's window.

Run with the system Python, not the Editor's — this script does not import unreal:

    python Tools/make_glass_crack.py

It writes into Art/Source/Generated/:

    glass_crack_mask.png     R: coverage  G: how brightly that stretch of split scatters
                             B: the milky halo of micro-fracture either side of it
    glass_crack_nor.png      the groove normals — what makes the lantern travel along a crack
                             as the player moves, instead of lighting the whole network flat
    glass_crack_preview.png  the same network lit by a fake lamp, for judging the shape without
                             a four-minute build

WHY A BAKED TEXTURE, when everything else in this room is generated at runtime:

A crack is a hairline, and this room has now learned twice over that no arrangement of boxes is a
hairline — a bar has to be thick enough to render, and anything thick enough to render shows its
side, has two square ends, and keeps one width the whole way.

The answer after that was to draw the network from the contour of a noise field, the way the wall
cracks and the cobwebs are drawn. That is the right trick for a wall and the wrong one here, and
the reason is worth writing down: the contour of a smooth field is a smooth meandering curve. It
wanders, it closes loops, it doubles back, and it has no idea where the stone hit. What came out
looked like a drawing of a crack because it is one — of smoke.

A fracture is not the contour of anything. It is a set of very nearly straight lines running out
of a single point, each kinking where the glass in front of it was weakest, forking where it had
energy to spare, and dying where it ran out; and those radials are then tied together by short
chords across the gaps between them. Straightness, and a common origin, are the whole of what the
eye reads as broken glass. A material graph cannot express "straight out of this point, with
kinks"; a generator can, and a texture is where a generator's output has to go.
"""

import math
import os

import numpy as np
from PIL import Image


HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(HERE)
OUT_DIR = os.path.join(PROJECT_DIR, "Art", "Source", "Generated")

# The map covers a square patch of glass the height of the pane, centred on the impact. Square,
# so it can be laid on a pane of any proportion without the star coming out elliptical; the
# height rather than the width, so the cracks running up and down leave the pane at the muntin —
# which is what a crack does when it reaches the frame — while the ones running across have room
# to taper away to nothing in the middle of the glass.
RES = 2048
PATCH_CM = 39.55
SEED = 1104

PX_PER_MM = RES / (PATCH_CM * 10.0)   # 5.2 pixels to the millimetre
CENTRE = np.array([0.5, 0.5])

# How wide a split is at the impact and at its tip.
#
# A hairline in a pane is a couple of tenths of a millimetre, and a couple of tenths of a
# millimetre is honest and invisible: at the distance the detective actually looks at this window
# — across the room, with the pane about three hundred pixels wide — a split that fine lands well
# under one pixel, the mip chain averages it into the glass, and the whole star disappears. This
# is the same trade the wall textures made at 09-18, for the same reason. These are the widths of
# a pane somebody has put a boot through rather than flicked with a stone, which is also what the
# room wants: the window is the first thing the detective sees when he opens his eyes.
W_TRUNK_MM = 0.90
W_TIP_MM = 0.30


def mm(width_mm):
    return width_mm * PX_PER_MM


def wrap(degrees):
    """Into -180..180, so one angle can be compared against another."""
    return (degrees + 180.0) % 360.0 - 180.0


def heading_of(point, origin=CENTRE):
    return math.degrees(math.atan2(point[1] - origin[1], point[0] - origin[0]))


def step_along(point, degrees, distance):
    radians = math.radians(degrees)
    return point + np.array([math.cos(radians), math.sin(radians)]) * distance


class Canvas:
    """
    Three fields, all accumulated by taking the larger value so that overlapping strokes join
    rather than stack: coverage (how much of a pixel the split takes), groove (how deep into the
    surface it goes, which the normals come from) and glint (how brightly that stretch scatters).
    """

    def __init__(self, res):
        self.res = res
        self.cov = np.zeros((res, res), np.float32)
        self.groove = np.zeros((res, res), np.float32)
        self.glint = np.zeros((res, res), np.float32)

    def stroke(self, p0, p1, w0_mm, w1_mm, bright):
        """One straight leg, antialiased, tapering from w0 to w1."""
        a = np.asarray(p0, dtype=np.float64) * self.res
        b = np.asarray(p1, dtype=np.float64) * self.res
        w0 = max(mm(w0_mm), 0.25)
        w1 = max(mm(w1_mm), 0.25)

        pad = max(w0, w1) * 1.6 + 2.0
        x0 = int(max(0, math.floor(min(a[0], b[0]) - pad)))
        x1 = int(min(self.res - 1, math.ceil(max(a[0], b[0]) + pad)))
        y0 = int(max(0, math.floor(min(a[1], b[1]) - pad)))
        y1 = int(min(self.res - 1, math.ceil(max(a[1], b[1]) + pad)))
        if x1 <= x0 or y1 <= y0:
            return

        gx = (np.arange(x0, x1 + 1, dtype=np.float32) - a[0])[None, :]
        gy = (np.arange(y0, y1 + 1, dtype=np.float32) - a[1])[:, None]
        dx, dy = float(b[0] - a[0]), float(b[1] - a[1])
        run = dx * dx + dy * dy
        if run < 1e-9:
            return

        t = np.clip((gx * dx + gy * dy) / run, 0.0, 1.0)
        px = gx - t * dx
        py = gy - t * dy
        dist = np.sqrt(px * px + py * py)
        width = w0 + (w1 - w0) * t

        # Half a pixel of feather, so a line narrower than a pixel arrives faint rather than
        # dotted. That is how a crack ends: it thins until there is nothing left of it.
        cov = np.clip(width * 0.5 + 0.5 - dist, 0.0, 1.0)
        # The groove is wider than the opening, because the glass either side of a split is
        # dished where it tore — and shallower on the hairlines, because a split a tenth of a
        # millimetre across dishes nothing.
        groove = np.clip(1.0 - dist / np.maximum(width * 1.35, 0.9), 0.0, 1.0) ** 1.4
        groove = groove * min(1.0, (w0 + w1) * 0.5 / mm(0.30))

        window = (slice(y0, y1 + 1), slice(x0, x1 + 1))
        ahead = cov > self.cov[window]
        self.glint[window] = np.where(ahead, bright, self.glint[window])
        np.maximum(self.cov[window], cov, out=self.cov[window])
        np.maximum(self.groove[window], groove, out=self.groove[window])

    def chip(self, rng, centre, radius, roughness=0.42):
        """
        The crushed spot where it was actually struck: a small piece of the surface driven out,
        with a conchoidal edge. Small — four or five millimetres. The previous pass put a bloom a
        third the width of the pane at the middle of the star, and a fracture does not glow.
        """
        c = np.asarray(centre) * self.res
        r = radius * self.res
        # Five harmonics round the circle rather than five bumps of the same size. Lobes that
        # all turn at the same rate mostly cancel, which is how the first version of this came
        # out as a clean white disc: a bullet-hole sticker at the middle of an otherwise
        # convincing star. A crushed zone has a coarse shape and a fine edge at the same time,
        # and that is what a falling series of harmonics is.
        phases = rng.uniform(0.0, 2.0 * math.pi, 5)
        weights = rng.uniform(0.45, 1.0, 5)

        pad = int(r * 2.0 + 3)
        x0 = int(max(0, c[0] - pad))
        x1 = int(min(self.res - 1, c[0] + pad))
        y0 = int(max(0, c[1] - pad))
        y1 = int(min(self.res - 1, c[1] + pad))
        gx = (np.arange(x0, x1 + 1, dtype=np.float32) - c[0])[None, :]
        gy = (np.arange(y0, y1 + 1, dtype=np.float32) - c[1])[:, None]
        dist = np.sqrt(gx * gx + gy * gy)
        angle = np.arctan2(gy, gx)

        wobble = np.zeros_like(dist)
        for harmonic, (phase, weight) in enumerate(zip(phases, weights), start=1):
            wobble = wobble + (weight / harmonic) * np.cos(harmonic * angle + phase)
        edge = r * (1.0 + roughness * wobble / 1.6)

        cov = np.clip(edge - dist + 0.5, 0.0, 1.0)
        window = (slice(y0, y1 + 1), slice(x0, x1 + 1))
        ahead = cov > self.cov[window]
        self.glint[window] = np.where(ahead, 1.0, self.glint[window])
        np.maximum(self.cov[window], cov, out=self.cov[window])
        np.maximum(self.groove[window], cov * 0.85, out=self.groove[window])


def walk(rng, canvas, start, heading, length, w_start, w_end, leash, branch_chance, depth=0,
         from_impact=True):
    """
    One crack, from where it started to where it ran out.

    The leash is the whole difference between this and the noise contour it replaces. At every
    step the crack may kink and wander — but never more than `leash` degrees off the straight
    line it is meant to be following. That single constraint is what makes a line read as
    radiating rather than as meandering: a crack that has strayed forty degrees looks hand-drawn
    however convincing each individual kink was.

    Which line it is held to depends on what it is. A radial is held to the line from the impact
    through wherever it has got to, because that is where its energy came from. A branch is held
    to its own course instead: leash a branch to the impact as well and it gets dragged back
    round until it runs parallel to the radial it left, and a pair of parallel cracks a few
    centimetres apart is a sliver of glass that would have fallen out.

    Returns its trail as (point, radius) pairs, so the chords between radials have something to
    hang off.
    """
    step = 0.009
    legs = max(2, int(round(length / step)))
    at = np.array(start, dtype=np.float64)
    angle = heading
    drift = rng.normal(0.0, 0.22)
    bright = float(np.clip(rng.uniform(0.62, 1.0), 0.0, 1.0))
    trail = [(at.copy(), float(np.linalg.norm(at - CENTRE)))]

    for leg in range(legs):
        f = leg / legs
        nf = (leg + 1) / legs

        angle += rng.normal(0.0, 1.1) + drift
        # Glass is not uniform, and a crack turns where it meets something harder than the last
        # thing it met. That turn is *sharp*: a corner, held, and then straight again. It is the
        # only kind of direction change a fracture makes, and the reason the drift either side of
        # it is kept small — a line that bends smoothly over a hand's width is a drawn line,
        # whatever it is bending around.
        if rng.random() < 0.11:
            angle += float(rng.choice([-1.0, 1.0])) * rng.uniform(5.0, 15.0)
        centreline = heading_of(at) if from_impact else heading
        angle = centreline + max(-leash, min(leash, wrap(angle - centreline)))

        nxt = step_along(at, angle, step)
        # A fracture spends the energy that drove it, the whole way out — but it spends it
        # slowly, and then all at once at the tip. At an exponent below one it spends it
        # immediately instead: the arm is down to half its width a fifth of the way along, and
        # the outer two thirds of every arm — which is most of the star — is too fine to survive
        # being drawn at a third of a pixel. What was left was a small dense knot with nothing
        # around it, on a pane the detective is looking at from across the room.
        w0 = w_start + (w_end - w_start) * (f ** 1.6)
        w1 = w_start + (w_end - w_start) * (nf ** 1.6)
        bright = float(np.clip(bright + rng.normal(0.0, 0.10), 0.34, 1.0))
        canvas.stroke(at, nxt, w0, w1, bright)

        if depth < 2 and f > 0.22 and rng.random() < branch_chance:
            side = float(rng.choice([-1.0, 1.0]))
            walk(rng, canvas, nxt,
                 angle + side * rng.uniform(14.0, 33.0),
                 length * (1.0 - f) * rng.uniform(0.28, 0.62),
                 w0 * 0.62, max(w_end * 0.6, W_TIP_MM * 0.7),
                 leash=17.0,
                 branch_chance=branch_chance * 0.45,
                 depth=depth + 1,
                 from_impact=False)

        at = nxt
        trail.append((at.copy(), float(np.linalg.norm(at - CENTRE))))
        if not (0.015 < at[0] < 0.985 and 0.015 < at[1] < 0.985):
            break

    return trail


def point_at(trail, radius):
    """Where a radial had got to when it was this far out. None if it never got that far."""
    if trail[-1][1] < radius:
        return None
    for point, r in trail:
        if r >= radius:
            return point
    return None


def chord(rng, canvas, a, b, width_mm):
    """
    One of the cracks that run *between* two radials.

    These are the other half of a spiderweb and they are not rings. A ring implies something
    circular happened; what actually happens is that the glass between two radials gives way
    along the shortest path across it. So each one is a short crossing bowed a little away from
    the impact, several of them stop partway, and plenty of the gaps have nothing across them.

    Drawn as a kinked walk rather than as a curve through control points. A bezier is smooth
    everywhere by construction, and a smooth arc a hand's width across is the single most drawn
    looking thing that can be put on a pane — it was most of what was wrong with the version
    before this one. The bow is still there; it is just carried by a run of straight legs that
    turn at corners, which is what the glass between two radials actually does.
    """
    out = (a + b) * 0.5 - CENTRE
    out = out / max(float(np.linalg.norm(out)), 1e-6)
    span = float(np.linalg.norm(b - a))
    bow = span * float(rng.uniform(0.04, 0.12))

    # Some of them stop partway across. A crack that gave up halfway is worth more than one more
    # complete polygon.
    reach = 1.0 if rng.random() > 0.28 else float(rng.uniform(0.45, 0.8))
    bright = float(rng.uniform(0.5, 0.92))

    legs = 7
    previous = a.copy()
    for i in range(1, legs + 1):
        t = (i / legs) * reach
        # The bow, as a height above the chord, plus a corner's worth of wander off it.
        lift = math.sin(math.pi * min(t, 1.0)) * bow
        point = a + (b - a) * t + out * lift
        point = point + rng.normal(0.0, span * 0.022, 2)
        taper = 1.0 if reach == 1.0 else (1.0 - (i / legs) * 0.55)
        canvas.stroke(previous, point, width_mm * taper, width_mm * taper, bright)
        previous = point


def fracture(canvas, rng):
    """The whole network: the chip, the radials out of it, and the chords across them."""
    # Impacts are not even. One side of the star always takes more of the blow than the other,
    # and a star with every arm the same length is a snowflake.
    strong = rng.uniform(0.0, 360.0)

    count = int(rng.integers(12, 16))
    spacing = 360.0 / count
    trails = []
    lengths = []

    for i in range(count):
        heading = i * spacing + rng.uniform(-spacing * 0.34, spacing * 0.34)
        bias = 0.5 + 0.5 * math.cos(math.radians(wrap(heading - strong)))
        # A third of them are stubs: cracks that started and got nowhere, which is what fills in
        # the middle of a star. Without them every arm is a long one and the network reads as
        # something laid out rather than something that happened.
        if rng.random() < 0.24:
            length = float(rng.uniform(0.04, 0.11))
        else:
            length = 0.14 + 0.42 * (0.45 + 0.55 * bias) * float(rng.uniform(0.65, 1.25))
        # Half the patch is the rebate, so the longest arms run into the frame and stop there,
        # which is what a crack does when it reaches something that is holding the glass.
        length = float(np.clip(length, 0.035, 0.55))

        trail = walk(rng, canvas,
                     start=step_along(CENTRE, heading, 0.009),
                     heading=heading,
                     length=length,
                     w_start=W_TRUNK_MM * float(rng.uniform(0.8, 1.15)),
                     w_end=W_TIP_MM,
                     leash=9.0,
                     branch_chance=0.05 + 0.05 * bias)
        trails.append(trail)
        lengths.append(trail[-1][1])

    # The chords. Tight and nearly complete near the middle, where the glass had nowhere to go;
    # sparse further out, where it did.
    for i in range(count):
        a_trail, b_trail = trails[i], trails[(i + 1) % count]
        reach = min(lengths[i], lengths[(i + 1) % count])
        for level, chance in ((0.17, 0.82), (0.32, 0.68), (0.5, 0.6), (0.7, 0.5), (0.89, 0.34)):
            if rng.random() > chance:
                continue
            a = point_at(a_trail, reach * level * float(rng.uniform(0.9, 1.1)))
            b = point_at(b_trail, reach * level * float(rng.uniform(0.9, 1.1)))
            if a is None or b is None:
                continue
            width = W_TRUNK_MM * (0.62 - 0.18 * level) * float(rng.uniform(0.78, 1.12))
            chord(rng, canvas, a, b, max(width, W_TIP_MM * 1.4))

    # The crushed zone: a handful of short splits with no order to them, and the chip itself.
    for _ in range(int(rng.integers(7, 12))):
        heading = float(rng.uniform(0.0, 360.0))
        walk(rng, canvas, step_along(CENTRE, heading, 0.006), heading,
             float(rng.uniform(0.016, 0.042)), W_TRUNK_MM * 0.5, W_TIP_MM,
             leash=22.0, branch_chance=0.0, depth=2, from_impact=True)
    canvas.chip(rng, CENTRE, float(rng.uniform(0.0050, 0.0072)), roughness=0.62)
    # And the flakes that came off around it. A blow does not take one tidy piece out of a pane.
    for _ in range(int(rng.integers(2, 5))):
        heading = float(rng.uniform(0.0, 360.0))
        canvas.chip(rng, step_along(CENTRE, heading, float(rng.uniform(0.006, 0.014))),
                    float(rng.uniform(0.0008, 0.0018)), roughness=0.7)


def border_fade(res, margin=0.045):
    """
    Nothing is allowed to reach the edge of the map still at full width.

    The sheet is exactly as tall as the pane, so top and bottom the edge of the map *is* the
    rebate and a split that runs off it has reached the frame, which is where splits stop. Left
    and right it is open glass, and a crack cut off square in the middle of a pane is the one
    kind of end a fracture never has. Half a centimetre of fade costs nothing and makes both
    edges behave.
    """
    axis = np.linspace(0.0, 1.0, res, dtype=np.float32)
    ramp = np.clip(np.minimum(axis, 1.0 - axis) / margin, 0.0, 1.0) ** 0.7
    return ramp[None, :] * ramp[:, None]


def blur_axis(field, radius):
    k = 2 * radius + 1
    padded = np.pad(field, ((0, 0), (radius, radius)), mode="constant")
    acc = np.cumsum(padded, axis=1, dtype=np.float32)
    acc = np.concatenate([np.zeros((field.shape[0], 1), np.float32), acc], axis=1)
    return (acc[:, k:] - acc[:, :-k]) / k


def blur(field, radius, passes=2):
    out = field
    for _ in range(passes):
        out = blur_axis(out, radius)
        out = blur_axis(np.ascontiguousarray(out.T), radius).T
    return np.ascontiguousarray(out)


def normals_from(groove, depth_px):
    """
    A V down the middle of every split. Both faces of the V are in there, which is the point: one
    of them catches the lantern and the other does not, so a crack glints along part of its
    length and goes dark along the rest, and which part changes as the player moves. A flat sheet
    with a line painted on it cannot do that, and that is most of why a painted one reads as
    painted.
    """
    height = -groove * depth_px
    dx = np.zeros_like(height)
    dy = np.zeros_like(height)
    dx[:, 1:-1] = (height[:, 2:] - height[:, :-2]) * 0.5
    dy[1:-1, :] = (height[2:, :] - height[:-2, :]) * 0.5

    nx, ny, nz = -dx, -dy, np.ones_like(height)
    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    return nx / length, ny / length, nz / length


def preview(cov, glint, halo, nx, ny, nz):
    """What it looks like lit, so the shape can be judged without building anything."""
    light = np.array([-0.42, -0.55, 0.72])
    light = light / np.linalg.norm(light)
    lambert = np.clip(nx * light[0] + ny * light[1] + nz * light[2], 0.0, 1.0)
    spec = lambert ** 42

    face = (0.30 + 0.85 * lambert + 1.3 * spec) * (0.55 + 0.45 * glint)
    alpha = np.clip(cov * 0.94 + halo * 0.10, 0.0, 1.0)

    pane = np.full_like(cov, 0.11)
    pane = pane + np.linspace(0.05, -0.02, cov.shape[0], dtype=np.float32)[:, None]
    return np.clip(pane * (1.0 - alpha) + np.clip(face, 0.0, 1.6) * alpha, 0.0, 1.0)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    rng = np.random.default_rng(SEED)

    canvas = Canvas(RES)
    fracture(canvas, rng)

    cov = np.clip(canvas.cov * border_fade(RES), 0.0, 1.0)
    glint = np.clip(canvas.glint, 0.0, 1.0)
    # The haze of micro-fracture that runs alongside a split. Tight, and faint: this is the knob
    # that turns into a glow the moment it is given any rope, and a glowing crack is a drawn one.
    halo = blur(cov, 8, passes=2)
    halo = np.clip(halo / max(float(halo.max()), 1e-6), 0.0, 1.0) ** 0.75

    mask = np.stack([cov, glint, halo], axis=-1)
    Image.fromarray((mask * 255.0 + 0.5).astype(np.uint8), mode="RGB").save(
        os.path.join(OUT_DIR, "glass_crack_mask.png"))

    nx, ny, nz = normals_from(canvas.groove, depth_px=1.7)
    normal = np.stack([nx * 0.5 + 0.5, ny * 0.5 + 0.5, nz * 0.5 + 0.5], axis=-1)
    Image.fromarray((normal * 255.0 + 0.5).astype(np.uint8), mode="RGB").save(
        os.path.join(OUT_DIR, "glass_crack_nor.png"))

    shot = preview(cov, glint, halo, nx, ny, nz)
    Image.fromarray((shot * 255.0 + 0.5).astype(np.uint8), mode="L").save(
        os.path.join(OUT_DIR, "glass_crack_preview.png"))

    print("crack covers {:.3f}% of the patch".format(100.0 * float((cov > 0.02).mean())))
    print("wrote " + OUT_DIR)


if __name__ == "__main__":
    main()
