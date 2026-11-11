@echo off
rem Brings the English texts of the game up to date. Run after Build.bat succeeded.
rem The game's texts are written in German in the C++ code (NSLOCTEXT). English lives in
rem Game\Content\Localization\Game\en\Game.po:
rem   1. what was translated in Game.po goes into the archive (skipped the very first time),
rem   2. all texts of the C++ code are gathered again,
rem   3. Game.po is written again - new texts have an empty msgstr "": translate them there and run this again,
rem   4. Game.locres is compiled, which the game loads.
setlocal DisableDelayedExpansion
call "%~dp0local.bat"
set "PROJ=%~dp0..\Game\HowCouldItBeYou.uproject"
for %%I in ("%PROJ%") do set "PROJ=%%~fI"
set "LOC=%~dp0..\Game\Content\Localization\Game"
set "STEPS=Config/Localization/Game_Gather.ini;Config/Localization/Game_Export.ini;Config/Localization/Game_Compile.ini"
if exist "%LOC%\Game.manifest" if exist "%LOC%\en\Game.po" set "STEPS=Config/Localization/Game_Import.ini;%STEPS%"

echo [HC] Texte sammeln und Englisch uebernehmen ...
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -run=GatherText -config="%STEPS%" -unattended -nopause -nosplash -NoSound
if %ERRORLEVEL% NEQ 0 (
  echo [HC] Texte sammeln FEHLGESCHLAGEN
  if "%HC_NOPAUSE%"=="" pause
  exit /b 1
)
echo [HC] Fertig: %LOC%\en\Game.po und Game.locres
if "%HC_NOPAUSE%"=="" pause
exit /b 0
