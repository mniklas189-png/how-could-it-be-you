param(
    [string]$Configuration = "Development",
    [switch]$SkipZip
)
# Packages the game (BuildCookRun) and zips it as Build\Release\HowCouldItBeYou-Win64-v<version>.zip,
# ready to upload as a GitHub Release asset for the launcher.
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$localBat = Join-Path $PSScriptRoot 'local.bat'
if (-not (Test-Path $localBat)) { throw "Tools\local.bat fehlt (Vorlage: Tools\local.example.bat)." }
$ueRoot = (Select-String -Path $localBat -Pattern 'UE_ROOT=(.+?)"?\s*$').Matches[0].Groups[1].Value.Trim('"')
$runUat = Join-Path $ueRoot 'Engine\Build\BatchFiles\RunUAT.bat'
if (-not (Test-Path $runUat)) { throw "RunUAT nicht gefunden: $runUat" }

# Version from DefaultGame.ini
$ini = Get-Content (Join-Path $repo 'Game\Config\DefaultGame.ini')
$version = ($ini | Select-String '^ProjectVersion=(.+)$').Matches[0].Groups[1].Value.Trim()
Write-Host "[HC] Version $version, Konfiguration $Configuration" -ForegroundColor Cyan

# RunUAT.bat (unlike Engine\Build\BatchFiles\Build.bat) has no delayed expansion, so "!" in the path is fine here.
# Do not use a junction: UnrealHeaderTool fails to rename its .tmp files when the project is reached through one.
$project = Join-Path $repo 'Game\HowCouldItBeYou.uproject'
$archive = Join-Path $repo 'Build\Package'
if (Test-Path $archive) { Remove-Item $archive -Recurse -Force }

$logDir = Join-Path $repo 'Game\Saved\Logs'
New-Item -ItemType Directory -Force $logDir | Out-Null
$log = Join-Path $logDir ("Package_{0:yyyyMMdd_HHmmss}.txt" -f (Get-Date))

$uatArgs = @(
    'BuildCookRun', "-project=`"$project`"", '-noP4', '-platform=Win64', "-clientconfig=$Configuration",
    '-build', '-cook', '-stage', '-pak', '-iostore', '-prereqs', '-archive', "-archivedirectory=`"$archive`"",
    '-utf8output', '-unattended', '-nocompileeditor'
)
Write-Host "[HC] Starte BuildCookRun (dauert beim ersten Mal 10-40 Minuten) ..." -ForegroundColor Cyan
& cmd /c "`"$runUat`" $($uatArgs -join ' ')" 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "BuildCookRun fehlgeschlagen (Exit $LASTEXITCODE). Log: $log" }

$staged = Join-Path $repo 'Build\Package\Windows'
if (-not (Test-Path (Join-Path $staged 'HowCouldItBeYou.exe'))) { throw "Paket nicht gefunden unter $staged" }

if ($SkipZip) { Write-Host "[HC] Paket liegt in $staged" -ForegroundColor Green; return }

$releaseDir = Join-Path $repo 'Build\Release'
New-Item -ItemType Directory -Force $releaseDir | Out-Null
$zip = Join-Path $releaseDir "HowCouldItBeYou-Win64-v$version.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Write-Host "[HC] Packe $zip ..." -ForegroundColor Cyan
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($staged, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)

$sizeMb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host ""
Write-Host "PAKET FERTIG: $zip ($sizeMb MB)" -ForegroundColor Green
Write-Host "Naechster Schritt: auf GitHub unter Releases -> 'Draft a new release' den Tag v$version anlegen,"
Write-Host "die ZIP-Datei hochladen und veroeffentlichen. Der Launcher findet das Update dann automatisch."
