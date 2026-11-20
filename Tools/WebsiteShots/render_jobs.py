"""Bot round for the v0.13.11 pictures (jobs, crisis, emote wheel, wardrobe), English UI, 2560x1440:
  Mia = host, the Shifter, starts a meltdown from the station map (-HCAutoCrisis)
  Ben = crew, Medic: changes clothes at the lobby wardrobe first, then holds emergency cooler A
  Kim = crew, Commander: opens the emote wheel, then holds emergency cooler B
  (the crew bots take the panels in the order of their names, so the two crew names sort before and after nothing
  else: Ben -> A, Kim -> B; the Shifter's name sorts last)
usage: python render_jobs.py [out name] [seconds]   -> shots/<out name>/<player>"""
import glob, os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(HERE, "Game", "HowCouldItBeYou.exe")
out = os.path.join(HERE, "shots", sys.argv[1] if len(sys.argv) > 1 else "jobs")
limit = float(sys.argv[2]) if len(sys.argv) > 2 else 200
shutil.rmtree(out, ignore_errors=True)
os.makedirs(out)
code = os.path.join(out, "code.txt")

base = ["-nosound", "-unattended", "-RenderOffscreen", "-HCNoIntro", "-HCLanguage=en", "-HCPhaseShots", "-HCAutoCrisis",
        "-ResX=2560", "-ResY=1440", "-ExecCmds=r.SetRes 2560x1440w, r.ScreenPercentage 100, scalability 3"]
# the round starts 40 s into the lobby, so the wardrobe bot is done by then
host = ["-HCAutoHost", "-HCName=Mia", "-HCWriteCodeFile=" + code, "-HCMinPlayers=3", "-HCForceOwnRole=Monster",
        "-HCAllowClientDebug", "-HCAutoStart=40", "-HCMap=Kepler7", "-HCForceJob=Ben:Medic,Kim:Commander,Mia:Technician"]
bots = [("Ben", ["-HCAutoWardrobe"]), ("Kim", ["-HCAutoEmote"])]

procs = []
t0 = time.time()
try:
    d = os.path.join(out, "Mia"); os.makedirs(d)
    procs.append(subprocess.Popen([GAME] + base + host + ["-HCShotDir=" + d, "-abslog=" + os.path.join(out, "Mia.log")]))
    time.sleep(10)
    for n, extra in bots:
        d = os.path.join(out, n); os.makedirs(d)
        procs.append(subprocess.Popen([GAME] + base + extra + ["-HCName=" + n, "-HCAutoJoinCodeFile=" + code, "-HCShotDir=" + d,
                                                           "-abslog=" + os.path.join(out, n + ".log")]))
        time.sleep(3)
    # done once both crew bots have their picture at the panel, or at the time limit
    while time.time() - t0 < limit and any(p.poll() is None for p in procs):
        if len(glob.glob(os.path.join(out, "*", "crisis_hold_*.png"))) >= 2:
            time.sleep(6)
            break
        time.sleep(3)
finally:
    for p in procs:
        subprocess.run(["taskkill", "/PID", str(p.pid), "/T", "/F"], capture_output=True)
    subprocess.run(["powershell", "-NoProfile", "-Command",
                    "Get-CimInstance Win32_Process -Filter \"Name='HowCouldItBeYou.exe'\" | Where-Object { $_.ExecutablePath -like '%s*' } "
                    "| ForEach-Object { Stop-Process -Id $_.ProcessId -Force }" % os.path.join(HERE, "Game")], capture_output=True)
    time.sleep(2)
print("%.0f s" % (time.time() - t0), flush=True)
for f in sorted(glob.glob(os.path.join(out, "*", "*.png"))):
    print(os.path.relpath(f, out))
