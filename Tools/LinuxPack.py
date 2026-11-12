"""Packs Linux builds on Windows so that programs keep their Unix "executable" flag.

A zip made by Windows (PowerShell, Explorer) carries no Unix file modes: on Linux the game's start script and
the launcher would then not start until someone runs chmod +x. This script writes the modes itself.

    python Tools/LinuxPack.py game <Build/Package/Linux> <Build/Release/HowCouldItBeYou-Linux-v1.2.3.zip>
        The game for a GitHub release. The folder's content goes to the top of the zip
        (HowCouldItBeYou.sh, HowCouldItBeYou/, Engine/), like the Windows zip.

    python Tools/LinuxPack.py launcher <Build/LauncherLinux> <Build/Release/HCIBY-Launcher-Linux.tar.gz>
        The Linux launcher for players: one folder "HowCouldItBeYou-Launcher" in a .tar.gz
        (tar keeps the modes for every unpacking tool, zip only for some).

Executable: *.sh, every file in a Binaries/Linux folder, and the launcher program "HowCouldItBeYouLauncher".
Scripts and text files get Unix line endings.
"""

import io
import os
import sys
import tarfile
import time
import zipfile

LAUNCHER_PROGRAM = "HowCouldItBeYouLauncher"
TEXT_SUFFIXES = (".sh", ".txt", ".desktop", ".json")


def is_executable(relative):
    parts = relative.replace("\\", "/").split("/")
    name = parts[-1]
    if name.endswith(".sh") or name == LAUNCHER_PROGRAM:
        return True
    for i in range(len(parts) - 2):
        if parts[i] == "Binaries" and parts[i + 1] == "Linux":
            return True
    return False


def unix_text(path):
    """Contents with LF line endings for scripts and texts, None for everything else."""
    if not path.lower().endswith(TEXT_SUFFIXES):
        return None
    with open(path, "rb") as f:
        data = f.read()
    return data.replace(b"\r\n", b"\n")


def walk(root):
    for folder, dirs, files in os.walk(root):
        dirs.sort()
        for name in sorted(files):
            full = os.path.join(folder, name)
            yield full, os.path.relpath(full, root).replace("\\", "/")


def pack_game(staged, target):
    if not os.path.isfile(os.path.join(staged, "HowCouldItBeYou.sh")):
        sys.exit(f"[HC] {staged} has no HowCouldItBeYou.sh - is it a Linux package?")
    tmp = target + ".part"
    count = 0
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as z:
        for full, rel in walk(staged):
            info = zipfile.ZipInfo.from_file(full, rel)
            info.create_system = 3  # Unix: unzip and file managers read the modes below
            mode = 0o755 if is_executable(rel) else 0o644
            info.external_attr = (0o100000 | mode) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            text = unix_text(full) if rel.endswith(".sh") else None
            if text is not None:
                z.writestr(info, text)
            else:
                with open(full, "rb") as src, z.open(info, "w", force_zip64=True) as dst:
                    while True:
                        chunk = src.read(1 << 20)
                        if not chunk:
                            break
                        dst.write(chunk)
            count += 1
    os.replace(tmp, target)
    print(f"[HC] {count} files -> {target}")


def pack_launcher(folder, target, top="HowCouldItBeYou-Launcher"):
    if not os.path.isfile(os.path.join(folder, LAUNCHER_PROGRAM)):
        sys.exit(f"[HC] {folder} has no {LAUNCHER_PROGRAM} - run Tools/BuildLauncherLinux.bat first")
    tmp = target + ".part"
    now = int(time.time())
    with tarfile.open(tmp, "w:gz", format=tarfile.PAX_FORMAT) as tar:
        root = tarfile.TarInfo(top)
        root.type = tarfile.DIRTYPE
        root.mode = 0o755
        root.mtime = now
        tar.addfile(root)
        for full, rel in walk(folder):
            if rel.endswith(".pdb"):
                continue
            info = tarfile.TarInfo(f"{top}/{rel}")
            info.mode = 0o755 if is_executable(rel) else 0o644
            info.mtime = int(os.path.getmtime(full))
            text = unix_text(full)
            if text is not None:
                info.size = len(text)
                tar.addfile(info, io.BytesIO(text))
            else:
                info.size = os.path.getsize(full)
                with open(full, "rb") as src:
                    tar.addfile(info, src)
    os.replace(tmp, target)
    print(f"[HC] {target}")


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in ("game", "launcher"):
        sys.exit(__doc__)
    kind, source, target = sys.argv[1], os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3])
    os.makedirs(os.path.dirname(target), exist_ok=True)
    if kind == "game":
        pack_game(source, target)
    else:
        pack_launcher(source, target)


if __name__ == "__main__":
    main()
