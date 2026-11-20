"""Small props: vent hatch, crate, chair, barrier, heater, ice."""
from mathutils import Vector

import hc_geo
from hc_spec import model


# ----------------------------------------------------------------------------------------------------
@model('Crate', (100, 80, 80), tex=1024, tris=3000)
def crate(b):
    body, trim = 'paint_mid', 'gunmetal'
    # runners, body, the dark gap under the lid, lid
    for x in (-0.36, 0.0, 0.36):
        b.box('paint_dark', (x - 0.06, x + 0.06), (-0.375, 0.375), (0.0, 0.035), bev=0.006)
    b.box(body, (-0.485, 0.485), (-0.385, 0.385), (0.03, 0.575), bev=0.008)
    b.box('pit_rib', (-0.475, 0.475), (-0.375, 0.375), (0.57, 0.60), bev=0.0)
    b.box(body, (-0.49, 0.49), (-0.39, 0.39), (0.595, 0.755), bev=0.010)
    # hazard band around the lid edge
    b.ring('hazard:xyz:0.11', [(0.0, 0.612), (0.003, 0.612), (0.003, 0.668), (0.0, 0.668)], 0.49, 0.39)
    # corner castings with lifting slots, edge posts between them
    for sx in (-1, 1):
        for sy in (-1, 1):
            for z0 in (0.0, 0.71):
                b.box(trim, (sx * 0.41, sx * 0.50), (sy * 0.31, sy * 0.40), (z0, z0 + 0.09), bev=0.008)
                b.cbox('pit', (sx * 0.5005, sy * 0.355, z0 + 0.045), (0.002, 0.04, 0.022), bev=0.0)
                b.cbox('pit', (sx * 0.455, sy * 0.4005, z0 + 0.045), (0.04, 0.002, 0.022), bev=0.0)
            b.cbox('pit', (sx * 0.455, sy * 0.355, 0.8005), (0.04, 0.022, 0.002), bev=0.0)
            b.box(trim, (sx * 0.445, sx * 0.495), (sy * 0.345, sy * 0.395), (0.09, 0.575), bev=0.006)
    # pressed ribs on the four sides
    def rib(x, y0, z0=0.07, z1=0.545):
        b.prism(body, [(x - 0.032, y0 + 0.002), (x + 0.032, y0 + 0.002), (x + 0.018, y0 - 0.013), (x - 0.018, y0 - 0.013)],
                'z', z0, z1, bev=0.0)
    for x in (-0.31, -0.19, 0.19, 0.31):
        rib(x, -0.385)
    with b.frame(rot=(0, 0, 180)):
        for x in (-0.3, -0.15, 0.0, 0.15, 0.3):
            rib(x, -0.385)
    for turn in (90, -90):
        with b.frame(rot=(0, 0, turn)):
            for x in (-0.22, 0.22):
                rib(x, -0.485)
            b.handle(0.0, -0.485, 0.36, w=0.20, h=0.08)
    # stacking ribs on the lid
    for y in (-0.21, 0.0, 0.21):
        b.box(body, (-0.36, 0.36), (y - 0.035, y + 0.035), (0.75, 0.768), bev=0.005)
    # latches across the seam on the front, hinges at the back
    for x in (-0.33, 0.33):
        b.box('steel', (x - 0.03, x + 0.03), (-0.397, -0.385), (0.50, 0.565), bev=0.003)
        b.box('steel', (x - 0.03, x + 0.03), (-0.402, -0.39), (0.605, 0.66), bev=0.003)
        b.box(trim, (x - 0.018, x + 0.018), (-0.412, -0.397), (0.515, 0.64), bev=0.004, rot=(-4, 0, 0))
        b.cyl(trim, (x - 0.026, -0.404, 0.522), (x + 0.026, -0.404, 0.522), 0.008, n=8)
        b.cyl(trim, (x - 0.05, 0.394, 0.586), (x + 0.05, 0.394, 0.586), 0.012, n=8)
        b.box(trim, (x - 0.04, x + 0.04), (0.385, 0.392), (0.53, 0.64), bev=0.0)
    # label plate
    b.label((-0.11, 0.11), -0.385, (0.27, 0.42), lines=4, frame=trim)


# ----------------------------------------------------------------------------------------------------
VENT_TOP = 0.047          # top of the frame; the grille lies flush with it
VENT_OPENING = 0.4425     # half width of the opening the grille sits in


@model('VentFrame', (100, 100, 5), tex=512, tris=3000,
       note="Seat of VentGrille: put its pivot at (0, +44.0, 3.2) cm in this model's Blender axes "
            "(Unreal: Y mirrored); the grille top is then flush with the frame top.")
def vent_frame(b):
    top, half = VENT_TOP, VENT_OPENING
    # frame: ramp on the outside, flat top, straight wall into the opening
    b.ring('floor_steel', [(0.0, 0.0), (0.0575, 0.0), (0.0575, 0.004), (0.031, top), (0.0, top)], half, half, bev=0.0015)
    # the lip the grille rests on, hazard-striped on top
    b.ring('floor_steel', [(-0.0425, 0.0), (0.0, 0.0), (0.0, 0.0145), (-0.0425, 0.0145)], half, half)
    b.ring('hazard:xy:0.07', [(-0.041, 0.0145), (-0.0005, 0.0145), (-0.0005, 0.016), (-0.041, 0.016)], half, half)
    # the pit: black floor a few millimetres down, dim duct ribs across it
    b.box('pit', (-0.401, 0.401), (-0.401, 0.401), (0.0, 0.005), bev=0.0)
    for y in (-0.27, -0.09, 0.09, 0.27):
        b.box('pit_rib', (-0.40, 0.40), (y - 0.012, y + 0.012), (0.004, 0.008), bev=0.0)
    # bolt heads in the corners and at mid length, hinge pads at the back
    d = half + 0.016
    for x, y in ((-d, -d), (d, -d), (-d, d), (d, d), (0.0, -d), (-d, 0.0), (d, 0.0)):
        b.bolt('steel', (x, y, top - 0.0005), 'z', r=0.009, h=0.0035)
    for x in (-0.26, 0.26):
        b.box('floor_metal', (x - 0.05, x + 0.05), (half + 0.002, half + 0.028), (top - 0.001, top + 0.0025), bev=0.001)
        for dx in (-0.032, 0.032):
            b.bolt('steel', (x + dx, half + 0.015, top + 0.002), 'z', r=0.005, h=0.001)


@model('VentGrille', (88, 88, 3), tex=512, tris=3000, pivot='hinge', ground=False,
       note="Pivot on the hinge axis: back edge (+Y in Blender), mid height. Turn around X to swing it open.")
def vent_grille(b):
    t = 0.015
    rim = 'floor_metal'
    # rim: side bars, front bar with the grip pocket, back bar with the hinge knuckles
    for sx in (-1, 1):
        b.box(rim, (sx * 0.405, sx * 0.44), (-0.88, 0.0), (-t, t), bev=0.003)
        b.box(rim, (sx * 0.075, sx * 0.405), (-0.88, -0.845), (-t, t), bev=0.003)
        b.box(rim, (sx * 0.31, sx * 0.405), (-0.035, 0.0), (-t, t), bev=0.003)
        b.cyl('steel', (sx * 0.213, -0.0165, 0.0), (sx * 0.307, -0.0165, 0.0), 0.0145, n=10, bev=0.002)
        b.box(rim, (sx * 0.225, sx * 0.295), (-0.036, -0.030), (-t, 0.006), bev=0.0)
    b.box(rim, (-0.21, 0.21), (-0.035, 0.0), (-t, t), bev=0.003)
    b.box('paint_yellow', (-0.076, 0.076), (-0.88, -0.845), (-t, -0.006), bev=0.0)
    b.pipe('steel', [(-0.076, -0.8625, 0.006), (0.076, -0.8625, 0.006)], 0.0055, n=6, caps=(False, False))
    # louvre slats, two carrier bars underneath
    for i in range(18):
        y = -0.845 + 0.045 * (i + 0.5)
        b.cbox('floor_steel', (0.0, y, 0.0), (0.812, 0.040, 0.005), bev=0.0015, rot=(32, 0, 0))
    for x in (-0.14, 0.14):
        b.box(rim, (x - 0.008, x + 0.008), (-0.846, -0.034), (-t, -0.006), bev=0.0)


# ----------------------------------------------------------------------------------------------------
@model('Chair', (60, 62, 110), tex=512, tris=3000)
def chair(b):
    frame, shell, cloth = 'gunmetal', 'plastic_black', 'fabric_blue'
    # five-star base
    for i in range(5):
        with b.frame(rot=(0, 0, 72 * i)):
            b.add(hc_geo.bm_prism([(-0.028, 0.03, 0.062), (0.028, 0.03, 0.062), (0.028, 0.03, 0.108), (-0.028, 0.03, 0.108)],
                                  [(-0.017, 0.285, 0.034), (0.017, 0.285, 0.034), (0.017, 0.285, 0.058), (-0.017, 0.285, 0.058)],
                                  bev=0.005), frame)
            b.cyl('rubber', (0.0, 0.268, 0.0), (0.0, 0.268, 0.036), 0.021, n=8)
    b.cyl(frame, (0, 0, 0.055), (0, 0, 0.125), 0.052, n=12, bev=0.006)
    # gas spring and seat mechanism
    b.cyl(shell, (0, 0, 0.12), (0, 0, 0.28), 0.029, n=10, bev=0.003)
    b.cyl('steel', (0, 0, 0.27), (0, 0, 0.41), 0.017, n=10)
    b.box(frame, (-0.085, 0.085), (-0.10, 0.12), (0.40, 0.447), bev=0.006)
    b.pipe(frame, [(0.07, -0.03, 0.42), (0.20, -0.05, 0.415), (0.235, -0.055, 0.40)], 0.006, n=6)
    b.cyl(shell, (0.225, -0.053, 0.404), (0.262, -0.058, 0.388), 0.012, n=8)
    # seat
    b.box(shell, (-0.235, 0.235), (-0.275, 0.20), (0.445, 0.475), bev=0.012)
    b.box(cloth, (-0.245, 0.245), (-0.30, 0.19), (0.468, 0.548), bev=0.028, seg=3)
    # backrest: leaning back 8 degrees, a spine bar holds it
    b.box(frame, (-0.03, 0.03), (0.165, 0.185), (0.42, 0.80), bev=0.004, rot=(-10, 0, 0))
    with b.frame(loc=(0.0, 0.18, 0.56), rot=(-8, 0, 0)):
        b.box(shell, (-0.215, 0.215), (0.028, 0.05), (0.0, 0.535), bev=0.010)
        b.box(cloth, (-0.225, 0.225), (-0.032, 0.034), (0.005, 0.30), bev=0.024, seg=3)
        b.box(cloth, (-0.19, 0.19), (-0.028, 0.034), (0.315, 0.54), bev=0.024, seg=3)
    # armrests
    for sx in (-1, 1):
        b.pipe(frame, [(sx * 0.08, 0.03, 0.425), (sx * 0.277, 0.03, 0.425), (sx * 0.277, 0.03, 0.705)], 0.013, n=8,
               fillet=0.05)
        b.box('rubber', (sx * 0.2545, sx * 0.2995), (-0.13, 0.15), (0.70, 0.732), bev=0.009, seg=2)


# ----------------------------------------------------------------------------------------------------
@model('Barrier', (150, 45, 100), tex=512, tris=3000, glow=(1.0, 0.42, 0.04), glow_strength=14)
def barrier(b):
    for sx in (-1, 1):
        x = sx * 0.62
        b.box('paint_dark', (x - 0.045, x + 0.045), (-0.225, 0.225), (0.012, 0.045), bev=0.006)
        for sy in (-1, 1):
            b.box('rubber', (x - 0.05, x + 0.05), (sy * 0.13, sy * 0.225), (0.0, 0.016), bev=0.004)
        b.box('paint_dark', (x - 0.02, x + 0.02), (-0.02, 0.02), (0.04, 0.90), bev=0.004)
        b.prism('paint_dark', [(-0.13, 0.045), (0.13, 0.045), (0.02, 0.17), (-0.02, 0.17)], 'x', x - 0.004, x + 0.004,
                bev=0.0)
        b.box('paint_yellow', (x - 0.025, x + 0.025), (-0.025, 0.025), (0.895, 0.912), bev=0.004)
        for z in (0.68, 0.80, 0.36):
            b.bolt('steel', (x, -0.036 if z > 0.5 else -0.02, z), '-y', r=0.009, h=0.005)
    # striped board in front of the posts, a plain rail below
    b.box('hazard:xz:0.19', (-0.75, 0.75), (-0.036, -0.02), (0.62, 0.86), bev=0.004)
    b.pipe('paint_yellow', [(-0.62, 0.0, 0.36), (0.62, 0.0, 0.36)], 0.017, n=8, caps=(False, False))
    # warning lamp on the left post
    b.box('plastic_black', (-0.665, -0.575), (-0.04, 0.04), (0.91, 0.94), bev=0.006)
    b.lathe('Glow', [(0.036, 0.0), (0.036, 0.03), (0.027, 0.052), (0.0, 0.06)], n=10, loc=(-0.62, 0.0, 0.94))


# ----------------------------------------------------------------------------------------------------
@model('Heater', (60, 40, 70), tex=512, tris=3000, glow=(1.0, 0.26, 0.03), glow_strength=18)
def heater(b):
    body = 'paint_mid'
    for sx in (-1, 1):
        b.box('rubber', (sx * 0.16, sx * 0.27), (-0.185, 0.185), (0.0, 0.05), bev=0.012)
    b.box(body, (-0.29, 0.29), (-0.05, 0.20), (0.05, 0.60), bev=0.014, seg=2)
    # frame around the heating opening (built lying down, then stood up)
    with b.frame(loc=(0.0, -0.05, 0.325), rot=(90, 0, 0)):
        b.ring(body, [(0.0, 0.0), (0.06, 0.0), (0.06, 0.15), (0.0, 0.15)], 0.23, 0.215, bev=0.008)
    b.box('steel', (-0.231, 0.231), (-0.056, -0.049), (0.109, 0.541), bev=0.0)
    for sx in (-1, 1):
        b.box('ceramic', (sx * 0.19, sx * 0.229), (-0.115, -0.056), (0.15, 0.50), bev=0.004)
    for z in (0.20, 0.285, 0.37, 0.455):
        b.cyl('Glow', (-0.192, -0.088, z), (0.192, -0.088, z), 0.011, n=8, caps=(False, False))
    # cage
    for i in range(11):
        x = -0.20 + i * 0.04
        b.box('gunmetal', (x - 0.0025, x + 0.0025), (-0.188, -0.183), (0.11, 0.54), bev=0.0)
    for z in (0.20, 0.325, 0.45):
        b.box('gunmetal', (-0.23, 0.23), (-0.194, -0.188), (z - 0.003, z + 0.003), bev=0.0)
    # knobs and switch on the top front, carrying handle
    for x in (0.12, 0.20):
        b.cyl('plastic_black', (x, -0.125, 0.60), (x, -0.125, 0.618), 0.021, n=10, bev=0.003)
        b.box('plastic_white', (x - 0.002, x + 0.002), (-0.144, -0.125), (0.618, 0.6195), bev=0.0)
    b.box('plastic_black', (-0.23, -0.15), (-0.15, -0.10), (0.599, 0.603), bev=0.0)
    b.box('paint_red', (-0.21, -0.17), (-0.14, -0.11), (0.60, 0.612), bev=0.003, rot=(8, 0, 0))
    b.pipe('gunmetal', [(-0.17, 0.08, 0.59), (-0.17, 0.08, 0.684), (0.17, 0.08, 0.684), (0.17, 0.08, 0.59)], 0.011, n=8,
           fillet=0.035)
    b.cyl('rubber', (-0.085, 0.08, 0.684), (0.085, 0.08, 0.684), 0.0155, n=8)
    # side vents, cable stub, rating plate
    for turn in (90, -90):
        with b.frame(rot=(0, 0, turn)):
            b.slats(body, (-0.03, 0.15), -0.29, (0.20, 0.46), 7, depth=0.013, back='pit')
    b.cyl('rubber', (0.285, 0.15, 0.11), (0.299, 0.15, 0.11), 0.012, n=8)
    b.pipe('rubber', [(0.288, 0.15, 0.11), (0.293, 0.15, 0.06), (0.293, 0.14, 0.012), (0.293, 0.02, 0.008),
                      (0.290, -0.10, 0.008)], 0.007, n=6, fillet=0.03)
    b.label((-0.21, -0.07), -0.20, (0.066, 0.098), lines=2)


# ----------------------------------------------------------------------------------------------------
@model('IceChunk', (160, 110, 120), tex=1024, tris=3000, sharp=24.0)
def ice_chunk(b):
    rng = b.rng
    blobs = [((-0.12, 0.08, 0.36), (0.50, 0.42, 0.80), 34),
             ((0.36, -0.08, 0.22), (0.46, 0.42, 0.52), 30),
             ((-0.50, -0.12, 0.16), (0.34, 0.38, 0.42), 26),
             ((0.10, 0.30, 0.18), (0.50, 0.28, 0.46), 26),
             ((0.66, 0.22, 0.04), (0.17, 0.16, 0.17), 14),
             ((-0.24, -0.42, 0.04), (0.23, 0.14, 0.18), 14)]
    for centre, radii, count in blobs:
        pts = []
        for _ in range(count):
            d = Vector((rng.gauss(0, 1), rng.gauss(0, 1), rng.gauss(0, 1))).normalized()
            f = rng.uniform(0.86, 1.0)
            pts.append((centre[0] + d.x * radii[0] * f, centre[1] + d.y * radii[1] * f, centre[2] + d.z * radii[2] * f))
        b.add(hc_geo.bm_hull(pts, bev=0.014, cut_z=0.0), 'ice')
    b.fit((1.60, 1.10, 1.20))
