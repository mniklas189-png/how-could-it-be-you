HOW COULD IT BE YOU - LAUNCHER FÜR LINUX / LAUNCHER FOR LINUX
==============================================================

Deutsch zuerst, English below.


DEUTSCH
-------

Was ist das?
  Der Launcher installiert das Spiel, hält es aktuell und startet es - wie unter Windows.
  Er lädt die Linux-Version des Spiels aus den GitHub-Releases von
  mniklas189-png/how-could-it-be-you (Datei "HowCouldItBeYou-Linux-v....zip").

Inhalt des Ordners
  HowCouldItBeYouLauncher   das Programm (bringt alles mit, .NET muss nicht installiert sein)
  launcher.json             Einstellungen (woher das Spiel kommt) - normalerweise nicht ändern
  how-could-it-be-you.png   Symbol für das Anwendungsmenü
  add-to-menu.sh            trägt den Launcher ins Anwendungsmenü ein (freiwillig)
  README-LINUX.txt          diese Anleitung

Voraussetzungen
  - 64-Bit-Linux (x86_64) mit Desktop, z. B. Ubuntu 22.04 oder neuer, Linux Mint 21+, Debian 12+,
    Fedora, Arch, SteamOS 3 (Steam Deck im Desktop-Modus)
  - X11 oder Wayland mit XWayland (bei GNOME und KDE Plasma eingeschaltet)
  - Grafikkarte mit Vulkan und aktuellem Treiber
    (NVIDIA: der proprietäre Treiber; AMD und Intel: Mesa, ist meist schon da)
  - einige GB freier Platz (die genaue Größe zeigt der Launcher unter INSTALLIEREN)
  - Internet für Installation und Updates

Einrichten (einmal)
  1. Den Ordner an einen Ort kopieren, an dem du schreiben darfst, z. B. ~/Spiele.
     Der Launcher legt das Spiel im Unterordner "Game" neben sich ab, also nicht nach /opt oder /usr.
     Aus dem Archiv:
         mkdir -p ~/Spiele
         tar -xzf HCIBY-Launcher-Linux.tar.gz -C ~/Spiele
  2. Nur wenn der Ordner von einem Windows-PC oder USB-Stick kommt (im .tar.gz ist das schon gesetzt):
     im Terminal ausführbar machen
         cd ~/Spiele/HowCouldItBeYou-Launcher
         chmod +x HowCouldItBeYouLauncher
  3. Freiwillig ins Anwendungsmenü eintragen:
         sh add-to-menu.sh
     Danach steht "How Could It Be You" im Menü unter "Spiele".

Spielen
  1. Launcher starten: über das Menü, per Doppelklick im Dateimanager oder im Terminal mit
         ./HowCouldItBeYouLauncher
  2. Beim ersten Mal "INSTALLIEREN" klicken. Der Balken unten zeigt den Download.
  3. "SPIELEN" klicken. Gibt es eine neue Version, erscheint "UPDATE".
  Oben rechts schaltet DE | EN die Sprache um. "Spielordner öffnen" (unten links) zeigt das
  installierte Spiel.

Mit Windows-Spielern zusammen
  Linux und Windows spielen in derselben Lobby, wenn alle dieselbe Version haben (steht im Launcher
  unten: INSTALLIERT ...). Gespielt wird im selben Netzwerk: Join-Code oder IP-Adresse wie unter Windows.
  Wer hostet und eine Firewall eingeschaltet hat (z. B. Fedora), gibt UDP 7777 (Spiel) und
  UDP 14001 (Join-Code-Suche) frei, z. B.
         sudo ufw allow 7777/udp && sudo ufw allow 14001/udp
         sudo firewall-cmd --add-port=7777/udp --add-port=14001/udp --permanent && sudo firewall-cmd --reload

Wenn etwas nicht klappt
  - Doppelklick tut nichts / "Keine Berechtigung" (Permission denied):
         chmod +x HowCouldItBeYouLauncher
    und im Terminal starten (manche Dateimanager starten Programme nicht per Doppelklick).
  - Fehlende Bibliotheken (nur auf sehr schlanken Systemen):
         Ubuntu/Debian/Mint:  sudo apt install libfontconfig1 libx11-6 libice6 libsm6 libvulkan1
         Fedora:              sudo dnf install fontconfig libX11 libICE libSM vulkan-loader
         Arch:                sudo pacman -S fontconfig libx11 libice libsm vulkan-icd-loader
  - "Noch keine Version": Auf GitHub gibt es noch kein Release mit Linux-Datei
    (HowCouldItBeYou-Linux-v....zip). Die Windows-Version installiert dieser Launcher nicht.
  - Spiel startet nicht oder bleibt schwarz: Vulkan prüfen mit  vulkaninfo --summary
    (Paket vulkan-tools). Das Protokoll des Spiels liegt in
         ~/.config/Epic/HowCouldItBeYou/Saved/Logs/HowCouldItBeYou.log
  - Launcher-Fehler sehen: im Terminal starten, die Meldungen erscheinen dort.

Entfernen
  Den Ordner löschen. Falls ins Menü eingetragen, zusätzlich:
         rm ~/.local/share/applications/how-could-it-be-you.desktop
  Einstellungen und Protokolle des Spiels: ~/.config/Epic/HowCouldItBeYou


ENGLISH
-------

What is this?
  The launcher installs the game, keeps it up to date and starts it - just like on Windows.
  It downloads the Linux version of the game from the GitHub releases of
  mniklas189-png/how-could-it-be-you (file "HowCouldItBeYou-Linux-v....zip").

What's in this folder
  HowCouldItBeYouLauncher   the program (self-contained, no .NET installation needed)
  launcher.json             settings (where the game comes from) - normally leave it alone
  how-could-it-be-you.png   icon for the application menu
  add-to-menu.sh            adds the launcher to the application menu (optional)
  README-LINUX.txt          this guide

Requirements
  - 64-bit Linux (x86_64) with a desktop, e.g. Ubuntu 22.04 or newer, Linux Mint 21+, Debian 12+,
    Fedora, Arch, SteamOS 3 (Steam Deck in desktop mode)
  - X11, or Wayland with XWayland (on by default in GNOME and KDE Plasma)
  - a graphics card with Vulkan and a current driver
    (NVIDIA: the proprietary driver; AMD and Intel: Mesa, usually already installed)
  - a few GB of free space (the launcher shows the exact size under INSTALL)
  - internet for installing and updating

Setup (once)
  1. Copy the folder somewhere you can write to, e.g. ~/Games.
     The launcher puts the game into a "Game" folder next to itself, so not into /opt or /usr.
     From the archive:
         mkdir -p ~/Games
         tar -xzf HCIBY-Launcher-Linux.tar.gz -C ~/Games
  2. Only if the folder came from a Windows PC or a USB stick (the .tar.gz already has this set):
     make it executable in a terminal
         cd ~/Games/HowCouldItBeYou-Launcher
         chmod +x HowCouldItBeYouLauncher
  3. Optional, add it to the application menu:
         sh add-to-menu.sh
     "How Could It Be You" then shows up in the menu under "Games".

Playing
  1. Start the launcher: from the menu, by double-clicking it in the file manager, or in a terminal with
         ./HowCouldItBeYouLauncher
  2. The first time, click "INSTALL". The bar along the bottom shows the download.
  3. Click "PLAY". When there is a new version, "UPDATE" appears.
  DE | EN at the top right switches the language. "Open game folder" (bottom left) shows the
  installed game.

Playing with Windows players
  Linux and Windows play in the same lobby as long as everyone has the same version (shown at the
  bottom of the launcher: INSTALLED ...). You play on the same network: join code or IP address,
  as on Windows. If you host and have a firewall switched on (e.g. Fedora), open UDP 7777 (game)
  and UDP 14001 (join code search), e.g.
         sudo ufw allow 7777/udp && sudo ufw allow 14001/udp
         sudo firewall-cmd --add-port=7777/udp --add-port=14001/udp --permanent && sudo firewall-cmd --reload

Troubleshooting
  - Double-click does nothing / "Permission denied":
         chmod +x HowCouldItBeYouLauncher
    and start it from a terminal (some file managers don't run programs on double-click).
  - Missing libraries (only on very minimal systems):
         Ubuntu/Debian/Mint:  sudo apt install libfontconfig1 libx11-6 libice6 libsm6 libvulkan1
         Fedora:              sudo dnf install fontconfig libX11 libICE libSM vulkan-loader
         Arch:                sudo pacman -S fontconfig libx11 libice libsm vulkan-icd-loader
  - "No version yet": there is no release with a Linux file on GitHub yet
    (HowCouldItBeYou-Linux-v....zip). This launcher does not install the Windows version.
  - The game doesn't start or stays black: check Vulkan with  vulkaninfo --summary
    (package vulkan-tools). The game's log is in
         ~/.config/Epic/HowCouldItBeYou/Saved/Logs/HowCouldItBeYou.log
  - To see launcher errors, start it from a terminal; messages appear there.

Removing it
  Delete the folder. If you added the menu entry, also run:
         rm ~/.local/share/applications/how-could-it-be-you.desktop
  The game's settings and logs: ~/.config/Epic/HowCouldItBeYou
