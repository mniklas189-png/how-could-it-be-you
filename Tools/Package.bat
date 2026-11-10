@echo off
rem Packages the game and creates Build\Release\HowCouldItBeYou-Win64-v<version>.zip for a GitHub Release.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Package.ps1" %*
if errorlevel 1 (
  echo ============================
  echo   PAKET FEHLGESCHLAGEN
  echo ============================
) else (
  echo ============================
  echo   PAKET ERFOLGREICH
  echo ============================
)
pause
