@echo off
rem Builds the editor target of How Could It Be You.
rem Calls UnrealBuildTool directly (not Engine\Build\BatchFiles\Build.bat),
rem because that script uses delayed expansion and would strip the "!" in paths like "!CODING".
setlocal DisableDelayedExpansion

if not exist "%~dp0local.bat" (
  echo [HC] Tools\local.bat fehlt. Kopiere Tools\local.example.bat nach Tools\local.bat.
  goto :fail
)
call "%~dp0local.bat"

set "UBT=%UE_ROOT%\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe"
set "PROJ=%~dp0..\Game\HowCouldItBeYou.uproject"
for %%I in ("%PROJ%") do set "PROJ=%%~fI"

if not exist "%UBT%" (
  echo [HC] UnrealBuildTool nicht gefunden: "%UBT%"
  echo [HC] Stimmt UE_ROOT in Tools\local.bat?
  goto :fail
)
if not exist "%PROJ%" (
  echo [HC] Projekt nicht gefunden: "%PROJ%"
  goto :fail
)

rem UnrealBuildTool needs the .NET runtime bundled with the engine (sets PATH and DOTNET_ROOT)
call "%UE_ROOT%\Engine\Build\BatchFiles\GetDotnetPath.bat"
if errorlevel 1 (
  echo [HC] .NET der Engine nicht gefunden.
  goto :fail
)

if not exist "%~dp0..\Game\Saved\Logs" mkdir "%~dp0..\Game\Saved\Logs"
for /f %%D in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "STAMP=%%D"
set "LOG=%~dp0..\Game\Saved\Logs\Build_%STAMP%.txt"

echo [HC] Baue HowCouldItBeYouEditor Win64 Development ...
"%UBT%" HowCouldItBeYouEditor Win64 Development -Project="%PROJ%" -WaitMutex -FromMsBuild > "%LOG%" 2>&1
set "RC=%ERRORLEVEL%"
type "%LOG%"
echo.
if not "%RC%"=="0" goto :fail

echo ============================
echo   BUILD ERFOLGREICH
echo ============================
if "%HC_NOPAUSE%"=="" pause
exit /b 0

:fail
echo ============================
echo   BUILD FEHLGESCHLAGEN
echo ============================
if "%HC_NOPAUSE%"=="" pause
exit /b 1
