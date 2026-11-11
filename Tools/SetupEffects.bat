@echo off
rem Brings the game's own effect pictures (blood splatters) into the project (Tools\Setup\SetupEffects.py).
rem Only needed after Tools\Setup\MakeBlood.py made new pictures.
setlocal DisableDelayedExpansion
call "%~dp0local.bat"
set "PROJ=%~dp0..\Game\HowCouldItBeYou.uproject"
for %%I in ("%PROJ%") do set "PROJ=%%~fI"
set "SCRIPT=%~dp0Setup\SetupEffects.py"
for %%I in ("%SCRIPT%") do set "SCRIPT=%%~fI"

"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -run=pythonscript -script="%SCRIPT%" -unattended -nopause -nosplash -NoSound
if %ERRORLEVEL% NEQ 0 (
  echo [HC] Effekte einrichten FEHLGESCHLAGEN
  if "%HC_NOPAUSE%"=="" pause
  exit /b 1
)
echo [HC] Effekte eingerichtet.
if "%HC_NOPAUSE%"=="" pause
exit /b 0
