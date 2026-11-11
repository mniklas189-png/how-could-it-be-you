@echo off
rem Replaces the built-in placeholder sounds with the WAV files in the folder Sounds (Tools\Setup\SetupSounds.py).
rem Run after Build.bat and SetupModels.bat; see Sounds\LIES-MICH.txt for the file names.
setlocal DisableDelayedExpansion
call "%~dp0local.bat"
set "PROJ=%~dp0..\Game\HowCouldItBeYou.uproject"
for %%I in ("%PROJ%") do set "PROJ=%%~fI"
set "SCRIPT=%~dp0Setup\SetupSounds.py"
for %%I in ("%SCRIPT%") do set "SCRIPT=%%~fI"

"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -run=pythonscript -script="%SCRIPT%" -unattended -nopause -nosplash -NoSound
if %ERRORLEVEL% NEQ 0 (
  echo [HC] Sounds einrichten FEHLGESCHLAGEN
  if "%HC_NOPAUSE%"=="" pause
  exit /b 1
)
echo [HC] Sounds eingerichtet.
if "%HC_NOPAUSE%"=="" pause
exit /b 0
