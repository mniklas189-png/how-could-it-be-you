"""Geometry helpers for MakeModels.py.

Small bmesh primitives with real bevels, and a Builder that collects the parts of one model,
joins them into a single mesh and gives it the right pivot and normals.

Everything is in metres, Z up, the front of a model faces -Y.
"""
import math
import random
import zlib
from contextlib import contextmanager

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector, noise

# Face attribute that is 1 on bevel faces. The bake reads it to put edge wear exactly on the bevels.
BEVEL_LAYER = "hc_bevel"


# ----------------------------------------------------------------------------------------------------
# bmesh primitives (each returns a new bmesh, the Builder turns it into a part)
# ----------------------------------------------------------------------------------------------------

def new_bm():
    bm = bmesh.new()
    bm.faces.layers.float.new(BEVEL_LAYER)
    return bm


def bevel(bm, width, seg=1, min_angle=35.0):
    """Bevels every edge sharper than min_angle (degrees) and marks the new faces."""
    if width <= 0.0:
        return
    bm.normal_update()
    limit = math.radians(min_angle)
    edges = [e for e in bm.edges if len(e.link_faces) == 2 and e.calc_face_angle(0.0) > limit]
    if not edges:
        return
    ret = bmesh.ops.bevel(bm, geom=edges, offset=width, offset_type='OFFSET', segments=seg,
                          profile=0.5, affect='EDGES', clamp_overlap=True)
    lay = bm.faces.layers.float[BEVEL_LAYER]
    for f in ret['faces']:
        f[lay] = 1.0


def fix_normals(bm):
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])


def bm_box(size, bev=0.0, seg=1):
    """Box centred on the origin."""
    bm = new_bm()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=Vector(size), verts=bm.verts[:])
    bevel(bm, min(bev, 0.45 * min(size)), seg)
    return bm


def bm_cyl(r, h, n=12, r2=None, bev=0.0, caps=(True, True), seg=1):
    """Cylinder or cone along +Z from z = 0 to z = h (r at the bottom, r2 at the top)."""
    bm = new_bm()
    r2 = r if r2 is None else r2
    ang = [2.0 * math.pi * i / n for i in range(n)]
    bot = [bm.verts.new((r * math.cos(a), r * math.sin(a), 0.0)) for a in ang]
    if r2 > 1e-6:
        top = [bm.verts.new((r2 * math.cos(a), r2 * math.sin(a), h)) for a in ang]
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((bot[i], bot[j], top[j], top[i]))
        if caps[1]:
            bm.faces.new(top)
    else:
        tip = bm.verts.new((0.0, 0.0, h))
        for i in range(n):
            bm.faces.new((bot[i], bot[(i + 1) % n], tip))
    if caps[0]:
        bm.faces.new(bot[::-1])
    bevel(bm, min(bev, 0.45 * min(r, r2 if r2 > 1e-6 else r, h)), seg, min_angle=58.0)
    return bm


def bm_lathe(profile, n=16, bev=0.0):
    """Revolves profile [(r, z), ...] around Z. A radius of 0 collapses to a single pole vertex."""
    bm = new_bm()
    rings = []
    for r, z in profile:
        if r <= 1e-6:
            rings.append([bm.verts.new((0.0, 0.0, z))])
        else:
            rings.append([bm.verts.new((r * math.cos(2.0 * math.pi * i / n), r * math.sin(2.0 * math.pi * i / n), z))
                          for i in range(n)])
    for a, b in zip(rings[:-1], rings[1:]):
        if len(a) == 1 and len(b) == 1:
            continue
        for i in range(n):
            j = (i + 1) % n
            if len(a) == 1:
                bm.faces.new((a[0], b[j], b[i]))
            elif len(b) == 1:
                bm.faces.new((a[i], a[j], b[0]))
            else:
                bm.faces.new((a[i], a[j], b[j], b[i]))
    if len(rings[0]) > 1:
        bm.faces.new(rings[0][::-1])
    if len(rings[-1]) > 1:
        bm.faces.new(rings[-1])
    fix_normals(bm)
    bevel(bm, bev, 1, min_angle=58.0)
    return bm


def bm_prism(pts3_a, pts3_b, bev=0.0, seg=1, min_angle=35.0):
    """Closed solid between two matching polygons (lists of 3D points)."""
    bm = new_bm()
    a = [bm.verts.new(p) for p in pts3_a]
    b = [bm.verts.new(p) for p in pts3_b]
    n = len(a)
    bm.faces.new(a[::-1])
    bm.faces.new(b)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((a[i], a[j], b[j], b[i]))
    fix_normals(bm)
    bevel(bm, bev, seg, min_angle=min_angle)
    return bm


def bm_ring(profile, hx, hy, bev=0.0):
    """Closed rectangular ring: the closed profile [(offset, z), ...] is swept around the rectangle
    |x| <= hx, |y| <= hy with mitred corners; offset widens the rectangle on all four sides."""
    bm = new_bm()
    loops = []
    for off, z in profile:
        x, y = hx + off, hy + off
        loops.append([bm.verts.new(p) for p in ((x, y, z), (-x, y, z), (-x, -y, z), (x, -y, z))])
    m = len(loops)
    for k in range(m):
        a, b = loops[k], loops[(k + 1) % m]
        for c in range(4):
            d = (c + 1) % 4
            bm.faces.new((a[c], a[d], b[d], b[c]))
    fix_normals(bm)
    bevel(bm, bev, 1)
    return bm


def bm_torus(big_r, small_r, n_big=16, n_small=6):
    """Torus around Z, centred on the origin."""
    bm = new_bm()
    rings = []
    for i in range(n_big):
        a = 2.0 * math.pi * i / n_big
        ring = []
        for k in range(n_small):
            b = 2.0 * math.pi * k / n_small
            rr = big_r + small_r * math.cos(b)
            ring.append(bm.verts.new((rr * math.cos(a), rr * math.sin(a), small_r * math.sin(b))))
        rings.append(ring)
    for i in range(n_big):
        a, b = rings[i], rings[(i + 1) % n_big]
        for k in range(n_small):
            l = (k + 1) % n_small
            bm.faces.new((a[k], b[k], b[l], a[l]))
    fix_normals(bm)
    return bm


def fillet_path(pts, radius, seg=3):
    """Rounds the inner corners of a polyline."""
    pts = [Vector(p) for p in pts]
    if radius <= 0.0 or len(pts) < 3:
        return pts
    out = [pts[0]]
    for i in range(1, len(pts) - 1):
        p, a, b = pts[i], pts[i - 1], pts[i + 1]
        d1, d2 = (a - p), (b - p)
        l1, l2 = d1.length, d2.length
        d1.normalize()
        d2.normalize()
        phi = d1.angle(d2) * 0.5
        if phi > math.radians(88.0) or phi < 1e-3:
            out.append(p)
            continue
        t = min(radius / math.tan(phi), 0.45 * l1, 0.45 * l2)
        rad = t * math.tan(phi)
        centre = p + (d1 + d2).normalized() * (rad / math.sin(phi))
        u = (p + d1 * t) - centre
        v = (p + d2 * t) - centre
        alpha = u.angle(v)
        for k in range(seg + 1):
            s = k / seg
            w = (u * math.sin((1.0 - s) * alpha) + v * math.sin(s * alpha)) / math.sin(alpha)
            out.append(centre + w)
    out.append(pts[-1])
    return out


def bm_pipe(pts, r, n=8, fillet=0.0, fseg=3, caps=(True, True), r_end=None):
    """Round tube along a polyline with mitred (or filleted) corners. r_end tapers the radius."""
    path = fillet_path(pts, fillet, fseg)
    bm = new_bm()
    dirs = [(path[i + 1] - path[i]).normalized() for i in range(len(path) - 1)]
    d0 = dirs[0]
    side = Vector((0.0, 0.0, 1.0)) if abs(d0.z) < 0.9 else Vector((1.0, 0.0, 0.0))
    u = d0.cross(side).normalized()
    v = d0.cross(u).normalized()
    rings = []
    count = len(path)
    cur = d0
    for i, p in enumerate(path):
        d_in = dirs[max(i - 1, 0)]
        if (d_in - cur).length > 1e-6:
            # carry the cross-section frame along without twisting it
            q = cur.rotation_difference(d_in)
            u = q @ u
            v = q @ v
            cur = d_in
        d_out = dirs[min(i, len(dirs) - 1)]
        plane_n = (d_in + d_out).normalized()
        rad = r if r_end is None else r + (r_end - r) * (i / (count - 1))
        ring = []
        for k in range(n):
            a = 2.0 * math.pi * k / n
            q0 = p + (u * math.cos(a) + v * math.sin(a)) * rad
            denom = d_in.dot(plane_n)
            if abs(denom) > 1e-4:
                q0 = q0 - d_in * ((q0 - p).dot(plane_n) / denom)
            ring.append(bm.verts.new(q0))
        rings.append(ring)
    for a, b in zip(rings[:-1], rings[1:]):
        for k in range(n):
            l = (k + 1) % n
            bm.faces.new((a[k], a[l], b[l], b[k]))
    if caps[0]:
        bm.faces.new(rings[0][::-1])
    if caps[1]:
        bm.faces.new(rings[-1])
    fix_normals(bm)
    return bm


def bm_grid(nx, ny, sx, sy, height, skirt=0.0):
    """Sheet of nx by ny quads (size sx by sy, centred) with z = height(u, v), u and v in 0..1.
    skirt > 0 adds walls down to z = -skirt around the border."""
    bm = new_bm()
    rows = []
    for j in range(ny + 1):
        v = j / ny
        row = []
        for i in range(nx + 1):
            u = i / nx
            row.append(bm.verts.new(((u - 0.5) * sx, (v - 0.5) * sy, height(u, v))))
        rows.append(row)
    for j in range(ny):
        for i in range(nx):
            bm.faces.new((rows[j][i], rows[j][i + 1], rows[j + 1][i + 1], rows[j + 1][i]))
    if skirt > 0.0:
        border = ([rows[0][i] for i in range(nx + 1)] + [rows[j][nx] for j in range(1, ny + 1)]
                  + [rows[ny][i] for i in range(nx - 1, -1, -1)] + [rows[j][0] for j in range(ny - 1, 0, -1)])
        low = [bm.verts.new((b.co.x, b.co.y, -skirt)) for b in border]
        m = len(border)
        for k in range(m):
            l = (k + 1) % m
            bm.faces.new((border[l], border[k], low[k], low[l]))
    bm.normal_update()
    return bm


def bm_hull(points, bev=0.0, cut_z=None):
    """Convex hull of a point cloud: faceted rocks, ice, bundles. cut_z slices the bottom off flat."""
    bm = new_bm()
    for p in points:
        bm.verts.new(p)
    ret = bmesh.ops.convex_hull(bm, input=bm.verts[:], use_existing_faces=False)
    junk = [e for e in ret.get('geom_interior', []) if isinstance(e, bmesh.types.BMVert)]
    junk += [e for e in ret.get('geom_unused', []) if isinstance(e, bmesh.types.BMVert)]
    if junk:
        bmesh.ops.delete(bm, geom=list(set(junk)), context='VERTS')
    bmesh.ops.dissolve_limit(bm, angle_limit=math.radians(4.0), verts=bm.verts[:], edges=bm.edges[:])
    if cut_z is not None:
        res = bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=(0.0, 0.0, cut_z),
                                     plane_no=(0.0, 0.0, 1.0), clear_outer=False, clear_inner=True)
        cut_edges = [e for e in res['geom_cut'] if isinstance(e, bmesh.types.BMEdge)]
        if cut_edges:
            bmesh.ops.edgeloop_fill(bm, edges=cut_edges)
    fix_normals(bm)
    bevel(bm, bev, 1, min_angle=12.0)
    return bm


def bm_leaf(length, width, droop, cup=0.25):
    """Low-poly leaf with two sides: starts at the origin, grows along +Y, droops in -Z at the tip."""
    bm = new_bm()
    outline = [(0.0, 0.0), (0.42, 0.28), (0.5, 0.62), (0.0, 1.0), (-0.5, 0.62), (-0.42, 0.28)]
    spine = [(0.0, 0.3), (0.0, 0.64)]

    def point(x, t, lift):
        return (x * width, t * length, -droop * length * t * t + lift)

    # the outline is shared by both sides, the spine is doubled: a thin closed lens, folded along the middle
    o = [bm.verts.new(point(x, t, 0.0)) for x, t in outline]
    for side in (1, -1):
        lift = -cup * width + (0.0 if side > 0 else -0.003)
        s = [bm.verts.new(point(x, t, lift)) for x, t in spine]
        faces = [(o[0], o[1], s[0]), (o[1], o[2], s[1], s[0]), (o[2], o[3], s[1]),
                 (o[3], o[4], s[1]), (o[4], o[5], s[0], s[1]), (o[5], o[0], s[0])]
        for f in faces:
            bm.faces.new(f if side > 0 else f[::-1])
    bm.normal_update()
    return bm


# ----------------------------------------------------------------------------------------------------
# Builder
# ----------------------------------------------------------------------------------------------------

def _euler(rot):
    return Euler((math.radians(rot[0]), math.radians(rot[1]), math.radians(rot[2])), 'XYZ').to_matrix().to_4x4()


def align_z(p0, p1):
    """Matrix that puts the origin on p0 and +Z along p0 -> p1. Returns (matrix, length)."""
    p0, p1 = Vector(p0), Vector(p1)
    d = p1 - p0
    quat = Vector((0.0, 0.0, 1.0)).rotation_difference(d.normalized())
    return Matrix.Translation(p0) @ quat.to_matrix().to_4x4(), d.length


class Builder:
    """Collects the parts of one model. Rotations are given in degrees."""

    def __init__(self, name, lib):
        self.name = name
        self.lib = lib
        self.rng = random.Random(zlib.crc32(name.encode("ascii")))
        self.parts = []
        self._stack = [Matrix.Identity(4)]

    # -- placement -------------------------------------------------------------------------------
    @contextmanager
    def frame(self, loc=(0.0, 0.0, 0.0), rot=(0.0, 0.0, 0.0)):
        """Everything added inside is placed relative to this frame (for example rot=(0, 0, 90) puts
        details that were written for the front on the +X side)."""
        self._stack.append(self._stack[-1] @ Matrix.Translation(loc) @ _euler(rot))
        try:
            yield
        finally:
            self._stack.pop()

    def add(self, bm, mat, loc=(0.0, 0.0, 0.0), rot=(0.0, 0.0, 0.0), matrix=None, flip=False):
        m = self._stack[-1] @ (matrix if matrix is not None else Matrix.Translation(loc) @ _euler(rot))
        bmesh.ops.transform(bm, matrix=m, verts=bm.verts[:])
        if flip:
            bmesh.ops.reverse_faces(bm, faces=bm.faces[:])
        me = bpy.data.meshes.new(self.name + "_part")
        bm.to_mesh(me)
        bm.free()
        me.materials.append(self.lib.get(mat))
        ob = bpy.data.objects.new(self.name + "_part", me)
        bpy.context.scene.collection.objects.link(ob)
        self.parts.append(ob)
        return ob

    # -- shortcuts -------------------------------------------------------------------------------
    def box(self, mat, xr, yr, zr, bev=0.004, seg=1, rot=(0.0, 0.0, 0.0)):
        """Box from its extents: xr = (x0, x1) and so on. rot turns it around its centre."""
        size = (abs(xr[1] - xr[0]), abs(yr[1] - yr[0]), abs(zr[1] - zr[0]))
        centre = ((xr[0] + xr[1]) * 0.5, (yr[0] + yr[1]) * 0.5, (zr[0] + zr[1]) * 0.5)
        return self.add(bm_box(size, bev, seg), mat, loc=centre, rot=rot)

    def cbox(self, mat, centre, size, bev=0.004, seg=1, rot=(0.0, 0.0, 0.0)):
        return self.add(bm_box(size, bev, seg), mat, loc=centre, rot=rot)

    def cyl(self, mat, p0, p1, r, n=12, bev=0.0, r2=None, caps=(True, True)):
        """Cylinder from point p0 to point p1."""
        m, length = align_z(p0, p1)
        return self.add(bm_cyl(r, length, n, r2, bev, caps), mat, matrix=m)

    def pipe(self, mat, pts, r, n=8, fillet=0.0, fseg=3, caps=(True, True), r_end=None):
        return self.add(bm_pipe(pts, r, n, fillet, fseg, caps, r_end), mat)

    def lathe(self, mat, profile, n=16, loc=(0.0, 0.0, 0.0), rot=(0.0, 0.0, 0.0), bev=0.0):
        return self.add(bm_lathe(profile, n, bev), mat, loc=loc, rot=rot)

    def prism(self, mat, pts, axis, a0, a1, bev=0.004, seg=1, min_angle=35.0):
        """Extrudes a 2D polygon along an axis from a0 to a1. The points are (x, z) for axis 'y',
        (y, z) for axis 'x' and (x, y) for axis 'z'."""
        def to3(p, a):
            if axis == 'y':
                return (p[0], a, p[1])
            if axis == 'x':
                return (a, p[0], p[1])
            return (p[0], p[1], a)
        return self.add(bm_prism([to3(p, a0) for p in pts], [to3(p, a1) for p in pts], bev, seg, min_angle), mat)

    def ring(self, mat, profile, hx, hy, loc=(0.0, 0.0, 0.0), rot=(0.0, 0.0, 0.0), bev=0.0):
        return self.add(bm_ring(profile, hx, hy, bev), mat, loc=loc, rot=rot)

    def torus(self, mat, loc, big_r, small_r, rot=(0.0, 0.0, 0.0), n_big=16, n_small=6):
        return self.add(bm_torus(big_r, small_r, n_big, n_small), mat, loc=loc, rot=rot)

    def bolt(self, mat, pos, axis='z', r=0.011, h=0.006, n=6):
        """Hex bolt head sitting on pos, pointing along axis ('z', '-y', 'x', ...)."""
        d = {'x': (1, 0, 0), '-x': (-1, 0, 0), 'y': (0, 1, 0), '-y': (0, -1, 0), 'z': (0, 0, 1), '-z': (0, 0, -1)}[axis]
        p0 = Vector(pos)
        return self.cyl(mat, p0, p0 + Vector(d) * h, r, n=n, caps=(False, True))

    def slats(self, mat, xr, y, zr, count, depth=0.014, thick=0.004, angle=35.0, back=None, back_y=None):
        """Louvre on a front (-Y) face: count angled slats across xr, stacked over zr. back is the
        material of a dark plate behind them."""
        if back is not None:
            by = y + 0.002 if back_y is None else back_y
            self.box(back, xr, (by - 0.0015, by), zr, bev=0.0)
        step = (zr[1] - zr[0]) / count
        for i in range(count):
            z = zr[0] + step * (i + 0.5)
            self.cbox(mat, ((xr[0] + xr[1]) * 0.5, y - depth * 0.25, z), (abs(xr[1] - xr[0]), depth, thick),
                      bev=0.0, rot=(angle, 0.0, 0.0))

    def label(self, xr, y, zr, lines=3, mat='label', ink='label_ink', frame=None):
        """Small inset-looking plate on a front (-Y) face with a few dark text bars."""
        if frame is not None:
            self.box(frame, (xr[0] - 0.006, xr[1] + 0.006), (y - 0.002, y), (zr[0] - 0.006, zr[1] + 0.006), bev=0.0)
        self.box(mat, xr, (y - 0.003, y), zr, bev=0.0)
        w, h = xr[1] - xr[0], zr[1] - zr[0]
        for i in range(lines):
            z = zr[1] - h * (i + 0.75) / (lines + 0.5)
            ln = w * (0.8 if i == 0 else self.rng.uniform(0.35, 0.75))
            bh = h * (0.16 if i == 0 else 0.09)
            self.box(ink, (xr[0] + w * 0.1, xr[0] + w * 0.1 + ln), (y - 0.0038, y - 0.003), (z - bh, z), bev=0.0)

    def handle(self, x, y, z, w=0.16, h=0.07, rim='gunmetal', bar='steel', pocket='pit'):
        """Recessed-looking grab handle on a front (-Y) face at height z: dark pocket, raised rim, grip bar."""
        self.box(pocket, (x - w * 0.5, x + w * 0.5), (y - 0.002, y), (z - h * 0.5, z + h * 0.5), bev=0.0)
        t = 0.012
        self.box(rim, (x - w * 0.5 - t, x + w * 0.5 + t), (y - 0.009, y), (z + h * 0.5, z + h * 0.5 + t), bev=0.002)
        self.box(rim, (x - w * 0.5 - t, x + w * 0.5 + t), (y - 0.009, y), (z - h * 0.5 - t, z - h * 0.5), bev=0.002)
        self.box(rim, (x - w * 0.5 - t, x - w * 0.5), (y - 0.009, y), (z - h * 0.5, z + h * 0.5), bev=0.002)
        self.box(rim, (x + w * 0.5, x + w * 0.5 + t), (y - 0.009, y), (z - h * 0.5, z + h * 0.5), bev=0.002)
        self.pipe(bar, [(x - w * 0.5, y - 0.006, z + h * 0.12), (x + w * 0.5, y - 0.006, z + h * 0.12)], 0.006, n=6,
                  caps=(False, False))

    def decal(self, mat, xr, y, zr):
        """Single quad on a front (-Y) face, a hair in front of it: slots, holes, screen content."""
        bm = new_bm()
        vs = [bm.verts.new(p) for p in ((xr[0], y, zr[0]), (xr[1], y, zr[0]), (xr[1], y, zr[1]), (xr[0], y, zr[1]))]
        bm.faces.new(vs)
        bm.normal_update()
        return self.add(bm, mat)

    def pull(self, x, y, z, length=0.14, mat='steel', vertical=False):
        """Bar handle standing off a front (-Y) face."""
        half = length * 0.5
        d = (0.0, 0.0, 1.0) if vertical else (1.0, 0.0, 0.0)
        ends = [(x - d[0] * half, y - 0.024, z - d[2] * half), (x + d[0] * half, y - 0.024, z + d[2] * half)]
        self.pipe(mat, ends, 0.006, n=6)
        for s in (-0.78, 0.78):
            px, pz = x + d[0] * half * s, z + d[2] * half * s
            self.pipe(mat, [(px, y, pz), (px, y - 0.024, pz)], 0.0045, n=6, caps=(False, False))

    def screen(self, xr, y, zr, bezel=0.014, depth=0.03, housing='plastic_black', ui=True, bev=0.006):
        """Display facing -Y: housing behind y, the lit area is a Glow quad, a few dark bars give it a layout."""
        if housing is not None:
            self.box(housing, (xr[0] - bezel, xr[1] + bezel), (y, y + depth), (zr[0] - bezel, zr[1] + bezel), bev=bev)
        self.decal('Glow', xr, y - 0.001, zr)
        if ui:
            w, h = xr[1] - xr[0], zr[1] - zr[0]
            head = zr[1] - h * 0.17
            split = xr[0] + w * 0.66
            t = max(0.0025, h * 0.02)
            self.decal('screen_off', xr, y - 0.0018, (head - t, head))
            self.decal('screen_off', (split - t, split), y - 0.0018, (zr[0], head))
            for i in range(3):
                z = zr[0] + h * (0.14 + 0.2 * i)
                self.decal('screen_off', (split + w * 0.04, xr[1] - w * (0.04 + 0.07 * i)), y - 0.0018, (z, z + t * 2.2))
            self.decal('screen_off', (xr[0] + w * 0.05, split - w * 0.06), y - 0.0018, (zr[0] + h * 0.1, zr[0] + h * 0.1 + t))

    def fit(self, size):
        """Scales everything built so far so that its bounds get exactly this size (for free shapes like
        rocks). The bottom stays on z = 0, X and Y are centred."""
        lo = Vector((1e9, 1e9, 1e9))
        hi = Vector((-1e9, -1e9, -1e9))
        for ob in self.parts:
            for v in ob.data.vertices:
                for i in range(3):
                    lo[i] = min(lo[i], v.co[i])
                    hi[i] = max(hi[i], v.co[i])
        scale = Matrix.Diagonal((size[0] / (hi.x - lo.x), size[1] / (hi.y - lo.y), size[2] / (hi.z - lo.z), 1.0))
        move = Matrix.Translation((-(lo.x + hi.x) * 0.5, -(lo.y + hi.y) * 0.5, -lo.z))
        for ob in self.parts:
            ob.data.transform(scale @ move)

    # -- finishing -------------------------------------------------------------------------------
    def join(self, sharp_angle=50.0, recentre=True):
        """Joins all parts into one object, fixes the pivot and the normals."""
        obs = self.parts
        if len(obs) > 1:
            with bpy.context.temp_override(active_object=obs[0], object=obs[0], selected_objects=obs,
                                           selected_editable_objects=obs):
                bpy.ops.object.join()
        ob = obs[0]
        ob.name = self.name
        me = ob.data
        me.name = self.name
        for other in list(bpy.data.meshes):
            if other.users == 0:
                bpy.data.meshes.remove(other)
        if recentre:
            xs = [v.co.x for v in me.vertices]
            ys = [v.co.y for v in me.vertices]
            zs = [v.co.z for v in me.vertices]
            shift = Vector((-(min(xs) + max(xs)) * 0.5, -(min(ys) + max(ys)) * 0.5, -min(zs)))
            me.transform(Matrix.Translation(shift))
        me.shade_smooth()
        me.set_sharp_from_angle(angle=math.radians(sharp_angle))
        mod = ob.modifiers.new("HCWeighted", 'WEIGHTED_NORMAL')
        mod.keep_sharp = True
        mod.weight = 50
        mod.mode = 'FACE_AREA'
        with bpy.context.temp_override(active_object=ob, object=ob, selected_objects=[ob]):
            bpy.ops.object.modifier_apply(modifier=mod.name)
        me.update()
        self.parts = [ob]
        return ob


def fbm(x, y, z=0.0, octaves=3):
    """Deterministic smooth noise in about -1..1."""
    total, amp, freq = 0.0, 1.0, 1.0
    for _ in range(octaves):
        total += amp * noise.noise(Vector((x * freq, y * freq, z * freq)))
        amp *= 0.5
        freq *= 2.0
    return total
