@echo off
rem Builds the project, then runs all automation tests starting with "HowCouldItBeYou" headless.
setlocal DisableDelayedExpansion

set "HC_NOPAUSE=1"
call "%~dp0Build.bat"
if errorlevel 1 (
  set "HC_NOPAUSE="
  echo [HC] Build fehlgeschlagen, Tests werden nicht gestartet.
  pause
  exit /b 1
)
set "HC_NOPAUSE="
call "%~dp0local.bat"

set "PROJ=%~dp0..\Game\HowCouldItBeYou.uproject"
for %%I in ("%PROJ%") do set "PROJ=%%~fI"
set "REPORT=%~dp0..\Game\Saved\TestReports"
for %%I in ("%REPORT%") do set "REPORT=%%~fI"
if exist "%REPORT%" rmdir /s /q "%REPORT%"
mkdir "%REPORT%"

echo [HC] Starte Tests (headless) ...
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -ExecCmds="Automation RunTests HowCouldItBeYou;Quit" -unattended -nullrhi -nopause -nosplash -NoSound -ReportExportPath="%REPORT%" -log=HC_Tests.log

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Summarize-Tests.ps1" -ReportDir "%REPORT%"
set "RC=%ERRORLEVEL%"
pause
exit /b %RC%
