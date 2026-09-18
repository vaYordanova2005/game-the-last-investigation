"""Diagnostic: report what each surface material instance actually has wired up.

Writes to a file rather than the log, because commandlet stdout swallows script output.
"""
import unreal

lines = []


def report(text):
    lines.append(text)


for name in ["MI_clay_plaster", "MI_decrepit_wallpaper", "MI_old_wooden_floor_02"]:
    path = "/Game/Materials/" + name
    mi = unreal.EditorAssetLibrary.load_asset(path)
    if not mi:
        report("MISSING " + path)
        continue

    parent = mi.get_editor_property("parent")
    report("{} parent={}".format(name, parent.get_name() if parent else None))

    for param in ("BaseColorMap", "NormalMap", "ARMMap"):
        try:
            tex = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mi, param)
            report("   {} = {}".format(param, tex.get_name() if tex else "NONE"))
        except Exception as error:
            report("   {} -> ERROR {}".format(param, error))

master = unreal.EditorAssetLibrary.load_asset("/Game/Materials/M_RoomSurface")
if master:
    try:
        names = unreal.MaterialEditingLibrary.get_texture_parameter_names(master)
        report("master texture params: {}".format([str(n) for n in names]))
    except Exception as error:
        report("master param query failed: {}".format(error))

with open(unreal.Paths.project_dir() + "Saved/material_report.txt", "w") as handle:
    handle.write("\n".join(lines))
