"""Unwrap, bake, FBX export, re-import check, preview render and contact sheet for MakeModels.py."""
import math
import os
import struct
import time
import zlib

import bmesh
import bpy
import numpy as np
from mathutils import Vector

import hc_mat
from hc_geo import BEVEL_LAYER

# The FBX settings every model is written with (also copied into manifest.json).
# FBX_SCALE_NONE + bake_space_transform writes the vertices themselves in centimetres, Y up, with an
# identity node transform and UnitScaleFactor 1: the size is right in Unreal with import scale 1 whether or
# not the importer converts scene units, and there is no node rotation or scale left to bake.
FBX_SETTINGS = dict(
    use_selection=True,
    object_types={'MESH'},
    global_scale=1.0,
    apply_unit_scale=True,
    apply_scale_options='FBX_SCALE_NONE',
    bake_space_transform=True,
    axis_forward='-Z',
    axis_up='Y',
    use_mesh_modifiers=True,
    mesh_smooth_type='FACE',
    use_triangles=True,
    use_tspace=False,
    colors_type='NONE',
    use_custom_props=False,
    add_leaf_bones=False,
    bake_anim=False,
    path_mode='AUTO',
    embed_textures=False,
)

_device = None


def log(text):
    print("[HC] " + text, flush=True)


# ----------------------------------------------------------------------------------------------------
# scene housekeeping
# ----------------------------------------------------------------------------------------------------

def reset_scene():
    """Empties the file: every model starts from nothing, so a rebuild of one model equals a full run."""
    for group in (bpy.data.objects, bpy.data.meshes, bpy.data.curves, bpy.data.materials, bpy.data.images,
                  bpy.data.node_groups, bpy.data.cameras, bpy.data.lights, bpy.data.worlds):
        for block in list(group):
            group.remove(block)


def select_only(ob):
    bpy.context.view_layer.update()
    for other in bpy.context.scene.objects:
        if other is not None:
            other.select_set(False)
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob


def setup_cycles(samples):
    """Cycles on the graphics card if there is one, else on the processor. CUDA comes first: with OptiX the
    first bake that uses the AO and Bevel nodes compiles its kernel for many minutes. The environment
    variable HC_CYCLES_DEVICE (CUDA, OPTIX or CPU) overrides the choice."""
    global _device
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    if _device is None:
        _device = 'CPU'
        try:
            prefs = bpy.context.preferences.addons['cycles'].preferences
            wanted = os.environ.get("HC_CYCLES_DEVICE", "").upper()
            for kind in ((wanted,) if wanted else ('CUDA', 'OPTIX')):
                if kind == 'CPU':
                    break
                try:
                    prefs.compute_device_type = kind
                    prefs.get_devices()
                except Exception:
                    continue
                found = [d for d in prefs.devices if d.type == kind]
                if found:
                    for d in prefs.devices:
                        d.use = (d.type == kind)
                    _device = kind
                    break
        except Exception as err:
            log("Cycles device setup failed (%s), using the CPU" % err)
        log("Cycles device: " + _device)
    scene.cycles.device = 'CPU' if _device == 'CPU' else 'GPU'
    scene.cycles.samples = samples
    scene.cycles.use_adaptive_sampling = False
    scene.cycles.use_denoising = False
    scene.cycles.seed = 0
    scene.cycles.use_animated_seed = False
    scene.render.dither_intensity = 0.0
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.look = 'None'
    return scene


def mesh_stats(ob):
    """World-space bounds in metres and the triangle count."""
    me = ob.data
    co = np.empty(len(me.vertices) * 3, np.float64)
    me.vertices.foreach_get('co', co)
    co = co.reshape(-1, 3)
    m = np.array(ob.matrix_world)
    world = co @ m[:3, :3].T + m[:3, 3]
    tris = sum(len(p.vertices) - 2 for p in me.polygons)
    return world.min(axis=0), world.max(axis=0), tris


# ----------------------------------------------------------------------------------------------------
# UV
# ----------------------------------------------------------------------------------------------------

def _read_uv(me):
    uv = np.empty(len(me.loops) * 2, np.float64)
    me.uv_layers.active.uv.foreach_get('vector', uv)
    return uv.reshape(-1, 2)


def _write_uv(me, uv):
    me.uv_layers.active.uv.foreach_set('vector', uv.reshape(-1))
    me.update()


def _hidden_polys(me, skip_slots, ground):
    """Faces nobody can ever see: every ray from them is blocked by the model itself (or by the floor).
    Glass does not block. Used only to give such faces less texture space."""
    from mathutils.bvhtree import BVHTree
    verts = [v.co.copy() for v in me.vertices]
    solid = [tuple(p.vertices) for p in me.polygons if p.material_index not in skip_slots]
    tree = BVHTree.FromPolygons(verts, solid, all_triangles=False, epsilon=0.0)
    fan = [(0.0, 0.0, 1.0)]
    for tilt, count, phase in ((40.0, 6, 0.0), (72.0, 6, 0.5)):
        t = math.radians(tilt)
        for k in range(count):
            a = 2.0 * math.pi * (k + phase) / count
            fan.append((math.sin(t) * math.cos(a), math.sin(t) * math.sin(a), math.cos(t)))
    hidden = np.zeros(len(me.polygons), bool)
    for p in me.polygons:
        n = p.normal
        if n.length < 0.5:
            continue
        side = Vector((1.0, 0.0, 0.0)) if abs(n.x) < 0.9 else Vector((0.0, 1.0, 0.0))
        u = n.cross(side).normalized()
        v = n.cross(u)
        centre = p.center
        idx = list(p.vertices)
        points = [centre] + [centre + (verts[i] - centre) * 0.75 for i in idx[::max(1, len(idx) // 4)]]
        blocked = True
        for point in points:
            origin = point + n * 0.0004
            for d in fan:
                ray = u * d[0] + v * d[1] + n * d[2]
                if tree.ray_cast(origin, ray, 6.0)[0] is None:
                    if ground and ray.z < -0.02:
                        continue
                    blocked = False
                    break
            if not blocked:
                break
        hidden[p.index] = blocked
    return hidden


def _uv_islands(me, uv):
    """Lists of polygon indices that hang together in UV space."""
    parent = list(range(len(me.polygons)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    seen = {}
    loop_vert = np.empty(len(me.loops), np.int32)
    me.loops.foreach_get('vertex_index', loop_vert)
    for p in me.polygons:
        for li in p.loop_indices:
            key = (int(loop_vert[li]), round(float(uv[li, 0]), 5), round(float(uv[li, 1]), 5))
            other = seen.setdefault(key, p.index)
            if other != p.index:
                a, b = find(p.index), find(other)
                if a != b:
                    parent[a] = b
    islands = {}
    for p in me.polygons:
        islands.setdefault(find(p.index), []).append(p.index)
    return list(islands.values())


def unwrap(ob, tex, ground=True):
    """Unique UV0 for the whole mesh: smart projection, then less space for islands that need no texture
    (Glow, Glass and plain black faces, faces hidden inside the model), then one tight pack."""
    me = ob.data
    if not me.uv_layers:
        me.uv_layers.new(name="UVMap")
    margin = 2.5 / tex
    bpy.context.scene.tool_settings.use_uv_select_sync = True
    select_only(ob)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_mode(type='FACE')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=margin, area_weight=0.0,
                             correct_aspect=True, scale_to_bounds=False)
    bpy.ops.object.mode_set(mode='OBJECT')

    glass = [i for i, s in enumerate(ob.material_slots) if s.material and s.material.name == hc_mat.GLASS]
    # surfaces without any detail: Glow and Glass are not textured at all, the pit black is one flat colour
    flat = [i for i, s in enumerate(ob.material_slots) if s.material and s.material.name in hc_mat.FLAT_NAMES]
    hidden = _hidden_polys(me, glass, ground)
    uv = _read_uv(me)
    for island in _uv_islands(me, uv):
        polys = [me.polygons[i] for i in island]
        if all(p.material_index in flat for p in polys):
            factor = 0.3
        else:
            # an island that is hidden but for its bevel strips keeps just enough space for those
            total = sum(p.area for p in polys)
            unseen = sum(p.area for p in polys if hidden[p.index])
            if total <= 0.0 or unseen < 0.85 * total:
                continue
            factor = 0.3 if unseen < total else 0.12
        idx = np.array([li for p in polys for li in p.loop_indices])
        centre = (uv[idx].min(axis=0) + uv[idx].max(axis=0)) * 0.5
        uv[idx] = centre + (uv[idx] - centre) * factor
    _write_uv(me, uv)

    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.select_all(action='SELECT')
    # the box packer: the two exact ones take a minute and more on some of these meshes
    bpy.ops.uv.pack_islands(rotate=True, margin=margin, margin_method='FRACTION', shape_method='AABB', scale=True)
    bpy.ops.object.mode_set(mode='OBJECT')
    uv = _read_uv(me)
    area = 0.0
    for p in me.polygons:
        pts = uv[list(p.loop_indices)]
        area += 0.5 * abs(float(np.sum(pts[:, 0] * np.roll(pts[:, 1], -1) - np.roll(pts[:, 0], -1) * pts[:, 1])))
    log("  UV use %.0f %%" % (area * 100.0))
    if uv.min() < -1e-4 or uv.max() > 1.0 + 1e-4:
        raise RuntimeError("%s: UVs leave the 0..1 square (%.4f .. %.4f)" % (ob.name, uv.min(), uv.max()))


# ----------------------------------------------------------------------------------------------------
# PNG
# ----------------------------------------------------------------------------------------------------

def write_png(path, rgb):
    """8-bit RGB PNG with per-row filter choice (rgb: uint8 array height x width x 3, top row first)."""
    h, w, _ = rgb.shape
    cur = rgb.reshape(h, w * 3).astype(np.int16)
    left = np.zeros_like(cur)
    left[:, 3:] = cur[:, :-3]
    up = np.zeros_like(cur)
    up[1:] = cur[:-1]
    diag = np.zeros_like(cur)
    diag[1:, 3:] = cur[:-1, :-3]
    guess = left + up - diag
    pa, pb, pc = np.abs(guess - left), np.abs(guess - up), np.abs(guess - diag)
    paeth = np.where((pa <= pb) & (pa <= pc), left, np.where(pb <= pc, up, diag))
    variants = [cur, cur - left, cur - up, cur - ((left + up) >> 1), cur - paeth]
    variants = np.stack([(v & 0xFF).astype(np.uint8) for v in variants])
    cost = np.abs(variants.astype(np.int8).astype(np.int16)).sum(axis=2)
    pick = cost.argmin(axis=0)
    rows = np.empty((h, w * 3 + 1), np.uint8)
    rows[:, 0] = pick
    rows[:, 1:] = variants[pick, np.arange(h)]

    def chunk(tag, payload):
        return struct.pack('>I', len(payload)) + tag + payload + struct.pack('>I', zlib.crc32(tag + payload) & 0xFFFFFFFF)

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(rows.tobytes(), 9)))
        f.write(chunk(b'IEND', b''))


def read_image(path):
    """Loads an image file as float array height x width x 4, top row first, values as stored."""
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    px = np.empty(w * h * 4, np.float32)
    img.pixels.foreach_get(px)
    bpy.data.images.remove(img)
    return px.reshape(h, w, 4)[::-1]


# ----------------------------------------------------------------------------------------------------
# bake
# ----------------------------------------------------------------------------------------------------

def _fill_empty(px):
    """Texels no face covers get the average colour instead of black, so mip maps do not darken the seams."""
    covered = px[:, :, 3] > 0.0
    rgb = px[:, :, :3].copy()
    if covered.any() and not covered.all():
        rgb[~covered] = rgb[covered].mean(axis=0)
    return np.clip(np.rint(rgb * 255.0), 0, 255).astype(np.uint8)


def bake(ob, lib, tex, bc_path, orm_path, samples, ground=True):
    """Bakes the procedural surfaces of ob to its UVs: base colour (sRGB) and AO / roughness / metallic."""
    scene = setup_cycles(samples)
    work = ob.copy()
    work.data = ob.data.copy()
    scene.collection.objects.link(work)
    ob.hide_render = True
    # glass must not shade what lies behind it
    glass = [i for i, s in enumerate(work.material_slots) if s.material and s.material.name == hc_mat.GLASS]
    if glass:
        bm = bmesh.new()
        bm.from_mesh(work.data)
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index in glass], context='FACES')
        bm.to_mesh(work.data)
        bm.free()
    floor = None
    if ground:
        fm = bpy.data.meshes.new("HC_BakeFloor")
        s = 30.0
        fm.from_pydata([(-s, -s, -0.0005), (s, -s, -0.0005), (s, s, -0.0005), (-s, s, -0.0005)], [], [(0, 1, 2, 3)])
        floor = bpy.data.objects.new("HC_BakeFloor", fm)
        scene.collection.objects.link(floor)
    margin = 8 if tex >= 1024 else 5
    result = {}
    for channel, is_data in (('bc', False), ('orm', True)):
        img = bpy.data.images.new("HC_Bake_" + channel, tex, tex, alpha=True, float_buffer=False, is_data=is_data)
        lib.wire(channel, img)
        select_only(work)
        started = time.time()
        bpy.ops.object.bake(type='EMIT', margin=margin, margin_type='ADJACENT_FACES', use_clear=True,
                            target='IMAGE_TEXTURES')
        log("  baked %s in %.1f s" % (channel, time.time() - started))
        px = np.empty(tex * tex * 4, np.float32)
        img.pixels.foreach_get(px)
        result[channel] = px.reshape(tex, tex, 4)[::-1]
        bpy.data.images.remove(img)
    write_png(bc_path, _fill_empty(result['bc']))
    write_png(orm_path, _fill_empty(result['orm']))
    for extra in (work, floor):
        if extra is not None:
            mesh = extra.data
            bpy.data.objects.remove(extra)
            bpy.data.meshes.remove(mesh)
    ob.hide_render = False


# ----------------------------------------------------------------------------------------------------
# final materials, export, import
# ----------------------------------------------------------------------------------------------------

def _plain_material(name, colour, emission=None, alpha=1.0):
    mat = bpy.data.materials.new(name)
    bsdf = next(n for n in mat.node_tree.nodes if n.bl_idname == 'ShaderNodeBsdfPrincipled')
    bsdf.inputs['Base Color'].default_value = (*colour, 1.0)
    bsdf.inputs['Alpha'].default_value = alpha
    if emission is not None:
        bsdf.inputs['Emission Color'].default_value = (*emission, 1.0)
        bsdf.inputs['Emission Strength'].default_value = 1.0
    return mat


def final_slots(ob, glow_color, glass_color):
    """Replaces the procedural surfaces by the slots the game gets: Body, then Glow, then Glass."""
    me = ob.data
    old = [s.material.name if s.material else "" for s in ob.material_slots]
    slots = [hc_mat.BODY]
    for special in (hc_mat.GLOW, hc_mat.GLASS):
        if special in old:
            slots.append(special)
    remap = np.array([slots.index(n) if n in slots else 0 for n in old], np.int32)
    index = np.empty(len(me.polygons), np.int32)
    me.polygons.foreach_get('material_index', index)
    index = remap[index]
    used = set(index.tolist())
    slots = [s for i, s in enumerate(slots) if i in used or i == 0]
    me.materials.clear()
    for mat in list(bpy.data.materials):
        bpy.data.materials.remove(mat)
    for name in slots:
        if name == hc_mat.BODY:
            me.materials.append(_plain_material(name, (0.5, 0.5, 0.5)))
        elif name == hc_mat.GLOW:
            me.materials.append(_plain_material(name, (0.0, 0.0, 0.0), emission=glow_color or (1.0, 1.0, 1.0)))
        else:
            me.materials.append(_plain_material(name, glass_color or (0.5, 0.6, 0.7), alpha=0.35))
    me.polygons.foreach_set('material_index', index)
    if BEVEL_LAYER in me.attributes:
        me.attributes.remove(me.attributes[BEVEL_LAYER])
    me.update()
    return slots


def export_fbx(ob, path):
    select_only(ob)
    bpy.ops.export_scene.fbx(filepath=path, **FBX_SETTINGS)


def import_fbx(path):
    """Reads an exported file back, the way a game engine would get it. Returns the mesh object."""
    before = set(bpy.data.objects)
    bpy.ops.wm.fbx_import(filepath=path)
    new = [o for o in bpy.data.objects if o not in before and o.type == 'MESH']
    if len(new) != 1:
        raise RuntimeError("%s: expected one mesh in the file, found %d" % (path, len(new)))
    bpy.context.view_layer.update()
    return new[0]


def fbx_unit_scale(path):
    """UnitScaleFactor stored in the file (1 = centimetres)."""
    try:
        from io_scene_fbx import parse_fbx
        root, _version = parse_fbx.parse(path)
        for el in root.elems:
            if el.id == b'GlobalSettings':
                for sub in el.elems:
                    if sub.id == b'Properties70':
                        for prop in sub.elems:
                            if prop.props[0] == b'UnitScaleFactor':
                                return float(prop.props[4])
    except Exception as err:
        log("could not read the unit scale from %s (%s)" % (os.path.basename(path), err))
    return None


# ----------------------------------------------------------------------------------------------------
# preview
# ----------------------------------------------------------------------------------------------------

def _image_node(tree, path, data):
    node = tree.nodes.new('ShaderNodeTexImage')
    node.image = bpy.data.images.load(path, check_existing=True)
    node.image.colorspace_settings.name = 'Non-Color' if data else 'sRGB'
    return node


def preview_materials(ob, bc_path, orm_path, glow_color, glow_strength, glass_color):
    """Shades the imported mesh with the baked textures, the way the game material will."""
    for slot in ob.material_slots:
        mat = slot.material
        name = mat.name.split('.')[0]
        tree = mat.node_tree
        tree.nodes.clear()
        out = tree.nodes.new('ShaderNodeOutputMaterial')
        if name == hc_mat.BODY:
            bsdf = tree.nodes.new('ShaderNodeBsdfPrincipled')
            if bc_path and os.path.exists(bc_path):
                tree.links.new(_image_node(tree, bc_path, False).outputs['Color'], bsdf.inputs['Base Color'])
            if orm_path and os.path.exists(orm_path):
                sep = tree.nodes.new('ShaderNodeSeparateColor')
                tree.links.new(_image_node(tree, orm_path, True).outputs['Color'], sep.inputs[0])
                tree.links.new(sep.outputs[1], bsdf.inputs['Roughness'])
                tree.links.new(sep.outputs[2], bsdf.inputs['Metallic'])
            tree.links.new(bsdf.outputs[0], out.inputs['Surface'])
        elif name == hc_mat.GLOW:
            emit = tree.nodes.new('ShaderNodeEmission')
            emit.inputs['Color'].default_value = (*(glow_color or (1.0, 1.0, 1.0)), 1.0)
            emit.inputs['Strength'].default_value = 2.5
            tree.links.new(emit.outputs[0], out.inputs['Surface'])
        else:
            # tinted pane: see-through, a reflection at flat angles, and no shadow on what lies behind it
            colour = glass_color or (0.5, 0.6, 0.7)
            clear = tree.nodes.new('ShaderNodeBsdfTransparent')
            clear.inputs['Color'].default_value = (0.25 + 0.75 * colour[0], 0.25 + 0.75 * colour[1], 0.25 + 0.75 * colour[2], 1.0)
            gloss = tree.nodes.new('ShaderNodeBsdfGlossy')
            gloss.inputs['Roughness'].default_value = 0.05
            fres = tree.nodes.new('ShaderNodeFresnel')
            fres.inputs['IOR'].default_value = 1.45
            mix = tree.nodes.new('ShaderNodeMixShader')
            tree.links.new(fres.outputs[0], mix.inputs[0])
            tree.links.new(clear.outputs[0], mix.inputs[1])
            tree.links.new(gloss.outputs[0], mix.inputs[2])
            path = tree.nodes.new('ShaderNodeLightPath')
            unshadowed = tree.nodes.new('ShaderNodeBsdfTransparent')
            final = tree.nodes.new('ShaderNodeMixShader')
            tree.links.new(path.outputs['Is Shadow Ray'], final.inputs[0])
            tree.links.new(mix.outputs[0], final.inputs[1])
            tree.links.new(unshadowed.outputs[0], final.inputs[2])
            tree.links.new(final.outputs[0], out.inputs['Surface'])


def _area_light(name, where, target, size, watts, colour):
    data = bpy.data.lights.new(name, 'AREA')
    data.shape = 'DISK'
    data.size = size
    data.energy = watts
    data.color = colour
    light = bpy.data.objects.new(name, data)
    light.location = where
    light.rotation_euler = (Vector(target) - Vector(where)).to_track_quat('-Z', 'Y').to_euler()
    bpy.context.scene.collection.objects.link(light)
    return light


def render_preview(ob, lo, hi, path, samples=64, size=800):
    """Three-quarter view from the front (-Y side) on a dark studio floor."""
    scene = setup_cycles(samples)
    scene.cycles.use_denoising = True
    lo, hi = Vector(lo), Vector(hi)
    centre = (lo + hi) * 0.5
    radius = max((hi - lo).length * 0.5, 0.05)
    flat = (hi.z - lo.z) < 0.2 * max(hi.x - lo.x, hi.y - lo.y)

    floor_mesh = bpy.data.meshes.new("HC_Floor")
    s = 60.0
    z = min(lo.z, 0.0) - 0.001
    floor_mesh.from_pydata([(-s, -s, z), (s, -s, z), (s, s, z), (-s, s, z)], [], [(0, 1, 2, 3)])
    floor_mat = bpy.data.materials.new("HC_Floor")
    bsdf = next(n for n in floor_mat.node_tree.nodes if n.bl_idname == 'ShaderNodeBsdfPrincipled')
    bsdf.inputs['Base Color'].default_value = (0.035, 0.036, 0.040, 1.0)
    bsdf.inputs['Roughness'].default_value = 0.55
    floor_mesh.materials.append(floor_mat)
    scene.collection.objects.link(bpy.data.objects.new("HC_Floor", floor_mesh))

    world = bpy.data.worlds.new("HC_World")
    bg = next(n for n in world.node_tree.nodes if n.bl_idname == 'ShaderNodeBackground')
    bg.inputs['Color'].default_value = (0.030, 0.033, 0.040, 1.0)
    bg.inputs['Strength'].default_value = 1.0
    scene.world = world

    d = radius * 3.2
    power = 13.0 * d * d
    _area_light("HC_Key", centre + Vector((-0.55, -1.0, 1.05)).normalized() * d, centre, radius * 1.6, power, (1.0, 0.96, 0.90))
    _area_light("HC_Rim", centre + Vector((0.9, 0.85, 0.8)).normalized() * d, centre, radius * 1.2,
                power * (0.45 if flat else 1.3), (0.75, 0.85, 1.0))
    _area_light("HC_Fill", centre + Vector((1.0, -0.55, 0.25)).normalized() * d, centre, radius * 2.0, power * 0.22,
                (0.9, 0.95, 1.0))

    cam_data = bpy.data.cameras.new("HC_Cam")
    cam_data.lens = 70.0
    cam_data.sensor_width = 36.0
    cam = bpy.data.objects.new("HC_Cam", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    view = Vector((0.62, -1.0, 0.95 if flat else 0.48)).normalized()
    quat = (-view).to_track_quat('-Z', 'Y')
    cam.rotation_euler = quat.to_euler()
    half = math.atan(18.0 / cam_data.lens) * 0.90
    inv = quat.to_matrix().inverted()
    corners = [Vector((x, y, z)) for x in (lo.x, hi.x) for y in (lo.y, hi.y) for z in (lo.z, hi.z)]
    dist = radius
    for _ in range(60):
        eye = centre + view * dist
        ok = True
        for c in corners:
            local = inv @ (c - eye)
            depth = -local.z
            if depth <= 0.01 or abs(local.x) > depth * math.tan(half) or abs(local.y) > depth * math.tan(half):
                ok = False
                break
        if ok:
            break
        dist *= 1.06
    cam.location = centre + view * dist
    cam_data.clip_start = 0.01
    cam_data.clip_end = 500.0

    scene.render.resolution_x = size
    scene.render.resolution_y = size
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    scene.render.image_settings.compression = 60
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


# ----------------------------------------------------------------------------------------------------
# contact sheet
# ----------------------------------------------------------------------------------------------------

def _label_mask(names, columns, tile, strip, width, height):
    """Renders all names as white text on black at the size of the sheet and returns the mask."""
    reset_scene()
    scene = setup_cycles(8)
    scene.cycles.device = 'CPU'
    world = bpy.data.worlds.new("HC_Black")
    next(n for n in world.node_tree.nodes if n.bl_idname == 'ShaderNodeBackground').inputs['Color'].default_value = (0, 0, 0, 1)
    scene.world = world
    mat = bpy.data.materials.new("HC_Text")
    mat.node_tree.nodes.clear()
    out = mat.node_tree.nodes.new('ShaderNodeOutputMaterial')
    emit = mat.node_tree.nodes.new('ShaderNodeEmission')
    emit.inputs['Strength'].default_value = 1.0
    mat.node_tree.links.new(emit.outputs[0], out.inputs['Surface'])
    for i, name in enumerate(names):
        col, row = i % columns, i // columns
        curve = bpy.data.curves.new("HC_Label", 'FONT')
        curve.body = name
        curve.align_x = 'CENTER'
        curve.align_y = 'CENTER'
        curve.size = strip * 0.62
        text = bpy.data.objects.new("HC_Label", curve)
        # sheet pixels map to scene units one to one, y down
        text.location = (col * tile + tile * 0.5, -(row * (tile + strip) + tile + strip * 0.5), 0.0)
        curve.materials.append(mat)
        scene.collection.objects.link(text)
    cam_data = bpy.data.cameras.new("HC_SheetCam")
    cam_data.type = 'ORTHO'
    cam_data.ortho_scale = max(width, height)
    cam = bpy.data.objects.new("HC_SheetCam", cam_data)
    cam.location = (width * 0.5, -height * 0.5, 10.0)
    scene.collection.objects.link(cam)
    scene.camera = cam
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    tmp = os.path.join(bpy.app.tempdir, "hc_labels.png")
    scene.render.filepath = tmp
    bpy.ops.render.render(write_still=True)
    mask = read_image(tmp)[:, :, 0]
    os.remove(tmp)
    return mask


def contact_sheet(names, preview_dir, path, columns=5, tile=400, strip=44):
    """All previews on one sheet, the names written under the tiles."""
    rows = (len(names) + columns - 1) // columns
    width, height = columns * tile, rows * (tile + strip)
    sheet = np.full((height, width, 3), 0.045, np.float32)
    for i, name in enumerate(names):
        file = os.path.join(preview_dir, name + ".png")
        if not os.path.exists(file):
            continue
        px = read_image(file)[:, :, :3]
        step = px.shape[0] // tile
        if step >= 1 and px.shape[0] == px.shape[1] == tile * step:
            px = px.reshape(tile, step, tile, step, 3).mean(axis=(1, 3))
        else:
            ys = (np.arange(tile) * px.shape[0] / tile).astype(int)
            xs = (np.arange(tile) * px.shape[1] / tile).astype(int)
            px = px[ys][:, xs]
        col, row = i % columns, i // columns
        y0, x0 = row * (tile + strip), col * tile
        sheet[y0:y0 + tile, x0:x0 + tile] = px
    mask = _label_mask(names, columns, tile, strip, width, height)
    mask = np.clip(mask, 0.0, 1.0)[:, :, None]
    sheet = sheet * (1.0 - mask) + np.array([0.86, 0.86, 0.82], np.float32) * mask
    write_png(path, np.clip(np.rint(sheet * 255.0), 0, 255).astype(np.uint8))
