"""Bot round for the website, English UI, 2560x1440:
  Mia  = host, the Shifter, kills whoever leaves the command center (lobby, role card, phase shots)
  Lena = the only crew bot doing tasks, so she is the victim: her jumpscare pictures
  Tom  = reports the remains and votes for Mia: the meeting and voting screens
  Ben  = small window, reports and votes
usage: python render_round.py <out name> [seconds]"""
import glob, os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(HERE, "Game", "HowCouldItBeYou.exe")
out = os.path.join(HERE, "shots", sys.argv[1] if len(sys.argv) > 1 else "round")
limit = float(sys.argv[2]) if len(sys.argv) > 2 else 420
shutil.rmtree(out, ignore_errors=True)
os.makedirs(out)
code = os.path.join(out, "code.txt")

base = ["-nosound", "-unattended", "-RenderOffscreen", "-HCNoIntro", "-HCDiscussion=8", "-HCVoting=12", "-HCLanguage=en"]
small = ["-ResX=640", "-ResY=360", "-ExecCmds=r.SetRes 640x360w, scalability 0"]
full = ["-ResX=2560", "-ResY=1440", "-ExecCmds=r.SetRes 2560x1440w, r.ScreenPercentage 100, scalability 3", "-HCPhaseShots"]
host = ["-HCAutoHost", "-HCName=Mia", "-HCWriteCodeFile=" + code, "-HCMinPlayers=4", "-HCForceOwnRole=Monster",
        "-HCAllowClientDebug", "-HCAutoStart=6", "-HCAutoKill", "-HCAutoVote=skip", "-HCQuitAfterRound"]
bots = [("Lena", full + ["-HCAutoTasks", "-HCAutoVote=skip", "-HCQuitAfterRound"]),
        ("Tom", full + ["-HCAutoReport", "-HCAutoVote=Mia", "-HCQuitAfterRound"]),
        ("Ben", small + ["-HCAutoReport", "-HCAutoVote=Mia", "-HCQuitAfterRound"])]

procs = []
t0 = time.time()
try:
    d = os.path.join(out, "Mia"); os.makedirs(d)
    procs.append(subprocess.Popen([GAME] + base + full + host + ["-HCShotDir=" + d]))
    time.sleep(10)
    for n, extra in bots:
        d = os.path.join(out, n); os.makedirs(d)
        procs.append(subprocess.Popen([GAME] + base + extra + ["-HCName=" + n, "-HCAutoJoinCodeFile=" + code, "-HCShotDir=" + d]))
        time.sleep(3)
    while time.time() - t0 < limit and any(p.poll() is None for p in procs):
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
