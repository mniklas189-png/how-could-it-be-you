@echo off
rem Builds the launcher as one self-contained .exe into Build\Launcher\.
setlocal DisableDelayedExpansion
set "DOTNET_CLI_TELEMETRY_OPTOUT=1"
set "DOTNET_NOLOGO=1"
dotnet publish "%~dp0..\Launcher\HowCouldItBeYouLauncher.csproj" -c Release -o "%~dp0..\Build\Launcher"
if errorlevel 1 (
  echo   LAUNCHER BUILD FEHLGESCHLAGEN
  pause
  exit /b 1
)
echo   LAUNCHER FERTIG: Build\Launcher\HowCouldItBeYouLauncher.exe
pause
