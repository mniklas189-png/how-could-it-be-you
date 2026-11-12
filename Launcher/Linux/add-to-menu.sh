#!/bin/sh
# How Could It Be You: puts the launcher into the application menu (category Games)
# and makes the launcher program executable. Run it once from the launcher folder:
#     sh add-to-menu.sh
# Remove the entry again:  rm ~/.local/share/applications/how-could-it-be-you.desktop
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
LAUNCHER="$DIR/HowCouldItBeYouLauncher"

if [ ! -f "$LAUNCHER" ]; then
    echo "HowCouldItBeYouLauncher not found in $DIR" >&2
    exit 1
fi
chmod +x "$LAUNCHER"

APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$APPS"
ENTRY="$APPS/how-could-it-be-you.desktop"

cat > "$ENTRY" <<EOF
[Desktop Entry]
Type=Application
Name=How Could It Be You
Comment=Social deduction horror - install, update and play
Comment[de]=Social-Deduction-Horror - installieren, aktualisieren, spielen
Exec="$LAUNCHER"
Path=$DIR
Icon=$DIR/how-could-it-be-you.png
Terminal=false
Categories=Game;
EOF
chmod +x "$ENTRY"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$APPS" >/dev/null 2>&1 || true
fi

echo "How Could It Be You is now in your application menu (Games)."
echo "Im Anwendungsmenue steht jetzt How Could It Be You (Spiele)."
