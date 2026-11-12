"""
Generates the trees of the forest round the house, in Blender.

    blender --background --factory-startup --python Tools/make_forest.py

Writes Art/Source/Models/forest_<name>/forest_<name>.fbx (with an empty GENERATED file beside it,
so Tools/build_art.py re-imports it with its own normals) and a render of the whole set to
Saved/ForestPreview/forest.png. Run Tools/make_storm_art.py first: the trees are leafed from its
forest_foliage.png, and FOLIAGE below is that script's table of cells.

A tree here is a trunk and limbs as tapering tubes, and its foliage as cards: flat sheets carrying
a whole bough (spruce), a spray of needle tufts (pine) or a branch end of leaves (broadleaf), cut
out by the texture's alpha. From a window, at night, a tree is its outline against the sky, and an
outline of real boughs is what a stack of cones never had.

Every model stands on its origin, +Z up, one material slot ("Foliage": bark is in the same atlas).
Two UV sets: the first into the atlas; the second is for the wind (AStormForest's material):
U how far up the tree a vertex is (0 at the root, 1 at the top), V how loose it is (0 on the
trunk, 1 at a bough's tip). Normals are written, not computed: every card's point outward from the
tree's axis, so a crown shades as one rounded mass instead of as a heap of facets.
"""

import math
import os
import random
import sys

import bpy
from mathutils import Matrix, Vector

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODEL_DIR = os.path.join(ROOT, "Art", "Source", "Models")
PREVIEW = os.path.join(ROOT, "Saved", "ForestPreview")
ATLAS = os.path.join(ROOT, "Art", "Source", "Generated", "forest_foliage.png")

# Tools/make_storm_art.py: FOLIAGE. (u0, v0, u1, v1), v from the top of the picture.
FOLIAGE = {
    "spruce": (0.0, 0.0, 0.5, 0.5),
    "pine": (0.5, 0.0, 1.0, 0.5),
    "leaves": (0.0, 0.5, 0.5, 1.0),
    "twigs": (0.5, 0.5, 1.0, 0.75),
    "bark": (0.5, 0.75, 1.0, 1.0),
}
INSET = 0.006

UP = Vector((0.0, 0.0, 1.0))


class Tree:
    """Vertices in centimetres; faces as index tuples with a UV per corner."""

    def __init__(self, seed, height):
        self.rng = random.Random(seed)
        self.height = height
        self.verts = []
        self.normals = []
        self.loose = []
        self.faces = []
        self.uvs = []

    def vert(self, p, normal, loose):
        self.verts.append(Vector(p))
        self.normals.append(Vector(normal).normalized())
        self.loose.append(loose)
        return len(self.verts) - 1

    def face(self, indices, uvs):
        self.faces.append(tuple(indices))
        self.uvs.append([tuple(uv) for uv in uvs])

    # ---- wood ------------------------------------------------------------------------------

    def tube(self, path, radii, sides=8, loose=(0.0, 0.0)):
        """A limb swept along a path. Bark runs up it, mirrored every two and a half metres."""
        u0, v0, u1, v1 = FOLIAGE["bark"]
        path = [Vector(p) for p in path]
        rings = []
        travelled = 0.0
        normal = None
        for i, p in enumerate(path):
            a = path[max(i - 1, 0)]
            b = path[min(i + 1, len(path) - 1)]
            tangent = (b - a).normalized()
            if normal is None:
                normal = tangent.cross(UP if abs(tangent.z) < 0.9 else Vector((1, 0, 0))).normalized()
            normal = (normal - tangent * normal.dot(tangent)).normalized()
            binormal = tangent.cross(normal)
            if i > 0:
                travelled += (p - path[i - 1]).length
            wave = abs(((travelled / 250.0) % 2.0) - 1.0)
            v = v0 + INSET + (v1 - v0 - 2 * INSET) * wave
            looseness = loose[0] + (loose[1] - loose[0]) * i / max(len(path) - 1, 1)
            ring = []
            for k in range(sides + 1):
                angle = 2 * math.pi * k / sides
                out = normal * math.cos(angle) + binormal * math.sin(angle)
                index = self.vert(p + out * radii[i], out, looseness)
                ring.append((index, (u0 + INSET + (u1 - u0 - 2 * INSET) * k / sides, v)))
            rings.append(ring)
        for a, b in zip(rings, rings[1:]):
            for k in range(sides):
                quad = (a[k], a[k + 1], b[k + 1], b[k])
                self.face([q[0] for q in quad], [q[1] for q in quad])

    # ---- foliage ---------------------------------------------------------------------------

    def card(self, cell, base, along, across, sag=0.0, rows=1, axis=None, loose=(0.35, 1.0)):
        """
        One sheet of foliage: `base` is the middle of the picture's bottom edge (where the bough
        leaves the wood), `along` runs to its tip, `across` is its full width. With rows > 1 the
        sheet droops by `sag` (cm at the tip) along a curve, as a bough does under its own weight.
        """
        u0, v0, u1, v1 = FOLIAGE[cell]
        u0, u1, v0, v1 = u0 + INSET, u1 - INSET, v0 + INSET, v1 - INSET
        base, along, across = Vector(base), Vector(along), Vector(across)
        axis = Vector(axis) if axis is not None else Vector((0.0, 0.0, base.z))
        rows_of = []
        for r in range(rows + 1):
            t = r / rows
            centre = base + along * t - UP * sag * t * t
            row = []
            for side, u in ((-0.5, u0), (0.5, u1)):
                p = centre + across * side
                out = (p - axis)
                out.z = out.length * 0.55 + 20.0
                row.append((self.vert(p, out, loose[0] + (loose[1] - loose[0]) * t), (u, v1 + (v0 - v1) * t)))
            rows_of.append(row)
        for a, b in zip(rows_of, rows_of[1:]):
            quad = (a[0], a[1], b[1], b[0])
            self.face([q[0] for q in quad], [q[1] for q in quad])

    def cluster(self, cell, at, size, count, spread=0.5, lift=0.35, axis=None):
        """Sheets fanned out from one point in all directions: a branch end in leaf."""
        rng = self.rng
        at = Vector(at)
        for i in range(count):
            yaw = 2 * math.pi * (i + rng.uniform(-0.3, 0.3)) / count
            pitch = rng.uniform(-0.25, 0.9) * lift * 2.0
            along = Vector((math.cos(yaw) * math.cos(pitch), math.sin(yaw) * math.cos(pitch), math.sin(pitch)))
            side = along.cross(UP).normalized()
            twist = rng.uniform(-1.2, 1.2)
            across = (side * math.cos(twist) + along.cross(side) * math.sin(twist)) * size * rng.uniform(0.8, 1.1)
            start = at + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.6, 0.6))) * size * spread * 0.3
            self.card(cell, start - along * size * 0.12, along * size * rng.uniform(0.85, 1.15), across, axis=axis)

    # ---- output ----------------------------------------------------------------------------

    def build(self, name):
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata([tuple(v * 0.01) for v in self.verts], [], self.faces)
        mesh.update()
        atlas = mesh.uv_layers.new(name="Atlas")
        wind = mesh.uv_layers.new(name="Wind")
        for polygon, uvs in zip(mesh.polygons, self.uvs):
            for corner, loop_index in enumerate(polygon.loop_indices):
                u, v = uvs[corner]
                atlas.data[loop_index].uv = (u, 1.0 - v)
                index = mesh.loops[loop_index].vertex_index
                wind.data[loop_index].uv = (min(max(self.verts[index].z / self.height, 0.0), 1.0), self.loose[index])
            polygon.use_smooth = True
        mesh.normals_split_custom_set_from_vertices([tuple(n) for n in self.normals])
        material = bpy.data.materials.get("Foliage") or make_material()
        mesh.materials.append(material)
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        return obj


def crooked(rng, points, wander):
    """A path through `points` with each inner point pushed sideways: no trunk is a ruled line."""
    out = [Vector(points[0])]
    for p in points[1:-1]:
        out.append(Vector(p) + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), 0.0)) * wander)
    out.append(Vector(points[-1]))
    return out


def spruce(seed, height, skirt=0.16, girth=1.0):
    """Norway spruce: a mast, and whorl on whorl of drooping boughs down to near the ground."""
    tree = Tree(seed, height)
    rng = tree.rng
    steps = 9
    path = crooked(rng, [(0, 0, height * i / steps) for i in range(steps + 1)], height * 0.004)
    base_radius = height * 0.013 * girth
    tree.tube(path, [base_radius * (1.0 - i / steps) ** 0.8 + 1.2 for i in range(steps + 1)], sides=8)
    whorls = int(height / 62)
    turn = rng.uniform(0, 6.28)
    for w in range(whorls):
        t = (w + rng.uniform(0.0, 0.5)) / whorls              # 0 at the lowest whorl
        z = height * (skirt + (0.95 - skirt) * t)
        reach = (height * 0.2 * (1.0 - t) ** 0.85 + 55.0) * girth
        if t < 0.12:
            reach *= 0.55 + t * 3.5                            # the lowest boughs are thin and short
        boughs = 5 if t < 0.7 else 4
        turn += 0.9
        for b in range(boughs):
            if rng.random() < 0.07:
                continue
            yaw = turn + 2 * math.pi * b / boughs + rng.uniform(-0.25, 0.25)
            out = Vector((math.cos(yaw), math.sin(yaw), 0.0))
            length = reach * rng.uniform(0.78, 1.12)
            droop = math.radians(rng.uniform(8, 24) + 14 * (1.0 - t))
            along = (out * math.cos(droop) - UP * math.sin(droop)) * length
            side = out.cross(UP)
            base = Vector((0, 0, z)) + out * 4.0
            axis = (0.0, 0.0, z - length * 0.5)
            # The bough itself, lying out from the trunk and sagging; and what hangs under it.
            tree.card("spruce", base, along, side * length * 0.82, sag=length * 0.1, rows=2, axis=axis)
            hang = (-UP * 0.9 + out * 0.42).normalized()
            tree.card("spruce", base + along * 0.12, along * 0.9, hang * length * 0.55, sag=length * 0.1, rows=2, axis=axis)
    # The leader.
    for yaw in (0.3, 0.3 + math.pi / 2):
        side = Vector((math.cos(yaw), math.sin(yaw), 0.0))
        tree.card("spruce", (0, 0, height * 0.9), UP * height * 0.115, side * height * 0.07, loose=(0.5, 1.0))
    return tree


def pine(seed, height, lean=0.0):
    """Scots pine: a long bare trunk, a flat open crown of crooked limbs with tufts at their ends."""
    tree = Tree(seed, height)
    rng = tree.rng
    clear = height * rng.uniform(0.5, 0.6)
    steps = 10
    bend = Vector((math.cos(lean * 7.0), math.sin(lean * 7.0), 0.0)) * height * lean
    path = crooked(rng, [(bend.x * (i / steps) ** 2, bend.y * (i / steps) ** 2, height * 0.9 * i / steps) for i in range(steps + 1)], height * 0.012)
    base_radius = height * 0.014
    tree.tube(path, [base_radius * (1.0 - 0.75 * i / steps) for i in range(steps + 1)], sides=8)

    def trunk_at(z):
        f = min(max(z / (height * 0.9), 0.0), 1.0) * steps
        i = min(int(f), steps - 1)
        return path[i].lerp(path[i + 1], f - i)

    crown = trunk_at(height * 0.82)

    def limb(start, heading, length, radius, depth):
        mid = start + heading * length * 0.5 + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.3, 0.6))) * length * 0.16
        end = start + heading * length + UP * length * 0.12
        tree.tube([start, mid, end], [radius, radius * 0.7, radius * 0.4], sides=5, loose=(0.05 * depth, 0.3))
        if depth >= 2:
            tree.cluster("pine", end, rng.uniform(190, 260), 5, lift=0.45, axis=crown)
            tree.cluster("pine", mid, rng.uniform(150, 200), 3, lift=0.4, axis=crown)
            return
        for _ in range(rng.randint(2, 3)):
            turn = Matrix.Rotation(rng.uniform(-0.9, 0.9), 3, "Z") @ heading
            turn = (turn + UP * rng.uniform(-0.1, 0.45)).normalized()
            limb(start.lerp(end, rng.uniform(0.55, 1.0)), turn, length * rng.uniform(0.55, 0.75), radius * 0.55, depth + 1)
        tree.cluster("pine", end, rng.uniform(170, 230), 4, lift=0.45, axis=crown)

    count = rng.randint(6, 8)
    for i in range(count):
        z = clear + (height * 0.9 - clear) * (i + rng.uniform(0, 0.8)) / count
        yaw = i * 2.4 + rng.uniform(-0.4, 0.4)
        rise = rng.uniform(0.05, 0.55)
        heading = Vector((math.cos(yaw), math.sin(yaw), rise)).normalized()
        limb(trunk_at(z), heading, height * rng.uniform(0.13, 0.22), base_radius * 0.42, 0)
    tree.cluster("pine", path[-1], 240, 6, lift=0.6, axis=crown)
    # One or two dead stubs on the bare trunk.
    for _ in range(rng.randint(1, 3)):
        z = rng.uniform(0.3, 0.9) * clear
        yaw = rng.uniform(0, 6.28)
        out = Vector((math.cos(yaw), math.sin(yaw), 0.15))
        tree.tube([trunk_at(z), trunk_at(z) + out * rng.uniform(40, 110)], [5.0, 2.0], sides=4)
    return tree


def broadleaf(seed, height, spread=1.0, fork=0.3):
    """An oak or a beech: a short bole forking into limbs, and those again, leaf at every end."""
    tree = Tree(seed, height)
    rng = tree.rng
    base_radius = height * 0.021
    bole = height * fork
    path = crooked(rng, [(0, 0, 0), (0, 0, bole * 0.5), (0, 0, bole)], height * 0.012)
    tree.tube(path, [base_radius * 1.25, base_radius * 0.9, base_radius * 0.82], sides=9)
    crown = Vector((0.0, 0.0, height * 0.62))

    def limb(start, heading, length, radius, depth):
        mid = start + heading * length * 0.5 + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.5, 0.5))) * length * 0.13
        end = start + heading * length
        if end.z > height * 0.97:
            end.z = height * rng.uniform(0.9, 0.97)
        tree.tube([start, mid, end], [radius, radius * 0.78, radius * 0.56], sides=6 if depth < 2 else 4,
                  loose=(0.04 * depth, 0.04 * (depth + 1)))
        size = height * rng.uniform(0.12, 0.16)
        if depth >= 3:
            tree.cluster("leaves", end, size, 6, axis=crown)
            tree.cluster("twigs", mid, size * 0.9, 3, axis=crown)
            return
        for i in range(rng.randint(2, 3)):
            away = (end - crown)
            away.z *= 0.6
            turn = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.25, 0.9)))
            next_heading = (heading * 0.9 + turn * 0.75 * spread + away.normalized() * 0.35).normalized()
            limb(start.lerp(end, rng.uniform(0.6, 1.0)), next_heading, length * rng.uniform(0.6, 0.8), radius * 0.6, depth + 1)
        if depth >= 1:
            tree.cluster("leaves", end, size, 5, axis=crown)
        if depth == 2:
            tree.cluster("leaves", mid, size * 0.9, 4, axis=crown)

    count = rng.randint(3, 4)
    for i in range(count):
        yaw = 2 * math.pi * i / count + rng.uniform(-0.5, 0.5)
        rise = rng.uniform(0.75, 1.5)
        heading = Vector((math.cos(yaw) * spread, math.sin(yaw) * spread, rise)).normalized()
        limb(path[-1], heading, height * rng.uniform(0.2, 0.27), base_radius * 0.6, 0)
    limb(path[-1], Vector((rng.uniform(-0.15, 0.15), rng.uniform(-0.15, 0.15), 1.0)).normalized(), height * 0.26, base_radius * 0.55, 0)
    return tree


# name -> (builder, authored height in cm). Shared with StormForest.cpp: ForestTrees.
TREES = (
    ("forest_spruce_a", lambda: spruce(101, 2300.0)),
    ("forest_spruce_b", lambda: spruce(102, 1850.0, skirt=0.2, girth=1.12)),
    ("forest_spruce_c", lambda: spruce(103, 1150.0, skirt=0.08, girth=1.25)),
    ("forest_pine_a", lambda: pine(201, 2100.0, lean=0.03)),
    ("forest_pine_b", lambda: pine(202, 1750.0, lean=-0.05)),
    ("forest_oak_a", lambda: broadleaf(301, 1650.0, spread=1.15, fork=0.24)),
    ("forest_oak_b", lambda: broadleaf(302, 1350.0, spread=1.3, fork=0.2)),
    ("forest_beech_a", lambda: broadleaf(303, 2000.0, spread=0.8, fork=0.38)),
)


def make_material():
    material = bpy.data.materials.new("Foliage")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    bsdf = nodes.get("Principled BSDF")
    if os.path.isfile(ATLAS) and bsdf is not None:
        image = nodes.new("ShaderNodeTexImage")
        image.image = bpy.data.images.load(ATLAS)
        material.node_tree.links.new(image.outputs["Color"], bsdf.inputs["Base Color"])
        material.node_tree.links.new(image.outputs["Alpha"], bsdf.inputs["Alpha"])
        bsdf.inputs["Roughness"].default_value = 0.9
    for attribute, value in (("blend_method", "HASHED"), ("surface_render_method", "DITHERED")):
        try:
            setattr(material, attribute, value)
        except Exception:
            pass
    return material


def export(obj):
    folder = os.path.join(MODEL_DIR, obj.name)
    os.makedirs(folder, exist_ok=True)
    open(os.path.join(folder, "GENERATED"), "w").close()
    for other in bpy.context.scene.objects:
        other.select_set(other == obj)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=os.path.join(folder, obj.name + ".fbx"), use_selection=True,
                             object_types={"MESH"}, mesh_smooth_type="FACE", add_leaf_bones=False,
                             bake_anim=False, apply_unit_scale=True)


def render_preview(objects):
    os.makedirs(PREVIEW, exist_ok=True)
    scene = bpy.context.scene
    for engine in ("BLENDER_EEVEE", "BLENDER_EEVEE_NEXT"):
        try:
            scene.render.engine = engine
            break
        except Exception:
            continue
    scene.render.resolution_x, scene.render.resolution_y = 2000, 900
    world = bpy.data.worlds.new("sky")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.65, 0.8, 1.0)
    world.node_tree.nodes["Background"].inputs[1].default_value = 1.0
    scene.world = world
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 3.0
    sun.rotation_euler = (math.radians(55), 0.0, math.radians(30))
    scene.collection.objects.link(sun)
    for i, obj in enumerate(objects):
        obj.location = (i * 12.0 - (len(objects) - 1) * 6.0, 0.0, 0.0)
    camera = bpy.data.objects.new("camera", bpy.data.cameras.new("camera"))
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = len(objects) * 12.5
    camera.location = (0.0, -80.0, 12.0)
    camera.rotation_euler = (math.radians(90), 0.0, 0.0)
    scene.collection.objects.link(camera)
    scene.camera = camera
    scene.render.filepath = os.path.join(PREVIEW, "forest.png")
    bpy.ops.render.render(write_still=True)
    for obj in objects:
        obj.location = (0.0, 0.0, 0.0)


def main():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj)
    objects = []
    for name, builder in TREES:
        tree = builder()
        obj = tree.build(name)
        objects.append(obj)
        print("{}: {} triangles, {:.0f} cm".format(name, sum(len(f) - 2 for f in tree.faces), tree.height))
    if "--no-preview" not in sys.argv:
        try:
            render_preview(objects)
        except Exception as error:
            print("preview failed:", error)
    for obj in objects:
        export(obj)


main()
