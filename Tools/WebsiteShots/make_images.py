"""Web images from the 2560x1440 renders. usage: python make_images.py <site img dir>"""
import os, sys, glob
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
S = os.path.join(HERE, "shots")
OUT = sys.argv[1]


def fill_like(im, box, sample):
    """fill box with the median colour of the sample area (a flat ground, so the patch never shows)"""
    from PIL import ImageStat
    c = tuple(int(v) for v in ImageStat.Stat(im.crop(sample)).median)
    im.paste(c, box)


def hud_clean(im):
    """the FPS counter (top right) and the TALK hint (bottom left) are small text on near-black: fill them flat"""
    im = im.copy()
    w, h = im.size
    fill_like(im, (w - 170, 0, w, 64), (w - 260, 0, w - 180, 60))
    fill_like(im, (0, h - 125, 150, h - 55), (0, h - 170, 150, h - 130))
    return im


def save(im, name, widths, q=82):
    im = im.convert("RGB")
    for i, w in enumerate(widths):
        out = im if im.width == w else im.resize((w, round(im.height * w / im.width)), Image.LANCZOS)
        fn = name if i == 0 else name.replace(".jpg", "-%d.jpg" % w)
        out.save(os.path.join(OUT, fn), quality=q, optimize=True, progressive=True)
        print(fn, out.size, os.path.getsize(os.path.join(OUT, fn)) // 1024, "KB")


def open_(p):
    return Image.open(os.path.join(S, p)).convert("RGB")


def scope(im, top):
    """a 2.39:1 band of a 2560x1440 frame starting at row top"""
    return im.crop((0, top, 2560, top + 1071))


# the old files go
for f in glob.glob(os.path.join(OUT, "*-640.jpg")) + glob.glob(os.path.join(OUT, "*-750.jpg")) + glob.glob(os.path.join(OUT, "*-840.jpg")):
    if "launcher" not in os.path.basename(f):
        os.remove(f)
for f in ("do-button.jpg",):
    p = os.path.join(OUT, f)
    if os.path.exists(p):
        os.remove(p)

save(hud_clean(open_("round/Lena/jumpscare_Lena_2.png")), "hero-shifter.jpg", [2560, 1280], q=84)
save(scope(open_("kepler/tour_1.png"), 300), "kepler-corridor.jpg", [2560, 1280])
save(scope(open_("kepler/tour_6.png"), 190), "trailer-poster.jpg", [1920])
save(open_("kepler/tour_3.png"), "do-fusebox.jpg", [1600, 800])
save(hud_clean(open_("round/Lena/jumpscare_Lena_1.png")), "do-face.jpg", [1600, 800])
save(open_("kepler/tour_2.png"), "do-doors.jpg", [1600, 800])
save(hud_clean(open_("round/Tom/meeting_results_Tom.png")).crop((400, 300, 2160, 1290)), "do-vote.jpg", [1600, 800])
save(open_("ikarus/tour_1.png"), "ikarus-admin.jpg", [1600, 800])
save(open_("ikarus/tour_2.png"), "ikarus-biolab.jpg", [1600, 800])
save(open_("ikarus/tour_3.png"), "ikarus-hangar.jpg", [1600, 800])

# the lobby panel; the host's home-network address is pixelated
lobby = open_("round/Mia/host_00_Lobby.png")
fill_like(lobby, (402, 282, 600, 312), (604, 284, 616, 310))
save(lobby.crop((40, 80, 1240, 1343)), "online-lobby.jpg", [1200, 640], q=86)
