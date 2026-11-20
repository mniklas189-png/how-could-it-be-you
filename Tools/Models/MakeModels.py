"""Builds the game's own prop models with Blender, headless.

    blender --background --factory-startup --python MakeModels.py -- [--only Name1,Name2] [--no-bake] [--no-preview]
                                                                     [--samples N]

For every model: build the mesh from code, unwrap it, bake the procedural surfaces to T_<Name>_BC.png and
T_<Name>_ORM.png, export <Name>.fbx, read the file back to measure and check it, and render a preview of
what was read back. Everything lands in Build/Models (not tracked); manifest.json lists the results.
The run is deterministic: fixed seeds, no time or machine dependent input.

Conventions the game side relies on are in hc_bake.FBX_SETTINGS and in check() below.
"""
import sys

sys.dont_write_bytecode = True

import json
import os
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import bpy

import hc_bake
import hc_geo
import hc_mat
import hc_props_furniture  # noqa: F401  (registers its models)
import hc_props_machines   # noqa: F401
import hc_props_small      # noqa: F401
from hc_spec import MODELS

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "Build", "Models")
PREVIEW = os.path.join(OUT, "Preview")

# Order of the manifest and of the contact sheet.
ORDER = ["VentFrame", "VentGrille", "Bunk", "Locker", "MedBed", "LabBench", "Shelf", "Crate", "MessTable", "Chair",
         "Desk", "Planter", "CryoPod", "IceCoreRack", "Pump", "Generator", "Barrier", "Bench", "Heater", "IceChunk"]


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    opts = dict(only=None, bake=True, preview=True, samples=48)
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg == "--only":
            i += 1
            opts["only"] = [n.strip() for n in argv[i].split(",") if n.strip()]
        elif arg == "--no-bake":
            opts["bake"] = False
        elif arg == "--no-preview":
            opts["preview"] = False
        elif arg == "--samples":
            i += 1
            opts["samples"] = int(argv[i])
        else:
            raise SystemExit("unknown argument: " + arg)
        i += 1
    return opts


def check(spec, lo, hi, tris, slots, unit_scale):
    """The rules of the brief, tested on the file that was read back. Raises on the first violation."""
    size = [(hi[i] - lo[i]) * 100.0 for i in range(3)]
    for i, axis in enumerate("XYZ"):
        want = spec.size_cm[i]
        if abs(size[i] - want) > max(want * 0.05, 0.25):
            raise RuntimeError("%s: size %s is %.1f cm, wanted %.1f cm (5 %% allowed)" % (spec.name, axis, size[i], want))
    eps = 0.002
    if abs(lo[0] + hi[0]) > eps:
        raise RuntimeError("%s: not centred in X (%.4f .. %.4f)" % (spec.name, lo[0], hi[0]))
    if spec.pivot == 'bottom':
        if abs(lo[1] + hi[1]) > eps or abs(lo[2]) > 0.001:
            raise RuntimeError("%s: pivot is not at the bottom centre (Y %.4f .. %.4f, Z from %.4f)"
                               % (spec.name, lo[1], hi[1], lo[2]))
    else:
        # hinge pivot: the body lies in front of the axis (-Y), the axis is at mid height
        if hi[1] > 0.02 or abs(lo[2] + hi[2]) > eps:
            raise RuntimeError("%s: hinge pivot is off (Y up to %.4f, Z %.4f .. %.4f)" % (spec.name, hi[1], lo[2], hi[2]))
    if tris > spec.tris:
        raise RuntimeError("%s: %d triangles, budget is %d" % (spec.name, tris, spec.tris))
    want_slots = [hc_mat.BODY] + ([hc_mat.GLOW] if spec.glow else []) + ([hc_mat.GLASS] if spec.glass else [])
    if slots != want_slots:
        raise RuntimeError("%s: material slots %s, wanted %s" % (spec.name, slots, want_slots))
    if unit_scale is not None and abs(unit_scale - 1.0) > 1e-6:
        raise RuntimeError("%s: the FBX is not in centimetres (UnitScaleFactor %s)" % (spec.name, unit_scale))


def run_model(spec, opts):
    start = time.time()
    name = spec.name
    fbx = os.path.join(OUT, name + ".fbx")
    bc = os.path.join(OUT, "T_%s_BC.png" % name)
    orm = os.path.join(OUT, "T_%s_ORM.png" % name)

    hc_bake.reset_scene()
    lib = hc_mat.Library()
    seed = zlib.crc32(name.encode("ascii"))
    hc_mat.set_seed(((seed & 0xFF) * 0.37, ((seed >> 8) & 0xFF) * 0.37, ((seed >> 16) & 0xFF) * 0.37))
    builder = hc_geo.Builder(name, lib)
    spec.build(builder)
    ob = builder.join(sharp_angle=spec.sharp, recentre=(spec.pivot == 'bottom'))
    hc_bake.unwrap(ob, spec.tex, ground=spec.ground)
    if opts["bake"]:
        hc_bake.bake(ob, lib, spec.tex, bc, orm, opts["samples"], ground=spec.ground)
    hc_bake.final_slots(ob, spec.glow, spec.glass)
    hc_bake.export_fbx(ob, fbx)

    # read the file back: everything below is measured on what the game will import
    hc_bake.reset_scene()
    got = hc_bake.import_fbx(fbx)
    lo, hi, tris = hc_bake.mesh_stats(got)
    lo, hi = [float(v) for v in lo], [float(v) for v in hi]
    slots = [s.material.name for s in got.material_slots]
    if len(got.data.uv_layers) != 1:
        raise RuntimeError("%s: expected exactly one UV set in the file" % name)
    if any(len(p.vertices) != 3 for p in got.data.polygons):
        raise RuntimeError("%s: the file is not triangulated" % name)
    check(spec, lo, hi, tris, slots, hc_bake.fbx_unit_scale(fbx))
    if opts["preview"]:
        hc_bake.preview_materials(got, bc, orm, spec.glow, spec.glow_strength, spec.glass)
        hc_bake.render_preview(got, lo, hi, os.path.join(PREVIEW, name + ".png"))

    entry = {
        "name": name,
        "fbx": name + ".fbx",
        "size_cm": [round((hi[i] - lo[i]) * 100.0, 2) for i in range(3)],
        "bounds_min_cm": [round(lo[i] * 100.0, 2) + 0.0 for i in range(3)],
        "bounds_max_cm": [round(hi[i] * 100.0, 2) + 0.0 for i in range(3)],
        "triangles": int(tris),
        "slots": slots,
    }
    if spec.glow:
        entry["glow_color"] = [round(c, 4) for c in spec.glow]
        entry["glow_strength"] = spec.glow_strength
    if spec.glass:
        entry["glass_color"] = [round(c, 4) for c in spec.glass]
    entry["texture_size"] = spec.tex
    entry["bc"] = "T_%s_BC.png" % name
    entry["orm"] = "T_%s_ORM.png" % name
    if spec.note:
        entry["note"] = spec.note
    hc_bake.log("%-12s %5d tris  %s cm  %.1f s" % (name, tris, entry["size_cm"], time.time() - start))
    return entry


def write_manifest(entries):
    path = os.path.join(OUT, "manifest.json")
    known = {}
    if os.path.exists(path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                known = {m["name"]: m for m in json.load(f).get("models", [])}
        except (ValueError, KeyError):
            known = {}
    known.update({e["name"]: e for e in entries})
    export = {k: (sorted(v) if isinstance(v, set) else v) for k, v in hc_bake.FBX_SETTINGS.items()}
    export["operator"] = "bpy.ops.export_scene.fbx"
    export["blender"] = bpy.app.version_string
    export["unit_scale_factor"] = 1.0
    export["units"] = ("Built in metres. The file stores the vertices in centimetres (Y up, the exporter's axis "
                       "conversion and the unit scale are applied to the mesh itself), node transform is identity, "
                       "UnitScaleFactor is 1. Import with scale 1. Blender -Y (front) arrives as +Y in Unreal.")
    data = {"export": export, "models": [known[n] for n in ORDER if n in known]}
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=2)
        f.write("\n")


def main():
    opts = parse_args()
    names = opts["only"] or ORDER
    for name in names:
        if name not in MODELS:
            raise SystemExit("unknown model: %s (known: %s)" % (name, ", ".join(ORDER)))
    os.makedirs(PREVIEW, exist_ok=True)
    start = time.time()
    entries = [run_model(MODELS[name], opts) for name in names]
    write_manifest(entries)
    if opts["preview"]:
        hc_bake.contact_sheet(ORDER, PREVIEW, os.path.join(PREVIEW, "_sheet.png"))
    hc_bake.log("%d models in %.0f s -> %s" % (len(entries), time.time() - start, OUT))


if __name__ == "__main__":
    try:
        main()
    except Exception:
        import traceback
        traceback.print_exc()
        sys.exit(1)
