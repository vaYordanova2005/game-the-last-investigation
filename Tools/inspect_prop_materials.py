"""Diagnostic: which textures the imported prop material instances actually ended up with.

Worth running whenever a prop looks like it is made of the wrong thing. An instance that never had
a map set for one of its samplers does not render without that map — it renders with the master
material's *default*, and those defaults are deliberately our own wall textures so the master
compiles at all. A prop with no normal map is therefore not flat: it is embossed with cracked
concrete, which is how the clock came to be a white dial that looked like stone.

Writes to a file rather than the log, because commandlet stdout swallows script output.
"""
import unreal

lines = []
for name in ["MI_wall_clock_wall_clock", "MI_wall_clock_wall_clock_glass",
             "MI_book_encyclopedia_set_01_book_encyclopedia_set_01_cover",
             "MI_hanging_picture_frame_01_hanging_picture_frame_01_artwork"]:
    mi = unreal.EditorAssetLibrary.load_asset("/Game/Materials/" + name)
    if not mi:
        lines.append("MISSING " + name)
        continue
    parent = mi.get_editor_property("parent")
    lines.append("{}  parent={}".format(name, parent.get_name() if parent else None))
    for param in ("BaseColorMap", "NormalMap", "ARMMap", "OpacityMap"):
        try:
            tex = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mi, param)
            lines.append("   {} = {}".format(param, tex.get_name() if tex else "NONE"))
        except Exception as error:
            lines.append("   {} -> {}".format(param, error))

with open(unreal.Paths.project_dir() + "Saved/clock_report.txt", "w") as handle:
    handle.write("\n".join(lines))
