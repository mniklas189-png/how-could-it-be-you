@echo off
rem Collects logs, test reports, bot-run CSVs and crash reports into a ZIP on your desktop.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0CollectLogs.ps1" -GameDir "%~dp0..\Game"
pause
