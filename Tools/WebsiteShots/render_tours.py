"""Stills for the website: one -HCTour run per map, 2560x1440, epic quality, offscreen.
usage: python render_tours.py [kepler|ikarus|europa|vent|crisis|wardrobe|emotes|all]"""
import glob, os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(HERE, "Game", "HowCouldItBeYou.exe")
RES = (2560, 1440)


def fmt(v):
    return ("%.3f" % v).rstrip("0").rstrip(".")


def run(name, stops, tour_map=None, blood=(), slime=(), extra=(), quality=4, timeout=900):
    out = os.path.join(HERE, "shots", name)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    # no stops: a picture sheet of the menu scene (extra=["-HCEmoteSheet"]) instead of a tour
    tour = ["-HCTour=" + ";".join(",".join(fmt(c) for c in s) for s in stops)] if stops else []
    args = [GAME] + tour + ["-HCShotDir=" + out, "-ResX=%d" % RES[0], "-ResY=%d" % RES[1], "-nosound", "-unattended", "-RenderOffscreen", "-HCLanguage=en",
            "-ExecCmds=r.SetRes %dx%dw, r.ScreenPercentage 100, scalability %d" % (RES[0], RES[1], quality)]
    if tour_map:
        args.append("-HCTourMap=" + tour_map)
    if blood:
        args.append("-HCTourBlood=" + ";".join(",".join(fmt(c) for c in b) for b in blood))
    if slime:
        args.append("-HCTourSlime=" + ";".join(",".join(fmt(c) for c in s) for s in slime))
    args.extend(extra)
    t0 = time.time()
    p = subprocess.Popen(args)
    try:
        p.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        pass
    finally:
        subprocess.run(["taskkill", "/PID", str(p.pid), "/T", "/F"], capture_output=True)
        subprocess.run(["powershell", "-NoProfile", "-Command",
                        "Get-CimInstance Win32_Process -Filter \"Name='HowCouldItBeYou.exe'\" | Where-Object { $_.ExecutablePath -like '%s*' } "
                        "| ForEach-Object { Stop-Process -Id $_.ProcessId -Force }" % os.path.join(HERE, "Game")], capture_output=True)
    files = sorted(glob.glob(os.path.join(out, "**", "*.png"), recursive=True))
    print("%s: %d of %d pictures, %.0f s" % (name, len(files), len(stops) or len(files), time.time() - t0), flush=True)


# stop i is shot at 13.5 + 2 i s game time; slime lives 60 s, so it is spawned shortly before its stop
KEPLER = [
    (19.0, 19.47, 162, 0, -8),          # 1 remains in the north corridor
    (44.5, 23.4, 163, 90, -1.5),         # 2 sealed atrium doors
    (27.3, 20.1, 150, -42, -7),          # 3 fuse box on the corridor wall
    (43.8, 9.3, 125, -70, -5.4),         # 4 emergency button, command center
    (72.0, 58.5, 159, 180, -8),          # 5 slime trail, south corridor
    (17.4, 58.5, 160, -1.5, -2),         # 6 long corridor
    (34.0, 0.8, 230, 22, -12),           # 7 command center, wide
    (17.2, 38.5, 150, 0, -1),            # 8 west spoke toward the atrium
]
KEPLER_SLIME = [(69.6, 58.8, 19.0), (68.1, 58.3, 19.0), (66.4, 58.7, 19.0)]
IKARUS = [
    (49.5, 8.6, 185, -90, -24),          # 1 admin table
    (66.5, 24.5, 170, -25, 0),           # 2 bio lab
    (82.2, 24, 190, 30, -6),             # 3 hangar
    (96.3, 30, 170, 180, -4),            # 4 reactor core
    (14.8, 30, 170, 180, -6),            # 5 bridge
    (50, 37, 170, -95, 6),               # 6 atrium
    (44, 56, 230, 200, -4),              # 7 cargo hold
    (72.5, 43, 170, -90, 0),             # 8 medbay
]
# since v0.13.9: the drill station under the ice
EUROPA = [
    (9, 10.5, 170, -35, -5),             # 1 quarters
    (71.5, 26, 170, -150, -5),           # 2 cryo lab
    (41.5, 16.5, 200, 55, 4),            # 3 drill hall
    (7.5, 46, 200, -30, -3),             # 4 vehicle hall
    (5, 49.5, 170, 0, -2),               # 5 ice gallery
    (36.5, 3, 230, 25, -12),             # 6 mess hall (meeting room)
]
# a ventilation hatch on Europa with its grille open (-HCVentOpen opens every grille, so it gets a run of its own)
EUROPA_VENT = [
    (11.6, 22.6, 120, -42, -30),         # 1 hatch in the security room
    (20.6, 8.6, 30, -115, 3),            # 2 the Shifter's look out of the quarters hatch, at floor level
]
# since v0.13.11: the meltdown's two emergency coolers on the side walls of the Kepler-7 reactor (the tour shows them
# idle: label and lever, no alarm light)
CRISIS = [
    (35.2, 74.0, 150, 180, -4),          # 1 cooler A, close
    (37.6, 72.4, 170, 160, -7),          # 2 cooler A at an angle, the reactor wall around it
    (54.8, 74.0, 150, 0, -4),            # 3 cooler B, close
    (52.4, 72.4, 170, 20, -7),           # 4 cooler B at an angle
    (42.0, 71.0, 220, 172, -10),         # 5 the west half of the reactor, cooler A at the far wall
]
# the wardrobe on the north wall of the lobby room (9.5, 0.25), its doors to the south
WARDROBE = [
    (9.5, 2.6, 160, -90, -4),            # 1 straight on
    (8.2, 3.8, 165, -70, -6),            # 2 from the south-west
    (11.0, 4.5, 170, -110, -8),          # 3 from the south-east, wider
]

if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    if which in ("kepler", "all"):
        run("kepler", KEPLER, blood=[(23.0, 19.2)], slime=KEPLER_SLIME)
    if which in ("ikarus", "all"):
        run("ikarus", IKARUS, tour_map="Ikarus")
    if which in ("europa", "all"):
        run("europa", EUROPA, tour_map="Europa")
    if which in ("vent", "all"):
        run("europa_vent", EUROPA_VENT, tour_map="Europa", extra=["-HCVentOpen"])
    if which in ("crisis", "all"):
        run("crisis", CRISIS)
    if which in ("wardrobe", "all"):
        run("wardrobe", WARDROBE, tour_map="Lobby")
    if which in ("emotes", "all"):
        # nine figures in the menu scene, eight emotes and one carrying a crate, every one in other clothes
        run("emotes", [], extra=["-HCEmoteSheet"])
