@echo off
rem Links the store model packs (Survival Character, Whisper) into the game (Tools\Setup\SetupModels.py).
rem Run after Build.bat succeeded and after both packs were added to the project with the Epic Games Launcher.
setlocal DisableDelayedExpansion
call "%~dp0local.bat"
set "PROJ=%~dp0..\Game\HowCouldItBeYou.uproject"
for %%I in ("%PROJ%") do set "PROJ=%%~fI"
set "SCRIPT=%~dp0Setup\SetupModels.py"
for %%I in ("%SCRIPT%") do set "SCRIPT=%%~fI"

"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -run=pythonscript -script="%SCRIPT%" -unattended -nopause -nosplash -NoSound
if %ERRORLEVEL% NEQ 0 (
  echo [HC] Modelle einrichten FEHLGESCHLAGEN
  if "%HC_NOPAUSE%"=="" pause
  exit /b 1
)
echo [HC] Modelle eingerichtet.
if "%HC_NOPAUSE%"=="" pause
exit /b 0
