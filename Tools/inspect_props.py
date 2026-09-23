"""Diagnostic: report the bounds, pivot and material slots of every imported prop mesh.

Placing props by hand-tuned numbers only works if you know where the mesh's origin sits inside
its own bounding box — a pivot at the base and a pivot at the centre want Z values a metre apart.
Writes to a file rather than the log, because commandlet stdout swallows script output.
"""
import unreal

names = [
    "vintage_cabinet_01", "wooden_bookshelf_worn", "book_encyclopedia_set_01", "decorative_book_set_01",
    "WoodenChair_01", "WoodenTable_01", "wall_clock", "hanging_picture_frame_01",
    "Lantern_01", "wooden_crate_01",
]

lines = []
for name in names:
    path = "/Game/Meshes/" + name
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    if not mesh:
        lines.append("MISSING " + path)
        continue
    box = mesh.get_bounding_box()
    mn, mx = box.min, box.max
    size = (mx.x - mn.x, mx.y - mn.y, mx.z - mn.z)
    lines.append("{}".format(name))
    lines.append("   min  ({:9.2f},{:9.2f},{:9.2f})".format(mn.x, mn.y, mn.z))
    lines.append("   max  ({:9.2f},{:9.2f},{:9.2f})".format(mx.x, mx.y, mx.z))
    lines.append("   size ({:9.2f},{:9.2f},{:9.2f})".format(*size))
    slots = mesh.get_editor_property("static_materials")
    lines.append("   slots: {}".format([str(s.material_slot_name) for s in slots]))
    for s in slots:
        mat = s.material_interface
        lines.append("      {} -> {}".format(s.material_slot_name, mat.get_name() if mat else "NONE"))

# Where the shelf boards actually are. A bounding box says how tall a bookcase is and nothing
# about where you can stand a book — so read the mesh back and look for the horizontal surfaces:
# upward-facing triangles, grouped by height, keeping the groups wide enough to be a shelf.
shelf = unreal.EditorAssetLibrary.load_asset("/Game/Meshes/wooden_bookshelf_worn")
if shelf:
    lines.append("")
    lines.append("wooden_bookshelf_worn — upward-facing surfaces by height (authored units)")
    buckets = {}
    for section in range(8):
        try:
            verts, tris, normals, uvs, tangents = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(shelf, 0, section)
        except Exception:
            break
        if not tris:
            continue
        lines.append("   section {}: {} verts {} tris".format(section, len(verts), len(tris) // 3))
        for i in range(0, len(tris), 3):
            a, b, c = tris[i], tris[i + 1], tris[i + 2]
            n = normals[a]
            if n.z < 0.7:
                continue
            zs = [verts[a].z, verts[b].z, verts[c].z]
            xs = [verts[a].x, verts[b].x, verts[c].x]
            ys = [verts[a].y, verts[b].y, verts[c].y]
            key = round(sum(zs) / 3.0)
            rec = buckets.setdefault(key, [1e9, -1e9, 1e9, -1e9, 0])
            rec[0] = min(rec[0], min(xs)); rec[1] = max(rec[1], max(xs))
            rec[2] = min(rec[2], min(ys)); rec[3] = max(rec[3], max(ys))
            rec[4] += 1
    for z in sorted(buckets):
        x0, x1, y0, y1, n = buckets[z]
        lines.append("   z={:8.2f}  x[{:8.2f},{:8.2f}] y[{:8.2f},{:8.2f}]  tris={}".format(z, x0, x1, y0, y1, n))

with open(unreal.Paths.project_dir() + "Saved/prop_report.txt", "w") as handle:
    handle.write("\n".join(lines))
