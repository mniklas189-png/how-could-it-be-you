"""Furniture: bunk, locker, beds, benches, tables, desk, shelf."""
import math

import hc_geo
from hc_spec import model


# ----------------------------------------------------------------------------------------------------
@model('Bunk', (205, 95, 190), tris=8000, glow=(1.0, 0.76, 0.48), glow_strength=6)
def bunk(b):
    frame, panel = 'paint_dark', 'paint_mid'
    px, py = 1.0, 0.45
    for sx in (-1, 1):
        for sy in (-1, 1):
            b.box(frame, (sx * px - 0.025, sx * px + 0.025), (sy * py - 0.025, sy * py + 0.025), (0.0, 1.90), bev=0.005)

    def bed(z, seed, blanket_from):
        for sy in (-1, 1):
            b.box(frame, (-0.976, 0.976), (sy * py - 0.015, sy * py + 0.015), (z - 0.07, z + 0.05), bev=0.004)
        for sx in (-1, 1):
            b.box(frame, (sx * px - 0.015, sx * px + 0.015), (-0.426, 0.426), (z - 0.07, z + 0.05), bev=0.004)
        b.box(panel, (-0.985, 0.985), (-0.435, 0.435), (z - 0.035, z), bev=0.0)
        b.box('mattress', (-0.965, 0.965), (-0.415, 0.415), (z, z + 0.11), bev=0.03, seg=2)
        top = z + 0.11
        b.cbox('pillow', (-0.73, 0.02 if seed else -0.03, top + 0.04), (0.36, 0.56, 0.11), bev=0.045, seg=3,
               rot=(0, 3, 7 if seed else -5))
        # rumpled blanket over the foot half, hanging down the sides, with a rolled hem towards the pillow
        length = 0.955 - blanket_from

        def height(u, v):
            edge = min(v, 1.0 - v)
            z_ = 0.008 + 0.034 * abs(hc_geo.fbm(u * 3.1 + seed * 7.3, v * 2.3 + seed * 3.1)) \
                + 0.014 * abs(hc_geo.fbm(u * 7.0 + seed, v * 5.0 + 1.7))
            if edge < 0.07:
                z_ -= (0.07 - edge) / 0.07 * 0.085
            if u > 0.95:
                z_ -= (u - 0.95) / 0.05 * 0.08
            return z_
        b.add(hc_geo.bm_grid(14, 10, length, 0.90, height, skirt=0.10), 'blanket',
              loc=(blanket_from + length * 0.5, 0.0, top))
        b.cbox('blanket', (blanket_from + 0.02, 0.0, top + 0.022), (0.11, 0.86, 0.045), bev=0.02, seg=2,
               rot=(0, 0, 2.5 if seed else -2))

    bed(0.32, 0, -0.22)
    bed(1.34, 1, 0.02)

    # head and foot boards
    b.box(panel, (-1.006, -0.994), (-0.425, 0.425), (0.37, 0.74), bev=0.0)
    b.box(panel, (-1.006, -0.994), (-0.425, 0.425), (1.39, 1.64), bev=0.0)
    b.box(panel, (0.994, 1.006), (-0.425, 0.425), (0.37, 0.60), bev=0.0)
    # guard rails of the upper bunk; the foot end stays open between the ladder stiles
    r = 0.014
    b.pipe(frame, [(-0.976, py, 1.66), (0.976, py, 1.66)], r, caps=(False, False))
    b.pipe(frame, [(-0.976, -py, 1.66), (0.30, -py, 1.66), (0.30, -py, 1.39)], r, fillet=0.06, caps=(False, False))
    b.pipe(frame, [(-px, -0.426, 1.66), (-px, 0.426, 1.66)], r, caps=(False, False))
    for x, y in ((-0.34, py), (0.32, py), (-0.34, -py)):
        b.pipe(frame, [(x, y, 1.39), (x, y, 1.66)], 0.010, n=6, caps=(False, False))
    for sy in (-1, 1):
        b.pipe(frame, [(px, sy * 0.426, 1.66), (px, sy * 0.21, 1.66)], r, caps=(False, False))
    # ladder on the foot end
    for sy in (-1, 1):
        b.pipe(frame, [(1.011, sy * 0.21, 0.0), (1.011, sy * 0.21, 1.70)], 0.014, n=8)
    for z in (0.26, 0.54, 0.82, 1.10, 1.38):
        b.box('gunmetal', (0.996, 1.025), (-0.21, 0.21), (z - 0.009, z + 0.009), bev=0.003)
    # reading lamp under the upper bunk
    b.box('plastic_black', (-0.84, -0.40), (0.385, 0.435), (1.246, 1.272), bev=0.005)
    b.box('Glow', (-0.82, -0.42), (0.395, 0.425), (1.2415, 1.2465), bev=0.0)
    # drawer unit under the lower bunk
    b.box('pit_rib', (-0.90, 0.90), (-0.37, 0.41), (0.0, 0.045), bev=0.0)
    b.box(panel, (-0.93, 0.93), (-0.40, 0.43), (0.04, 0.25), bev=0.005)
    for sx in (-1, 1):
        b.box(frame, (sx * 0.015, sx * 0.915), (-0.416, -0.40), (0.056, 0.236), bev=0.004)
        b.handle(sx * 0.465, -0.416, 0.15, w=0.20, h=0.045)
    b.label((0.74, 0.86), -0.416, (0.17, 0.215), lines=2)


# ----------------------------------------------------------------------------------------------------
@model('Locker', (100, 50, 200), tris=8000)
def locker(b):
    body, door_mat = 'paint_dark', 'paint_white'
    front = -0.21
    for sx in (-1, 1):
        for sy in (-0.15, 0.20):
            b.box('gunmetal', (sx * 0.47 - 0.028, sx * 0.47 + 0.028), (sy - 0.028, sy + 0.028), (0.0, 0.09), bev=0.005)
    b.box(body, (-0.5, 0.5), (front, 0.25), (0.08, 1.88), bev=0.006)
    b.prism(body, [(front, 1.874), (0.25, 1.874), (0.25, 2.0), (front, 1.90)], 'x', -0.5, 0.5, bev=0.006)
    b.decal('pit', (-0.485, 0.485), front - 0.0008, (0.10, 1.86))

    def door(x0, x1, hinge_at_x0):
        cx = (x0 + x1) * 0.5
        b.box(door_mat, (x0, x1), (-0.02, 0.0), (0.105, 1.855), bev=0.004)
        # pressed field in the middle, louvres top and bottom
        b.box(door_mat, (x0 + 0.05, x1 - 0.05), (-0.024, -0.018), (0.50, 1.28), bev=0.003)
        for zr in ((1.50, 1.70), (0.22, 0.40)):
            b.slats(door_mat, (cx - 0.15, cx + 0.15), -0.02, zr, 6, depth=0.012, back='pit')
        b.label((cx - 0.05, cx + 0.05), -0.02, (1.36, 1.42), lines=1, frame='gunmetal')
        hx = (x1 - 0.11) if hinge_at_x0 else (x0 + 0.11)
        b.handle(hx, -0.024, 0.98, w=0.07, h=0.11)
        b.cyl('steel', (hx, -0.024, 0.87), (hx, -0.032, 0.87), 0.012, n=8)
        b.decal('label_ink', (hx - 0.002, hx + 0.002), -0.0325, (0.863, 0.877))
        edge = x0 if hinge_at_x0 else x1
        for z in (0.30, 0.98, 1.66):
            b.cyl('gunmetal', (edge, -0.014, z - 0.045), (edge, -0.014, z + 0.045), 0.009, n=8)

    with b.frame(loc=(-0.492, front, 0.0)):
        door(0.0, 0.488, True)
    with b.frame(loc=(0.492, front, 0.0), rot=(0, 0, 3.2)):      # this one stands slightly ajar
        door(-0.488, 0.0, False)
    # vents high on the sides
    for turn in (90, -90):
        with b.frame(rot=(0, 0, turn)):
            b.slats(body, (-0.12, 0.12), -0.5, (1.60, 1.74), 5, depth=0.012, back='pit')


# ----------------------------------------------------------------------------------------------------
@model('MedBed', (200, 80, 95), tris=8000, glow=(0.50, 1.0, 0.72), glow_strength=8)
def med_bed(b):
    white, pad, metal = 'paint_white', 'vinyl_teal', 'gunmetal'
    # pedestal with stepped covers, foot pedal on the front
    b.box('paint_dark', (-0.55, 0.55), (-0.27, 0.27), (0.0, 0.05), bev=0.012)
    b.box(white, (-0.30, 0.30), (-0.17, 0.17), (0.05, 0.30), bev=0.010)
    b.box(white, (-0.27, 0.27), (-0.15, 0.15), (0.29, 0.42), bev=0.008)
    b.box(white, (-0.24, 0.24), (-0.13, 0.13), (0.41, 0.50), bev=0.008)
    b.box(metal, (0.06, 0.09), (-0.37, -0.16), (0.07, 0.09), bev=0.004, rot=(-6, 0, 0))
    b.box('rubber', (0.01, 0.14), (-0.40, -0.32), (0.075, 0.098), bev=0.006, rot=(-6, 0, 0))
    b.label((-0.2, -0.06), -0.17, (0.14, 0.20), lines=2)
    # frame, control box under the foot end
    b.box(metal, (-0.97, 0.97), (-0.35, 0.35), (0.50, 0.556), bev=0.008)
    b.box(white, (0.46, 0.80), (-0.20, 0.20), (0.40, 0.50), bev=0.008)
    b.pipe('rubber', [(0.46, 0.0, 0.44), (0.36, 0.0, 0.40), (0.30, 0.0, 0.33)], 0.008, n=6, fillet=0.03)
    # mattress: two flat pads, the head pad raised
    b.box(pad, (-0.33, 0.30), (-0.32, 0.32), (0.556, 0.64), bev=0.022, seg=2)
    b.box(pad, (0.312, 0.96), (-0.32, 0.32), (0.556, 0.64), bev=0.022, seg=2)
    with b.frame(loc=(-0.345, 0.0, 0.566), rot=(0, 24, 0)):
        b.box(metal, (-0.60, -0.01), (-0.30, 0.30), (-0.022, 0.0), bev=0.004)
        b.box(pad, (-0.61, 0.0), (-0.32, 0.32), (0.0, 0.085), bev=0.022, seg=2)
        b.box('plastic_black', (-0.56, -0.36), (-0.15, 0.15), (0.075, 0.118), bev=0.018, seg=2)
    b.cyl('steel', (-0.80, 0.0, 0.535), (-0.66, 0.0, 0.69), 0.013, n=8)
    b.cyl(metal, (-0.88, 0.0, 0.50), (-0.79, 0.0, 0.545), 0.02, n=8)
    # side rails, push handle at the foot end
    for sy in (-1, 1):
        y = sy * 0.37
        b.pipe('steel', [(-0.15, y, 0.53), (-0.15, y, 0.80), (0.62, y, 0.80), (0.62, y, 0.53)], 0.012, n=8, fillet=0.06)
        b.pipe('steel', [(-0.15, y, 0.68), (0.62, y, 0.68)], 0.008, n=6, caps=(False, False))
        for x in (0.11, 0.36):
            b.pipe('steel', [(x, y, 0.68), (x, y, 0.80)], 0.008, n=6, caps=(False, False))
    b.pipe('steel', [(0.95, -0.26, 0.535), (0.988, -0.26, 0.535), (0.988, -0.26, 0.72), (0.988, 0.26, 0.72),
                     (0.988, 0.26, 0.535), (0.95, 0.26, 0.535)], 0.012, n=8, fillet=0.045)
    # hand control hanging on the front rail
    b.box('plastic_grey', (0.22, 0.28), (-0.397, -0.378), (0.56, 0.67), bev=0.005)
    for i in range(3):
        b.decal('paint_orange' if i == 0 else 'label_ink', (0.235, 0.265), -0.3975, (0.635 - i * 0.025, 0.65 - i * 0.025))
    # folded arm with the diagnostic screen at the head end, back side
    b.box(metal, (-0.64, -0.56), (0.35, 0.40), (0.50, 0.556), bev=0.004)
    b.cyl(metal, (-0.60, 0.385, 0.55), (-0.60, 0.385, 0.74), 0.014, n=8)
    b.box(metal, (-0.61, -0.35), (0.375, 0.397), (0.735, 0.758), bev=0.004)
    b.cyl('plastic_black', (-0.36, 0.386, 0.728), (-0.36, 0.386, 0.80), 0.02, n=10, bev=0.003)
    b.box(metal, (-0.66, -0.35), (0.375, 0.397), (0.772, 0.795), bev=0.004)
    b.box(metal, (-0.68, -0.64), (0.36, 0.397), (0.775, 0.86), bev=0.004)
    b.screen((-0.785, -0.535), 0.336, (0.775, 0.925), depth=0.03)


# ----------------------------------------------------------------------------------------------------
@model('LabBench', (180, 75, 150), tris=8000, glow=(0.22, 0.92, 1.0), glow_strength=8)
def lab_bench(b):
    carc, top = 'paint_white', 'plastic_black'
    y0 = -0.345
    b.box('paint_dark', (-0.87, 0.87), (-0.30, 0.345), (0.0, 0.105), bev=0.0)
    b.box(carc, (-0.88, 0.88), (y0, 0.355), (0.10, 0.862), bev=0.005)
    b.box(top, (-0.90, 0.90), (-0.375, 0.375), (0.86, 0.90), bev=0.006)

    def front(x0, x1, z0, z1, pull_vertical=False, pull_x=None):
        b.box(carc, (x0, x1), (y0 - 0.016, y0), (z0, z1), bev=0.004)
        if pull_vertical:
            b.pull(pull_x, y0 - 0.016, z1 - 0.16, 0.16, vertical=True)
        else:
            b.pull((x0 + x1) * 0.5, y0 - 0.016, z1 - 0.045, 0.16)

    for z0, z1 in ((0.12, 0.33), (0.34, 0.58), (0.59, 0.845)):
        front(-0.87, -0.325, z0, z1)
    front(-0.315, -0.005, 0.12, 0.845, True, -0.045)
    front(0.005, 0.315, 0.12, 0.845, True, 0.045)
    for z0, z1 in ((0.12, 0.47), (0.48, 0.665), (0.675, 0.845)):
        front(0.325, 0.87, z0, z1)
    b.label((-0.80, -0.70), y0 - 0.016, (0.775, 0.815), lines=1)
    b.label((0.69, 0.80), y0 - 0.016, (0.60, 0.64), lines=2)
    # backsplash with a service strip, shelf with a retaining rail
    b.box(carc, (-0.90, 0.90), (0.335, 0.375), (0.90, 1.14), bev=0.005)
    b.box('plastic_grey', (-0.55, 0.55), (0.322, 0.336), (0.97, 1.06), bev=0.004)
    for x in (-0.42, -0.22, 0.22, 0.42):
        b.decal('pit', (x - 0.022, x + 0.022), 0.3212, (0.99, 1.04))
    b.cyl('steel', (0.0, 0.335, 1.015), (0.0, 0.28, 1.015), 0.012, n=8)
    b.cyl('paint_yellow', (0.0, 0.29, 1.015), (0.0, 0.29, 1.06), 0.008, n=6)
    for sx in (-1, 1):
        b.box('paint_dark', (sx * 0.87 - 0.02, sx * 0.87 + 0.02), (0.335, 0.375), (1.14, 1.30), bev=0.004)
    b.box(carc, (-0.90, 0.90), (0.16, 0.375), (1.30, 1.335), bev=0.005)
    b.pipe('steel', [(-0.88, 0.175, 1.335), (-0.88, 0.175, 1.385), (0.88, 0.175, 1.385), (0.88, 0.175, 1.335)], 0.007, n=6,
           fillet=0.02)
    b.pipe('steel', [(0.0, 0.175, 1.335), (0.0, 0.175, 1.385)], 0.006, n=6, caps=(False, False))
    # bottles and a box on the shelf
    bottle = [(0.0, 0.0), (0.034, 0.0), (0.034, 0.10), (0.014, 0.128), (0.014, 0.145), (0.019, 0.145), (0.019, 0.162),
              (0.0, 0.162)]
    for x, mat, s in ((-0.70, 'vial', 1.0), (-0.60, 'plastic_white', 0.85), (-0.50, 'vial', 0.92), (0.30, 'plastic_white', 1.0)):
        b.lathe(mat, [(r * s, z * s) for r, z in bottle], n=10, loc=(x, 0.27, 1.335))
    b.box('plastic_grey', (0.50, 0.74), (0.20, 0.34), (1.335, 1.43), bev=0.006)
    b.label((0.54, 0.70), 0.20, (1.36, 1.405), lines=2)
    # microscope
    with b.frame(loc=(-0.52, -0.04, 0.90)):
        dark = 'paint_dark'
        b.box(dark, (-0.09, 0.09), (-0.12, 0.11), (0.0, 0.035), bev=0.008)
        b.box(dark, (-0.03, 0.03), (0.035, 0.10), (0.03, 0.30), bev=0.008, rot=(-5, 0, 0))
        b.box('gunmetal', (-0.07, 0.07), (-0.085, 0.045), (0.128, 0.14), bev=0.002)
        b.cyl('plastic_black', (0.0, -0.02, 0.035), (0.0, -0.02, 0.062), 0.026, n=8)
        b.cyl('steel', (0.0, -0.02, 0.158), (0.0, -0.02, 0.215), 0.015, n=8)
        b.cyl('gunmetal', (0.0, -0.02, 0.215), (0.0, -0.02, 0.238), 0.034, n=10)
        b.box(dark, (-0.04, 0.04), (-0.055, 0.09), (0.238, 0.305), bev=0.010)
        b.cyl('plastic_black', (0.0, -0.035, 0.295), (0.0, -0.105, 0.385), 0.018, n=8)
        b.cyl('rubber', (0.0, -0.10, 0.378), (0.0, -0.115, 0.398), 0.022, n=8)
        for sx in (-1, 1):
            b.cyl('plastic_black', (sx * 0.028, 0.068, 0.10), (sx * 0.056, 0.068, 0.10), 0.02, n=10)
    # rack of sample tubes
    with b.frame(loc=(-0.13, -0.16, 0.90), rot=(0, 0, -6)):
        b.box('plastic_white', (-0.115, 0.115), (-0.035, 0.035), (0.0, 0.012), bev=0.002)
        b.box('plastic_white', (-0.115, 0.115), (-0.035, 0.035), (0.068, 0.078), bev=0.002)
        for sx in (-1, 1):
            b.box('plastic_white', (sx * 0.103, sx * 0.115), (-0.035, 0.035), (0.01, 0.07), bev=0.0)
        caps = ('paint_orange', 'plastic_black', 'paint_blue', 'paint_orange', 'plastic_black', 'paint_orange')
        for i in range(6):
            x = -0.08 + i * 0.032
            b.cyl('vial', (x, 0.0, 0.012), (x, 0.0, 0.112), 0.0095, n=8, caps=(False, False))
            b.cyl(caps[i], (x, 0.0, 0.11), (x, 0.0, 0.126), 0.0115, n=8)
    # two flasks
    b.lathe('vial', [(0.0, 0.0), (0.043, 0.0), (0.046, 0.008), (0.016, 0.10), (0.014, 0.14), (0.019, 0.146), (0.0, 0.146)],
            n=12, loc=(0.13, -0.10, 0.90))
    b.lathe('vial', [(0.0, 0.0), (0.04, 0.0), (0.04, 0.105), (0.044, 0.11), (0.036, 0.11), (0.036, 0.012), (0.0, 0.012)],
            n=12, loc=(0.25, -0.20, 0.90))
    # analyser with screen
    with b.frame(loc=(0.56, 0.03, 0.90)):
        for sx in (-1, 1):
            for sy in (-1, 1):
                b.box('rubber', (sx * 0.15 - 0.02, sx * 0.15 + 0.02), (sy * 0.12 - 0.02, sy * 0.12 + 0.02), (0.0, 0.016), bev=0.0)
        b.box(carc, (-0.19, 0.19), (-0.15, 0.17), (0.012, 0.27), bev=0.012)
        b.box('plastic_black', (-0.172, 0.172), (-0.154, -0.15), (0.035, 0.25), bev=0.0)
        b.screen((-0.15, 0.03), -0.154, (0.125, 0.23), housing=None)
        b.box('gunmetal', (-0.15, 0.03), (-0.158, -0.154), (0.05, 0.10), bev=0.002)
        b.pull(-0.06, -0.158, 0.075, 0.07)
        colours = ('plastic_grey', 'plastic_grey', 'paint_orange', 'plastic_grey', 'plastic_white', 'plastic_grey')
        for i in range(6):
            x = 0.07 + (i % 2) * 0.045
            z = 0.20 - (i // 2) * 0.04
            b.box(colours[i], (x - 0.016, x + 0.016), (-0.160, -0.154), (z - 0.012, z + 0.012), bev=0.002)
        b.cyl('plastic_black', (0.092, -0.154, 0.075), (0.092, -0.172, 0.075), 0.022, n=10, bev=0.003)
        with b.frame(rot=(0, 0, 90)):
            b.slats(carc, (-0.08, 0.10), -0.19, (0.08, 0.20), 5, depth=0.011, back='pit')
        b.pipe('rubber', [(-0.10, 0.10, 0.27), (-0.10, 0.10, 0.31), (-0.10, 0.20, 0.31), (-0.10, 0.275, 0.25)], 0.008, n=6,
               fillet=0.03)


# ----------------------------------------------------------------------------------------------------
@model('Shelf', (200, 60, 220), tris=12000)
def shelf(b):
    post, beam, deck = 'paint_blue', 'paint_orange', 'steel'
    levels = (0.16, 0.82, 1.48, 2.14)
    # uprights with slots, base plates
    for sx in (-1, 1):
        for sy in (-1, 1):
            b.box(post, (sx * 0.97 - 0.03, sx * 0.97 + 0.03), (sy * 0.27 - 0.03, sy * 0.27 + 0.03), (0.0, 2.20), bev=0.004)
            b.box('gunmetal', (sx * 0.95 - 0.05, sx * 0.95 + 0.05), (sy * 0.25 - 0.05, sy * 0.25 + 0.05), (0.0, 0.008), bev=0.0)
            b.bolt('steel', (sx * 0.915, sy * 0.215, 0.008), 'z', r=0.009, h=0.006)
    for i in range(21):
        z = 0.10 + i * 0.10
        if any(abs(z - lv + 0.035) < 0.06 for lv in levels):
            continue
        for sx in (-1, 1):
            b.decal('pit', (sx * 0.97 - 0.009, sx * 0.97 + 0.009), -0.3008, (z - 0.022, z + 0.022))
        for turn in (90, -90):
            with b.frame(rot=(0, 0, turn)):
                for sy in (-1, 1):
                    b.decal('pit', (sy * 0.27 - 0.009, sy * 0.27 + 0.009), -1.0008, (z - 0.022, z + 0.022))
    # beams and decks
    for lv in levels:
        for sy in (-1, 1):
            b.box(beam, (-0.94, 0.94), (sy * 0.27 - 0.02, sy * 0.27 + 0.02), (lv - 0.085, lv + 0.008), bev=0.004)
            for sx in (-1, 1):
                b.box(beam, (sx * 0.94, sx * 0.905), (sy * 0.30, sy * 0.306), (lv - 0.11, lv + 0.03), bev=0.0)
                b.bolt('steel', (sx * 0.922, sy * 0.306, lv - 0.02), 'y' if sy > 0 else '-y', r=0.007, h=0.004)
        b.box(deck, (-0.94, 0.94), (-0.25, 0.25), (lv - 0.02, lv), bev=0.0)
    # bracing: a cross at the back, struts and diagonals on the sides
    span = math.hypot(1.88, 1.70)
    ang = math.degrees(math.atan2(1.70, 1.88))
    for sign in (-1, 1):
        b.cbox('gunmetal', (0.0, 0.2965, 1.10), (span, 0.005, 0.04), bev=0.0, rot=(0, sign * ang, 0))
    b.bolt('steel', (0.0, 0.294, 1.10), '-y', r=0.012, h=0.005)
    side = math.hypot(0.48, 0.56)
    side_ang = math.degrees(math.atan2(0.56, 0.48))
    for sx in (-1, 1):
        for z in (0.48, 1.15, 1.82):
            b.box(post, (sx * 0.97 - 0.003, sx * 0.97 + 0.003), (-0.24, 0.24), (z - 0.016, z + 0.016), bev=0.0)
        for z, sign in ((0.815, 1), (1.485, -1)):
            b.cbox(post, (sx * 0.97, 0.0, z), (0.005, side, 0.03), bev=0.0, rot=(sign * side_ang, 0, 0))

    # --- the load ---
    def crate(x, y, z, w, d, h, mat, lid, turn=0.0):
        with b.frame(loc=(x, y, z), rot=(0, 0, turn)):
            b.box(mat, (-w / 2, w / 2), (-d / 2, d / 2), (0.0, h * 0.78), bev=0.006)
            b.box(lid, (-w / 2 - 0.006, w / 2 + 0.006), (-d / 2 - 0.006, d / 2 + 0.006), (h * 0.76, h), bev=0.008)
            for sx in (-0.3, 0.3):
                b.prism(mat, [(sx * w - 0.02, -d / 2 + 0.001), (sx * w + 0.02, -d / 2 + 0.001),
                              (sx * w + 0.012, -d / 2 - 0.009), (sx * w - 0.012, -d / 2 - 0.009)], 'z', 0.02, h * 0.74, bev=0.0)
                b.box('steel', (sx * w - 0.018, sx * w + 0.018), (-d / 2 - 0.016, -d / 2), (h * 0.70, h * 0.86), bev=0.003)
            b.label((-w * 0.14, w * 0.14), -d / 2, (h * 0.25, h * 0.52), lines=3)

    def bin_(x, y, z, w, d, h, mat, turn=0.0):
        with b.frame(loc=(x, y, z), rot=(0, 0, turn)):
            b.prism(mat, [(-w / 2 + 0.03, 0.0), (w / 2 - 0.03, 0.0), (w / 2, h), (-w / 2, h)], 'y', -d / 2, d / 2, bev=0.006)
            b.ring(mat, [(0.0, h - 0.03), (0.014, h - 0.03), (0.014, h + 0.006), (0.0, h + 0.006)], w / 2, d / 2, bev=0.003)
            b.box('pit_rib', (-w / 2 + 0.01, w / 2 - 0.01), (-d / 2 + 0.01, d / 2 - 0.01), (h - 0.01, h + 0.001), bev=0.0)
            b.label((-0.06, 0.06), -d / 2 - 0.0145, (h - 0.028, h + 0.002), lines=1)

    def jerrycan(x, y, z, mat, turn=0.0):
        with b.frame(loc=(x, y, z), rot=(0, 0, turn)):
            b.box(mat, (-0.075, 0.075), (-0.17, 0.17), (0.0, 0.40), bev=0.022, seg=2)
            b.box(mat, (-0.079, 0.079), (-0.12, 0.12), (0.09, 0.30), bev=0.012)
            b.cyl('plastic_black', (0.0, -0.11, 0.39), (0.0, -0.13, 0.445), 0.026, n=8)
            b.pipe(mat, [(0.0, -0.05, 0.395), (0.0, -0.03, 0.44), (0.0, 0.11, 0.44), (0.0, 0.13, 0.395)], 0.012, n=6, fillet=0.02)

    def drum(x, y, z, r, h, mat):
        prof = [(0.0, 0.0), (r, 0.0)]
        for f in (0.33, 0.66):
            prof += [(r, h * f - 0.018), (r + 0.007, h * f), (r, h * f + 0.018)]
        prof += [(r, h), (r - 0.012, h), (r - 0.012, h - 0.012), (0.0, h - 0.012)]
        b.lathe(mat, prof, n=14, loc=(x, y, z))
        b.cyl('steel', (x + r * 0.5, y, z + h - 0.012), (x + r * 0.5, y, z + h + 0.004), 0.022, n=8)

    def canister(x, y, z, mat):
        b.lathe(mat, [(0.0, 0.0), (0.072, 0.0), (0.075, 0.01), (0.075, 0.30), (0.05, 0.345), (0.022, 0.355), (0.022, 0.385),
                      (0.0, 0.385)], n=10, loc=(x, y, z))
        b.cyl('plastic_black', (x, y, z + 0.38), (x, y, z + 0.405), 0.026, n=8)
        b.pipe('steel', [(x - 0.05, y, z + 0.33), (x - 0.05, y, z + 0.43), (x + 0.05, y, z + 0.43), (x + 0.05, y, z + 0.33)],
               0.006, n=6, fillet=0.025)

    def tarp(x, y, z, w, d, turn=0.0):
        with b.frame(loc=(x, y, z), rot=(0, 0, turn)):
            zz = 0.0
            for i, (dw, dd, t) in enumerate(((0.0, 0.0, 0.07), (-0.03, 0.015, 0.06), (0.02, -0.02, 0.055))):
                b.cbox('tarp', (dw, dd, zz + t / 2), (w - i * 0.02, d - i * 0.015, t + 0.008), bev=0.022, seg=2,
                       rot=(0, 0, (i - 1) * 2.0))
                zz += t
            for sx in (-0.25, 0.25):
                # strap around the bundle: a ring stood on its side
                with b.frame(loc=(sx * w, 0.0, zz / 2), rot=(0, 90, 0)):
                    b.ring('rubber', [(0.0, -0.02), (0.004, -0.02), (0.004, 0.02), (0.0, 0.02)], zz / 2 + 0.003, d / 2 + 0.003)

    def case(x, y, z, w, d, h, turn=0.0):
        with b.frame(loc=(x, y, z), rot=(0, 0, turn)):
            b.box('plastic_black', (-w / 2, w / 2), (-d / 2, d / 2), (0.0, h), bev=0.014, seg=2)
            b.box('pit_rib', (-w / 2 - 0.001, w / 2 + 0.001), (-d / 2 - 0.001, d / 2 + 0.001), (h * 0.55, h * 0.55 + 0.006),
                  bev=0.0)
            for sx in (-0.3, 0.3):
                b.box('steel', (sx * w - 0.02, sx * w + 0.02), (-d / 2 - 0.008, -d / 2), (h * 0.35, h * 0.8), bev=0.002)
            b.pull(0.0, -d / 2, h * 0.5, 0.12, mat='plastic_grey')

    l0, l1, l2, l3 = levels
    crate(-0.52, 0.0, l0, 0.72, 0.46, 0.44, 'paint_mid', 'paint_dark')
    drum(0.17, 0.02, l0, 0.20, 0.52, 'paint_teal')
    jerrycan(0.56, 0.02, l0, 'paint_yellow', 4.0)
    jerrycan(0.76, 0.03, l0, 'paint_dark', -3.0)
    bin_(-0.60, 0.0, l1, 0.52, 0.42, 0.26, 'plastic_grey')
    crate(-0.03, 0.01, l1, 0.46, 0.40, 0.30, 'paint_white', 'paint_white', 5.0)
    for i, mat in enumerate(('paint_red', 'steel', 'steel')):
        canister(0.42 + i * 0.19, 0.03 - (i % 2) * 0.05, l1, mat)
    tarp(-0.50, 0.0, l2, 0.62, 0.44, -3.0)
    case(0.36, 0.0, l2, 0.74, 0.40, 0.15, 3.0)
    crate(0.42, 0.02, l2 + 0.15, 0.36, 0.28, 0.22, 'paint_mid', 'paint_orange', -8.0)
    b.torus('rubber', (0.40, 0.0, l3 + 0.02), 0.17, 0.02, n_big=18, n_small=6)
    b.torus('rubber', (0.405, 0.005, l3 + 0.042), 0.16, 0.018, n_big=18, n_small=6)


# ----------------------------------------------------------------------------------------------------
@model('MessTable', (180, 160, 76), tris=8000)
def mess_table(b):
    frame, seat = 'paint_dark', 'paint_teal'
    top = 0.705
    for sx in (-1, 1):
        x = sx * 0.62
        b.box(frame, (x - 0.03, x + 0.03), (-0.78, 0.78), (0.008, 0.058), bev=0.006)
        for y in (-0.72, 0.0, 0.72):
            b.box('rubber', (x - 0.035, x + 0.035), (y - 0.05, y + 0.05), (0.0, 0.012), bev=0.003)
        for sy in (-1, 1):
            b.box(frame, (x - 0.025, x + 0.025), (sy * 0.22 - 0.025, sy * 0.22 + 0.025), (0.055, top - 0.04), bev=0.005)
            b.box(frame, (x - 0.025, x + 0.025), (sy * 0.65 - 0.025, sy * 0.65 + 0.025), (0.055, 0.41), bev=0.005)
            b.box(frame, (x - 0.025, x + 0.025), (sy * 0.52, sy * 0.78), (0.375, 0.412), bev=0.005)
        b.box(frame, (x - 0.025, x + 0.025), (-0.36, 0.36), (top - 0.075, top - 0.038), bev=0.005)
    b.box(frame, (-0.62, 0.62), (-0.02, 0.02), (top - 0.11, top - 0.05), bev=0.004)
    b.box(frame, (-0.62, 0.62), (-0.025, 0.025), (0.012, 0.052), bev=0.004)
    for sy in (-1, 1):
        b.box(frame, (-0.62, 0.62), (sy * 0.65 - 0.02, sy * 0.65 + 0.02), (0.34, 0.378), bev=0.004)
        # bench
        b.box(seat, (-0.90, 0.90), (sy * 0.50, sy * 0.80), (0.41, 0.45), bev=0.010, seg=2)
        for x in (-0.62, 0.62):
            for dy in (0.58, 0.72):
                b.bolt('steel', (x, sy * dy, 0.4495), 'z', r=0.008, h=0.003)
    # table top with a steel edge
    b.box('laminate', (-0.896, 0.896), (-0.396, 0.396), (top - 0.04, top), bev=0.004)
    b.ring('steel', [(0.0, top - 0.038), (0.004, top - 0.038), (0.004, top - 0.002), (0.0, top - 0.002)], 0.896, 0.396)
    # tray with a plate, cutlery and a mug; a second mug and a caddy
    with b.frame(loc=(-0.32, -0.12, top), rot=(0, 0, 7)):
        b.box('paint_orange', (-0.20, 0.20), (-0.14, 0.14), (0.0, 0.006), bev=0.002)
        b.ring('paint_orange', [(-0.012, 0.004), (0.0, 0.004), (0.006, 0.02), (-0.002, 0.02)], 0.20, 0.14)
        b.lathe('ceramic', [(0.0, 0.006), (0.07, 0.006), (0.10, 0.02), (0.096, 0.024), (0.066, 0.012), (0.0, 0.012)], n=14,
                loc=(-0.07, 0.0, 0.0))
        b.box('steel', (0.06, 0.072), (-0.09, 0.07), (0.006, 0.009), bev=0.0, rot=(0, 0, 4))
        b.box('steel', (0.085, 0.095), (-0.08, 0.08), (0.006, 0.009), bev=0.0, rot=(0, 0, -3))
        mug(b, (0.135, 0.07, 0.006), 30)
    mug(b, (0.36, 0.14, top), 200)
    with b.frame(loc=(0.05, 0.02, top)):
        b.box('gunmetal', (-0.07, 0.07), (-0.04, 0.04), (0.0, 0.035), bev=0.004)
        b.cyl('ceramic', (-0.035, 0.0, 0.01), (-0.035, 0.0, 0.07), 0.018, n=8)
        b.cyl('steel', (-0.035, 0.0, 0.07), (-0.035, 0.0, 0.08), 0.019, n=8)
        b.cyl('plastic_black', (0.035, 0.0, 0.01), (0.035, 0.0, 0.07), 0.018, n=8)
        b.cyl('steel', (0.035, 0.0, 0.07), (0.035, 0.0, 0.08), 0.019, n=8)


def mug(b, loc, turn):
    with b.frame(loc=loc, rot=(0, 0, turn)):
        b.lathe('ceramic', [(0.0, 0.0), (0.034, 0.0), (0.038, 0.084), (0.033, 0.084), (0.030, 0.008), (0.0, 0.008)], n=10)
        b.pipe('ceramic', [(0.034, 0.0, 0.066), (0.062, 0.0, 0.066), (0.062, 0.0, 0.022), (0.034, 0.0, 0.022)], 0.005, n=5,
               fillet=0.012, caps=(False, False))


# ----------------------------------------------------------------------------------------------------
@model('Desk', (160, 75, 125), tris=8000, glow=(0.16, 0.72, 0.60), glow_strength=4)
def desk(b):
    side, shell, work = 'paint_dark', 'paint_white', 'plastic_black'
    for sx in (-1, 1):
        b.prism(side, [(-0.33, 0.0), (0.375, 0.0), (0.375, 0.90), (0.21, 0.90), (-0.375, 0.745), (-0.375, 0.66), (-0.33, 0.60)],
                'x', sx * 0.76, sx * 0.80, bev=0.006)
    b.box(work, (-0.76, 0.76), (-0.375, -0.02), (0.70, 0.735), bev=0.006)
    slope = math.degrees(math.atan2(0.135, 0.24))
    b.prism(shell, [(-0.04, 0.72), (0.20, 0.855), (0.375, 0.855), (0.375, 0.60), (-0.04, 0.60)], 'x', -0.76, 0.76, bev=0.006)
    b.box('paint_mid', (-0.76, 0.76), (0.33, 0.372), (0.05, 0.60), bev=0.0)
    # drawer pedestal on the right, foot rail on the left
    b.box('paint_mid', (0.36, 0.76), (-0.33, 0.33), (0.06, 0.70), bev=0.005)
    for z0, z1 in ((0.08, 0.25), (0.26, 0.43), (0.44, 0.66)):
        b.box(shell, (0.375, 0.745), (-0.345, -0.33), (z0, z1), bev=0.004)
        b.pull(0.56, -0.345, z1 - 0.04, 0.14)
    b.pipe('steel', [(-0.76, -0.12, 0.12), (0.36, -0.12, 0.12)], 0.015, n=8, caps=(False, False))
    # keyboard and jog dial on the work surface
    b.box(work, (-0.47, -0.05), (-0.31, -0.15), (0.735, 0.748), bev=0.003)
    for row in range(4):
        for col in range(12):
            x = -0.436 + col * 0.032
            y = -0.288 + row * 0.034
            b.box('plastic_grey', (x - 0.013, x + 0.013), (y - 0.013, y + 0.013), (0.747, 0.755), bev=0.0)
    b.cyl('gunmetal', (0.07, -0.22, 0.735), (0.07, -0.22, 0.752), 0.04, n=12, bev=0.003)
    b.cyl('plastic_black', (0.07, -0.22, 0.752), (0.07, -0.22, 0.765), 0.028, n=12, bev=0.003)
    b.label((0.16, 0.30), -0.02, (0.705, 0.73), lines=1)
    # the slanted panel: everything here is placed in the panel's own plane (y up the slope, z out of it)
    with b.frame(loc=(0.0, -0.04, 0.72), rot=(slope, 0, 0)):
        b.box(work, (-0.70, -0.32), (0.035, 0.245), (0.0, 0.004), bev=0.0)
        tones = ('plastic_grey', 'plastic_grey', 'paint_orange', 'plastic_grey', 'plastic_white', 'plastic_grey',
                 'paint_yellow', 'plastic_grey', 'plastic_grey', 'paint_red', 'plastic_grey', 'plastic_white')
        for i in range(12):
            x = -0.645 + (i % 4) * 0.09
            y = 0.075 + (i // 4) * 0.065
            b.box(tones[(i * 5) % 12], (x - 0.03, x + 0.03), (y - 0.02, y + 0.02), (0.003, 0.012), bev=0.003)
        b.box(work, (-0.26, 0.24), (0.035, 0.245), (0.0, 0.004), bev=0.0)
        for i in range(3):
            x = -0.19 + i * 0.10
            b.cyl('plastic_black', (x, 0.19, 0.003), (x, 0.19, 0.026), 0.022, n=10, bev=0.003)
            b.box('plastic_white', (x - 0.002, x + 0.002), (0.19, 0.212), (0.026, 0.0275), bev=0.0)
        for i in range(4):
            x = -0.21 + i * 0.065
            b.box('pit', (x - 0.004, x + 0.004), (0.055, 0.145), (0.0035, 0.0045), bev=0.0)
            b.box('plastic_grey', (x - 0.012, x + 0.012), (0.075 + i * 0.015, 0.095 + i * 0.015), (0.004, 0.016), bev=0.002)
        for i in range(5):
            x = 0.09 + (i % 3) * 0.045
            y = 0.20 - (i // 3) * 0.045
            b.box('Glow', (x - 0.008, x + 0.008), (y - 0.008, y + 0.008), (0.003, 0.0075), bev=0.0)
        for i in range(3):
            x = 0.09 + i * 0.045
            b.cyl('steel', (x, 0.085, 0.003), (x, 0.075, 0.03), 0.005, n=6)
        b.box(work, (0.32, 0.70), (0.035, 0.245), (0.0, 0.004), bev=0.0)
        b.box('plastic_black', (0.35, 0.57), (0.07, 0.21), (0.003, 0.012), bev=0.003)
        with b.frame(rot=(-90, 0, 0)):
            # small display lying in the panel: local -Y here is the panel's outward direction
            b.screen((0.365, 0.555), -0.012, (0.085, 0.195), housing=None)
        for i in range(4):
            y = 0.085 + i * 0.04
            b.box('plastic_grey' if i else 'paint_orange', (0.62, 0.665), (y - 0.013, y + 0.013), (0.003, 0.012), bev=0.003)
    # two monitors on posts, angled towards the seat
    for sx in (-1, 1):
        x = sx * 0.31
        b.cyl('gunmetal', (x, 0.29, 0.855), (x, 0.29, 0.868), 0.06, n=12, bev=0.003)
        b.cyl('gunmetal', (x, 0.29, 0.86), (x, 0.29, 1.09), 0.018, n=10)
        b.box('gunmetal', (x - 0.03, x + 0.03), (0.225, 0.305), (1.05, 1.10), bev=0.006)
        with b.frame(loc=(x, 0.205, 1.075), rot=(-6, 0, -8 * sx)):
            b.screen((-0.255, 0.255), -0.02, (-0.148, 0.148), bezel=0.016, depth=0.04, bev=0.008)
        b.pipe('rubber', [(x + 0.06, 0.225, 1.02), (x + 0.07, 0.30, 0.97), (x + 0.07, 0.345, 0.90)], 0.006, n=6, fillet=0.03)
    # cable trunk along the back of the ledge
    b.box('plastic_grey', (-0.62, 0.62), (0.322, 0.375), (0.855, 0.905), bev=0.005)
    for x in (-0.5, 0.0, 0.5):
        b.decal('pit_rib', (x - 0.002, x + 0.002), 0.3212, (0.858, 0.902))


# ----------------------------------------------------------------------------------------------------
@model('Bench', (180, 55, 85), tris=8000)
def bench(b):
    frame, cloth, pan = 'paint_dark', 'fabric_blue', 'paint_mid'
    for sx in (-1, 1):
        x = sx * 0.70
        b.box(frame, (x - 0.028, x + 0.028), (-0.25, 0.265), (0.008, 0.045), bev=0.006)
        for y in (-0.21, 0.225):
            b.box('rubber', (x - 0.032, x + 0.032), (y - 0.04, y + 0.04), (0.0, 0.012), bev=0.003)
        b.box(frame, (x - 0.022, x + 0.022), (-0.03, 0.03), (0.04, 0.33), bev=0.005)
        b.box(frame, (x - 0.022, x + 0.022), (0.175, 0.21), (0.33, 0.84), bev=0.005, rot=(-9, 0, 0))
        b.prism(frame, [(-0.16, 0.045), (0.16, 0.045), (0.03, 0.17), (-0.03, 0.17)], 'x', x - 0.004, x + 0.004, bev=0.0)
    b.box(frame, (-0.86, 0.86), (-0.035, 0.035), (0.31, 0.372), bev=0.005)
    b.box(frame, (-0.86, 0.86), (0.13, 0.17), (0.335, 0.372), bev=0.004)
    b.box(pan, (-0.88, 0.88), (-0.262, 0.18), (0.37, 0.392), bev=0.004)
    for i in range(3):
        x0 = -0.876 + i * 0.586
        b.box(cloth, (x0 + 0.003, x0 + 0.583), (-0.275, 0.165), (0.39, 0.472), bev=0.028, seg=3)
    with b.frame(loc=(0.0, 0.155, 0.47), rot=(-10, 0, 0)):
        b.box(pan, (-0.88, 0.88), (0.045, 0.06), (0.0, 0.385), bev=0.004)
        for i in range(3):
            x0 = -0.876 + i * 0.586
            b.box(cloth, (x0 + 0.003, x0 + 0.583), (-0.022, 0.047), (0.012, 0.38), bev=0.024, seg=3)
    # armrests at both ends
    for sx in (-1, 1):
        x = sx * 0.886
        b.pipe('steel', [(x, -0.22, 0.375), (x, -0.22, 0.615), (x, 0.11, 0.615), (x, 0.11, 0.375)], 0.014, n=8, fillet=0.05)
        b.box('plastic_black', (x - 0.022, x + 0.022), (-0.19, 0.08), (0.618, 0.642), bev=0.008, seg=2)
