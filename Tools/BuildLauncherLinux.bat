@echo off
rem Builds the Linux launcher on Windows:
rem   Build\LauncherLinux\                         the folder for Linux players (program, launcher.json, icon,
rem                                                add-to-menu.sh, README-LINUX.txt)
rem   Build\Release\HCIBY-Launcher-Linux.tar.gz    the same folder packed with the "executable" flag set (to share)
setlocal DisableDelayedExpansion
set "DOTNET_CLI_TELEMETRY_OPTOUT=1"
set "DOTNET_NOLOGO=1"
set "AVALONIA_TELEMETRY_OPTOUT=1"
dotnet publish "%~dp0..\Launcher\HowCouldItBeYouLauncher.csproj" -c Release -r linux-x64 -o "%~dp0..\Build\LauncherLinux"
if errorlevel 1 goto :fail
python -B "%~dp0LinuxPack.py" launcher "%~dp0..\Build\LauncherLinux" "%~dp0..\Build\Release\HCIBY-Launcher-Linux.tar.gz"
if errorlevel 1 goto :fail
echo   LINUX-LAUNCHER FERTIG: Build\LauncherLinux\  und  Build\Release\HCIBY-Launcher-Linux.tar.gz
if "%HC_NOPAUSE%"=="" pause
exit /b 0

:fail
echo   LINUX-LAUNCHER BUILD FEHLGESCHLAGEN
if "%HC_NOPAUSE%"=="" pause
exit /b 1
