Pictures for the website (Website/img), rendered from a packaged game, 2560x1440, English UI.

1. Copy an installed game (the launcher's Game folder) next to these scripts as .\Game
   (the scripts start .\Game\HowCouldItBeYou.exe offscreen; its own settings stay in that copy).
2. python render_tours.py all          camera stops on Kepler-7 and Ikarus  -> shots\kepler, shots\ikarus
3. python render_round.py round 480    bot round (Mia = Shifter, Lena = victim, Tom = reports)  -> shots\round\<name>
4. python make_images.py <repo>\Website\img   crops, paints over the FPS counter and the TALK hint,
                                              removes the host's home-network IP from the lobby, writes the web sizes

Copying to a short path without "!" (for example %TEMP%\hcweb) avoids the Unreal path quirk.
