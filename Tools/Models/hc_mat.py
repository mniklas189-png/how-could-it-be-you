"""Procedural surface library for MakeModels.py.

Every surface is a small Cycles node tree that ends in the shared group HC_Weather. The group adds
ambient occlusion, crevice grime, dust on upward faces and chipped paint on exposed edges, and hands
out the finished Color / Roughness / Metallic / AO. Nothing here is rendered directly: the bake wires
those outputs into an Emission shader and writes them to the model's own texture.
"""
import bpy

from hc_geo import BEVEL_LAYER

GLOW = "Glow"
GLASS = "Glass"
BODY = "Body"
# materials whose faces need UVs of their own but hardly any texture space
FLAT_NAMES = (GLOW, GLASS, "HC_pit")

STEEL = (0.46, 0.46, 0.47)

# key: base colour (linear), then options. Defaults are in _DEFAULTS.
_DEFAULTS = dict(rough=0.55, metal=0.0, wear=1.0, wear_col=STEEL, wear_metal=1.0, grime=1.0, dust=1.0, scuff=0.16,
                 var=0.10, streaks=1.0, kind='plain')

SURFACES = {
    # painted steel
    'paint_dark':   dict(col=(0.055, 0.060, 0.068), rough=0.50, scuff=0.12),
    'paint_mid':    dict(col=(0.130, 0.140, 0.155), rough=0.52, scuff=0.12),
    'paint_white':  dict(col=(0.540, 0.520, 0.460), rough=0.50, scuff=0.10, wear_col=(0.30, 0.30, 0.31)),
    'paint_frost':  dict(col=(0.620, 0.670, 0.700), rough=0.38, scuff=0.05, wear_col=(0.34, 0.35, 0.37)),
    'paint_yellow': dict(col=(0.600, 0.340, 0.020), rough=0.50, scuff=0.15),
    'paint_orange': dict(col=(0.400, 0.115, 0.026), rough=0.50, scuff=0.20),
    'paint_red':    dict(col=(0.300, 0.035, 0.030), rough=0.48, scuff=0.12),
    'paint_teal':   dict(col=(0.060, 0.140, 0.140), rough=0.50, scuff=0.12),
    'paint_blue':   dict(col=(0.085, 0.120, 0.170), rough=0.50, scuff=0.12),
    # bare metal
    'steel':        dict(col=(0.500, 0.500, 0.510), rough=0.34, metal=1.0, wear=0.5, wear_col=(0.66, 0.66, 0.67),
                         kind='brushed', scuff=0.0),
    'gunmetal':     dict(col=(0.150, 0.155, 0.165), rough=0.42, metal=1.0, wear=0.8, wear_col=(0.50, 0.50, 0.51),
                         kind='brushed', scuff=0.05),
    # rubber and plastic
    'rubber':        dict(col=(0.016, 0.016, 0.018), rough=0.86, wear=0.45, wear_col=(0.07, 0.07, 0.07), wear_metal=0.0,
                          dust=1.4, scuff=0.0),
    'plastic_black': dict(col=(0.022, 0.023, 0.025), rough=0.42, wear=0.6, wear_col=(0.10, 0.10, 0.10), wear_metal=0.0),
    'plastic_grey':  dict(col=(0.170, 0.172, 0.175), rough=0.45, wear=0.6, wear_col=(0.32, 0.32, 0.32), wear_metal=0.0),
    'plastic_white': dict(col=(0.560, 0.550, 0.500), rough=0.40, wear=0.5, wear_col=(0.36, 0.34, 0.30), wear_metal=0.0),
    'ceramic':       dict(col=(0.600, 0.580, 0.520), rough=0.35, wear=0.3, wear_col=(0.40, 0.38, 0.34), wear_metal=0.0),
    'laminate':      dict(col=(0.385, 0.375, 0.330), rough=0.38, wear=0.8, wear_col=(0.20, 0.17, 0.13), wear_metal=0.0,
                          scuff=0.42),
    # soft things
    'fabric_teal':  dict(col=(0.045, 0.105, 0.110), rough=0.92, wear=0.5, wear_col=(0.10, 0.17, 0.17), wear_metal=0.0,
                         kind='fabric', grime=1.2, scuff=0.2, streaks=0.15, dust=0.6),
    'fabric_blue':  dict(col=(0.060, 0.085, 0.125), rough=0.92, wear=0.5, wear_col=(0.13, 0.165, 0.21), wear_metal=0.0,
                         kind='fabric', grime=1.2, scuff=0.2, streaks=0.15, dust=0.6),
    'vinyl_teal':   dict(col=(0.045, 0.105, 0.110), rough=0.50, wear=0.5, wear_col=(0.10, 0.17, 0.17), wear_metal=0.0,
                         kind='fabric', scuff=0.15, streaks=0.2, dust=0.6),
    'mattress':     dict(col=(0.360, 0.355, 0.310), rough=0.94, wear=0.3, wear_col=(0.46, 0.45, 0.41), wear_metal=0.0,
                         kind='fabric', grime=1.4, scuff=0.25, streaks=0.2, dust=0.5),
    'blanket':      dict(col=(0.060, 0.080, 0.100), rough=0.96, wear=0.4, wear_col=(0.12, 0.145, 0.17), wear_metal=0.0,
                         kind='fabric', scuff=0.2, streaks=0.1, dust=0.5),
    'pillow':       dict(col=(0.430, 0.425, 0.380), rough=0.94, wear=0.2, wear_col=(0.52, 0.51, 0.47), wear_metal=0.0,
                         kind='fabric', grime=1.3, scuff=0.2, streaks=0.1, dust=0.5),
    'tarp':         dict(col=(0.075, 0.105, 0.100), rough=0.70, wear=0.6, wear_col=(0.15, 0.19, 0.18), wear_metal=0.0,
                         kind='fabric', scuff=0.2, streaks=0.3),
    # dark and special
    'pit':          dict(col=(0.003, 0.003, 0.004), rough=1.00, wear=0.0, grime=0.0, dust=0.0, scuff=0.0, var=0.0),
    'pit_rib':      dict(col=(0.011, 0.012, 0.014), rough=0.90, wear=0.0, grime=0.5, dust=0.0, scuff=0.0, var=0.0),
    # floor hardware: walked on, scraped, oily
    'floor_steel':  dict(col=(0.050, 0.054, 0.060), rough=0.48, scuff=0.42, dust=0.45, grime=1.2),
    'floor_metal':  dict(col=(0.150, 0.155, 0.165), rough=0.40, metal=1.0, wear=0.9, wear_col=(0.50, 0.50, 0.51),
                         kind='brushed', scuff=0.3, dust=0.45, grime=1.2),
    'screen_off':   dict(col=(0.008, 0.010, 0.012), rough=0.16, wear=0.0, grime=0.3, dust=0.5, scuff=0.0, var=0.0),
    'label':        dict(col=(0.560, 0.540, 0.450), rough=0.50, wear=0.4, wear_metal=0.0, wear_col=(0.3, 0.29, 0.25),
                         scuff=0.2),
    'label_ink':    dict(col=(0.020, 0.020, 0.022), rough=0.55, wear=0.0, scuff=0.0, var=0.0),
    'dial':         dict(col=(0.640, 0.630, 0.570), rough=0.25, wear=0.0, scuff=0.0, var=0.0),
    'vial':         dict(col=(0.420, 0.540, 0.540), rough=0.12, wear=0.0, grime=0.4, dust=0.3, scuff=0.0, var=0.25),
    'substrate':    dict(col=(0.030, 0.021, 0.015), rough=0.95, wear=0.0, grime=0.6, dust=0.0, scuff=0.0, kind='soil'),
    'leaf':         dict(col=(0.045, 0.105, 0.030), rough=0.48, wear=0.0, grime=0.25, dust=0.25, scuff=0.0, var=0.6,
                         kind='leaf'),
    'ice':          dict(col=(0.600, 0.770, 0.860), kind='ice'),
    'ice_core':     dict(col=(0.640, 0.790, 0.860), rough=0.22, wear=0.0, grime=0.2, dust=0.0, scuff=0.0, streaks=0.0,
                         kind='core'),
}

HAZARD_YELLOW = (0.600, 0.340, 0.020)
HAZARD_BLACK = (0.020, 0.020, 0.022)


class Graph:
    """Short-hand for building a node tree."""

    def __init__(self, tree):
        self.tree = tree

    def new(self, idname, **props):
        node = self.tree.nodes.new(idname)
        for key, value in props.items():
            setattr(node, key, value)
        return node

    def set(self, sock, value):
        if value is None:
            return
        if isinstance(value, bpy.types.NodeSocket):
            self.tree.links.new(value, sock)
        elif isinstance(value, (tuple, list)) and sock.type == 'RGBA' and len(value) == 3:
            sock.default_value = (value[0], value[1], value[2], 1.0)
        else:
            sock.default_value = value

    def math(self, op, a, b=None, c=None, clamp=False):
        node = self.new('ShaderNodeMath', operation=op, use_clamp=clamp)
        self.set(node.inputs[0], a)
        self.set(node.inputs[1], b)
        self.set(node.inputs[2], c)
        return node.outputs[0]

    def vmath(self, op, a, b=None):
        node = self.new('ShaderNodeVectorMath', operation=op)
        self.set(node.inputs[0], a)
        self.set(node.inputs[1], b)
        return node.outputs[1] if op in ('DOT_PRODUCT', 'LENGTH', 'DISTANCE') else node.outputs[0]

    def mix(self, fac, a, b, blend='MIX'):
        node = self.new('ShaderNodeMix', data_type='RGBA', blend_type=blend, clamp_factor=True)
        self.set(node.inputs[0], fac)
        self.set(node.inputs[6], a)
        self.set(node.inputs[7], b)
        return node.outputs[2]

    def mixf(self, fac, a, b):
        node = self.new('ShaderNodeMix', data_type='FLOAT', clamp_factor=True)
        self.set(node.inputs[0], fac)
        self.set(node.inputs[2], a)
        self.set(node.inputs[3], b)
        return node.outputs[0]

    def smooth(self, value, lo, hi):
        node = self.new('ShaderNodeMapRange', interpolation_type='SMOOTHSTEP')
        self.set(node.inputs[0], value)
        node.inputs[1].default_value = lo
        node.inputs[2].default_value = hi
        return node.outputs[0]

    def noise(self, vec, scale, detail=2.0, rough=0.5):
        node = self.new('ShaderNodeTexNoise', noise_dimensions='3D')
        self.set(node.inputs['Vector'], vec)
        node.inputs['Scale'].default_value = scale
        node.inputs['Detail'].default_value = detail
        node.inputs['Roughness'].default_value = rough
        return node.outputs[0]

    def scale_vec(self, vec, factors):
        node = self.new('ShaderNodeVectorMath', operation='MULTIPLY')
        self.set(node.inputs[0], vec)
        node.inputs[1].default_value = factors
        return node.outputs[0]

    def grey(self, value):
        node = self.new('ShaderNodeCombineColor')
        for i in range(3):
            self.set(node.inputs[i], value)
        return node.outputs[0]

    def xyz(self, vec):
        node = self.new('ShaderNodeSeparateXYZ')
        self.set(node.inputs[0], vec)
        return node.outputs


def _socket(tree, name, kind, in_out, default=None):
    sock = tree.interface.new_socket(name=name, in_out=in_out, socket_type=kind)
    if default is not None:
        sock.default_value = default
    return sock


def _position(g):
    """Model-space position plus the per-model seed offset (node 'Seed' in HC_Seed)."""
    geo = g.new('ShaderNodeNewGeometry')
    seed = g.new('ShaderNodeGroup')
    seed.node_tree = _seed_group()
    return g.vmath('ADD', geo.outputs['Position'], seed.outputs[0]), geo


def _seed_group():
    tree = bpy.data.node_groups.get('HC_Seed')
    if tree is None:
        tree = bpy.data.node_groups.new('HC_Seed', 'ShaderNodeTree')
        _socket(tree, 'Offset', 'NodeSocketVector', 'OUTPUT')
        out = tree.nodes.new('NodeGroupOutput')
        comb = tree.nodes.new('ShaderNodeCombineXYZ')
        comb.name = 'Seed'
        tree.links.new(comb.outputs[0], out.inputs[0])
    return tree


def set_seed(offset):
    """Moves all noise patterns, so every model gets its own dirt."""
    comb = _seed_group().nodes['Seed']
    for i in range(3):
        comb.inputs[i].default_value = offset[i]


def _weather_group():
    tree = bpy.data.node_groups.get('HC_Weather')
    if tree is not None:
        return tree
    tree = bpy.data.node_groups.new('HC_Weather', 'ShaderNodeTree')
    _socket(tree, 'Color', 'NodeSocketColor', 'INPUT', (0.5, 0.5, 0.5, 1.0))
    _socket(tree, 'Roughness', 'NodeSocketFloat', 'INPUT', 0.5)
    _socket(tree, 'Metallic', 'NodeSocketFloat', 'INPUT', 0.0)
    _socket(tree, 'Wear', 'NodeSocketFloat', 'INPUT', 1.0)
    _socket(tree, 'WearColor', 'NodeSocketColor', 'INPUT', (0.46, 0.46, 0.47, 1.0))
    _socket(tree, 'WearMetal', 'NodeSocketFloat', 'INPUT', 1.0)
    _socket(tree, 'Grime', 'NodeSocketFloat', 'INPUT', 1.0)
    _socket(tree, 'Dust', 'NodeSocketFloat', 'INPUT', 1.0)
    _socket(tree, 'Scuff', 'NodeSocketFloat', 'INPUT', 0.0)
    _socket(tree, 'Streaks', 'NodeSocketFloat', 'INPUT', 1.0)
    _socket(tree, 'Color', 'NodeSocketColor', 'OUTPUT')
    _socket(tree, 'Roughness', 'NodeSocketFloat', 'OUTPUT')
    _socket(tree, 'Metallic', 'NodeSocketFloat', 'OUTPUT')
    _socket(tree, 'AO', 'NodeSocketFloat', 'OUTPUT')
    g = Graph(tree)
    gin = g.new('NodeGroupInput')
    gout = g.new('NodeGroupOutput')
    pos, geo = _position(g)
    normal_z = g.xyz(geo.outputs['Normal'])[2]

    # occlusion: soft and wide, it drives both the grime and the texture's AO channel
    ao_node = g.new('ShaderNodeAmbientOcclusion', samples=16, only_local=False, inside=False)
    ao_node.inputs['Distance'].default_value = 0.30
    ao = ao_node.outputs['AO']

    # exposed edges: where the rounded normal leaves the real one, plus the real bevel faces
    bev = g.new('ShaderNodeBevel', samples=8)
    bev.inputs['Radius'].default_value = 0.013
    turn = g.math('SUBTRACT', 1.0, g.vmath('DOT_PRODUCT', bev.outputs['Normal'], geo.outputs['True Normal']))
    edge = g.math('MULTIPLY', turn, 12.0, clamp=True)
    flag = g.new('ShaderNodeAttribute', attribute_type='GEOMETRY', attribute_name=BEVEL_LAYER)
    edge = g.math('MAXIMUM', edge, g.math('MULTIPLY', flag.outputs['Factor'], 0.9))
    convex = g.smooth(ao, 0.55, 0.85)

    n_chip = g.noise(pos, 48.0, 2.0, 0.60)
    n_mid = g.noise(pos, 11.0, 3.0, 0.55)
    n_big = g.noise(pos, 2.6, 2.0, 0.50)
    n_scuff = g.noise(pos, 6.5, 5.0, 0.68)

    # chipped paint along edges, patchy
    patchy = g.math('ADD', g.math('ADD', g.math('MULTIPLY', n_chip, 0.45), g.math('MULTIPLY', n_mid, 0.45)),
                    g.math('MULTIPLY', n_big, 0.30))
    chip = g.math('MULTIPLY', g.math('MULTIPLY', edge, convex), patchy)
    wear_edge = g.math('MULTIPLY', g.smooth(chip, 0.46, 0.56), gin.outputs['Wear'], clamp=True)
    # worn patches on open faces: Scuff moves the threshold, so it sets how much area is worn
    lo = g.math('SUBTRACT', 0.80, g.math('MULTIPLY', gin.outputs['Scuff'], 0.42))
    scuff_raw = g.math('DIVIDE', g.math('SUBTRACT', n_scuff, lo), 0.05, clamp=True)
    scuff = g.math('MULTIPLY', g.math('MULTIPLY', scuff_raw, convex),
                   g.math('MULTIPLY', gin.outputs['Scuff'], 6.0, clamp=True))
    scuff = g.math('MULTIPLY', scuff, g.math('MINIMUM', gin.outputs['Wear'], 1.0), clamp=True)
    wear = g.math('MAXIMUM', wear_edge, g.math('MULTIPLY', scuff, 0.85))

    # grime in crevices plus faint vertical streaks
    cavity = g.math('SUBTRACT', 1.0, g.smooth(ao, 0.30, 0.97))
    grime = g.math('MULTIPLY', g.math('MULTIPLY', cavity, g.math('ADD', 0.55, g.math('MULTIPLY', n_big, 1.3))),
                   gin.outputs['Grime'], clamp=True)
    streak_n = g.noise(g.scale_vec(pos, (13.0, 13.0, 0.5)), 1.0, 2.0, 0.5)
    upright = g.math('SUBTRACT', 1.0, g.math('ABSOLUTE', normal_z), clamp=True)
    streak = g.math('MULTIPLY', g.math('MULTIPLY', g.smooth(streak_n, 0.50, 0.78), upright),
                    g.math('MULTIPLY', gin.outputs['Streaks'], 0.62), clamp=True)
    grime = g.math('MAXIMUM', grime, streak)
    # dirt kicked up from the floor, and large soft blotches of old dirt everywhere
    low = g.smooth(g.xyz(geo.outputs['Position'])[2], 0.24, 0.0)
    splash = g.math('MULTIPLY', g.math('MULTIPLY', low, g.math('ADD', 0.25, g.math('MULTIPLY', n_mid, 0.9))),
                    g.math('MULTIPLY', gin.outputs['Grime'], 0.60), clamp=True)
    grime = g.math('MAXIMUM', grime, splash)
    blotch = g.math('MULTIPLY', g.smooth(g.noise(pos, 1.7, 3.0, 0.6), 0.48, 0.74),
                    g.math('MULTIPLY', gin.outputs['Grime'], 0.42), clamp=True)
    grime = g.math('MAXIMUM', grime, blotch)

    # dust settles on faces that look up
    dust = g.math('MULTIPLY', g.math('MULTIPLY', g.smooth(normal_z, 0.30, 0.95),
                                     g.math('ADD', 0.30, g.math('MULTIPLY', n_mid, 0.7))),
                  g.math('MULTIPLY', gin.outputs['Dust'], 0.30), clamp=True)

    # colour
    shade = g.math('ADD', 0.80, g.math('MULTIPLY', n_big, 0.40))
    col = g.mix(1.0, gin.outputs['Color'], g.grey(shade), 'MULTIPLY')
    wear_tint = g.mix(g.math('MULTIPLY', n_mid, 0.5), gin.outputs['WearColor'], (0.22, 0.20, 0.18))
    col = g.mix(wear, col, wear_tint)
    col = g.mix(g.math('MULTIPLY', grime, 0.88), col, (0.020, 0.017, 0.013))
    col = g.mix(dust, col, (0.215, 0.200, 0.175))
    ao_soft = g.math('POWER', ao, 1.15)
    col = g.mix(0.55, col, g.grey(ao_soft), 'MULTIPLY')

    # roughness and metallic
    rough = g.math('MULTIPLY', gin.outputs['Roughness'], g.math('ADD', 0.84, g.math('MULTIPLY', n_mid, 0.32)))
    wear_m = g.math('MULTIPLY', wear, gin.outputs['WearMetal'])
    rough = g.mixf(wear_m, rough, 0.34)
    rough = g.mixf(g.math('MULTIPLY', grime, 0.75), rough, 0.92)
    rough = g.mixf(dust, rough, 0.88)
    rough = g.math('MAXIMUM', g.math('MINIMUM', rough, 1.0), 0.06)
    metal = g.mixf(wear, gin.outputs['Metallic'], g.math('MAXIMUM', gin.outputs['Metallic'], gin.outputs['WearMetal']))
    metal = g.math('MULTIPLY', metal, g.math('SUBTRACT', 1.0, g.math('MULTIPLY', grime, 0.85)))
    metal = g.math('MULTIPLY', metal, g.math('SUBTRACT', 1.0, g.math('MULTIPLY', dust, 1.2), clamp=True), clamp=True)

    tree.links.new(col, gout.inputs['Color'])
    tree.links.new(rough, gout.inputs['Roughness'])
    tree.links.new(metal, gout.inputs['Metallic'])
    tree.links.new(ao, gout.inputs['AO'])
    return tree


def _ice_group():
    """Ice: pale and glossy, deep blue in the crevices, frost on top and on the edges."""
    tree = bpy.data.node_groups.get('HC_Ice')
    if tree is not None:
        return tree
    tree = bpy.data.node_groups.new('HC_Ice', 'ShaderNodeTree')
    _socket(tree, 'Color', 'NodeSocketColor', 'OUTPUT')
    _socket(tree, 'Roughness', 'NodeSocketFloat', 'OUTPUT')
    _socket(tree, 'Metallic', 'NodeSocketFloat', 'OUTPUT')
    _socket(tree, 'AO', 'NodeSocketFloat', 'OUTPUT')
    g = Graph(tree)
    gout = g.new('NodeGroupOutput')
    pos, geo = _position(g)
    normal_z = g.xyz(geo.outputs['Normal'])[2]
    ao_node = g.new('ShaderNodeAmbientOcclusion', samples=16, only_local=False, inside=False)
    ao_node.inputs['Distance'].default_value = 0.45
    ao = ao_node.outputs['AO']
    n_big = g.noise(pos, 2.2, 3.0, 0.55)
    n_mid = g.noise(pos, 7.0, 3.0, 0.6)
    flag = g.new('ShaderNodeAttribute', attribute_type='GEOMETRY', attribute_name=BEVEL_LAYER)

    open_ = g.smooth(ao, 0.30, 0.95)
    col = g.mix(open_, (0.045, 0.170, 0.330), (0.500, 0.680, 0.790))
    col = g.mix(g.smooth(n_big, 0.35, 0.75), col, (0.330, 0.530, 0.680))
    veins = g.smooth(g.math('ABSOLUTE', g.math('SUBTRACT', n_mid, 0.5)), 0.035, 0.0)
    col = g.mix(g.math('MULTIPLY', veins, 0.16), col, (0.720, 0.850, 0.920))
    frost = g.smooth(g.math('ADD', normal_z, g.math('MULTIPLY', g.math('SUBTRACT', n_mid, 0.5), 0.9)), 0.45, 0.95)
    frost = g.math('MAXIMUM', g.math('MULTIPLY', frost, open_), g.math('MULTIPLY', flag.outputs['Factor'], 0.7))
    col = g.mix(frost, col, (0.800, 0.860, 0.890))
    rough = g.mixf(frost, g.mixf(open_, 0.30, 0.10), 0.55)
    tree.links.new(col, gout.inputs['Color'])
    tree.links.new(rough, gout.inputs['Roughness'])
    gout.inputs['Metallic'].default_value = 0.0
    tree.links.new(ao, gout.inputs['AO'])
    return tree


class Library:
    """Hands out one material per surface key and remembers which ones exist for the bake."""

    def __init__(self):
        self.mats = {}

    def get(self, key):
        mat = self.mats.get(key)
        if mat is None:
            mat = self._make(key)
            self.mats[key] = mat
        return mat

    # -- construction ----------------------------------------------------------------------------
    def _blank(self, name):
        mat = bpy.data.materials.new(name)
        tree = mat.node_tree
        tree.nodes.clear()
        g = Graph(tree)
        out = g.new('ShaderNodeOutputMaterial')
        emit = g.new('ShaderNodeEmission')
        emit.name = 'HC_Emit'
        tree.links.new(emit.outputs[0], out.inputs['Surface'])
        img = g.new('ShaderNodeTexImage')
        img.name = 'HC_Target'
        tree.nodes.active = img
        return mat, g, emit

    def _make(self, key):
        if key in (GLOW, GLASS):
            # not baked into the texture; they only need to exist and to shade the faces around them
            mat, g, emit = self._blank(key)
            emit.inputs['Color'].default_value = (0.0, 0.0, 0.0, 1.0)
            return mat
        mat, g, emit = self._blank("HC_" + key)
        tree = mat.node_tree
        if key.startswith('hazard'):
            spec = dict(_DEFAULTS, col=HAZARD_YELLOW, scuff=0.22, kind='hazard')
            parts = key.split(':')
            axes = parts[1] if len(parts) > 1 else 'xz'
            period = float(parts[2]) if len(parts) > 2 else 0.14
        else:
            spec = dict(_DEFAULTS, **SURFACES[key])
        kind = spec['kind']

        if kind == 'ice':
            node = g.new('ShaderNodeGroup')
            node.node_tree = _ice_group()
        else:
            node = g.new('ShaderNodeGroup')
            node.node_tree = _weather_group()
            pos, geo = _position(g)
            col = g.new('ShaderNodeRGB').outputs[0]
            col.default_value = (*spec['col'], 1.0)
            rough = spec['rough']
            if spec['var'] > 0.0:
                # separate parts get slightly different shades
                rnd = geo.outputs['Random Per Island']
                shade = g.math('ADD', 1.0 - spec['var'] * 0.5, g.math('MULTIPLY', rnd, spec['var']))
                col = g.mix(1.0, col, g.grey(shade), 'MULTIPLY')
            if kind == 'hazard':
                vec = {'x': (1.0, 0.0, 0.0), 'y': (0.0, 1.0, 0.0), 'z': (0.0, 0.0, 1.0)}
                direction = [sum(vec[a][i] for a in axes) for i in range(3)]
                along = g.math('DIVIDE', g.vmath('DOT_PRODUCT', geo.outputs['Position'], tuple(direction)), period)
                stripe = g.math('GREATER_THAN', g.math('FRACT', along), 0.5)
                col = g.mix(stripe, col, HAZARD_BLACK)
            elif kind == 'fabric':
                # faded blotches and a slightly lighter, dusty nap
                fade = g.smooth(g.noise(pos, 4.0, 3.0, 0.6), 0.40, 0.75)
                light = tuple(min(1.0, c * 1.45 + 0.02) for c in spec['col'])
                col = g.mix(g.math('MULTIPLY', fade, 0.55), col, light)
                dark = tuple(c * 0.55 for c in spec['col'])
                stain = g.smooth(g.noise(pos, 2.3, 2.0, 0.5), 0.58, 0.72)
                col = g.mix(g.math('MULTIPLY', stain, 0.6), col, dark)
            elif kind == 'brushed':
                grain = g.noise(g.scale_vec(pos, (2.0, 2.0, 40.0)), 1.0, 2.0, 0.6)
                rough = g.math('ADD', spec['rough'] - 0.08, g.math('MULTIPLY', grain, 0.18))
                tone = g.math('ADD', 0.88, g.math('MULTIPLY', grain, 0.24))
                col = g.mix(1.0, col, g.grey(tone), 'MULTIPLY')
            elif kind == 'soil':
                grit = g.smooth(g.noise(pos, 38.0, 2.0, 0.6), 0.52, 0.70)
                col = g.mix(g.math('MULTIPLY', grit, 0.6), col, (0.095, 0.070, 0.048))
            elif kind == 'leaf':
                rnd = geo.outputs['Random Per Island']
                col = g.mix(g.math('MULTIPLY', rnd, 0.7), col, (0.110, 0.160, 0.040))
                col = g.mix(g.smooth(g.noise(pos, 9.0, 2.0, 0.5), 0.62, 0.78), col, (0.150, 0.130, 0.040))
            elif kind == 'core':
                bands = g.noise(g.scale_vec(pos, (0.6, 0.6, 26.0)), 1.0, 3.0, 0.6)
                col = g.mix(g.smooth(bands, 0.56, 0.66), col, (0.200, 0.200, 0.180))
                col = g.mix(g.smooth(bands, 0.30, 0.42), (0.760, 0.840, 0.880), col)
            g.set(node.inputs['Color'], col)
            g.set(node.inputs['Roughness'], rough)
            node.inputs['Metallic'].default_value = spec['metal']
            node.inputs['Wear'].default_value = spec['wear']
            node.inputs['WearColor'].default_value = (*spec['wear_col'], 1.0)
            node.inputs['WearMetal'].default_value = spec['wear_metal']
            node.inputs['Grime'].default_value = spec['grime']
            node.inputs['Dust'].default_value = spec['dust']
            node.inputs['Scuff'].default_value = spec['scuff']
            node.inputs['Streaks'].default_value = spec['streaks'] * spec['grime']
        node.name = 'HC_Surface'
        orm = g.new('ShaderNodeCombineColor')
        orm.name = 'HC_ORM'
        tree.links.new(node.outputs['AO'], orm.inputs[0])
        tree.links.new(node.outputs['Roughness'], orm.inputs[1])
        tree.links.new(node.outputs['Metallic'], orm.inputs[2])
        tree.links.new(node.outputs['Color'], emit.inputs['Color'])
        tree.nodes.active = tree.nodes['HC_Target']
        return mat

    # -- bake wiring -----------------------------------------------------------------------------
    def wire(self, channel, image):
        """channel 'bc' or 'orm': which result every surface sends to its Emission shader."""
        for key, mat in self.mats.items():
            tree = mat.node_tree
            target = tree.nodes['HC_Target']
            target.image = image
            tree.nodes.active = target
            surf = tree.nodes.get('HC_Surface')
            if surf is None:
                continue
            emit = tree.nodes['HC_Emit']
            src = surf.outputs['Color'] if channel == 'bc' else tree.nodes['HC_ORM'].outputs[0]
            tree.links.new(src, emit.inputs['Color'])
