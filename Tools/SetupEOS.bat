@echo off
rem Saves the EOS ClientSecret locally in Game\Config\Windows\WindowsEngine.ini (git-ignored), docs/03 section 6.4.
rem Only needed on the PC that packages the game.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0SetupEOS.ps1"
echo.
pause
