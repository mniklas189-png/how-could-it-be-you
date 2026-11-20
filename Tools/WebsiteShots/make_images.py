"""Web images from the 2560x1440 renders.
usage: python make_images.py <site img dir> [what ...]
  what: all (the default), a group (round, kepler, ikarus, europa, lobby) or one picture by its name
        (ikarus-biolab, do-vent ...), so a re-render replaces only the pictures that changed"""
import os, sys, glob
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
S = os.path.join(HERE, "shots")
OUT = sys.argv[1]
WHICH = set(sys.argv[2:]) or {"all"}


def want(group, name):
    return bool({"all", group, name} & WHICH)


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


def lobby():
    """the lobby panel (render_round.py lobby 150 lobby, else the round's host); the host's home-network address is filled"""
    src = "lobby/Mia/host_00_Lobby.png"
    if os.path.exists(os.path.join(S, src)):
        im = open_(src)  # since v0.13.9 the IP line sits 13 px lower (digits on rows 300-316)
        fill_like(im, (402, 295, 600, 325), (604, 297, 616, 323))
    else:
        im = open_("round/Mia/host_00_Lobby.png")
        fill_like(im, (402, 282, 600, 312), (604, 284, 616, 310))
    return im.crop((40, 80, 1240, 1343))


# (group, file, how it is made, widths, quality)
PICTURES = [
    ("round", "hero-shifter.jpg", lambda: hud_clean(open_("round/Lena/jumpscare_Lena_2.png")), [2560, 1280], 84),
    ("kepler", "kepler-corridor.jpg", lambda: scope(open_("kepler/tour_1.png"), 300), [2560, 1280], 82),
    ("kepler", "trailer-poster.jpg", lambda: scope(open_("kepler/tour_6.png"), 190), [1920], 82),
    ("kepler", "do-fusebox.jpg", lambda: open_("kepler/tour_3.png"), [1600, 800], 82),
    ("round", "do-face.jpg", lambda: hud_clean(open_("round/Lena/jumpscare_Lena_1.png")), [1600, 800], 82),
    ("kepler", "do-doors.jpg", lambda: open_("kepler/tour_2.png"), [1600, 800], 82),
    ("round", "do-vote.jpg", lambda: hud_clean(open_("round/Tom/meeting_results_Tom.png")).crop((400, 300, 2160, 1290)), [1600, 800], 82),
    ("ikarus", "ikarus-admin.jpg", lambda: open_("ikarus/tour_1.png"), [1600, 800], 82),
    ("ikarus", "ikarus-biolab.jpg", lambda: open_("ikarus/tour_2.png"), [1600, 800], 82),
    ("ikarus", "ikarus-hangar.jpg", lambda: open_("ikarus/tour_3.png"), [1600, 800], 82),
    ("europa", "europa-drillhall.jpg", lambda: open_("europa/tour_3.png"), [1600, 800], 82),
    ("europa", "europa-cryolab.jpg", lambda: open_("europa/tour_2.png"), [1600, 800], 82),
    ("europa", "europa-gallery.jpg", lambda: open_("europa/tour_5.png"), [1600, 800], 82),
    ("europa", "do-vent.jpg", lambda: open_("europa_vent/tour_1.png"), [1600, 800], 82),
    ("europa", "do-ventview.jpg", lambda: open_("europa_vent/tour_2.png"), [1600, 800], 82),
    ("lobby", "online-lobby.jpg", lobby, [1200, 640], 86),
]

# the old files go (only when everything is written again)
if "all" in WHICH:
    for f in glob.glob(os.path.join(OUT, "*-640.jpg")) + glob.glob(os.path.join(OUT, "*-750.jpg")) + glob.glob(os.path.join(OUT, "*-840.jpg")):
        if "launcher" not in os.path.basename(f):
            os.remove(f)
    for f in ("do-button.jpg",):
        p = os.path.join(OUT, f)
        if os.path.exists(p):
            os.remove(p)

for group, name, make, widths, q in PICTURES:
    if want(group, name) or want(group, name[:-4]):
        save(make(), name, widths, q=q)
