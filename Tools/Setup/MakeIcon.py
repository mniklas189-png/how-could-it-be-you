# Draws the app icon (launcher and game): one figure, half bone white and half blood red - the crew member and
# the Wandler wearing the same shape. Needs Pillow:  python Tools\Setup\MakeIcon.py
# Writes Launcher\Assets\app.ico, Game\Build\Windows\Application.ico and a preview PNG next to this script's output.
import os
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SIZE = 1024  # drawn large, then scaled down for every icon size

INK = (11, 12, 15, 255)
BONE = (236, 228, 212, 255)
BLOOD = (200, 22, 34, 255)


def figure_mask(size):
    """Head and shoulders, centred."""
    mask = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(mask)
    s = size / 1024.0
    d.ellipse([(512 - 178) * s, (392 - 178) * s, (512 + 178) * s, (392 + 178) * s], fill=255)  # head
    d.ellipse([(512 - 360) * s, 640 * s, (512 + 360) * s, 1320 * s], fill=255)  # shoulders run out of the tile
    return mask


def draw(size=SIZE):
    s = size / 1024.0
    # Rounded dark tile with a faint red glow from the right
    tile = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    tile_mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(tile_mask).rounded_rectangle([0, 0, size - 1, size - 1], radius=int(210 * s), fill=255)
    back = Image.new("RGBA", (size, size), INK)
    glow = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse([520 * s, 180 * s, 1240 * s, 980 * s], fill=(150, 10, 20, 150))
    back = Image.alpha_composite(back, glow.filter(ImageFilter.GaussianBlur(140 * s)))

    # The figure: left half bone, right half blood, with a dark seam between them
    mask = figure_mask(size)
    halves = Image.new("RGBA", (size, size), BONE)
    ImageDraw.Draw(halves).rectangle([size // 2, 0, size, size], fill=BLOOD)
    seam = int(13 * s)
    ImageDraw.Draw(halves).rectangle([size // 2 - seam, 0, size // 2 + seam, size], fill=INK)
    shadow = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    shadow.paste((0, 0, 0, 170), mask=mask)
    back = Image.alpha_composite(back, shadow.filter(ImageFilter.GaussianBlur(26 * s)))
    back.paste(halves, mask=mask)

    # One pale eye on the red side: the half that is not what it seems
    d = ImageDraw.Draw(back)
    d.ellipse([(592 - 40) * s, (384 - 40) * s, (592 + 40) * s, (384 + 40) * s], fill=(255, 236, 200, 255))
    d.ellipse([(592 - 15) * s, (384 - 15) * s, (592 + 15) * s, (384 + 15) * s], fill=INK)

    tile.paste(back, mask=tile_mask)
    return tile


def main():
    art = draw()
    sizes = [256, 128, 64, 48, 32, 24, 16]
    targets = [os.path.join(ROOT, "Launcher", "Assets", "app.ico"), os.path.join(ROOT, "Game", "Build", "Windows", "Application.ico")]
    for target in targets:
        os.makedirs(os.path.dirname(target), exist_ok=True)
        art.save(target, format="ICO", sizes=[(n, n) for n in sizes])
        print("written", target)
    # Preview: the large icon and the small sizes blown up, to judge readability
    preview = Image.new("RGBA", (256 + 20 + 160 * 3 + 40, 296), (40, 44, 52, 255))
    preview.paste(art.resize((256, 256), Image.LANCZOS), (20, 20), art.resize((256, 256), Image.LANCZOS))
    x = 296
    for n in (48, 32, 16):
        small = art.resize((n, n), Image.LANCZOS)
        big = small.resize((144, 144), Image.NEAREST)
        preview.paste(big, (x, 76), big)
        x += 160
    out = os.path.join(ROOT, "docs", "screenshots", "21-icon.png")
    preview.save(out)
    print("preview", out)


if __name__ == "__main__":
    main()
