# Draws the slime the Wandler leaves when it changes shape (Tools/Setup/Effects/slime_N.png and slime_N_n.png). Run with
# a normal Python that has numpy and Pillow:  python Tools/Setup/MakeSlime.py   - then Tools/Setup/SetupEffects.py brings
# them into the project. Like the blood (MakeBlood.py) the pictures are drawn, not photographed, but slime is thick: it
# lies in round lobes, creeps out in short fat tongues with a bulb at the end, drips in blobs that hang on to the pool
# by thin strings, and carries bubbles. Next to the colour picture each one gets a normal map, so the lamps of the
# station shine on the lumps and the bubbles.
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

SIZE = 1024
SUPER = 2  # shapes are drawn at twice the size, then scaled down
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Effects")


def smooth(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def noise(rng, cells, size):
    # Smooth random values between 0 and 1
    grid = (rng.random((cells, cells)) * 255).astype(np.uint8)
    return np.asarray(Image.fromarray(grid).resize((size, size), Image.BICUBIC), dtype=np.float32) / 255.0


def fbm(rng, size, cells=4, octaves=5):
    total, weight, amount = np.zeros((size, size), np.float32), 0.0, 1.0
    for octave in range(octaves):
        total += amount * noise(rng, cells * 2 ** octave, size)
        weight += amount
        amount *= 0.5
    return total / weight


def sample(picture, x, y):
    # Bilinear lookup at pixel positions (wraps around)
    h, w = picture.shape
    x0, y0 = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64)
    fx, fy = x - x0, y - y0
    x0, y0 = x0 % w, y0 % h
    x1, y1 = (x0 + 1) % w, (y0 + 1) % h
    top = picture[y0, x0] * (1 - fx) + picture[y0, x1] * fx
    bottom = picture[y1, x0] * (1 - fx) + picture[y1, x1] * fx
    return top * (1 - fy) + bottom * fy


def blur(picture, radius):
    # Gaussian blur of a float picture (Pillow only blurs 8 bit ones), done in the frequency domain
    h, w = picture.shape
    fy, fx = np.fft.fftfreq(h)[:, None], np.fft.rfftfreq(w)[None, :]
    falloff = np.exp(-2.0 * math.pi ** 2 * radius ** 2 * (fx * fx + fy * fy))
    return np.fft.irfft2(np.fft.rfft2(picture) * falloff, s=(h, w)).astype(np.float32)


def outline(rng, base, count=720):
    # Radius of the pool around its centre: a circle bent by a few slow waves (slime has no sharp edge)
    waves = [(k, rng.uniform(0.4, 1.0) * 0.3 / k ** 0.7, rng.uniform(0, math.tau)) for k in range(1, 8)]
    angles = np.linspace(0, math.tau, count, endpoint=False)
    radius = np.full(count, base)
    for k, amount, phase in waves:
        radius = radius * (1.0 + amount * np.cos(k * angles + phase) * 0.5)
    return angles, radius


def blob(draw, x, y, long, wide, angle):
    points = []
    for i in range(28):
        a = math.tau * i / 28
        px, py = math.cos(a) * long, math.sin(a) * wide
        points.append((x + px * math.cos(angle) - py * math.sin(angle), y + px * math.sin(angle) + py * math.cos(angle)))
    draw.polygon(points, fill=255)


def make(seed, pool, lobes, tongues, blobs, strings, one_sided):
    rng = np.random.default_rng(seed)
    n = SIZE * SUPER
    centre = n / 2
    body = Image.new("L", (n, n), 0)  # thick parts: merged with a wide blur
    fine = Image.new("L", (n, n), 0)  # strings and specks: only a little blur, or they would melt away
    draw, draw_fine = ImageDraw.Draw(body), ImageDraw.Draw(fine)
    main = rng.uniform(0, math.tau)  # where it mostly ran

    def direction(spread=0.7):
        return rng.normal(main, spread) if one_sided and rng.random() < 0.75 else rng.uniform(0, math.tau)

    if pool > 0:
        angles, radius = outline(rng, n * pool)
        draw.polygon([(centre + math.cos(a) * r, centre + math.sin(a) * r) for a, r in zip(angles, radius)], fill=255)
    else:
        angles, radius = np.linspace(0, math.tau, 720, endpoint=False), np.full(720, n * 0.03)

    def edge(a):
        return radius[int((a % math.tau) / math.tau * len(radius)) % len(radius)]

    # Bulges on the rim
    for _ in range(lobes):
        a = rng.uniform(0, math.tau)
        d = edge(a) * rng.uniform(0.75, 1.0)
        r = n * rng.uniform(0.025, 0.06)
        blob(draw, centre + math.cos(a) * d, centre + math.sin(a) * d, r * rng.uniform(1.0, 1.4), r, a)
    # Tongues that crept out: fat, slowly getting thinner, with a round bulb at the end
    for _ in range(tongues):
        a = direction()
        start, length = edge(a) * 0.7, n * rng.uniform(0.05, 0.14)
        width = n * rng.uniform(0.014, 0.026)
        bend = rng.uniform(-0.3, 0.3)
        steps = 80
        for i in range(steps + 1):
            t = i / steps
            d = start + length * t
            aa = a + bend * t * t * length / (start + length)
            w = width * (1.0 - 0.45 * t) * (1.0 + 0.12 * math.sin(t * 9.0 + seed))
            x, y = centre + math.cos(aa) * d, centre + math.sin(aa) * d
            draw.ellipse((x - w, y - w, x + w, y + w), fill=255)
        d = start + length
        aa = a + bend * length / (start + length)
        r = width * rng.uniform(0.85, 1.25)
        blob(draw, centre + math.cos(aa) * d, centre + math.sin(aa) * d, r * 1.25, r, aa)
    # Loose blobs, some still hanging on to the pool by a string
    for i in range(blobs):
        a = direction(1.0)
        far = rng.random() ** 1.3
        d = edge(a) + n * (0.03 + 0.2 * far)
        if d > n * 0.45:
            continue
        r = n * rng.uniform(0.006, 0.024) * (1.0 - 0.5 * far)
        x, y = centre + math.cos(a) * d, centre + math.sin(a) * d
        blob(draw, x, y, r * rng.uniform(1.0, 1.5), r, a + rng.normal(0, 0.4))
        if i < strings:
            # The string sags a little and is thinnest in the middle
            start = edge(a) * 0.9
            sag = rng.uniform(-0.05, 0.05)
            thick = n * rng.uniform(0.0016, 0.0032)
            for s in range(61):
                t = s / 60
                dd = start + (d - start) * t
                aa = a + sag * math.sin(t * math.pi)
                w = thick * (0.45 + 0.55 * abs(1.0 - 2.0 * t) ** 1.5)
                px, py = centre + math.cos(aa) * dd, centre + math.sin(aa) * dd
                draw_fine.ellipse((px - w, py - w, px + w, py + w), fill=255)
    # A few specks
    for _ in range(blobs // 2):
        a = direction(1.2)
        d = edge(a) + n * rng.uniform(0.02, 0.24)
        if d > n * 0.47:
            continue
        r = n * rng.uniform(0.002, 0.005)
        blob(draw_fine, centre + math.cos(a) * d, centre + math.sin(a) * d, r * rng.uniform(1.0, 1.8), r, a)

    # Slime merges more than blood: wide blur, soft threshold
    def merge(mask, amount):
        soft = np.asarray(mask.filter(ImageFilter.GaussianBlur(n * amount)), dtype=np.float32) / 255.0
        return smooth(0.4, 0.6, soft)

    shape = np.maximum(merge(body, 0.006), merge(fine, 0.0012))
    alpha_image = Image.fromarray((shape * 255).astype(np.uint8)).resize((SIZE, SIZE), Image.LANCZOS)
    alpha = np.asarray(alpha_image, dtype=np.float32) / 255.0

    # How deep it is: deep in the middle of the pool, a rounded rim, lumps and bubbles on top
    deep = np.clip(blur(alpha, SIZE * 0.035) * 1.5, 0.0, 1.0) ** 0.9
    rim = blur(alpha, SIZE * 0.006)
    lumps = fbm(rng, SIZE, 5, 4)
    height = alpha * (0.55 * smooth(0.15, 0.85, rim) + 0.45 * deep) * (0.75 + 0.5 * lumps)
    bubble_light = np.zeros_like(alpha)
    yy, xx = np.mgrid[0:SIZE, 0:SIZE].astype(np.float32)
    placed = 0
    for _ in range(4000):
        if placed >= 10 + int(26 * pool / 0.2):
            break
        x, y = rng.uniform(0, SIZE), rng.uniform(0, SIZE)
        if deep[int(y), int(x)] < 0.5:
            continue
        # Mostly tiny ones, now and then a big one
        r = SIZE * (rng.uniform(0.0025, 0.007) if rng.random() < 0.8 else rng.uniform(0.009, 0.016))
        x0, x1, y0, y1 = int(max(0, x - r - 2)), int(min(SIZE, x + r + 3)), int(max(0, y - r - 2)), int(min(SIZE, y + r + 3))
        d = np.sqrt((xx[y0:y1, x0:x1] - x) ** 2 + (yy[y0:y1, x0:x1] - y) ** 2) / r
        dome = np.sqrt(np.clip(1.0 - d * d, 0.0, 1.0))
        height[y0:y1, x0:x1] += dome * r / SIZE * 9.0
        bubble_light[y0:y1, x0:x1] = np.maximum(bubble_light[y0:y1, x0:x1], 0.45 * smooth(0.6, 0.95, d) * (d < 1.0) + 0.2 * dome)
        placed += 1

    # Colour: thin slime is a bright, sickly lime, deep slime murky olive. Swirls of yellow pus and dark threads run
    # through it the way two liquids mix that do not want to.
    px = xx + 90.0 * (fbm(rng, SIZE, 3, 4) - 0.5) * 2.0
    py = yy + 90.0 * (fbm(rng, SIZE, 3, 4) - 0.5) * 2.0
    swirl = sample(fbm(rng, SIZE, 6, 5), px, py)
    # Thin threads of pus along one height line of the swirl, a faint cloud of it around them
    streak = smooth(0.025, 0.0, np.abs(swirl - 0.56)) * 0.5 + smooth(0.5, 0.7, swirl) * 0.3
    dark_vein = smooth(0.04, 0.0, np.abs(sample(fbm(rng, SIZE, 5, 4), px * 1.3, py * 1.3) - 0.5)) * 0.5
    # Small blobs bead up: thicker in their middle too
    bead = smooth(0.35, 1.0, blur(alpha, SIZE * 0.007))
    thick = np.clip(np.maximum(deep * 1.15, bead * 0.55) + (lumps - 0.5) * 0.25, 0.0, 1.0)
    thin_colour = np.array([0.58, 0.86, 0.16])
    mid_colour = np.array([0.22, 0.46, 0.05])
    deep_colour = np.array([0.07, 0.17, 0.02])
    pus_colour = np.array([0.62, 0.68, 0.12])
    t1 = smooth(0.0, 0.45, thick)[..., None]
    t2 = smooth(0.4, 1.0, thick)[..., None]
    rgb = thin_colour * (1 - t1) + mid_colour * t1
    rgb = rgb * (1 - t2) + deep_colour * t2
    rgb = rgb * (1 - 0.55 * streak[..., None] * t1) + pus_colour * 0.55 * streak[..., None] * t1
    rgb = rgb * (1.0 - 0.45 * dark_vein[..., None] * t2)
    # The rim is a thin film and catches light
    film = (alpha * (1.0 - smooth(0.2, 0.75, rim)))[..., None]
    rgb = rgb * (1 - 0.35 * film) + np.array([0.7, 0.95, 0.3]) * 0.35 * film
    rgb = rgb + bubble_light[..., None] * np.array([0.07, 0.11, 0.02])
    grain = noise(rng, SIZE // 6, SIZE)
    rgb = rgb * (0.93 + 0.14 * grain[..., None])
    coverage = alpha * (0.62 + 0.36 * smooth(0.0, 0.6, thick))
    picture = np.dstack([rgb, coverage])

    # Normal map (DirectX order, as Unreal wants it: green points down the picture)
    lift = blur(height, 1.2) * 34.0
    gy, gx = np.gradient(lift)
    normal = np.dstack([-gx, gy, np.ones_like(gx)])
    normal /= np.linalg.norm(normal, axis=2, keepdims=True)
    normal = normal * 0.5 + 0.5

    def as_image(values):
        return Image.fromarray((np.clip(values, 0.0, 1.0) * 255 + 0.5).astype(np.uint8))

    return as_image(picture), as_image(normal)


def preview(colour, path):
    # How it looks on a dark floor (for checking by eye only)
    floor = Image.new("RGBA", colour.size, (52, 55, 58, 255))
    Image.alpha_composite(floor, colour).convert("RGB").resize((512, 512), Image.LANCZOS).save(path)


if __name__ == "__main__":
    import sys

    os.makedirs(OUT, exist_ok=True)
    # 0: wide pool with lobes, 1: pool that ran to one side with tongues and strings, 2: a small splash of blobs
    variants = (
        (101, 0.17, 9, 5, 22, 4, False),
        (202, 0.14, 7, 8, 30, 6, True),
        (303, 0.06, 4, 3, 16, 3, True),
    )
    for index, settings in enumerate(variants):
        colour, normal = make(*settings)
        colour.save(os.path.join(OUT, "slime_%d.png" % index), optimize=True)
        normal.save(os.path.join(OUT, "slime_%d_n.png" % index), optimize=True)
        if len(sys.argv) > 1:
            preview(colour, os.path.join(sys.argv[1], "slime_preview_%d.jpg" % index))
    print("slime pictures written to", OUT)
