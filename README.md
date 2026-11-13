# How Could It Be You

First-Person-Multiplayer-Horrorspiel (4–8 Spieler) in **Unreal Engine 5.8**: Eine Forscher-Crew auf einer Raumstation, und einer von ihnen ist ein formwandelndes Monster. Social Deduction wie *Among Us*, aber realistisch und dunkel, mit Proximity-Voice.

**Status:** Meilenstein **M7 – Licht, Sabotage & Atmosphäre** fertig (Stand 2026-11-12), Version 0.11.5. Die Kernschleife ist komplett spielbar: Aufgaben, Verwandeln, Kill, Melden, Diskutieren, Abstimmen, Auswerfen. Spieler und Monster haben jetzt echte Modelle (siehe „Modelle“). Planung unter [`docs/`](docs/README.md).

## Was schon geht

**Zweite Karte „Ikarus“, Kameras, Lageplan und neue Aufgaben (0.13.0)**
- Kartenwahl in der Lobby (Host): `World/HCMapCatalog` (`HCMaps`, Index 0 = Kepler-7), `AHCGameState::StationMap`, `AHCGameMode::SetStationMap`, gespeichert als Text-Option „StationMap“; Testflag `-HCMap=Ikarus`. Karte `/Game/HowCouldItBeYou/Maps/Ikarus/Map_Ikarus` (`Tools\CreateMaps.bat`), in `MapsToCook`
- Räume aus mehreren Teilen: `FHCSpace::ExtraRects` (L, T, Kreuz, gestufter Bug) und `Holes` (Reaktorkern, Container-Stapel, rundum Wand), dazu `LightColor`/`LightScale`; `FHCStationLayout::MakeIkarus()` richtet jeden Raum von Hand ein (`bAutoRoomProps` aus, `PlacedLights` mit `bPulse`). Neue Modelle in `SetupModels.py`: TankBig, PowerCells, Gate, Nitrogen, Truss, PipeValve, Beacon, Monolith
- Sicherheitskameras (`World/HCSecurityCamera`, lokal aus `Layout.Cameras`, SceneCapture nur, solange jemand hinschaut, rote Lampe blinkt bei Zuschauern) und Pult/Lageplan-Tisch (`World/HCMonitorDevices`: `AHCSecurityConsole`, `AHCAdminTable` zählt auf dem Server Lebende und Überreste pro Raum), Ansichten in `UI/HCMonitorWidgets`. Bildschirm-Material: `Widget3DPassThrough_Opaque` der Engine über `UHCModelSet::ScreenMaterial`. Test-Bilder: `-HCDeviceShots` (Host)
- Neue Aufgaben-Minispiele (Kabel, Schalter, Gedächtnis, Strom umleiten) mit Tipp nach langer Zeit und Lobby-Option „Aufgaben des Wandlers“. Die Raum-IDs der Aufgaben gibt es auf beiden Karten: jede neue Aufgabe braucht eine Station auf Kepler-7 **und** Ikarus (Tests `Tasks.StationsOnKepler7`, `Tasks.StationsOnIkarus`, `Layout.Ikarus.Valid`)
- Launcher 0.5.0: kleine Updates und Selbst-Update (siehe „Neue Version veröffentlichen“)

**Scharfes Bild und mehr Leistung auf schwächeren PCs (0.12.2)**
- Die Auflösung wird nie mehr gesenkt: Jede Grafikstufe rechnet mit 100 % (`Game/Config/DefaultScalability.ini` setzt `PerfIndexValues_ResolutionQuality` auf 100; die Engine nimmt sonst 50/71/87 %). Auf der Seite **Bild** gibt es die Zeilen „Auflösung“ und „Render-Auflösung“ nicht mehr. Vollbild und Randlos nutzen immer die Bildschirmauflösung, ein Fenster öffnet mit 4/5 der Bildschirmgröße und lässt sich frei ziehen. Alte Einstellungen mit weniger als 100 % stellt `UHCGraphicsSubsystem` (`Core/HCGraphics`) beim Start um
- Jede Stufe behält alle Effekte: Schatten der Taschenlampen, Lichtkegel im Nebel (Volumetric Fog), Vignette und Filmkorn (`r.Tonemapper.Quality` ≥ 4), Farbsaum der Jumpscares, Umgebungsverdeckung, Spiegelungen, scharfe Texturen. Die unteren Stufen rechnen nur einfacher: gröberes Nebelgitter, kleinere Schattenkarte, TAA statt TSR auf Niedrig und Mittel (`r.AntiAliasingMethod` setzt `HCGraphics` per Code, eine Scalability-Datei darf das nicht). Test: `HowCouldItBeYou.Graphics.LevelsKeepEffects`
- Gemessen auf einer RTX 4070 (1920×1080, GPU-Zeit pro Bild in der Station): Sehr hoch 13,4 ms, Hoch 11,3, Mittel 7,3, Niedrig 6,5
- Beim ersten Start misst das Hauptmenü ein paar Sekunden die GPU-Zeit (unteres Quartil von 180 Bildern) und wählt die Stufe (`HCGraphics::ChooseQualityLevel`). Eine selbst gewählte Stufe bleibt. Starts mit `-HC…`-Schaltern messen nicht, `-HCAutoQuality` erzwingt die Messung
- Nebenbei behoben: Bei einer frischen Installation stand bei der Grafikqualität „Gemischt“ statt „Sehr hoch“
- Außerdem in 0.12.2: weicher Übergang von der dunklen Menüseite in die Station im Hauptmenü; in der Lobby wählt der Host, ob die Meeting-Ergebnisse anonym sind („Abstimmung im Meeting“: OFFEN / ANONYM)

**Online mit Freunden in anderen Netzen (0.12.0, M9)**
- Der Join-Code funktioniert über das Internet: `Online/HCEos` spricht Epic Online Services direkt an (eigene Schicht über das EOS-SDK, kein `OnlineSubsystemEOS`, kein Plugin-Patch). Anmeldung per Device ID ohne Epic-Konto, EOS-Lobby mit `JOINCODE`, `BUILDVER` und `STATE`, Verbindung über `NetDriverEOS` (P2P mit NAT-Traversal und Relay von Epic, keine Portfreigabe nötig). Beim Beitreten sucht das Spiel online und im LAN gleichzeitig
- LAN-Code und IP-Adresse laufen weiter über den `IpNetDriver` wie bisher. Ohne Zugangsdaten oder wenn die Anmeldung bei Epic scheitert, bleibt es automatisch beim Heimnetz. Linux: nur LAN/IP, Epic liefert `SocketSubsystemEOS` nicht für Linux
- Zugangsdaten: die vier IDs in `Game/Config/DefaultEngine.ini` (`[EOSSDK.Platform.HCDev]`), das ClientSecret nur in der git-ignorierten `Game/Config/Windows/WindowsEngine.ini` (`Tools/SetupEOS.bat` fragt danach). Details in [docs/03](docs/03-multiplayer-und-backend.md) §6

**Startet auch auf PCs ohne Visual-C++-Laufzeit (0.11.9)**
- Auf einem PC ohne Visual-C++-Laufzeit brach das Spiel sofort mit „MSVCP140.dll wurde nicht gefunden“ ab, auch nach einer Neuinstallation. `-prereqs` legt nur `vc_redist.x64.exe` ins Paket (`Engine\Extras\Redist\en-us`), und niemand führt sie aus
- `Tools/Package.ps1` übergibt jetzt `-applocaldirectory="$(EngineDir)/Binaries/ThirdParty/AppLocalDependencies"`, denselben Schalter, den der Editor für „Include app-local prerequisites“ setzt (`IncludeAppLocalPrerequisites=True` in `DefaultGame.ini`). Unreal legt damit `msvcp140.dll`, `vcruntime140.dll` usw. neben `HowCouldItBeYou\Binaries\Win64\HowCouldItBeYou.exe`, und Windows lädt sie aus dem Programmordner. UAT liest die ini-Einstellung nicht, nur der Schalter zählt

**Beitritt mit Code findet die Lobby auch auf PCs mit virtuellen Netzwerkadaptern (0.11.8)**
- Der Code wird jetzt mit einer eigenen Suche im Netzwerk gefunden (`Online/HCLanDiscovery`, UDP-Port 14011). Die Suche der Engine (OnlineSubsystemNull) schickt Frage und Antwort an 255.255.255.255, und Windows schickt so ein Paket nur über einen Adapter hinaus: den mit der besten Metrik, oft einen virtuellen (Hyper-V „Default Switch“, VirtualBox, VPN). Die Antwort des Hosts kam dann nie im Heimnetz an, und der andere PC meldete „Keine Lobby mit diesem Code im Netzwerk gefunden“, obwohl beide dieselbe Version hatten
- Die Suche geht jetzt über jeden Adapter hinaus (je ein Socket, der an die Adresse des Adapters gebunden ist), und der Host antwortet direkt dem, der gefragt hat. Danach läuft der Beitritt wie über die IP-Adresse, mit Sprachchat (`HCVoiceOnly`-Sitzung). Die Sitzung des Hosts bleibt für den Sprachchat, ihr Engine-Beacon wird nicht mehr gebraucht
- Test: `HowCouldItBeYou.Online.LanSearchPackets` und `LanSearchSockets` (echte UDP-Sockets) in `Tests/HCMatchLogicTests.cpp`

**Sprachchat auch beim Beitritt über die IP-Adresse (0.11.7)**
- Wer über die IP-Adresse beitritt, hat jetzt auch Sprachchat. Ohne Online-Sitzung lässt die Engine den ganzen Sprachchat liegen (`FOnlineVoiceImpl::Tick` fragt `GetNumSessions() > 0`), und nur der Beitritt mit Code bringt eine mit. Wer über die Adresse kommt, bekommt deshalb eine eigene, nie angekündigte Sitzung auf dem eigenen Rechner (`UHCSessionSubsystem::CreateVoiceOnlySession`, Name `HCVoiceOnly`), die beim Verlassen oder bei einem Verbindungsfehler wieder verschwindet
- „… spricht“ unten links zeigt jetzt auch nach einem Kartenwechsel alle, die reden: Der Sprech-Zustand wird für das ganze Spiel geführt (`HCVoicePlayback::TalkingNow`), nicht mehr je Karte, sonst ging die Meldung „spricht“ verloren, wenn sie genau während des Wechsels kam
- Technik: Die Farben von Story- und Rollen-Bildschirm heißen jetzt `IntroInk`, `CardInk` usw. Ein Unity-Build kann `HCHUD.cpp`, `HCIntroWidget.cpp` und `HCRoleRevealWidget.cpp` in eine Datei legen, und dann stießen die gleich benannten Konstanten zusammen

**Sprachchat wieder an, der Host wählt, wann ihr euch hört (0.11.6)**
- Der **Sprachchat** ist wieder eingeschaltet (`bHasVoiceEnabled=true`). Der Absturz mit drei und mehr Spielern kam vom Kartenwechsel Lobby ↔ Station: Die Wiedergabe einer Stimme, die gerade lief, blieb an der alten, schon abgebauten Welt hängen. `UHCVoiceSubsystem` gibt die Stimmen einer Welt jetzt vor ihrem Abbau frei (`HCVoicePlayback`). Getestet mit 4 und 6 Spielern, die durch beide Wechsel hindurch reden
- **Lobby → Sprachchat:** Der Host wählt *Meetings* (in der Runde nur in Meetings, die Toten reden untereinander), *Nähe* (wer in der Nähe ist, im Meeting alle) oder *Immer* (alle überall gleich laut); alle anderen sehen die Wahl. Die Mute-Listen des Hosts setzen das durch, das eigene Mikrofon bleibt zu, solange niemand zuhören kann. Die Lebenden hören die Toten in keiner Einstellung. Die Wahl bleibt für die nächste Lobby
- „… spricht“ unten links zeigt nur noch, wer gerade wirklich zu hören ist (über die Sprech-Ereignisse der Voice-Schnittstelle; `IsRemotePlayerTalking` bleibt nach dem ersten Wort wahr)
- Test: `-HCVoiceRule=0|1|2` beim Host, `-HCVoiceTest` hält das Mikrofon offen und schreibt ins Log, wer gehört wird; `Tests/HCVoiceRuleTests`

**Meeting und Aufgaben im Look des Spiels, Abstimmen in zwei Schritten (0.11.5)**
- Der **Meeting-Bildschirm** ist neu gezeichnet (Tintenschwarz, Knochenweiß, Blutrot, wie Launcher und Benutzen-Hinweis): Die Phase steht groß oben (Versammlung, Diskussion, Abstimmung, Ergebnis), daneben läuft die Zeit ab, in den letzten zehn Sekunden rot. Jeder Spieler hat eine Karte mit Anzugfarbe, Symbol und Namen; man sieht, wer schon gewählt hat. Im Ergebnis fallen die Stimmen als Farbquadrate auf die Karten (wer hat wen gewählt), die Karte des Ausgeworfenen wird rot. Der Chat passt dazu: Name in der Anzugfarbe, Text darunter
- **Abstimmen in zwei Schritten**, weil die Stimme endgültig ist: Karte anklicken (oder „Überspringen“), dann „Stimme abgeben“. Ein zweiter Klick auf dieselbe Karte nimmt die Auswahl zurück
- Die **Aufgaben-Menüs** haben gezeichnete Instrumente statt grauer Regler und Knöpfe: Ventile zeigen den Bereich, den man treffen muss; das Antennen-Signal läuft über ein Oszilloskop; die Kühlung hat eine Anzeige mit markiertem grünen Bereich; Felder und Kisten füllen sich, während sie eingelesen werden, die Reihenfolge der Kisten steht als Kette darüber; das Haltefeld ist ein Ring, der sich schließt; die Übertragung zeigt ihre Pakete. Der Fortschritt ist nur grün, solange es läuft. Regeln und Zeiten der Aufgaben sind unverändert
- **Aufgabenliste und Crew-Balken** im Stil des Benutzen-Hinweises: Erledigtes ist abgehakt und durchgestrichen, rechts steht der Raum, bei wartenden Schritten die Sekunden. Während eines Meetings scheint die Anzeige nicht mehr durch den Meeting-Bildschirm
- Technik: gezeichnete Bedienelemente in `UI/HCLook` (`UHCPaintBox`, `FHCDraw`). Test: `-HCMeetingPick` markiert im Meeting eine Karte für Bilder des Stimmzettels

**Geschichte zum Rundenstart, neue Rollenkarte, neuer Benutzen-Hinweis (0.11.4)**
- Jede Runde beginnt mit der Geschichte von Kepler-7, Seite für Seite getippt wie ein Notfallprotokoll: die Probe aus dem Eis von Europa, die Nacht im Bio-Labor, warum ihr euch in der Kommandozentrale eingeschlossen habt und was jetzt zu tun ist. Zum Schluss: „Es ist einer von euch.“ In der ersten Runde einer Session kommt die ganze Geschichte (36 s), danach nur noch die Lage (12,5 s)
- E halten = für Überspringen stimmen. Will mehr als die Hälfte überspringen, geht es für alle weiter. Alle sehen, wie viele schon überspringen wollen
- Die Rollen-Anzeige ist neu: CREW oder DER WANDLER groß in der Rollenfarbe, eine Ausweiskarte mit deinem Symbol, Namen und deiner Farbe, drei Ziele mit deinen eigenen Tasten und ein Tipp zur Rolle. Sie bleibt 7 s statt 5 s, damit man alles lesen kann
- Der Hinweis zum Benutzen hat den Look von Launcher und Trailer: knochenweiße Großbuchstaben auf einem weichen dunklen Band, eine schmale Taste, die sich beim Halten füllt. Melden und Notfall-Knopf sind blutrot, Geister geisterblau, Gesperrtes grau. Der Hinweis blendet weich ein

**Lampen knistern hörbar (0.11.3)**
- Die kaputten, flackernden Deckenlampen sind jetzt gut zu hören: Zwischen dem Flackern knistern sie etwa so laut wie die Lüftung summt, beim Flackern deutlich lauter, im ganzen Raum der Lampe. Vorher ging das Knistern zwischen den Schüben im Summen der Lüftung unter
- Die Aufnahme ist gleichmäßiger (keine fast stillen Stellen mehr). Lampen außer Hörweite belegen keinen Tonkanal mehr und knistern trotzdem nie im Gleichtakt

**Taschenlampe in der Hand, Schalter-Klick, Geräusche der Station (0.11.2)**
- Jeder hält jetzt eine echte Taschenlampe: die eigene unten rechts im Bild (schwingt beim Umsehen leicht nach und wippt beim Gehen), die der anderen in ihrer rechten Hand. Der Lichtkegel kommt aus der Linse, die Linse leuchtet, solange die Lampe an ist. An einer Wand zieht die Hand die Lampe zurück
- Vor einer Wand blendet die Lampe nicht mehr: Unter 1,5 m Abstand wird der Kegel schwächer, statt eine grellweiße Scheibe zu werfen
- Die Lampe klickt beim Ein- und Ausschalten, auch mit leerem Akku. Andere hören den Klick bis etwa 9 m weit
- Überall auf der Station summt leise die Lüftung, und die kaputten, flackernden Deckenlampen knistern (laut während sie flackern, leise dazwischen, still ohne Strom)

**Schrei-Mischung (0.11.1)**
- Bei jedem Kill sind beide aufgenommenen Schreie gleichzeitig übereinander zu hören, statt nur einer von beiden. Das ist Absicht und soll so bleiben

**Eigene Tasten, Mikrofon und Headset, Deutsch oder Englisch (0.11.0)**
- **Einstellungen → Steuerung**: Jede Aktion (Laufen, Benutzen, Taschenlampe, Töten, Verwandeln, Karte, Aufgabenliste, Geist hoch/runter, Sprechen …) auf eine eigene Taste oder Maustaste legen: Taste anklicken, neue Taste drücken, Esc bricht ab. Hat eine andere Aktion die Taste schon, tauschen beide. „Standard-Tasten“ setzt alles zurück; Esc/F1 bleibt das Pausemenü. Alle Hinweise im Spiel („[E] Benutzen“, „VERWANDELN [Q]“ …) zeigen die eigenen Tasten
- **Einstellungen → Sprach-Chat**: Mikrofon und Ausgabe (Headset/Lautsprecher) wählen, ein Pegel zeigt, was das Mikrofon hört. Die Ausgabe gilt für den ganzen Ton des Spiels. Solange der Sprach-Chat abgeschaltet ist (`bHasVoiceEnabled=false`), zeigt die Seite nur Mikrofon, Pegel und Ausgabe
- **Einstellungen → Allgemein → Sprache**: Deutsch oder English, gilt sofort. Ohne eigene Wahl: Deutsch auf deutschem Windows, sonst Englisch (wie der Launcher). Test: `-HCLanguage=en`

**Ecken und Türen (0.10.1)**
- **Innenecken**: Zwei Wände laufen nicht mehr ineinander. Die eine endet vor dem Rahmen der anderen, Rahmen und Sockelleisten kreuzen sich nicht mehr (z. B. in den Ecken der Lobby)
- **Türen**: Die Türflügel aus 0.10.0 waren so dick wie die Wand und ragten beim Öffnen über die Wand hinaus – die großen Wandflächen liegen weit hinten, nur Rahmen und Leisten stehen vor. Jetzt sind die Flügel wieder schmal und verschwinden ganz in der Wand; geschlossen sitzt die Tür eingerahmt in der Wand, ohne dunklen Spalt daneben

**Meeting-Chat (0.10.0)**
- Rechts im Meeting-Bildschirm steht ein **Chat** für das ganze Meeting, vom Versammeln bis zum Ergebnis. **Enter** öffnet das Eingabefeld, **Enter** sendet, **Esc** verlässt das Feld (ein zweites Esc öffnet das Menü). Jede Zeile zeigt Farbe und Namen; höchstens 120 Zeichen, und nicht schneller als eine Zeile pro 0,8 s
- Der Chat bleibt die ganze Runde über erhalten; jedes Meeting beginnt mit einer Trennlinie „— MEETING 2 —“. Eine neue Runde beginnt mit leerem Chat
- **Geister** schreiben nur für Geister: Ihre Zeilen (grau, „(Geist)“) erreichen den PC eines Lebenden gar nicht erst. Geister lesen alles mit
- Test-Bots: `-HCAutoChat=<Text>` schreibt in jedem Meeting eine Zeile (`{name}` = eigener Name); jeder PC schreibt empfangene Zeilen ins Log („Chat in:“)

**Alles steht vor der Wand (0.10.0)**
- Die echten Wandteile stehen rund 38 cm vor der alten Greybox-Wand. Aufgaben-Geräte und Sicherungskästen standen deshalb halb in der Wand, ihre Beschriftung teils hinter den Wandrahmen. Jetzt rücken sie vor die Wand; Kisten, Fässer und Lampen bleiben ebenfalls frei davon
- **Deckenlampen** hängen mitten in den Deckenplatten statt quer über den Trägern; ein Raum mit mehr Lampen bekommt schwächere, insgesamt ist er so hell wie vorher
- **Außenecken**, wo Gänge zusammentreffen, sind geschlossen statt offen mit abgeschnittenen Wandenden
- **Türen** sitzen bündig in der Wand statt tief in einem dunklen Schacht, und gleiten weiter ganz in die Wand
- **Lobby**: dieselben Gegenstände wie bisher, aber von Hand und ungleichmäßig verteilt statt in allen vier Ecken dieselbe Gruppe
- Prüfen ohne Runde: `-HCTour=x,y,z,yaw,pitch;...` fotografiert die Station, mit `-HCTourMap=Lobby` die Lobby

**Raumnamen, Stromausfall-Zeichen, lesbare Interaktions-Anzeige (0.9.1)**
- **Interaktions-Anzeige**: „E“ als gelbe Taste auf einer dunklen Tafel mit leuchtendem Rand, Text weiß – auch vor hellen Lampen lesbar; der Halte-Fortschritt läuft direkt darunter
- **Mini-Karte**: größer, mit den Namen aller Räume; darunter steht, wo du gerade bist („ORT: LAGER“)
- **Stromausfall**: In jedem Raum des dunklen Sektors blinkt auf Mini-Karte und Stationsplan ein Blitz; der Sicherungskasten, an dem der Strom zurückkommt, ist orange eingerahmt
- Das Türgeräusch ist kürzer

**Wahre Gestalt und flackerndes Licht (0.9.0)**
- **Wahre Gestalt** (Taste **R**, nur der Wandler): Er zeigt sich 20 Sekunden lang als Monster – 40 % schneller und ohne außer Atem zu kommen. Dafür sieht jeder das Monster-Modell, und über ihm steht sein echter Name in Rot. Danach 60 Sekunden Pause; Verkleidung (Q) und wahre Gestalt teilen sich diese Pause. Ein Meeting beendet die wahre Gestalt sofort. Zeiten und Tempo stehen in `DefaultGame.ini` (`TrueFormDuration`, `TrueFormCooldown`, `TrueFormSpeedFactor`)
- **Licht**: Die Deckenlampen sind deutlich dunkler, und etwa jede dritte flackert in unregelmäßigen Schüben

**Echte Sounds (0.8.1)**
- Beim Kill sind beide aufgenommenen Schreie gleichzeitig übereinander zu hören (sehr laut), Türen haben ein aufgenommenes Öffnungsgeräusch. Die Aufnahmen stammen von Pixabay und liegen wie die Modelle nur im fertigen Spiel, nicht im Repository
- Ein Sound kann mehrere Aufnahmen haben (`Scream.wav`, `Scream2.wav`, `Scream3.wav`); das Spiel wechselt zwischen ihnen

**Echte Texturen, Sieg-Bildschirm, Einstellungen, Leiche (0.8.0)**
- **Texturen**: Im fertigen Spiel wurden Station und Objekte bisher mit grauem Ersatzmaterial gezeichnet – den Materialien der Pakete fehlte die Freigabe für instanzierte Meshes (der Editor repariert das unbemerkt selbst). `Tools\SetupModels.bat` setzt sie jetzt. Die Station ist dunkles, glänzendes Metall mit roten Zierleisten; Lampen und Belichtung sind darauf neu abgestimmt
- **Sieg-Bildschirm**: „DIE FORSCHER GEWINNEN“ oder „DAS MONSTER GEWINNT“ in der Farbe der Siegerseite, darüber dein eigenes Ergebnis (Sieg oder Niederlage), darunter wer wer war. Gewinner hören drei helle Glockentöne, Verlierer den Schock-Akkord
- **Einstellungen** (Hauptmenü und Pause-Menü): Mausempfindlichkeit, Sichtfeld, Grafikqualität, Fenster/Randlos/Vollbild, VSync, Schock-Effekte. Auf der Seite **Ton**: Gesamtlautstärke und getrennte Lautstärken für Geräusche, Schreck (Jumpscares, Schreie), Umgebung (Lüftung, Lampen) und Signale – beim Verstellen hört man eine kurze Probe – sowie der Ton, solange das Spielfenster im Hintergrund ist. Alles gilt sofort und wird gespeichert
- **Leiche**: Am Tatort liegt jetzt der Körper des Opfers in seiner Anzugfarbe in der Blutlache. Wer in der Nähe ist und freie Sicht hat, sieht „[E] Leiche melden“ – das ruft sofort ein Meeting ein

**Türen, Blut und Schrei (0.7.5)**
- **Türen** teilen sich in der Mitte und fahren nach links und rechts in die Wand, mit Zischen und Anschlag – jeder in der Nähe hört, dass eine Tür aufgeht
- **Blut**: Am Tatort liegt statt der roten Scheibe eine Blutlache mit Spritzern (drei gezeichnete Varianten aus `Tools/Setup/MakeBlood.py`, als Decal auf dem Boden, nass glänzend im Lampenlicht)
- **Schrei**: Das Opfer schreit deutlich lauter und menschlicher; man hört es jetzt bis etwa 30 m weit
- **Echte Sounds einsetzen**: WAV-Dateien in den Ordner [`Sounds/`](Sounds/LIES-MICH.txt) legen (z. B. `Scream.wav`), `Tools\SetupSounds.bat` ausführen – sie ersetzen die eingebauten Platzhalter. Gekaufte Aufnahmen liegen nie im Repository

**Belebte Station (0.7.4)**
- **Echte Geräte statt grauer Blöcke**: Aufgaben-Stationen sind Server-Schränke und Terminals, Ladestationen Batterie-Einheiten, Sicherungskästen echte Trennschalter, der Notfall-Knopf sitzt auf einer Maschine, und die Türen sind rote Doppeltüren mit Fenster. Die einfachen Blöcke bleiben unsichtbar als Kollision darunter – am Verhalten ändert sich nichts
- **Herumstehendes**: Fässer, Kisten, Paletten, Gasflaschen, Kabeltrommeln, Werkzeug und lose Kabel in Raumecken und an Gangwänden (`AHCStationBuilder::BuildProps`), jede Gruppe mit einer Stehlampe, die am Strom ihres Sektors hängt. Alles wird aus dem Grundriss berechnet, steht nie vor Türen oder Geräten und ist massiv
- **Karten zeigen deine Aufgaben**: gelbe Punkte auf Mini-Karte und Stationsplan (blass, solange ein Schritt noch wartet) – nur deine eigenen, keine Mitspieler
- **Lobby**: Klick auf Join-Code oder IP kopiert sie in die Zwischenablage
- **Hauptmenü**: statt Schwarz ein Raum der Station mit vier Gestalten im roten Licht; Eingabefelder sind dunkel mit heller Schrift

**Aufgaben-Menüs und Karte (0.7.3)**
- **Jede Aufgabe öffnet ein Menü**: ein gerahmter Geräte-Bildschirm in der Bildmitte, nie bildschirmfüllend – du siehst weiter, wer hinter dir auftaucht. Auch die früheren „E halten“-Aufgaben haben jetzt eines:
  - **Feld halten** (Ausweis registrieren, Probe entnehmen, Zentrifuge starten): Feld mit Maus oder Leertaste gedrückt halten
  - **Zahlenfelder** (Kern kalibrieren mit 10 Feldern, Probe auswerten mit 4): verstreute Felder in aufsteigender Reihenfolge antippen; ein falsches Feld heißt von vorn
  - **Übertragung** (Funkdaten laden und senden): starten und am Terminal bleiben
  - **Arbeitsschritte** (Luftfilter wechseln): vier Schritte in der richtigen Reihenfolge
  - Ventile, Signalkurve, Kühlung und Kisten-Scan wie bisher, im neuen Rahmen
- **Mini-Karte** oben rechts: Räume und Gänge von Kepler-7 und dein eigener Standort mit Blickrichtung, seit 0.7.4 auch deine offenen Aufgaben. Keine Mitspieler, keine Überreste
- **Stationsplan** (Taste **M**): derselbe Plan groß mit Raumnamen und eigenem Standort, darunter der Strom der Sektoren. Ein Sektor ohne Strom ist dunkelrot. Der Wandler sabotiert weiterhin von hier aus

**Modelle**
- **Spieler**: realistische Figur mit Geh- und Lauf-Animation statt des Zylinders. Jacke und Rucksack tragen die Spielerfarbe, der **Name schwebt über dem Kopf** und dreht sich immer zum Betrachter. Die Verkleidung des Wandlers kopiert Farbe und Namen. Das Namensschild leuchtet nicht selbst: Das Helmlicht der Figur macht es aus der Nähe lesbar, im Dunkeln auf Entfernung nicht (weicht bewusst vom Design-Dokument ab, das keine Namen über Köpfen vorsah)
- **Wandler**: Im Jumpscare greift jetzt das Monster-Modell nach dir, sein Schädel direkt vor der Kamera
- Die Modelle stammen aus kostenlosen Fab-Paketen (*Survival Character* von Arberry, *Whisper* von Leks, *Modular SciFi* samt *Engineer*- und *Props*-Teil für Station und Objekte, *Sci-Fi Light* für Lampen und Geräte, *Flash Light* von Slav3106 für die Taschenlampe). Sie liegen **nicht in diesem Repository** (Lizenz), sondern nur im fertigen Spiel. Wer selbst baut: die Pakete im Epic Games Launcher zum Projekt hinzufügen, dann `Tools\SetupModels.bat` ausführen. Ohne die Pakete läuft das Spiel mit den alten Platzhalter-Figuren. *Flash Light* gibt es auf Fab nur als FBX: Der Launcher legt es in seinen Download-Ordner (`C:\ProgramData\Epic\EpicGamesLauncher\VaultCache\FabLibrary`), `Tools\SetupModels.bat` holt es von dort.
- **Station**: Böden, Decken, Wände mit Sockelleiste und Deckenlampen stammen aus dem Bausatz *Modular SciFi* (ModSciInteriors). Sie werden im Code über den vorhandenen Grundriss gelegt (`AHCStationBuilder::BuildDressing`); Laufwege, Sichtlinien und Türen nutzen weiter die einfachen Greybox-Formen darunter
- Noch Platzhalter: Geister und Überreste. Zeugen eines Kills sehen noch keine Angriffs-Animation

**M7 – Licht, Sabotage & Atmosphäre**
- **Stationsplan** (Taste **M**, für alle): zeigt, welche Sektoren Strom haben. Der Wandler sabotiert aus genau diesem Plan heraus – von außen sieht Sabotieren aus wie Kartenlesen
- **Stromausfall** (Sektor Nord, Ost oder Süd-West): Die Lichter flackern 1 s und gehen aus, rote Notbeleuchtung geht an. Ansage und Alarmton für alle, ohne Hinweis auf den Auslöser. Die Kommandozentrale fällt nie aus
- **Reparieren**: am gelben Sicherungskasten im Gang des Sektors E 4 s halten (seine Lampe blinkt bei Ausfall rot). Jeder Lebende darf, auch der Wandler. Sonst kommt nach 90 s der Notstrom von selbst. Danach 45 s Abklingzeit
- **Tür verriegeln** (6 markierte Türen): Die Tür schließt, wird rot und bleibt höchstens 10 s zu, dann 30 s Abklingzeit. Sie schließt nie auf einem Spieler (steht 2 s jemand im Rahmen, schlägt die Sabotage fehl). **Notentriegelung**: an der Tür E 3 s halten. Ein Meeting öffnet jede verriegelte Tür sofort
- Regeln: keine Sabotage in den ersten 30 s der Runde und nach jedem Meeting, höchstens ein dunkler Sektor und eine verriegelte Tür gleichzeitig, nie die Türen der Kommandozentrale. Der Map-Check garantiert, dass auch mit allen sechs Türen zu jeder Raum erreichbar bleibt
- **Unbehagen**: Allein im Dunkeln wird der Bildrand dunkler und ein leiser Herzschlag setzt ein. Das hängt nur von Licht und sichtbarer Gesellschaft ab, nie davon, ob der Wandler in der Nähe ist (per Test abgesichert)
- **Dunst**: dünner Nebel, damit Taschenlampe und Notlicht einen sichtbaren Kegel bekommen
- Platzhalter: Der Plan ist eine Liste statt einer Karte, der Sicherungskasten ein Halte-Knopf statt drei Schaltern. Noch nicht drin: Kameras, Türprotokoll, Grafik-Stufen, Stations-Brummen

**M6 – Formwandel**
- **Verwandeln** (nur der Wandler, Taste **Q**): Menü mit allen lebenden Mitspielern, Auswahl per Klick oder Zifferntaste. Die Verwandlung dauert 1,5 s, der Wandler steht dabei still und kann weder töten noch etwas benutzen
- **Kopiert wird** die Anzugfarbe und das **Namensschild** (Brust und Rücken). Alle Mitspieler sehen die Verkleidung; nur der Wandler sieht unten rechts „ALS *NAME* 31 s“
- **Namensschilder**: Jede Figur trägt ihren Namen über dem Kopf (seit 0.7.2; vorher auf Brust und Rücken). Sie leuchten nicht selbst
- **Dauer** 45 s, dann automatische Rückverwandlung (wieder 1,5 s); vorher mit **Q** beenden. Danach 30 s Abklingzeit; auch bei Rundenstart und nach jedem Meeting
- **Spuren**: Jede Verwandlung und Rückverwandlung macht ein nasses Knack-Geräusch (ca. 8 m hörbar), ein kurzes grünes Aufleuchten und hinterlässt 60 s lang eine Schleimpfütze. Verschickt wird nur der Ort, nie wer es war
- **Töten in fremder Gestalt** ist erlaubt: Zeugen sehen dann „Mia“ töten
- **Meeting-Start** beendet jede Verkleidung sofort und ohne Effekt; im Meeting sind alle sie selbst
- Stirbt das Ziel während der 1,5 s, bricht die Verwandlung ohne Abklingzeit ab; stirbt es später, bleibt die Verkleidung („Toter, der herumläuft“). Verlässt das Ziel das Spiel, verwandelt sich der Wandler zurück
- Platzhalter: Das Menü ist eine Liste statt des geplanten Radialmenüs, der Ton ist im Code erzeugt

**M5 – Meetings, Abstimmung, Auswurf**
- **Meeting auslösen**: Überreste melden (E) oder den roten **Notfall-Knopf** in der Mitte der Kommandozentrale drücken (1× pro Spieler und Runde, nach jedem Meeting 20 s gesperrt). Lösen zwei gleichzeitig aus, gewinnt der erste; der zweite behält seinen Knopfdruck. Läuft gerade ein Kill, wartet das Meeting kurz, bis er durch ist
- **Versammeln** (3 s): Alle Lebenden werden auf feste Plätze im Kreis um den Knopf gesetzt und können sich nicht bewegen (auch der Server lässt es nicht zu), Aufgaben brechen ab, alle Überreste verschwinden
- **Diskussion** (60 s) → **Abstimmung** (45 s, endet sofort, wenn alle gewählt haben): Meeting-Bildschirm mit einer Zeile pro Spieler, „Wählen“ oder „Überspringen“. Die Stimme ist endgültig; man sieht, wer schon gewählt hat, aber nicht wen
- **Ergebnis** (5 s): Stimmen pro Person mit den Farben der Wähler. Ausgeworfen wird nur, wer allein vorn liegt **und** mehr Stimmen hat als „Überspringen“ (Nichtwähler zählen als Überspringen). Jeder Gleichstand: niemand
- **Auswurf** (8 s): Texttafel „*Name* war der Wandler.“ / „*Name* war nicht der Wandler.“. Der Ausgeworfene wird Geist. Wandler ausgeworfen → Crew gewinnt; falscher Auswurf bis zum Gleichstand → Wandler gewinnt
- Danach geht die Runde von den Plätzen aus weiter, die Kill-Abklingzeit beginnt neu
- Verbindungsabbruch im Meeting: Die Stimme fällt weg; wer den Gegangenen gewählt hatte, darf neu wählen
- Geister sehen das Meeting, stimmen aber nicht ab

**M4 – Kill, Überreste, Geister, Jumpscare**
- **Töten** (nur der Wandler, linke Maustaste): Ziel im Fadenkreuz, höchstens 2 m, freie Sicht, 30 s Abklingzeit, erster Kill frühestens nach 20 s, nie in der Kommandozentrale. Der Server prüft alles. Der Wandler sieht unten rechts „TÖTEN“ mit Abklingzeit und einen roten Rahmen ums Fadenkreuz, wenn ein Ziel in Reichweite ist
- **Jumpscare** nur beim Opfer: Die Kamera wird zum Angreifer gerissen, das Gesicht des Wandlers schießt auf dich zu, Stinger, Blitz, Schwarz, dann „Du bist tot“. Zeugen sehen nur den Ruck der Figur und hören einen Schrei. Monster und Opfer sind 2,5 s gesperrt
- **Schreck-Intensität** Voll / Reduziert / Minimal: im Hauptmenü, in der Warnung beim ersten Start und im Esc-Menü
- **Überreste**: Lache und ID-Marke mit dem Namen des Opfers, ohne Kollision. Lebende melden sie mit E aus bis zu 2,5 m (hat Vorrang vor Stationen). Das Melden startet ein Meeting (M5)
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
| `hc.Sabotage Power <Nord\|Ost\|SW>` / `hc.Sabotage Door <TürId>` / `hc.Sabotage Repair` | Stromausfall, Türsperre oder Reparatur auslösen, ohne Rollen- und Zeitregeln |
| `hc.GotoFuseBox <Nord\|Ost\|SW>` | Stellt dich vor einen Sicherungskasten |
| `hc.StartMeeting` | Sofort ein Meeting starten (wie der Notfall-Knopf, ohne ihn zu verbrauchen) |
| `hc.GotoButton` | Stellt dich vor den Notfall-Knopf |
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
| `Launcher/` | Launcher für Windows und Linux (C# / Avalonia, .NET 9; `Launcher/Linux/` = Anleitung und Menü-Skript für Linux) |
| `Tools/` | Skripte zum Doppelklicken (siehe unten) |
| `docs/` | Planungsdokumente |

## Skripte (`Tools/`)

Einmalig: `Tools\local.example.bat` nach `Tools\local.bat` kopieren (Engine-Pfad, Standard `C:\Program Files\Epic Games\UE_5.8`).

| Skript | Was es tut |
|---|---|
| `Build.bat` | Kompiliert den C++-Code (Editor schließen!) → „BUILD ERFOLGREICH“ |
| `RunTests.bat` | Baut und startet alle Tests `HowCouldItBeYou.*` → „ALLE TESTS GRÜN“ |
| `CreateMaps.bat` | Legt `Map_Kepler7` an (nur nötig, wenn sie fehlt; `HC_FORCE=1` erzeugt sie neu) |
| `Package.bat` | Baut das fertige Spiel und packt `Build\Release\HowCouldItBeYou-Win64-v<Version>.zip`, dazu `.manifest.json` und `.chunks` für kleine Updates |
| `MakePatch.ps1` | Nur die Dateien für kleine Updates zu einer fertigen ZIP (`-Zip <zip> -Platform Win64\|Linux`); `Package.ps1` ruft es selbst auf |
| `PackageLinux.bat` | Dasselbe für Linux → `Build\Release\HowCouldItBeYou-Linux-v<Version>.zip` (Voraussetzungen siehe „Linux“) |
| `BuildLauncher.bat` | Baut `Build\Launcher\HowCouldItBeYouLauncher.exe` |
| `BuildLauncherLinux.bat` | Baut den Linux-Launcher: Ordner `Build\LauncherLinux\` und `Build\Release\HCIBY-Launcher-Linux.tar.gz` |
| `PackLauncher.ps1` | Launcher-Dateien fürs Release aus sauberen Builds: `HCIBY-Launcher.zip`, `HCIBY-Launcher-Linux.tar.gz` und `HCIBY-Launcher.json` (Version + SHA-256, daraus aktualisieren sich installierte Launcher selbst) |
| `CollectLogs.bat` | Sammelt Logs als ZIP auf dem Desktop (für Fehlerberichte) |
| `Localize.bat` | Sammelt alle `NSLOCTEXT`-Texte des Codes und baut die englischen Texte (`Game\Content\Localization\Game`). Neue Texte: Englisch in `Tools\Setup\english.py` eintragen, `Localize.bat`, `python -I Tools\Setup\FillEnglish.py`, `Localize.bat` |

## Spielen und testen

- **Im Editor:** `Game\HowCouldItBeYou.uproject` öffnen (startet in `Map_Lobby`) → oben *Play* → *Number of Players* 2–4, *Net Mode: Play As Listen Server*. Im Host-Fenster im Lobby-Menü „Runde starten“. Esc beendet im Editor das Spiel, das Menü liegt deshalb zusätzlich auf F1.
- **Ohne Editor:** über den Launcher oder `Build\Package\Windows\HowCouldItBeYou.exe`. Das Spiel startet im Hauptmenü.
- **Mit Freunden im selben Netzwerk:** einer klickt „Lobby hosten“ und nennt den Code (steht im Lobby-Menü), die anderen geben den Code oder die angezeigte IP ein und klicken „Beitreten“. Beim ersten Hosten fragt die Windows-Firewall, ob das Spiel im privaten Netzwerk kommunizieren darf: zulassen. Seit 0.12.0 klappt der Code auch über das Internet (Epic Online Services, unter Windows).

## Neue Version veröffentlichen (Update für den Launcher)

1. In `Game\Config\DefaultGame.ini` die `ProjectVersion` erhöhen (z. B. `0.1.1`).
2. `Tools\Package.bat` → erzeugt `Build\Release\HowCouldItBeYou-Win64-v0.1.1.zip` samt `HowCouldItBeYou-Win64-v0.1.1.manifest.json` und `.chunks`, `Tools\PackageLinux.bat` dasselbe mit `Linux`.
3. `Tools\PackLauncher.ps1` → `HCIBY-Launcher.zip`, `HCIBY-Launcher-Linux.tar.gz`, `HCIBY-Launcher.json` (Launcher für neue Spieler und für das Selbst-Update).
4. Auf GitHub: *Releases* → *Draft a new release* → Tag `v0.1.1` → für jedes System ZIP, `.manifest.json` und `.chunks`, dazu die drei Launcher-Dateien ins selbe Release hochladen → *Publish release*.
5. Jeder Launcher zeigt beim nächsten Start bzw. Klick auf „Nach Updates suchen“ den Knopf **UPDATE**. Der Windows-Launcher nimmt die Dateien mit `Win64` im Namen, der Linux-Launcher die mit `Linux`. Fehlt in einem Release die Linux-ZIP, bleibt der Linux-Launcher beim letzten Release, das eine hat.

**Kleine Updates (ab Launcher 0.5.0):** Ist das Spiel schon installiert, lädt der Launcher nur, was sich geändert hat, statt der ganzen ZIP (~700 MB). Die Spieldateien sind in Stücke („Chunks“, ~64 KB, an inhaltsabhängigen Stellen geschnitten) zerlegt; was der Spieler schon hat, wird aus seinen Dateien kopiert, der Rest kommt per HTTP-Range aus den `.chunks`-Dateien der Releases. Programm und `.pdb` ändern sich mit jedem Build fast überall (verschobene Adressen), dafür gibt es Binär-Patches (bsdiff-Verfahren) von den letzten Versionen. Gemessen: 0.11.7 → 0.11.8 lädt 47 MB, 0.11.8 → 0.12.0 60 MB, 0.11.7 → 0.12.0 107 MB. Der UPDATE-Knopf zeigt die Größe, `update.log` neben dem Launcher, was passiert ist. Klappt etwas nicht, bevor eine Datei ersetzt wurde (Release ohne Manifest, Download- oder Prüffehler), nimmt der Launcher die ZIP; das Spiel bleibt in jedem Fall bei der alten Version, bis die neue komplett ist. Ordner namens `Saved` (Einstellungen, Logs) lässt ein kleines Update unangetastet.
- `Tools\MakePatch.ps1` (von `Package.ps1` aufgerufen, ~6 Minuten) holt das Manifest des letzten Releases von GitHub und legt in die `.chunks` nur, was neu ist; die Patches rechnet es gegen die ZIP der Vorversion in `Build\Release`. Beim ersten Release mit kleinen Updates (noch kein Manifest auf GitHub) kommt einmal das ganze Spiel in die `.chunks` (~630 MB).
- **Alte Releases nie löschen:** spätere Manifeste verweisen auf ihre `.chunks`. Fehlt eine, nimmt der Launcher die ZIP.
- Code: `Tools\PatchTool\` (Release-Seite, `Shared\` wird auch in den Launcher kompiliert), `Launcher\Patching\PatchUpdater.cs`. Test ohne GitHub: `HowCouldItBeYouLauncher.exe --selftest-patch <url> <tag>` gegen einen Server mit `<url>/<tag>/<datei>`.

**Der Launcher aktualisiert sich selbst (ab 0.5.0):** `Tools\PackLauncher.ps1` legt `HCIBY-Launcher.zip`, `HCIBY-Launcher-Linux.tar.gz` und `HCIBY-Launcher.json` nach `Build\Release`; alle drei kommen in jedes Release. Nach jeder Update-Suche schaut der Launcher in die `HCIBY-Launcher.json` des neuesten Releases, das eine hat. Steht dort eine höhere Version als seine eigene, lädt er sein Archiv, prüft Größe und SHA-256 und legt das neue Programm als `HowCouldItBeYouLauncher(.exe).new` neben sich. Läuft gerade kein Spiel-Update und wurde kein Spiel aus ihm gestartet, startet er sich gleich neu, sonst beim nächsten Öffnen. Die laufende Programmdatei wird dabei nie angefasst (ein .NET-Einzeldateiprogramm liest beim Laufen aus ihr nach): Das neue Programm läuft kurz als `HowCouldItBeYouLauncher-update(.exe)`, wartet, bis der alte Launcher zu ist, kopiert sich über die Programmdatei und startet sie. `launcher.json`, `language.txt` und der Spielordner bleiben, wie sie sind. **Wichtig:** Bei jeder Änderung am Launcher `<Version>` in `Launcher\HowCouldItBeYouLauncher.csproj` erhöhen, sonst holen sich installierte Launcher die neue Fassung nicht. Nur der veröffentlichte Einzeldatei-Launcher ersetzt sich, ein Entwickler-Build in `bin\` nie. Test ohne GitHub: `--selftest-selfupdate <url>` (die URL ersetzt `https://api.github.com`; im Fenster-Betrieb dasselbe über die Umgebungsvariable `HC_LAUNCHER_API`).

**Release-Text in zwei Sprachen:** Der Launcher zeigt den Release-Text unter „Neuigkeiten“ und lässt oben rechts zwischen Deutsch und Englisch wechseln (DE | EN, gespeichert in `language.txt` neben der `.exe`). Zuerst kommt der deutsche Text, darunter `---` und die Überschrift `## English – <englischer Titel>` mit der englischen Fassung. Auf Deutsch zeigt der Launcher den Teil darüber, auf Englisch den darunter samt englischem Titel. Fehlt der englische Teil, erscheint in beiden Sprachen der deutsche Text. Vorschau vor dem Veröffentlichen: `HowCouldItBeYouLauncher.exe --selftest-screenshot vorschau.png - notes.md` (die Sprache kommt aus `language.txt`).

Das Repo muss öffentlich sein, damit der Launcher die Releases ohne Anmeldung sieht. Der Launcher liest `launcher.json` neben der `.exe` (Repo-Name, Installationsordner; optional `AssetNameContains` und `GameExecutable`, sonst passend zum System: `HowCouldItBeYou-Win64` + `HowCouldItBeYou.exe` bzw. `HowCouldItBeYou-Linux` + `HowCouldItBeYou.sh`).

## Linux

Spiel und Launcher laufen auch unter Linux (x86_64, Vulkan). Linux- und Windows-Spieler können zusammen spielen, wenn alle dieselbe Version haben.

**Launcher:** derselbe Code wie unter Windows (Avalonia statt WinForms). `Tools\BuildLauncherLinux.bat` baut auf dem Windows-PC den Ordner `Build\LauncherLinux\` (Programm `HowCouldItBeYouLauncher`, `launcher.json`, Symbol, `add-to-menu.sh`, `README-LINUX.txt`) und dasselbe als `Build\Release\HCIBY-Launcher-Linux.tar.gz` – das bekommen die Linux-Spieler (im `.tar.gz` ist das Programm schon ausführbar). Die Anleitung für die Spieler steht in `Launcher\Linux\README-LINUX.txt`. Wie der Launcher unter Linux aussieht (Roboto statt Segoe UI), zeigt unter Windows: `set HC_LAUNCHER_SHIPPED_FONTS=1` und dann `HowCouldItBeYouLauncher.exe --selftest-screenshot linux.png`.

**Spiel für Linux packen** (auf dem Windows-PC, einmalig einzurichten):
1. Epic Games Launcher → Bibliothek → UE 5.8 → Pfeil neben „Starten“ → *Optionen* → *Zielplattformen* → **Linux** anhaken → *Anwenden* (lädt die Linux-Teile der Engine).
2. Epics Linux-Toolchain installieren: `https://cdn.unrealengine.com/CrossToolchain_Linux/v26_clang-20.1.8-rockylinux8.exe` (ca. 1 GB, die Version steht in `Engine\Config\Linux\Linux_SDK.json`). Der Installer setzt `LINUX_MULTIARCH_ROOT`.
3. `Tools\PackageLinux.bat` → `Build\Package\Linux\` und `Build\Release\HowCouldItBeYou-Linux-v<Version>.zip`. Die ZIP schreibt `Tools\LinuxPack.py` (Python), damit `HowCouldItBeYou.sh` und die Programme unter `Binaries\Linux` ausführbar bleiben; der Launcher setzt das nach dem Entpacken zur Sicherheit noch einmal.

## Hinweis zum Pfad

Der Projektordner enthält ein `!` (`!CODING`). Unreals eigene Batch-Dateien verlieren dieses Zeichen. Die Skripte in `Tools/` umgehen das. Wenn im Editor seltsame Pfadfehler auftauchen, das Projekt in einen Ordner ohne `!` verschieben (Empfehlung aus `docs/06`: `C:\Dev\how-could-it-be-you`).
