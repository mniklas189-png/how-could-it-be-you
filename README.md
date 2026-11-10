# How Could It Be You

First-Person-Multiplayer-Horrorspiel (4–8 Spieler) in **Unreal Engine 5.8**: Eine Forscher-Crew auf einer Raumstation, und einer von ihnen ist ein formwandelndes Monster. Social Deduction wie *Among Us*, aber realistisch und dunkel, mit Proximity-Voice.

**Status:** Meilenstein **M4 – Kill, Überreste, Geister, Jumpscare** fertig (Stand 2026-11-10), Version 0.4.0. Planung unter [`docs/`](docs/README.md).

## Was schon geht

**M4 – Kill, Überreste, Geister, Jumpscare**
- **Töten** (nur der Wandler, linke Maustaste): Ziel im Fadenkreuz, höchstens 2 m, freie Sicht, 30 s Abklingzeit, erster Kill frühestens nach 20 s, nie in der Kommandozentrale. Der Server prüft alles. Der Wandler sieht unten rechts „TÖTEN“ mit Abklingzeit und einen roten Rahmen ums Fadenkreuz, wenn ein Ziel in Reichweite ist
- **Jumpscare** nur beim Opfer: Die Kamera wird zum Angreifer gerissen, das Gesicht des Wandlers schießt auf dich zu, Stinger, Blitz, Schwarz, dann „Du bist tot“. Zeugen sehen nur den Ruck der Figur und hören einen Schrei. Monster und Opfer sind 2,5 s gesperrt
- **Schreck-Intensität** Voll / Reduziert / Minimal: im Hauptmenü, in der Warnung beim ersten Start und im Esc-Menü
- **Überreste**: Lache und ID-Marke mit dem Namen des Opfers, ohne Kollision. Lebende melden sie mit E aus bis zu 2,5 m (hat Vorrang vor Stationen). Bis M5 erscheint dabei nur eine Meldung für alle
- **Geister**: fliegen frei (auch durch Wände, Leertaste/Strg hoch/runter), bleiben in der Station, erledigen ihre restlichen Aufgaben, sehen den Wandler mit roter Markierung und andere Geister halbtransparent. Lebende sehen Geister nie: Deren PCs bekommen sie gar nicht erst geschickt
- **Sieg des Wandlers**: sobald er so viele ist wie die lebende Crew (bei 4 Spielern nach dem 2. Kill)
- Ausfallsicherung: Meldet sich das Opfer nicht, macht der Server es nach 3 s trotzdem zum Geist; der PC des Opfers beendet den Jumpscare spätestens nach 5 s
- Platzhalter: Monster-Gesicht aus Grundformen und im Code erzeugte Töne. Ein echtes Modell und echte Sounds kommen später (M11/M12)

**M3 – Aufgaben**
- 9 Aufgaben mit allen vier Typen: **Halten** (E gedrückt halten), **Abfolge** (Kisten in Reihenfolge scannen), **Minispiel** (Ventile, Signalkurve, Temperatur) und **Mehrstufig** (z. B. Probe: Lager → Bio-Labor → 20 s Zentrifuge → Medbay)
- 12 Stationen auf Kepler-7, per Code platziert. Der Bildschirm der Station leuchtet nur für dich: gelb = hier ist deine Aufgabe, blau = noch nicht bereit, grün = erledigt
- Jeder bekommt 1 gemeinsame, 2 lange und 3 kurze Aufgaben, verteilt über mindestens 3 Räume. Aufgabenliste oben links (Tab klappt sie ein), Balken „Aufgaben der Crew“ oben in der Mitte
- Der Wandler bekommt eine gleich aussehende **Fake-Liste**: gleiche Stationen, gleiche Dauer, aber der Balken steigt nicht
- Die Crew gewinnt, wenn alle echten Aufgaben erledigt sind. Wer geht, dessen offene Aufgaben fallen aus der Summe
- Bewegung (W/A/S/D) oder Esc bricht jede Aufgabe ab. Der Server prüft Dauer und Abstand, Minispiele gehen nie schneller als ihre Soll-Dauer
- Spieler blockieren sich nicht mehr gegenseitig (man läuft durcheinander hindurch), damit niemand eine Tür oder Station zustellt
- Testrunden zu zweit: Die Regel „Wandler gewinnt bei Gleichstand“ gilt erst ab 3 Spielern, sonst wäre die Runde sofort vorbei

**M2 – Match-Ablauf & Rollen**
- Hauptmenü mit Epilepsie-Warnung (einmalig), Namenseingabe, „Lobby hosten“, Beitritt per **Join-Code** (6 Zeichen, LAN) oder **IP-Adresse**
- Lobby-Raum mit Lobby-Menü (Tab): Code, LAN-IP, Spielerliste mit Farbe und Symbol, „Runde starten“ (nur Host, ab 2 Spielern)
- Ablauf: Lobby → Laden → Intro-Texttafel (nur 1. Runde) → private Rollen-Enthüllung (5 s) → Runde → Ergebnis (15 s, Wandler wird enthüllt) → zurück in die Lobby, alles ohne Verbindungsabbruch (Seamless Travel)
- Genau 1 Wandler, zufällig; jeder sieht nur seine eigene Rolle. Siegbedingungen (Wandler weg, Gleichstand, Aufgaben, Timer) sind fertig und getestet; Kills und Meetings kommen in M4–M5
- Verbindungsabbruch wird für alle gleich angesagt, Wandler weg = Crew gewinnt; Host verlässt → alle landen mit Meldung im Hauptmenü
- Menü mit Esc (im Editor F1): Weiter, „Ich stecke fest“, Spiel verlassen (Host mit Bestätigung)

**M1 – Greybox & Bewegung**
- Station **Kepler-7** als Greybox, komplett im Code erzeugt (`AHCStationBuilder` aus `FHCStationLayout`): 10 Räume, 16 Gänge, 25 automatische Türen, Deckenlichter, Raumnamen am Boden
- Ego-Figur: Gehen 300 cm/s, Sprinten 500 cm/s mit 6 s Ausdauer, kein Springen
- Taschenlampe (F) mit Akku 120 s, lädt aus in 90 s, Ladestationen L1–L3 (E halten), Helmlicht
- Kill-Z-Schutz, Launcher mit Update-Knopf (GitHub-Releases)

## Konsolenbefehle (Konsole: `^` oder F10)

| Befehl | Wirkung |
|---|---|
| `hc.StartMatch` | Host: Runde starten (auch direkt auf Kepler-7 ohne Lobby) |
| `hc.EndMatch Crew` / `hc.EndMatch Monster` | Runde mit Gewinner beenden |
| `hc.ForceRole <Name> Monster` | Rolle für die nächste Verteilung erzwingen |
| `hc.KillMe` | Eigener Tod mit Jumpscare und Geist, ohne Monster |
| `hc.Jumpscare` | Jumpscare nur ansehen, ohne zu sterben |
| `hc.CompleteAllTasks` | Alle echten Aufgaben erledigen (Crew gewinnt) |
| `hc.GotoStation <TaskId> [Schritt]` | Stellt dich vor eine Aufgaben-Station, z. B. `hc.GotoStation Reactor_Valves` |
| `hc.SkipIntro`, `hc.SetTimer <s>` | Intro überspringen, Restzeit der Phase setzen |
| `hc.ShowDebug 1` / `2` | Phase, Timer, eigene Rolle; Stufe 2 zeigt beim Host alle Rollen |
| `hc.ListPlayers` | Spielerliste ins Log (Rollen nur beim Host) |
| `hc.Connect <IP>` | Direkt zu einem Host im LAN verbinden |
| `hc.SimulateHostLeave` | Host verlässt das Spiel (testet die Meldung bei allen) |
| `hc.Unstuck`, `hc.Where`, `hc.Exposure <EV>`, `hc.ValidateLayout` | Feststecken, Position, Helligkeit, Map-Prüfung |

## Ordner

| Ordner | Inhalt |
|---|---|
| `Game/` | UE-5.8-Projekt (`HowCouldItBeYou.uproject`, C++ unter `Source/`) |
| `Launcher/` | Launcher (C# / WinForms, .NET 9) |
| `Tools/` | Skripte zum Doppelklicken (siehe unten) |
| `docs/` | Planungsdokumente |

## Skripte (`Tools/`)

Einmalig: `Tools\local.example.bat` nach `Tools\local.bat` kopieren (Engine-Pfad, Standard `C:\Program Files\Epic Games\UE_5.8`).

| Skript | Was es tut |
|---|---|
| `Build.bat` | Kompiliert den C++-Code (Editor schließen!) → „BUILD ERFOLGREICH“ |
| `RunTests.bat` | Baut und startet alle Tests `HowCouldItBeYou.*` → „ALLE TESTS GRÜN“ |
| `CreateMaps.bat` | Legt `Map_Kepler7` an (nur nötig, wenn sie fehlt; `HC_FORCE=1` erzeugt sie neu) |
| `Package.bat` | Baut das fertige Spiel und packt `Build\Release\HowCouldItBeYou-Win64-v<Version>.zip` |
| `BuildLauncher.bat` | Baut `Build\Launcher\HowCouldItBeYouLauncher.exe` |
| `CollectLogs.bat` | Sammelt Logs als ZIP auf dem Desktop (für Fehlerberichte) |

## Spielen und testen

- **Im Editor:** `Game\HowCouldItBeYou.uproject` öffnen (startet in `Map_Lobby`) → oben *Play* → *Number of Players* 2–4, *Net Mode: Play As Listen Server*. Im Host-Fenster im Lobby-Menü „Runde starten“. Esc beendet im Editor das Spiel, das Menü liegt deshalb zusätzlich auf F1.
- **Ohne Editor:** über den Launcher oder `Build\Package\Windows\HowCouldItBeYou.exe`. Das Spiel startet im Hauptmenü.
- **Mit Freunden im selben Netzwerk:** einer klickt „Lobby hosten“ und nennt den Code (steht im Lobby-Menü), die anderen geben den Code oder die angezeigte IP ein und klicken „Beitreten“. Beim ersten Hosten fragt die Windows-Firewall, ob das Spiel im privaten Netzwerk kommunizieren darf: zulassen. Online über das Internet kommt mit EOS in M9.

## Neue Version veröffentlichen (Update für den Launcher)

1. In `Game\Config\DefaultGame.ini` die `ProjectVersion` erhöhen (z. B. `0.1.1`).
2. `Tools\Package.bat` → erzeugt `Build\Release\HowCouldItBeYou-Win64-v0.1.1.zip`.
3. Auf GitHub: *Releases* → *Draft a new release* → Tag `v0.1.1` → ZIP hochladen → *Publish release*.
4. Jeder Launcher zeigt beim nächsten Start bzw. Klick auf „Nach Updates suchen“ den Knopf **UPDATE**.

Das Repo muss öffentlich sein, damit der Launcher die Releases ohne Anmeldung sieht. Der Launcher liest `launcher.json` neben der `.exe` (Repo-Name, Name der ZIP, Installationsordner).

## Hinweis zum Pfad

Der Projektordner enthält ein `!` (`!CODING`). Unreals eigene Batch-Dateien verlieren dieses Zeichen. Die Skripte in `Tools/` umgehen das. Wenn im Editor seltsame Pfadfehler auftauchen, das Projekt in einen Ordner ohne `!` verschieben (Empfehlung aus `docs/06`: `C:\Dev\how-could-it-be-you`).
