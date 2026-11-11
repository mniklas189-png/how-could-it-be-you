# Draws the blood splatter pictures of the game (Tools/Setup/Effects/blood_N.png). Run with a normal Python that has
# numpy and Pillow:  python Tools/Setup/MakeBlood.py   - then Tools/Setup/SetupEffects.py brings them into the project.
# The pictures are drawn, not photographed: a pool with an uneven edge, streaks that shoot out of it and drops that
# get smaller and longer the further they fly. Everything is merged with a blur and a threshold, the way liquid merges.
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

SIZE = 1024
SUPER = 2  # drawn at twice the size, then scaled down
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Effects")


def outline(rng, base, count=720):
    # Radius of the pool around its centre: a circle bent by a handful of waves
    waves = [(k, rng.uniform(0.3, 1.0) * 0.26 / k ** 0.85, rng.uniform(0, math.tau)) for k in range(1, 12)]
    angles = np.linspace(0, math.tau, count, endpoint=False)
    radius = np.full(count, base)
    for k, amount, phase in waves:
        radius = radius * (1.0 + amount * np.cos(k * angles + phase) * 0.55)
    return angles, radius


def drop(draw, x, y, long, wide, angle):
    # A drop stretched along its flight direction
    points = []
    for i in range(20):
        a = math.tau * i / 20
        px, py = math.cos(a) * long, math.sin(a) * wide
        points.append((x + px * math.cos(angle) - py * math.sin(angle), y + px * math.sin(angle) + py * math.cos(angle)))
    draw.polygon(points, fill=255)


def make(seed, pool, streaks, drops, one_sided):
    rng = np.random.default_rng(seed)
    n = SIZE * SUPER
    centre = n / 2
    mask = Image.new("L", (n, n), 0)
    draw = ImageDraw.Draw(mask)
    angles, radius = outline(rng, n * pool)
    draw.polygon([(centre + math.cos(a) * r, centre + math.sin(a) * r) for a, r in zip(angles, radius)], fill=255)
    main = rng.uniform(0, math.tau)  # where most of it flew

    def direction():
        return rng.normal(main, 0.75) if one_sided and rng.random() < 0.8 else rng.uniform(0, math.tau)

    def edge(a):
        return radius[int((a % math.tau) / math.tau * len(radius)) % len(radius)]

    for _ in range(streaks):
        a = direction()
        start, length = edge(a) * 0.85, n * rng.uniform(0.06, 0.2)
        width = n * rng.uniform(0.004, 0.011)
        bend = rng.uniform(-0.25, 0.25)
        steps = 60
        for i in range(steps + 1):
            t = i / steps
            d = start + length * t
            aa = a + bend * t * t
            w = width * (1.0 - t) ** 1.4 + n * 0.0012
            x, y = centre + math.cos(aa) * d, centre + math.sin(aa) * d
            draw.ellipse((x - w, y - w, x + w, y + w), fill=255)
        # the drop at the end of the streak
        d = start + length * rng.uniform(1.02, 1.12)
        drop(draw, centre + math.cos(a + bend) * d, centre + math.sin(a + bend) * d, width * rng.uniform(1.6, 2.6), width * rng.uniform(0.9, 1.3), a + bend)
    for _ in range(drops):
        a = direction()
        far = rng.random() ** 1.6  # most drops land close to the pool
        d = edge(a) * 1.02 + n * (0.01 + 0.2 * far)
        if d > n * 0.485:
            continue
        size = n * rng.uniform(0.0016, 0.0085) * (1.0 - 0.6 * far)
        drop(draw, centre + math.cos(a) * d, centre + math.sin(a) * d, size * (1.0 + 2.2 * far * rng.random()), size, a)

    # Liquid merges: blur, then a soft threshold
    soft = np.asarray(mask.filter(ImageFilter.GaussianBlur(n * 0.0022)), dtype=np.float32) / 255.0
    alpha = np.clip((soft - 0.38) / 0.24, 0.0, 1.0)
    alpha = alpha * alpha * (3.0 - 2.0 * alpha)
    alpha_image = Image.fromarray((alpha * 255).astype(np.uint8)).resize((SIZE, SIZE), Image.LANCZOS)
    alpha = np.asarray(alpha_image, dtype=np.float32) / 255.0

    # Thick blood is almost black, thin blood and the rim are red
    thick = np.asarray(alpha_image.filter(ImageFilter.GaussianBlur(SIZE * 0.03)), dtype=np.float32) / 255.0
    thick = np.clip(thick * 1.25, 0.0, 1.0) ** 0.8
    grain = np.asarray(Image.fromarray((rng.random((SIZE // 8, SIZE // 8)) * 255).astype(np.uint8)).resize((SIZE, SIZE), Image.BICUBIC), dtype=np.float32) / 255.0
    thick = np.clip(thick + (grain - 0.5) * 0.12, 0.0, 1.0)
    thin, deep = np.array([0.52, 0.03, 0.02]), np.array([0.2, 0.004, 0.004])
    rgb = thin[None, None, :] * (1.0 - thick[..., None]) + deep[None, None, :] * thick[..., None]
    out = np.dstack([rgb, alpha * (0.86 + 0.14 * thick)])
    return Image.fromarray((np.clip(out, 0.0, 1.0) * 255).astype(np.uint8))


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    # 0: wide pool, 1: pool with a spray to one side, 2: mostly spray
    for index, settings in enumerate(((11, 0.2, 18, 150, False), (23, 0.15, 26, 240, True), (37, 0.085, 34, 330, True))):
        make(*settings).save(os.path.join(OUT, "blood_%d.png" % index))
    print("blood pictures written to", OUT)
