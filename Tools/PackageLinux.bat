@echo off
rem Packages the game for Linux and creates Build\Release\HowCouldItBeYou-Linux-v<version>.zip for a GitHub Release.
rem Needs: the engine's Linux target platform (Epic Games Launcher) and Epic's Linux toolchain (README, "Linux").
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Package.ps1" -Platform Linux %*
if errorlevel 1 (
  echo ============================
  echo   LINUX-PAKET FEHLGESCHLAGEN
  echo ============================
) else (
  echo ============================
  echo   LINUX-PAKET ERFOLGREICH
  echo ============================
)
if "%HC_NOPAUSE%"=="" pause
