@echo off
rem Creates the game maps headless (Tools\Setup\CreateMaps.py). Run after Build.bat succeeded.
rem Set HC_FORCE=1 to delete and recreate all generated maps.
setlocal DisableDelayedExpansion
call "%~dp0local.bat"
set "PROJ=%~dp0..\Game\HowCouldItBeYou.uproject"
for %%I in ("%PROJ%") do set "PROJ=%%~fI"
set "SCRIPT=%~dp0Setup\CreateMaps.py"
for %%I in ("%SCRIPT%") do set "SCRIPT=%%~fI"
set "MAPS=%~dp0..\Game\Content\HowCouldItBeYou\Maps"

if "%HC_FORCE%"=="1" (
  echo [HC] HC_FORCE=1: loesche vorhandene Karten
  del /q "%MAPS%\Map_MainMenu.umap" "%MAPS%\Map_Transition.umap" "%MAPS%\Map_Lobby.umap" "%MAPS%\Kepler7\Map_Kepler7.umap" "%MAPS%\Ikarus\Map_Ikarus.umap" 2>nul
)

"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -run=pythonscript -script="%SCRIPT%" -unattended -nopause -nosplash -NoSound
if %ERRORLEVEL% NEQ 0 (
  echo [HC] Karten anlegen FEHLGESCHLAGEN
  if "%HC_NOPAUSE%"=="" pause
  exit /b 1
)
echo [HC] Karten angelegt.
if "%HC_NOPAUSE%"=="" pause
exit /b 0
