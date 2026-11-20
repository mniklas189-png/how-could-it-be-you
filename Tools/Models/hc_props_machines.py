"""Machines and lab equipment: planter, cryo pod, ice-core rack, pump, generator."""
import math

import bmesh
from mathutils import Euler, Matrix, Vector

import hc_geo
from hc_spec import model


def flange(b, mat, p0, axis, r, t=0.025, bolts=8, bolt_r=0.011, bolt_mat='steel'):
    """Pipe flange: a disc from p0 along axis ('x', 'z', ...) with a ring of bolt heads on its far face."""
    d = Vector({'x': (1, 0, 0), '-x': (-1, 0, 0), 'y': (0, 1, 0), '-y': (0, -1, 0), 'z': (0, 0, 1), '-z': (0, 0, -1)}[axis])
    p0 = Vector(p0)
    b.cyl(mat, p0, p0 + d * t, r, n=14, bev=0.003)
    side = Vector((0, 0, 1)) if abs(d.z) < 0.5 else Vector((1, 0, 0))
    u = d.cross(side).normalized()
    v = d.cross(u)
    for i in range(bolts):
        a = 2.0 * math.pi * (i + 0.5) / bolts
        q = p0 + d * t + (u * math.cos(a) + v * math.sin(a)) * (r - 0.022)
        b.cyl(bolt_mat, q - d * 0.001, q + d * 0.008, bolt_r, n=6, caps=(False, True))


# ----------------------------------------------------------------------------------------------------
@model('Planter', (120, 50, 165), tris=8000, glow=(0.85, 0.20, 1.0), glow_strength=10)
def planter(b):
    rng = b.rng
    frame, trough = 'paint_dark', 'paint_white'
    for sx in (-1, 1):
        for sy in (-1, 1):
            b.box(frame, (sx * 0.56 - 0.02, sx * 0.56 + 0.02), (sy * 0.21 - 0.02, sy * 0.21 + 0.02), (0.0, 0.64), bev=0.004)
            b.box('rubber', (sx * 0.56 - 0.026, sx * 0.56 + 0.026), (sy * 0.21 - 0.026, sy * 0.21 + 0.026), (0.0, 0.015), bev=0.0)
    b.box(frame, (-0.56, 0.56), (-0.21, 0.21), (0.08, 0.11), bev=0.004)
    # nutrient tank with pressed ribs, filler cap, sight tube; pump box beside it
    b.box('plastic_white', (-0.50, 0.30), (-0.19, 0.19), (0.11, 0.44), bev=0.025, seg=2)
    for x in (-0.36, -0.10, 0.16):
        b.box('plastic_white', (x - 0.035, x + 0.035), (-0.197, 0.197), (0.14, 0.41), bev=0.010)
    b.cyl('plastic_black', (-0.36, 0.0, 0.43), (-0.36, 0.0, 0.47), 0.05, n=10, bev=0.005)
    b.cyl('vial', (0.305, -0.13, 0.15), (0.305, -0.13, 0.41), 0.011, n=6)
    for z in (0.15, 0.41):
        b.box('gunmetal', (0.285, 0.325), (-0.145, -0.115), (z - 0.012, z + 0.012), bev=0.002)
    b.box('paint_mid', (0.35, 0.53), (-0.15, 0.15), (0.11, 0.30), bev=0.008)
    b.label((0.375, 0.505), -0.15, (0.20, 0.27), lines=2)
    b.cyl('plastic_black', (0.44, -0.15, 0.15), (0.44, -0.166, 0.15), 0.02, n=10, bev=0.003)
    b.pipe('rubber', [(0.46, 0.06, 0.30), (0.46, 0.06, 0.46), (0.40, 0.06, 0.56), (0.40, 0.06, 0.64)], 0.014, n=6, fillet=0.04)
    b.pipe('rubber', [(-0.10, 0.09, 0.64), (-0.10, 0.09, 0.43)], 0.017, n=6)
    # trough: walls, floor, lip, substrate
    b.ring(trough, [(-0.03, 0.62), (0.0, 0.62), (0.0, 0.86), (-0.03, 0.86)], 0.60, 0.25, bev=0.004)
    b.box(trough, (-0.58, 0.58), (-0.23, 0.23), (0.62, 0.66), bev=0.0)
    b.ring('paint_mid', [(-0.036, 0.852), (0.0, 0.852), (0.0, 0.872), (-0.036, 0.872)], 0.60, 0.25, bev=0.002)
    b.label((-0.10, 0.10), -0.25, (0.70, 0.78), lines=3)
    for x in (-0.45, 0.45):
        b.bolt('steel', (x, -0.25, 0.74), '-y', r=0.010, h=0.005)
    b.add(hc_geo.bm_grid(12, 5, 1.142, 0.442, lambda u, v: 0.010 * hc_geo.fbm(u * 9.0, v * 4.0)), 'substrate',
          loc=(0.0, 0.0, 0.825))
    # plants
    for i in range(5):
        for row in (-1, 1):
            x = -0.46 + i * 0.23 + rng.uniform(-0.025, 0.025)
            y = row * 0.10 + rng.uniform(-0.02, 0.02)
            size = rng.uniform(0.8, 1.2)
            b.cyl('leaf', (x, y, 0.82), (x, y, 0.82 + 0.10 * size), 0.006, n=5, r2=0.003)
            leaves = 7
            for k in range(leaves):
                length = rng.uniform(0.12, 0.20) * size
                yaw = k * 360.0 / leaves + rng.uniform(-18.0, 18.0)
                lift = rng.uniform(22.0, 62.0)
                slim, droop = rng.uniform(0.36, 0.48), rng.uniform(0.35, 0.8)
                place = Matrix.Translation((x, y, 0.83 + 0.013 * k * size)) @ Euler(
                    (math.radians(lift), 0.0, math.radians(yaw)), 'XYZ').to_matrix().to_4x4()
                # keep every leaf inside the trough's footprint: shorten it until it fits
                while True:
                    bm = hc_geo.bm_leaf(length, length * slim, droop)
                    tips = [place @ v.co for v in bm.verts]
                    if length < 0.06 or all(abs(p.x) < 0.585 and abs(p.y) < 0.24 for p in tips):
                        break
                    bm.free()
                    length *= 0.86
                b.add(bm, 'leaf', matrix=place)
    # grow light: two posts, a bar with lit faces looking down and outwards, cooling fins on top
    for sx in (-1, 1):
        b.box(frame, (sx * 0.575 - 0.015, sx * 0.575 + 0.015), (-0.022, 0.022), (0.86, 1.60), bev=0.004)
        b.box(frame, (sx * 0.585 - 0.015, sx * 0.585 + 0.015), (-0.05, 0.05), (0.86, 0.90), bev=0.004)
    b.prism(frame, [(-0.035, 1.56), (0.035, 1.56), (0.08, 1.60), (0.08, 1.642), (-0.08, 1.642), (-0.08, 1.60)], 'x', -0.60, 0.60,
            bev=0.004)
    tilt = math.degrees(math.atan2(0.04, 0.045))
    n = Vector((0.0, 0.04, -0.045)).normalized()
    for sy in (-1, 1):
        b.cbox('Glow', (0.0, sy * (0.0575 + n.y * 0.0015), 1.58 + n.z * 0.0015), (1.10, 0.044, 0.003), bev=0.0,
               rot=(sy * tilt, 0, 0))
    b.box('Glow', (-0.55, 0.55), (-0.026, 0.026), (1.5575, 1.56), bev=0.0)
    for i in range(10):
        x = -0.495 + i * 0.11
        b.box('gunmetal', (x - 0.004, x + 0.004), (-0.07, 0.07), (1.64, 1.65), bev=0.0)
    b.pipe('rubber', [(0.585, 0.03, 1.60), (0.585, 0.034, 1.30), (0.585, 0.034, 0.90)], 0.006, n=6, fillet=0.02,
           caps=(False, False))


# ----------------------------------------------------------------------------------------------------
def _arch(b, xs, segs, ry, rz, z0, cells, mat, inward=False, ends=None):
    """Part of a half-elliptic vault along X. cells(i, k) says which grid cells to make (i along X, k around).
    ends: indices into xs where a flat end wall closes the vault."""
    bm = hc_geo.new_bm()
    grid = [[bm.verts.new((x, ry * math.cos(math.pi * k / segs), z0 + rz * math.sin(math.pi * k / segs)))
             for k in range(segs + 1)] for x in xs]
    for i in range(len(xs) - 1):
        for k in range(segs):
            if cells(i, k):
                quad = (grid[i][k], grid[i][k + 1], grid[i + 1][k + 1], grid[i + 1][k])
                bm.faces.new(quad[::-1] if inward else quad)
    for i in (ends or ()):
        loop = grid[i]
        first = (i == 0)
        # seen from outside, the wall at the low-X end faces -X
        outward = loop[::-1] if first else loop
        bm.faces.new(outward[::-1] if inward else outward)
    lonely = [v for v in bm.verts if not v.link_faces]
    if lonely:
        bmesh.ops.delete(bm, geom=lonely, context='VERTS')
    bm.normal_update()
    return b.add(bm, mat)


def _reveal(b, xs, segs, outer, inner, z0, window, mat):
    """The cut edge around the window: joins the outer skin to the inner liner."""
    bm = hc_geo.new_bm()

    def pt(x, k, radii):
        return Vector((x, radii[0] * math.cos(math.pi * k / segs), z0 + radii[1] * math.sin(math.pi * k / segs)))

    def quad(a, b_, mid):
        vs = [bm.verts.new(p) for p in (pt(*a, outer), pt(*b_, outer), pt(*b_, inner), pt(*a, inner))]
        f = bm.faces.new(vs)
        f.normal_update()
        centre = (vs[0].co + vs[1].co + vs[2].co + vs[3].co) / 4.0
        if f.normal.dot(mid - centre) < 0.0:
            f.normal_flip()

    for i in range(len(xs) - 1):
        for k in range(segs):
            if not window(i, k):
                continue
            mid = (pt(xs[i], k, outer) + pt(xs[i + 1], k + 1, outer)) * 0.5
            if i == 0 or not window(i - 1, k):
                quad((xs[i], k), (xs[i], k + 1), mid)
            if i == len(xs) - 2 or not window(i + 1, k):
                quad((xs[i + 1], k), (xs[i + 1], k + 1), mid)
            if k == 0 or not window(i, k - 1):
                quad((xs[i], k), (xs[i + 1], k), mid)
            if k == segs - 1 or not window(i, k + 1):
                quad((xs[i], k + 1), (xs[i + 1], k + 1), mid)
    bm.normal_update()
    return b.add(bm, mat)


def _arch_band(b, mat, x0, x1, ry, rz, z0, thick, segs=16, k0=0, k1=None):
    """Raised band over the vault from x0 to x1 (a closed strip standing thick above the skin)."""
    k1 = segs if k1 is None else k1
    bm = hc_geo.new_bm()

    def ring(x, extra):
        return [bm.verts.new((x, (ry + extra) * math.cos(math.pi * k / segs), z0 + (rz + extra) * math.sin(math.pi * k / segs)))
                for k in range(k0, k1 + 1)]

    a_out, b_out = ring(x0, thick), ring(x1, thick)
    a_in, b_in = ring(x0, -0.004), ring(x1, -0.004)
    for j in range(k1 - k0):
        bm.faces.new((a_out[j], a_out[j + 1], b_out[j + 1], b_out[j]))
        bm.faces.new((a_in[j], a_out[j], a_out[j + 1], a_in[j + 1])[::-1])
        bm.faces.new((b_in[j], b_out[j], b_out[j + 1], b_in[j + 1]))
        bm.faces.new((a_in[j], a_in[j + 1], b_in[j + 1], b_in[j])[::-1])
    bm.faces.new((a_in[0], a_out[0], b_out[0], b_in[0]))
    bm.faces.new((a_in[-1], a_out[-1], b_out[-1], b_in[-1])[::-1])
    hc_geo.fix_normals(bm)
    return b.add(bm, mat)


@model('CryoPod', (220, 95, 115), tris=8000, glow=(0.42, 0.80, 1.0), glow_strength=8, glass=(0.045, 0.080, 0.115))
def cryo_pod(b):
    shell, trim, base = 'paint_mid', 'paint_frost', 'paint_dark'
    y0 = -0.475
    # base with toe kick, louvre, access panel and status strip
    b.box('pit_rib', (-1.07, 1.07), (-0.445, 0.445), (0.0, 0.04), bev=0.0)
    b.box(base, (-1.10, 1.10), (y0, 0.475), (0.035, 0.30), bev=0.012)
    b.slats(base, (-0.95, -0.45), y0, (0.09, 0.24), 6, depth=0.014, back='pit')
    b.box('paint_mid', (-0.35, 0.30), (y0 - 0.006, y0), (0.075, 0.265), bev=0.003)
    for x in (-0.32, 0.27):
        for z in (0.10, 0.24):
            b.bolt('steel', (x, y0 - 0.006, z), '-y', r=0.008, h=0.004)
    for x in (-0.20, 0.15):
        b.cyl('gunmetal', (x, y0 - 0.006, 0.17), (x, y0 - 0.012, 0.17), 0.016, n=10)
        b.decal('pit', (x - 0.011, x + 0.011), y0 - 0.0126, (0.168, 0.172))
    b.box('plastic_black', (0.42, 1.0), (y0 - 0.005, y0), (0.19, 0.245), bev=0.002)
    b.decal('Glow', (0.44, 0.98), y0 - 0.0058, (0.208, 0.227))
    b.label((0.44, 0.60), y0, (0.085, 0.15), lines=2)
    # tub
    cx = -0.19                      # the capsule sits towards the head end, the service unit at the foot end
    b.box(shell, (-1.06, 0.68), (-0.42, 0.42), (0.29, 0.62), bev=0.03, seg=2)
    b.ring(trim, [(0.0, 0.595), (0.012, 0.595), (0.012, 0.64), (0.0, 0.64)], 0.87, 0.42, loc=(cx, 0.0, 0.0), bev=0.003)
    b.box('plastic_black', (-0.60, 0.40), (-0.426, -0.42), (0.43, 0.475), bev=0.0)
    b.decal('Glow', (-0.58, 0.38), -0.4268, (0.445, 0.46))
    b.box('plastic_black', (-1.0, -0.76), (-0.427, -0.42), (0.39, 0.56), bev=0.002)
    for i in range(6):
        x = -0.965 + (i % 3) * 0.06
        z = 0.50 - (i // 3) * 0.055
        b.box('paint_orange' if i == 2 else 'plastic_grey', (x - 0.02, x + 0.02), (-0.433, -0.427), (z - 0.018, z + 0.018),
              bev=0.002)
    b.decal('Glow', (-0.985, -0.80), -0.4278, (0.535, 0.548))
    for x in (-0.50, 0.25):
        b.box('gunmetal', (x - 0.035, x + 0.035), (-0.442, -0.42), (0.555, 0.665), bev=0.004)
        b.box('paint_yellow', (x - 0.022, x + 0.022), (-0.458, -0.44), (0.57, 0.66), bev=0.005, rot=(-5, 0, 0))
        b.cyl('gunmetal', (x - 0.06, 0.435, 0.64), (x + 0.06, 0.435, 0.64), 0.02, n=8)
    # bed inside
    b.box('vinyl_teal', (-0.99, 0.61), (-0.345, 0.345), (0.61, 0.69), bev=0.03, seg=2)
    b.box('vinyl_teal', (-0.94, -0.70), (-0.17, 0.17), (0.675, 0.74), bev=0.028, seg=2)
    # lid: outer skin with the window cut out, glass in the cut, dark liner inside
    segs = 16
    xs = [-1.04, -0.90, -0.535, -0.17, 0.20, 0.43, 0.66]
    outer, inner = (0.41, 0.44), (0.378, 0.408)
    z0 = 0.635

    def window(i, k):
        return 1 <= i <= 3 and 3 <= k <= 12

    _arch(b, xs, segs, outer[0], outer[1], z0, lambda i, k: not window(i, k), shell, ends=(0, len(xs) - 1))
    _arch(b, xs, segs, outer[0], outer[1], z0, window, 'Glass')
    xi = [xs[0] + 0.03] + xs[1:-1] + [xs[-1] - 0.03]
    _arch(b, xi, segs, inner[0], inner[1], z0, lambda i, k: not window(i, k), 'plastic_black', inward=True,
          ends=(0, len(xs) - 1))
    _reveal(b, xs, segs, outer, inner, z0, window, trim)
    for x0, x1 in ((-0.965, -0.895), (0.195, 0.265)):
        _arch_band(b, trim, x0, x1, outer[0], outer[1], z0, 0.014, segs)
    _arch_band(b, trim, -0.54, -0.50, outer[0], outer[1], z0, 0.010, segs, 3, 13)
    for k in (3, 13):
        a = math.pi * k / segs
        y, z = (outer[0] + 0.004) * math.cos(a), z0 + (outer[1] + 0.004) * math.sin(a)
        b.cbox(trim, (-0.35, y, z), (1.16, 0.035, 0.016), bev=0.004, rot=(math.degrees(a) - 90.0, 0, 0))
    b.box(trim, (0.50, 0.60), (-0.10, 0.10), (1.062, 1.095), bev=0.008)
    # service unit at the foot end with coolant bottles and hoses to the pod
    b.box('paint_white', (0.74, 1.08), (-0.41, 0.43), (0.29, 0.95), bev=0.012)
    b.slats('paint_white', (0.78, 1.04), -0.41, (0.36, 0.56), 6, depth=0.013, back='pit')
    b.cyl('gunmetal', (0.84, -0.41, 0.78), (0.84, -0.43, 0.78), 0.05, n=14, bev=0.004)
    b.cyl('dial', (0.84, -0.4302, 0.78), (0.84, -0.4312, 0.78), 0.041, n=14)
    b.cbox('paint_red', (0.84, -0.432, 0.785), (0.05, 0.001, 0.005), bev=0.0, rot=(0, -40, 0))
    b.box('plastic_black', (0.93, 1.04), (-0.416, -0.41), (0.73, 0.83), bev=0.002)
    b.decal('Glow', (0.945, 1.025), -0.4168, (0.795, 0.815))
    b.label((0.945, 1.025), -0.416, (0.74, 0.78), lines=2)
    for y in (0.0, 0.22):
        b.lathe(trim, [(0.0, 0.0), (0.078, 0.0), (0.078, 0.10), (0.06, 0.15), (0.025, 0.165), (0.025, 0.185), (0.0, 0.185)], n=12,
                loc=(0.93, y, 0.95))
        b.cyl('hazard:xz:0.06', (0.93, y, 0.98), (0.93, y, 1.015), 0.0795, n=12, caps=(False, False))
    for y in (-0.14, 0.14):
        b.pipe('rubber', [(0.82, y, 0.94), (0.82, y, 1.05), (0.66, y, 1.03), (0.655, y, 0.92)], 0.022, n=8, fillet=0.05)
        b.cyl('steel', (0.82, y, 0.95), (0.82, y, 0.975), 0.03, n=10)
        b.cyl('steel', (0.648, y, 0.925), (0.675, y, 0.932), 0.03, n=10)
    b.pipe('rubber', [(0.82, -0.40, 0.66), (0.82, -0.452, 0.66), (0.62, -0.452, 0.60), (0.50, -0.452, 0.52), (0.50, -0.41, 0.52)],
           0.02, n=8, fillet=0.04)
    b.cyl('steel', (0.82, -0.41, 0.66), (0.82, -0.428, 0.66), 0.028, n=10)
    b.cyl('steel', (0.50, -0.42, 0.52), (0.50, -0.436, 0.52), 0.028, n=10)


# ----------------------------------------------------------------------------------------------------
@model('IceCoreRack', (120, 50, 180), tris=8000, glow=(0.20, 0.90, 1.0), glow_strength=8, glass=(0.70, 0.85, 0.92))
def ice_core_rack(b):
    rng = b.rng
    body, steel = 'paint_white', 'steel'
    y0 = -0.25
    # base unit: cooling grille, status strip, controls
    b.box('paint_dark', (-0.58, 0.58), (-0.22, 0.24), (0.0, 0.06), bev=0.0)
    b.box(body, (-0.60, 0.60), (y0, 0.25), (0.05, 0.45), bev=0.008)
    b.slats('paint_mid', (-0.52, 0.06), y0, (0.11, 0.37), 9, depth=0.013, back='pit')
    b.box('plastic_black', (0.14, 0.52), (y0 - 0.005, y0), (0.30, 0.37), bev=0.002)
    b.decal('Glow', (0.16, 0.50), y0 - 0.0058, (0.325, 0.345))
    b.label((0.14, 0.30), y0, (0.13, 0.24), lines=4)
    b.cyl('plastic_black', (0.40, y0, 0.19), (0.40, y0 - 0.018, 0.19), 0.026, n=10, bev=0.003)
    b.box('paint_red', (0.465, 0.505), (y0 - 0.008, y0), (0.165, 0.215), bev=0.003)
    for turn in (90, -90):
        with b.frame(rot=(0, 0, turn)):
            b.handle(0.0, -0.60, 0.33, w=0.16, h=0.055)
    b.box('gunmetal', (-0.588, 0.588), (-0.238, 0.238), (0.445, 0.465), bev=0.004)
    # back panel, head unit, front posts
    b.box(body, (-0.60, 0.60), (0.215, 0.25), (0.45, 1.74), bev=0.005)
    b.box(body, (-0.60, 0.60), (y0, 0.25), (1.72, 1.80), bev=0.008)
    b.label((-0.52, -0.38), y0, (1.74, 1.78), lines=1)
    for x in (0.30, 0.38, 0.46):
        b.decal('pit', (x, x + 0.05), y0 - 0.0008, (1.752, 1.768))
    for sx in (-1, 1):
        b.box('paint_dark', (sx * 0.578 - 0.022, sx * 0.578 + 0.022), (-0.245, -0.205), (0.465, 1.72), bev=0.004)
        b.pipe('gunmetal', [(sx * 0.27, 0.198, 0.465), (sx * 0.27, 0.198, 1.72)], 0.013, n=6, caps=(False, False))
    # eight sample tubes in two rows: steel end caps, glass, ice cores in pieces
    xs = (-0.405, -0.135, 0.135, 0.405)
    ys = (-0.105, 0.105)
    for x in xs:
        for y in ys:
            b.cyl(steel, (x, y, 0.465), (x, y, 0.535), 0.068, n=12, bev=0.005)
            b.cyl(steel, (x, y, 1.65), (x, y, 1.72), 0.068, n=12, bev=0.005)
            b.cyl('Glass', (x, y, 0.535), (x, y, 1.65), 0.056, n=12, caps=(False, False))
            z = 0.538
            while z < 1.56:
                h = min(rng.uniform(0.22, 0.44), 1.635 - z)
                b.cyl('ice_core', (x, y, z), (x, y, z + h), rng.uniform(0.037, 0.043), n=8)
                z += h + rng.uniform(0.006, 0.022)
    # clamp rails
    for z in (0.92, 1.34):
        b.box('gunmetal', (-0.578, 0.578), (-0.012, 0.012), (z - 0.02, z + 0.02), bev=0.003)
        for x in xs:
            for y in ys:
                s = 1 if y > 0 else -1
                b.cyl(steel, (x, y, z - 0.018), (x, y, z + 0.018), 0.064, n=12)
                b.box('gunmetal', (x - 0.012, x + 0.012), (s * 0.010, s * 0.046), (z - 0.012, z + 0.012), bev=0.0)
            b.box('gunmetal', (x - 0.014, x + 0.014), (-0.19, -0.163), (z - 0.014, z + 0.014), bev=0.002)
            b.bolt(steel, (x + 0.014, -0.177, z), 'x', r=0.007, h=0.005)
    # label rail in front of the tube feet
    b.box('gunmetal', (-0.56, 0.56), (-0.236, -0.226), (0.465, 0.52), bev=0.002)
    for x in xs:
        b.label((x - 0.05, x + 0.05), -0.236, (0.474, 0.512), lines=2)


# ----------------------------------------------------------------------------------------------------
@model('Pump', (160, 90, 150), tris=12000)
def pump(b):
    base, motor, cast = 'paint_dark', 'paint_blue', 'paint_mid'
    az = 0.46                           # height of the shaft
    # skid
    for sy in (-1, 1):
        b.box(base, (-0.80, 0.80), (sy * 0.41 - 0.04, sy * 0.41 + 0.04), (0.0, 0.10), bev=0.006)
    for x in (-0.55, 0.55):
        b.decal('pit', (x - 0.07, x + 0.07), -0.4508, (0.03, 0.07))
    for x in (-0.70, 0.0, 0.70):
        b.box(base, (x - 0.04, x + 0.04), (-0.38, 0.38), (0.02, 0.09), bev=0.0)
    b.box(base, (-0.78, 0.78), (-0.44, 0.44), (0.095, 0.115), bev=0.003)
    for x in (-0.74, 0.74):
        for y in (-0.40, 0.40):
            b.bolt('steel', (x, y, 0.115), 'z', r=0.012, h=0.008)
    # motor with cooling fins, fan cowl, feet, terminal box and conduit
    b.cyl(motor, (-0.66, 0.0, az), (-0.18, 0.0, az), 0.19, n=16, bev=0.006)
    for k in range(16):
        ang = k * 22.5
        if 135.0 < ang < 225.0:
            continue                    # no fins between the feet
        with b.frame(loc=(-0.42, 0.0, az), rot=(ang, 0, 0)):
            b.box(motor, (-0.21, 0.21), (-0.006, 0.006), (0.18, 0.214), bev=0.0)
    b.cyl(base, (-0.77, 0.0, az), (-0.66, 0.0, az), 0.206, n=16, bev=0.012)
    b.cyl('pit', (-0.7708, 0.0, az), (-0.77, 0.0, az), 0.15, n=16, caps=(True, False))
    b.cyl(base, (-0.776, 0.0, az), (-0.77, 0.0, az), 0.05, n=10)
    for i in range(6):
        with b.frame(loc=(-0.7715, 0.0, az), rot=(i * 30.0, 0, 0)):
            b.box(base, (-0.002, 0.002), (-0.15, 0.15), (-0.006, 0.006), bev=0.0)
    b.cyl(motor, (-0.18, 0.0, az), (-0.13, 0.0, az), 0.17, n=16, r2=0.11)
    for x in (-0.58, -0.26):
        b.box(motor, (x - 0.04, x + 0.04), (-0.21, 0.21), (0.115, 0.32), bev=0.006)
        for y in (-0.18, 0.18):
            b.bolt('steel', (x, y, 0.1155), 'z', r=0.012, h=0.01)
    b.box(motor, (-0.52, -0.34), (-0.09, 0.09), (0.62, 0.735), bev=0.008)
    b.box('gunmetal', (-0.51, -0.35), (-0.08, 0.08), (0.733, 0.742), bev=0.002)
    b.pipe('gunmetal', [(-0.43, 0.08, 0.69), (-0.43, 0.30, 0.69), (-0.43, 0.30, 0.115)], 0.016, n=8, fillet=0.06)
    b.label((-0.50, -0.36), -0.1915, (0.43, 0.50), lines=3)
    # local control box on a post
    b.pipe(base, [(-0.62, 0.34, 0.115), (-0.62, 0.34, 0.80)], 0.022, n=8)
    b.box('paint_white', (-0.74, -0.50), (0.27, 0.39), (0.78, 1.08), bev=0.008)
    b.cyl('paint_red', (-0.67, 0.27, 0.98), (-0.67, 0.25, 0.98), 0.024, n=10, bev=0.004)
    b.cyl('paint_teal', (-0.57, 0.27, 0.98), (-0.57, 0.255, 0.98), 0.02, n=10, bev=0.003)
    b.label((-0.71, -0.53), 0.27, (0.82, 0.90), lines=2)
    b.pipe('rubber', [(-0.54, 0.33, 0.78), (-0.54, 0.33, 0.72), (-0.46, 0.31, 0.70)], 0.008, n=6, fillet=0.02)
    # coupling guard, bearing housing
    b.cyl('paint_yellow', (-0.13, 0.0, az), (0.06, 0.0, az), 0.125, n=12, bev=0.005)
    b.box('paint_yellow', (-0.12, 0.05), (-0.125, 0.125), (0.115, az), bev=0.004)
    for i in range(4):
        b.decal('pit', (-0.10 + i * 0.04, -0.085 + i * 0.04), -0.1258, (0.30, 0.40))
    b.cyl(cast, (0.06, 0.0, az), (0.17, 0.0, az), 0.10, n=12, bev=0.005)
    b.box(cast, (0.07, 0.16), (-0.08, 0.08), (0.115, 0.40), bev=0.006)
    # volute casing: a fat disc around the shaft, feet below
    b.lathe(cast, [(0.0, 0.0), (0.19, 0.0), (0.285, 0.03), (0.31, 0.075), (0.31, 0.135), (0.285, 0.18), (0.19, 0.21),
                   (0.0, 0.21)],
            n=18, loc=(0.16, 0.0, az), rot=(0, 90, 0))
    b.box(cast, (0.20, 0.33), (-0.22, 0.22), (0.115, 0.24), bev=0.008)
    for a in range(8):
        ang = math.radians(a * 45.0 + 22.5)
        b.bolt('steel', (0.37, 0.16 * math.cos(ang), az + 0.16 * math.sin(ang)), 'x', r=0.012, h=0.01)
    b.bolt('steel', (0.265, 0.0, az - 0.31), '-z', r=0.016, h=0.014)
    # suction: out of the casing along the shaft, up through an elbow, capped
    b.cyl(cast, (0.36, 0.0, az), (0.38, 0.0, az), 0.085, n=14)
    flange(b, cast, (0.375, 0.0, az), 'x', 0.135)
    flange(b, cast, (0.425, 0.0, az), '-x', 0.135, bolts=0)
    b.pipe(cast, [(0.425, 0.0, az), (0.655, 0.0, az), (0.655, 0.0, 1.30)], 0.075, n=12, fillet=0.13, fseg=5, caps=(False, False))
    flange(b, cast, (0.655, 0.0, 1.30), 'z', 0.135, bolts=0)
    flange(b, 'gunmetal', (0.655, 0.0, 1.325), 'z', 0.135)
    # discharge: up from the top of the casing, gate valve with hand wheel, gauge, capped
    dx, dy = 0.265, -0.15
    b.cyl(cast, (dx, dy, az + 0.16), (dx, dy, 0.86), 0.07, n=14)
    flange(b, cast, (dx, dy, 0.86), 'z', 0.12, bolts=0)
    flange(b, cast, (dx, dy, 0.885), 'z', 0.12, bolts=6)
    b.cyl(cast, (dx, dy, 0.91), (dx, dy, 1.42), 0.06, n=14, caps=(False, False))
    b.box(cast, (dx - 0.085, dx + 0.085), (dy - 0.075, dy + 0.075), (1.0, 1.17), bev=0.016, seg=2)
    b.cyl(cast, (dx, dy - 0.07, 1.085), (dx, dy - 0.14, 1.085), 0.05, n=12, r2=0.035)
    b.cyl('steel', (dx, dy - 0.14, 1.085), (dx, dy - 0.24, 1.085), 0.012, n=8)
    b.torus('paint_red', (dx, dy - 0.215, 1.085), 0.115, 0.012, rot=(90, 0, 0), n_big=18, n_small=6)
    for i in range(3):
        b.cbox('paint_red', (dx, dy - 0.215, 1.085), (0.23, 0.012, 0.016), bev=0.0, rot=(0, i * 60.0, 0))
    b.cyl('paint_red', (dx, dy - 0.20, 1.085), (dx, dy - 0.232, 1.085), 0.026, n=10, bev=0.004)
    flange(b, cast, (dx, dy, 1.42), 'z', 0.11, bolts=0)
    flange(b, 'gunmetal', (dx, dy, 1.445), 'z', 0.11, bolts=6)
    b.pipe('steel', [(dx, dy - 0.05, 1.28), (dx, dy - 0.12, 1.28), (dx, dy - 0.12, 1.33)], 0.009, n=6, fillet=0.02)
    b.cyl('gunmetal', (dx, dy - 0.10, 1.385), (dx, dy - 0.145, 1.385), 0.058, n=14, bev=0.005)
    b.cyl('dial', (dx, dy - 0.1452, 1.385), (dx, dy - 0.1462, 1.385), 0.048, n=14, caps=(False, True))
    b.cbox('label_ink', (dx + 0.008, dy - 0.1468, 1.392), (0.05, 0.001, 0.005), bev=0.0, rot=(0, -38, 0))
    # small priming line from the casing to the suction side
    b.pipe('steel', [(0.30, 0.10, az + 0.28), (0.30, 0.10, 0.90), (0.60, 0.06, 0.90), (0.62, 0.05, 0.84)], 0.011, n=6,
           fillet=0.04)


# ----------------------------------------------------------------------------------------------------
@model('Generator', (200, 100, 140), tris=12000, glow=(1.0, 0.52, 0.07), glow_strength=10)
def generator(b):
    hull, base, dark = 'paint_white', 'paint_dark', 'gunmetal'
    y0 = -0.45
    # skid with fork pockets
    for sy in (-1, 1):
        b.box(base, (-1.0, 1.0), (sy * 0.44 - 0.06, sy * 0.44 + 0.06), (0.0, 0.14), bev=0.008)
    for x in (-0.90, -0.30, 0.30, 0.90):
        b.box(base, (x - 0.05, x + 0.05), (-0.40, 0.40), (0.02, 0.13), bev=0.0)
    for x in (-0.45, 0.45):
        b.decal('pit', (x - 0.13, x + 0.13), -0.5008, (0.035, 0.105))
    # housing, hazard band along its foot, roof
    b.box(hull, (-0.95, 0.95), (y0, 0.45), (0.135, 1.22), bev=0.018, seg=2)
    b.ring('hazard:xyz:0.15', [(0.0, 0.155), (0.004, 0.155), (0.004, 0.225), (0.0, 0.225)], 0.95, 0.45)
    b.box(hull, (-0.97, 0.97), (-0.47, 0.47), (1.205, 1.25), bev=0.012)
    # front: two louvred doors and the control bay
    for x0, x1 in ((-0.90, -0.30), (-0.28, 0.32)):
        b.box(hull, (x0, x1), (y0 - 0.008, y0), (0.26, 1.16), bev=0.004)
        b.slats(hull, (x0 + 0.07, x1 - 0.07), y0 - 0.008, (0.50, 1.06), 12, depth=0.018, back='pit')
        for z in (0.40, 1.02):
            b.cyl(dark, (x0 + 0.002, y0 - 0.012, z - 0.05), (x0 + 0.002, y0 - 0.012, z + 0.05), 0.009, n=8)
        b.cyl(dark, (x1 - 0.045, y0 - 0.008, 0.71), (x1 - 0.045, y0 - 0.02, 0.71), 0.02, n=10, bev=0.003)
        b.decal('pit', (x1 - 0.048, x1 - 0.042), y0 - 0.0208, (0.697, 0.723))
        b.label((x0 + 0.08, x0 + 0.22), y0 - 0.008, (0.32, 0.40), lines=2)
    b.box(hull, (0.36, 0.88), (y0 - 0.008, y0), (0.26, 0.56), bev=0.004)
    b.slats(hull, (0.42, 0.82), y0 - 0.008, (0.31, 0.50), 5, depth=0.016, back='pit')
    b.box(dark, (0.36, 0.88), (y0 - 0.012, y0), (0.60, 1.14), bev=0.004)
    b.box('plastic_black', (0.385, 0.855), (y0 - 0.015, y0 - 0.011), (0.625, 1.115), bev=0.0)
    py = y0 - 0.015
    for i in range(6):
        x = 0.43 + i * 0.05
        b.cyl('Glow', (x, py, 1.065), (x, py - 0.006, 1.065), 0.013, n=8, caps=(False, True))
        b.decal('label', (x - 0.016, x + 0.016), py - 0.0006, (1.025, 1.037))
    for x in (0.47, 0.60):
        b.cyl(dark, (x, py, 0.935), (x, py - 0.02, 0.935), 0.052, n=14, bev=0.004)
        b.cyl('dial', (x, py - 0.0202, 0.935), (x, py - 0.021, 0.935), 0.043, n=14, caps=(False, True))
        b.cbox('paint_red', (x + 0.006, py - 0.0216, 0.94), (0.045, 0.001, 0.005), bev=0.0, rot=(0, -30 - (x * 70) % 40, 0))
    b.cyl('paint_yellow', (0.77, py, 0.935), (0.77, py - 0.012, 0.935), 0.045, n=12, bev=0.003)
    b.cyl('paint_red', (0.77, py - 0.012, 0.935), (0.77, py - 0.04, 0.935), 0.03, n=12, bev=0.006)
    for i in range(4):
        x = 0.44 + i * 0.06
        b.box('plastic_grey' if i != 1 else 'paint_teal', (x - 0.018, x + 0.018), (py - 0.008, py), (0.80, 0.836), bev=0.003)
    b.cyl('steel', (0.72, py, 0.82), (0.72, py - 0.012, 0.82), 0.02, n=10, bev=0.003)
    b.box('plastic_grey', (0.43, 0.52), (py - 0.022, py), (0.655, 0.75), bev=0.004)
    b.box(dark, (0.455, 0.495), (py - 0.045, py - 0.02), (0.70, 0.74), bev=0.004)
    b.label((0.58, 0.83), py, (0.655, 0.745), lines=3)
    # radiator end (-X), connection end (+X)
    with b.frame(rot=(0, 0, -90)):
        b.ring(dark, [(-0.03, 0.0), (0.0, 0.0), (0.0, 0.012), (-0.03, 0.012)], 0.38, 0.40, loc=(0.0, -0.95, 0.70), rot=(90, 0, 0))
        b.slats(dark, (-0.35, 0.35), -0.95, (0.33, 1.07), 15, depth=0.02, back='pit')
    with b.frame(rot=(0, 0, 90)):
        b.box('paint_mid', (-0.30, 0.12), (-0.972, -0.95), (0.45, 0.85), bev=0.006)
        for i in range(3):
            b.cyl('plastic_black', (-0.20 + i * 0.11, -0.972, 0.58), (-0.20 + i * 0.11, -0.99, 0.58), 0.038, n=10, bev=0.004)
        b.label((-0.26, 0.08), -0.972, (0.70, 0.80), lines=3)
        b.cyl(dark, (0.28, -0.95, 0.98), (0.28, -0.985, 1.0), 0.045, n=10)
        b.cyl('paint_red', (0.28, -0.983, 0.999), (0.28, -0.998, 1.008), 0.05, n=10, bev=0.004)
        b.slats(hull, (-0.30, 0.12), -0.95, (0.95, 1.10), 4, depth=0.016, back='pit')
    # roof: air hood, exhaust stub with a flap, lifting eyes
    b.box(hull, (0.15, 0.80), (-0.30, 0.30), (1.245, 1.31), bev=0.014)
    b.slats(hull, (0.20, 0.75), -0.30, (1.258, 1.30), 3, depth=0.012, back='pit')
    b.cyl(dark, (-0.55, 0.12, 1.25), (-0.55, 0.12, 1.268), 0.095, n=14, bev=0.004)
    b.cyl(dark, (-0.55, 0.12, 1.26), (-0.55, 0.12, 1.378), 0.058, n=14)
    b.cyl('pit', (-0.55, 0.12, 1.3775), (-0.55, 0.12, 1.3785), 0.05, n=14, caps=(False, True))
    b.cyl(dark, (-0.60, 0.12, 1.372), (-0.49, 0.12, 1.398), 0.004, n=4)
    b.cbox(dark, (-0.545, 0.12, 1.388), (0.14, 0.13, 0.006), bev=0.0, rot=(0, -13, 0))
    for sx in (-1, 1):
        for sy in (-1, 1):
            b.box(dark, (sx * 0.86 - 0.04, sx * 0.86 + 0.04), (sy * 0.38 - 0.025, sy * 0.38 + 0.025), (1.25, 1.265), bev=0.003)
            b.torus(dark, (sx * 0.86, sy * 0.38, 1.305), 0.036, 0.011, rot=(90, 0, 0), n_big=12, n_small=6)
    # plain service door at the back
    b.box(hull, (-0.60, 0.60), (0.45, 0.456), (0.26, 1.16), bev=0.003)
