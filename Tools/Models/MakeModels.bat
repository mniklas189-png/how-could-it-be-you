@echo off
rem Builds the game's own prop models with Blender (Tools\Models\MakeModels.py) into Build\Models:
rem <Name>.fbx, T_<Name>_BC.png, T_<Name>_ORM.png, manifest.json and Preview\<Name>.png.
rem Arguments go to the script, for example:  MakeModels.bat --only Crate,Bunk   or   MakeModels.bat --no-bake
rem Blender is taken from BLENDER_EXE if set, else from the Steam folder.
setlocal DisableDelayedExpansion
if "%BLENDER_EXE%"=="" set "BLENDER_EXE=C:\Program Files (x86)\Steam\steamapps\common\Blender\blender.exe"
if not exist "%BLENDER_EXE%" (
  echo [HC] Blender nicht gefunden: "%BLENDER_EXE%"
  echo [HC] BLENDER_EXE auf die blender.exe setzen und noch einmal starten.
  if "%HC_NOPAUSE%"=="" pause
  exit /b 1
)
set "SCRIPT=%~dp0MakeModels.py"

echo [HC] Modelle bauen ...
"%BLENDER_EXE%" --background --factory-startup --python-exit-code 1 --python "%SCRIPT%" -- %*
if %ERRORLEVEL% NEQ 0 (
  echo [HC] Modelle bauen FEHLGESCHLAGEN
  if "%HC_NOPAUSE%"=="" pause
  exit /b 1
)
echo [HC] Modelle gebaut: Build\Models
if "%HC_NOPAUSE%"=="" pause
exit /b 0
