"""Stills for the website: one -HCTour run per map, 2560x1440, epic quality, offscreen.
usage: python render_tours.py [kepler|ikarus|all]"""
import glob, os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(HERE, "Game", "HowCouldItBeYou.exe")
RES = (2560, 1440)


def fmt(v):
    return ("%.3f" % v).rstrip("0").rstrip(".")


def run(name, stops, tour_map=None, blood=(), slime=(), quality=4, timeout=900):
    out = os.path.join(HERE, "shots", name)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    args = [GAME, "-HCTour=" + ";".join(",".join(fmt(c) for c in s) for s in stops), "-HCShotDir=" + out,
            "-ResX=%d" % RES[0], "-ResY=%d" % RES[1], "-nosound", "-unattended", "-RenderOffscreen",
            "-ExecCmds=r.SetRes %dx%dw, r.ScreenPercentage 100, scalability %d" % (RES[0], RES[1], quality)]
    if tour_map:
        args.append("-HCTourMap=" + tour_map)
    if blood:
        args.append("-HCTourBlood=" + ";".join(",".join(fmt(c) for c in b) for b in blood))
    if slime:
        args.append("-HCTourSlime=" + ";".join(",".join(fmt(c) for c in s) for s in slime))
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
    files = sorted(glob.glob(os.path.join(out, "**", "tour_*.png"), recursive=True))
    print("%s: %d of %d pictures, %.0f s" % (name, len(files), len(stops), time.time() - t0), flush=True)


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

if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    if which in ("kepler", "all"):
        run("kepler", KEPLER, blood=[(23.0, 19.2)], slime=KEPLER_SLIME)
    if which in ("ikarus", "all"):
        run("ikarus", IKARUS, tour_map="Ikarus")
