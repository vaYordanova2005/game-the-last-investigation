"""
Shared building blocks for the Blender model scripts (Tools/make_nursery.py).

The same parts kit Tools/make_piano.py grew, pulled out so a second script does not copy it: a
Part is a bmesh in the game's own frame (centimetres, +Z up) with a material slot and an optional
bevel; the shape helpers add boxes, extrusions, turned and swept parts and metaball blobs to one;
bake() bevels, merges, UVs and mirrors them into Blender's frame and joins them into one object;
export() writes the FBX the art pipeline imports. make_piano.py keeps its own copy, untouched,
because the piano is finished and re-baking it is not this file's business.

Every function here runs inside Blender (bpy, bmesh, mathutils).
"""

import math
import os

import bmesh
import bpy
from mathutils import Matrix, Vector

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODEL_DIR = os.path.join(ROOT, "Art", "Source", "Models")

# Centimetres to metres, and Y mirrored because Unreal's FBX import mirrors it back.
TO_BLENDER = Matrix.Diagonal((0.01, -0.01, 0.01, 1.0))


class Part:
    def __init__(self, slot, bevel=0.0, segments=2, angle=35.0, smooth=False):
        self.bm = bmesh.new()
        self.slot = slot
        self.bevel = bevel
        self.segments = segments
        self.angle = angle
        # Blobs and cushions are smooth all over: no sharp edges marked anywhere on them.
        self.smooth = smooth
        self.cutter = None


def catmull(points, steps):
    out = []
    for i in range(len(points) - 1):
        p0 = Vector(points[max(i - 1, 0)])
        p1 = Vector(points[i])
        p2 = Vector(points[i + 1])
        p3 = Vector(points[min(i + 2, len(points) - 1)])
        for k in range(steps):
            t = k / steps
            t2, t3 = t * t, t * t * t
            p = 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3)
            out.append(tuple(p))
    out.append(tuple(points[-1]))
    return out


def at(x, y, z, yaw=0.0, pitch=0.0, roll=0.0):
    """Translation and rotation in degrees: roll about X, then pitch about Y, then yaw about Z."""
    return (Matrix.Translation((x, y, z)) @ Matrix.Rotation(math.radians(yaw), 4, "Z")
            @ Matrix.Rotation(math.radians(pitch), 4, "Y") @ Matrix.Rotation(math.radians(roll), 4, "X"))


def box(part, x0, x1, y0, y1, z0, z1, matrix=None, bm=None):
    bm = bm or part.bm
    verts = [bm.verts.new((x, y, z)) for z in (z0, z1) for y in (y0, y1) for x in (x0, x1)]
    for f in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)):
        bm.faces.new([verts[i] for i in f])
    if matrix is not None:
        bmesh.ops.transform(bm, matrix=matrix, verts=verts)
    return verts


def cbox(part, cx, cy, cz, sx, sy, sz, matrix=None, bm=None):
    """A box by its centre and size."""
    verts = box(part, cx - sx * 0.5, cx + sx * 0.5, cy - sy * 0.5, cy + sy * 0.5, cz - sz * 0.5, cz + sz * 0.5, bm=bm)
    if matrix is not None:
        bmesh.ops.transform(bm or part.bm, matrix=matrix, verts=verts)
    return verts


def prism(part, points, z0, z1, matrix=None):
    """An outline in XY (ccw), extruded up Z."""
    bottom = [part.bm.verts.new((x, y, z0)) for x, y in points]
    top = [part.bm.verts.new((x, y, z1)) for x, y in points]
    part.bm.faces.new(bottom[::-1])
    part.bm.faces.new(top)
    for i in range(len(points)):
        j = (i + 1) % len(points)
        part.bm.faces.new((bottom[i], bottom[j], top[j], top[i]))
    if matrix is not None:
        bmesh.ops.transform(part.bm, matrix=matrix, verts=bottom + top)
    return bottom + top


def prism_xz(part, points, y0, y1, matrix=None):
    """A profile drawn in XZ (ccw seen from -Y), extruded across Y from y0 to y1."""
    near = [part.bm.verts.new((x, y0, z)) for x, z in points]
    far = [part.bm.verts.new((x, y1, z)) for x, z in points]
    part.bm.faces.new(near)
    part.bm.faces.new(far[::-1])
    for i in range(len(points)):
        j = (i + 1) % len(points)
        part.bm.faces.new((near[j], near[i], far[i], far[j]))
    if matrix is not None:
        bmesh.ops.transform(part.bm, matrix=matrix, verts=near + far)
    return near + far


def lathe(part, profile, matrix, sides=32):
    """A turned part: profile is (radius, z) from the bottom up; zero radius closes a pole."""
    rings = []
    for r, z in profile:
        if r < 1e-4:
            rings.append([part.bm.verts.new((0.0, 0.0, z))])
        else:
            rings.append([part.bm.verts.new((r * math.cos(2 * math.pi * k / sides), r * math.sin(2 * math.pi * k / sides), z))
                          for k in range(sides)])
    for a, b in zip(rings, rings[1:]):
        if len(a) == 1 and len(b) == 1:
            continue
        if len(a) == 1:
            for k in range(sides):
                part.bm.faces.new((a[0], b[k], b[(k + 1) % sides]))
        elif len(b) == 1:
            for k in range(sides):
                part.bm.faces.new((a[k], a[(k + 1) % sides], b[0]))
        else:
            for k in range(sides):
                part.bm.faces.new((a[k], a[(k + 1) % sides], b[(k + 1) % sides], b[k]))
    if len(rings[0]) > 1:
        part.bm.faces.new(rings[0][::-1])
    if len(rings[-1]) > 1:
        part.bm.faces.new(rings[-1])
    verts = [v for ring in rings for v in ring]
    bmesh.ops.transform(part.bm, matrix=matrix, verts=verts)
    return verts


def tube(part, path, radius, sides=14):
    """A round rod swept along a polyline, with parallel-transported rings and flat ends."""
    path = [Vector(p) for p in path]
    radii = radius if isinstance(radius, (list, tuple)) else [radius] * len(path)
    tangents = []
    for i in range(len(path)):
        a = path[max(i - 1, 0)]
        b = path[min(i + 1, len(path) - 1)]
        tangents.append((b - a).normalized())
    up = Vector((0, 0, 1)) if abs(tangents[0].z) < 0.9 else Vector((1, 0, 0))
    normal = tangents[0].cross(up).normalized()
    rings = []
    for i, p in enumerate(path):
        if i > 0:
            normal = (normal - tangents[i] * normal.dot(tangents[i])).normalized()
        binormal = tangents[i].cross(normal)
        rings.append([part.bm.verts.new(p + (normal * math.cos(2 * math.pi * k / sides) + binormal * math.sin(2 * math.pi * k / sides)) * radii[i])
                      for k in range(sides)])
    for a, b in zip(rings, rings[1:]):
        for k in range(sides):
            part.bm.faces.new((a[k], a[(k + 1) % sides], b[(k + 1) % sides], b[k]))
    part.bm.faces.new(rings[0][::-1])
    part.bm.faces.new(rings[-1])


def ellipsoid(part, centre, radii, matrix=None, rings=12, sides=20):
    """A closed ellipsoid: buttons, noses, the bobbles of a toy."""
    profile = [(0.0, -1.0)]
    for i in range(1, rings):
        a = -math.pi * 0.5 + math.pi * i / rings
        profile.append((math.cos(a), math.sin(a)))
    profile.append((0.0, 1.0))
    m = Matrix.Translation(centre) @ (matrix or Matrix.Identity(4)) @ Matrix.Diagonal((radii[0], radii[1], radii[2], 1.0))
    return lathe(part, profile, m, sides)


def blob(part, elements, resolution=0.9, threshold=0.6):
    """
    A soft shape made of metaball ellipsoids fused into one skin: a plush toy, a stuffed backpack,
    the toe of a shoe. Overlapping closed shapes each keep their own outline, and a teddy built
    from spheres is a clutch of balls; metaballs share one surface that swells where they meet,
    which is what stuffing does.

    elements: (centre, radii, matrix or None) in centimetres. Resolution is in centimetres too.
    The radii are the surface's, for an element on its own: a metaball's surface sits well inside
    its radius of influence (at threshold 0.6, at 0.571 of it — measured), so they are divided
    back out here. Where elements meet they swell past that, as stuffing does.
    """
    reach = {0.6: 0.5707}.get(round(threshold, 2), 0.5707)
    name = "blob{}".format(len(bpy.data.metaballs))
    data = bpy.data.metaballs.new(name)
    data.resolution = resolution
    data.render_resolution = resolution
    data.threshold = threshold
    obj = bpy.data.objects.new(name, data)
    bpy.context.scene.collection.objects.link(obj)
    for centre, radii, matrix in elements:
        element = data.elements.new(type="ELLIPSOID")
        element.co = centre
        element.radius = max(radii) / reach
        element.size_x = radii[0] / max(radii)
        element.size_y = radii[1] / max(radii)
        element.size_z = radii[2] / max(radii)
        if matrix is not None:
            element.rotation = matrix.to_quaternion()
    bpy.context.view_layer.update()
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = evaluated.to_mesh()
    offset = len(part.bm.verts)
    part.bm.from_mesh(mesh)
    evaluated.to_mesh_clear()
    bpy.data.objects.remove(obj)
    bpy.data.metaballs.remove(data)
    part.bm.verts.ensure_lookup_table()
    return part.bm.verts[offset:]


def box_uvs(bm, texel):
    """Box-projected UVs in repeats of the slot's texel size, measured in centimetres."""
    uv = bm.loops.layers.uv.verify()
    for face in bm.faces:
        axis = max(range(3), key=lambda a: abs(face.normal[a]))
        for loop in face.loops:
            c = loop.vert.co * 100.0
            if axis == 2:
                loop[uv].uv = (c.x / texel, c.y / texel)
            elif axis == 0:
                loop[uv].uv = (c.y / texel, c.z / texel)
            else:
                loop[uv].uv = (c.x / texel, c.z / texel)


def bake(parts, name, slots):
    """
    One object per part with its modifiers applied, then joined. The piano's bake: into Blender's
    frame first, normals recalculated after the mirror (every part is closed), bevels with
    hardened normals so flat faces stay flat up to their rounded edges, joined with the operator
    so the custom normals survive into the FBX.

    slots: name -> (preview colour, texel size in cm).
    """
    scene = bpy.context.scene
    objects = []
    cutters = []
    for index, part in enumerate(parts):
        if not part.bm.verts:
            continue
        bmesh.ops.transform(part.bm, matrix=TO_BLENDER, verts=part.bm.verts)
        bmesh.ops.recalc_face_normals(part.bm, faces=part.bm.faces)
        box_uvs(part.bm, slots[part.slot][1])
        mesh = bpy.data.meshes.new("{}_part{}".format(name, index))
        part.bm.to_mesh(mesh)
        mesh.shade_smooth()
        material = bpy.data.materials.get(part.slot) or bpy.data.materials.new(part.slot)
        material.diffuse_color = slots[part.slot][0]
        mesh.materials.append(material)
        obj = bpy.data.objects.new(mesh.name, mesh)
        scene.collection.objects.link(obj)
        if part.cutter is not None:
            bmesh.ops.transform(part.cutter, matrix=TO_BLENDER, verts=part.cutter.verts)
            bmesh.ops.recalc_face_normals(part.cutter, faces=part.cutter.faces)
            cutter_mesh = bpy.data.meshes.new(mesh.name + "_cut")
            part.cutter.to_mesh(cutter_mesh)
            cutter = bpy.data.objects.new(cutter_mesh.name, cutter_mesh)
            scene.collection.objects.link(cutter)
            cutters.append(cutter)
            boolean = obj.modifiers.new("cut", "BOOLEAN")
            boolean.operation = "DIFFERENCE"
            boolean.object = cutter
            boolean.solver = "EXACT"
            boolean.use_self = True
        if part.bevel > 0.0:
            bevel = obj.modifiers.new("bevel", "BEVEL")
            bevel.width = part.bevel * 0.01
            bevel.segments = part.segments
            bevel.limit_method = "ANGLE"
            bevel.angle_limit = math.radians(part.angle)
            bevel.use_clamp_overlap = True
            bevel.harden_normals = True
        elif not part.smooth:
            obj.modifiers.new("smooth", "WEIGHTED_NORMAL").keep_sharp = True
            mesh.set_sharp_from_angle(angle=math.radians(50.0))
        obj.modifiers.new("tris", "TRIANGULATE").keep_custom_normals = True
        objects.append(obj)

    bpy.context.view_layer.update()
    for obj in objects:
        bpy.context.view_layer.objects.active = obj
        for modifier in list(obj.modifiers):
            bpy.ops.object.modifier_apply(modifier=modifier.name)
    for cutter in cutters:
        bpy.data.objects.remove(cutter)

    for other in scene.objects:
        other.select_set(other in objects)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    joined = bpy.context.view_layer.objects.active
    joined.name = name
    joined.data.name = name
    return joined


def export(obj):
    folder = os.path.join(MODEL_DIR, obj.name)
    os.makedirs(folder, exist_ok=True)
    for other in bpy.context.scene.objects:
        other.select_set(other == obj)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=os.path.join(folder, obj.name + ".fbx"), use_selection=True,
                             object_types={"MESH"}, mesh_smooth_type="FACE", add_leaf_bones=False,
                             bake_anim=False, apply_unit_scale=True)


def preview(objects, folder, prefix, views, resolution=(1600, 1000)):
    """Workbench renders from a set of (label, camera location, target) in Blender metres."""
    os.makedirs(folder, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_cavity = True
    scene.display.shading.show_shadows = True
    scene.render.resolution_x, scene.render.resolution_y = resolution
    scene.world = bpy.data.worlds.new("preview")
    target = bpy.data.objects.new("target", None)
    scene.collection.objects.link(target)
    camera_data = bpy.data.cameras.new("camera")
    camera_data.lens = 40.0
    camera = bpy.data.objects.new("camera", camera_data)
    scene.collection.objects.link(camera)
    track = camera.constraints.new("TRACK_TO")
    track.target = target
    scene.camera = camera
    for label, location, aim in views:
        camera.location = location
        target.location = aim
        scene.render.filepath = os.path.join(folder, "{}_{}.png".format(prefix, label))
        bpy.ops.render.render(write_still=True)
