# Website

Statische Seite für How Could It Be You (Englisch): Trailer, Rollen, beide Karten, Online-Spiel, Launcher-Download, Änderungen, Fragen. Kein Build-Schritt: `index.html` plus die Ordner `assets/`, `img/` und `media/` liegen so, wie sie ausgeliefert werden.

## Ansehen

Aus diesem Ordner einen beliebigen statischen Server starten, zum Beispiel:

```
python -m http.server 8080
```

und `http://localhost:8080/` öffnen. Direkt als Datei (`file://`) laufen die Module und das Video nicht überall.

## Was sich von selbst aktuell hält

- **Download-Knöpfe** zeigen auf `releases/latest/download/HCIBY-Launcher.zip` bzw. `HCIBY-Launcher-Linux.tar.gz`, also immer auf das neueste Release.
- **Änderungen:** Ein kleines Skript am Ende von `index.html` holt beim Laden die neuesten Releases über die GitHub-API und zeigt deren englischen Titel (die Zeile `## English – …` im Release-Text) sowie die Anzahl aller Releases. Schlägt die Anfrage fehl, bleibt die Liste stehen, wie sie im HTML steht.

Von Hand nachziehen, wenn sich etwas ändert: die Größen in den Download-Karten (Launcher 41,5 MB / 39,6 MB, Spiel etwa 780 MB), die Bilder des Launchers (`img/launcher-*.jpg`, erzeugt mit `HowCouldItBeYouLauncher.exe --selftest-screenshot <png> install|-` und `language.txt` = `en`).

## Bilder und Video

Alles stammt aus dem Spiel selbst:

| Datei | Herkunft |
|---|---|
| `img/hero-shifter.jpg`, `img/do-face.jpg`, `img/do-vote.jpg`, `img/online-lobby.jpg` | Bot-Runde mit v0.13.2, englische Oberfläche, 2560×1440 (FPS-Anzeige und Hinweis übermalt, lokale IP entfernt) |
| `img/kepler-corridor.jpg`, `img/do-fusebox.jpg`, `img/do-doors.jpg`, `img/trailer-poster.jpg` | `-HCTour` auf Kepler-7, 2560×1440, Qualität Episch |
| `img/ikarus-*.jpg` | `-HCTour -HCTourMap=Ikarus`, 2560×1440 |
| `img/launcher-*.jpg` | Launcher 0.5.0, `--selftest-screenshot` |
| `media/trailer.mp4`, `media/trailer-de.mp4` | Trailer ohne schwarze Balken (2,39:1), H.264, je etwa 13 MB |

Die kleineren Varianten (`*-800.jpg`, `*-1280.jpg` usw.) sind für Handys (`srcset`). Die Render-Skripte liegen in `Tools/WebsiteShots` (Anleitung in deren README.txt).

## Prüfen

Die Seite wurde mit dem Kit „taste-skill“ gebaut und geprüft (Layout an 1440, 1024, 820, 768 und 390 px, Text, Kontrast, Tastatur, Ladezeit). Prüf-Ergebnisse landen in `.taste-skill/` und `shots/`, beide sind in `.gitignore`.

## Veröffentlichen

Die Seite liegt auf GitHub Pages: **https://mniklas189-png.github.io/how-could-it-be-you/**

Pages liefert den Zweig `gh-pages` aus. Er enthält nur den Inhalt dieses Ordners, ohne eigene Vorgeschichte im Hauptzweig. Nach einer Änderung hier: committen, dann aus dem Repo-Stamm (Git Bash) den Ordner als neuen Stand von `gh-pages` hochladen, ohne etwas auszuchecken:

```
git fetch origin gh-pages
COMMIT=$(git commit-tree "$(git rev-parse HEAD:Website)" -p origin/gh-pages -m "Website")
git push origin "$COMMIT:refs/heads/gh-pages"
```

Nach etwa einer Minute ist der neue Stand online.

## Bewegung

`site.css` und `site.js` sind die eigene Bewegung der Seite, auf den Easing-Tokens des Kits: die Leuchtröhre im Hero flackert beim ersten Laden an, der Trailer hat einen großen Play-Knopf, Screenshots öffnen sich groß aus ihrem Vorschaubild (Esc oder Klick schließt), Bildgruppen kommen einmal beim Hineinscrollen, die Release-Liste blendet über, wenn die Live-Daten kommen. Bei „Bewegung reduzieren“ im System bleibt alles ruhig.
