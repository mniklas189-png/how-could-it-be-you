Pictures for the website (Website/img), rendered from a packaged game, 2560x1440, English UI.

1. Copy an installed game (the launcher's Game folder, or a release zip unpacked) next to these scripts as .\Game
   (the scripts start .\Game\HowCouldItBeYou.exe offscreen; its own settings stay in that copy).
2. python render_tours.py all          camera stops on Kepler-7, Ikarus and Europa, and a vent hatch on Europa
                                       with its grille open (-HCVentOpen)
                                       -> shots\kepler, shots\ikarus, shots\europa, shots\europa_vent
                                       (or one of them: kepler | ikarus | europa | vent)
3. python render_round.py round 480    bot round (Mia = Shifter, Lena = victim, Tom = reports)  -> shots\round\<name>
   python render_round.py lobby 150 lobby
                                       only the host, until its lobby picture is taken  -> shots\lobby\Mia
4. python make_images.py <repo>\Website\img [what ...]
                                       crops, paints over the FPS counter and the TALK hint,
                                       removes the host's home-network IP from the lobby, writes the web sizes;
                                       "what" is a group (round, kepler, ikarus, europa, lobby) or a picture's name
                                       (ikarus-biolab), so only the pictures that changed are written again

Copying to a short path without "!" (for example %TEMP%\hcweb) avoids the Unreal path quirk.

v0.13.9 (map Europa, vents, own furniture): the Kepler-7 stops on the page looked the same as before, so only
Europa, the two vent pictures, the Ikarus bio lab and the lobby were written:
  python make_images.py <repo>\Website\img europa ikarus-biolab lobby
