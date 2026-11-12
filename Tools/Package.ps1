param(
    [string]$Configuration = "Development",
    # Win64 (default) or Linux. Linux needs the engine's Linux target platform and Epic's Linux toolchain (see README).
    [ValidateSet('Win64', 'Linux')]
    [string]$Platform = "Win64",
    [switch]$SkipZip
)
# Packages the game (BuildCookRun) and zips it as Build\Release\HowCouldItBeYou-<Win64|Linux>-v<version>.zip,
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
Write-Host "[HC] Version $version, Konfiguration $Configuration, Plattform $Platform" -ForegroundColor Cyan

if ($Platform -eq 'Linux') {
    # 1) The engine's Linux target platform (Epic Games Launcher: UE 5.8 -> Optionen -> Zielplattformen -> Linux)
    if (-not (Test-Path (Join-Path $ueRoot 'Engine\Binaries\Linux\UnrealGame.target'))) {
        throw ("Der Engine fehlt die Zielplattform Linux. Im Epic Games Launcher: Bibliothek -> UE 5.8 -> Pfeil neben " +
               "'Starten' -> Optionen -> Zielplattformen -> 'Linux' anhaken -> Anwenden. Danach erneut packen.")
    }
    # 2) Epic's clang toolchain for building Linux programs on Windows. Its installer sets LINUX_MULTIARCH_ROOT,
    #    which an already open console does not see yet: then read it from the registry or the default folder.
    if (-not $env:LINUX_MULTIARCH_ROOT -or -not (Test-Path $env:LINUX_MULTIARCH_ROOT)) {
        $found = @([Environment]::GetEnvironmentVariable('LINUX_MULTIARCH_ROOT', 'User'),
                   [Environment]::GetEnvironmentVariable('LINUX_MULTIARCH_ROOT', 'Machine')) |
                 Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
        if (-not $found -and (Test-Path 'C:\UnrealToolchains')) {
            $found = Get-ChildItem 'C:\UnrealToolchains' -Directory -Filter 'v*_clang-*' |
                     Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
        }
        if (-not $found) {
            $sdk = (Get-Content (Join-Path $ueRoot 'Engine\Config\Linux\Linux_SDK.json') -Raw | ConvertFrom-Json).MainVersion
            throw ("Linux-Toolchain fehlt. Installieren: https://cdn.unrealengine.com/CrossToolchain_Linux/$sdk.exe " +
                   "(Epic, ca. 1 GB; setzt LINUX_MULTIARCH_ROOT). Danach erneut packen.")
        }
        $env:LINUX_MULTIARCH_ROOT = $found
    }
    Write-Host "[HC] Linux-Toolchain: $env:LINUX_MULTIARCH_ROOT" -ForegroundColor Cyan
}

# RunUAT.bat (unlike Engine\Build\BatchFiles\Build.bat) has no delayed expansion, so "!" in the path is fine here.
# Do not use a junction: UnrealHeaderTool fails to rename its .tmp files when the project is reached through one.
$project = Join-Path $repo 'Game\HowCouldItBeYou.uproject'
$archive = Join-Path $repo 'Build\Package'
# BuildCookRun puts each platform into its own folder; only this platform's old package is removed
$stagedName = if ($Platform -eq 'Linux') { 'Linux' } else { 'Windows' }
$staged = Join-Path $archive $stagedName
if (Test-Path $staged) { Remove-Item $staged -Recurse -Force }

$logDir = Join-Path $repo 'Game\Saved\Logs'
New-Item -ItemType Directory -Force $logDir | Out-Null
$log = Join-Path $logDir ("Package_{0}_{1:yyyyMMdd_HHmmss}.txt" -f $Platform, (Get-Date))

$uatArgs = @(
    'BuildCookRun', "-project=`"$project`"", '-noP4', "-platform=$Platform", "-clientconfig=$Configuration",
    '-build', '-cook', '-stage', '-pak', '-iostore', '-archive', "-archivedirectory=`"$archive`"",
    '-utf8output', '-unattended', '-nocompileeditor'
)
if ($Platform -eq 'Win64') {
    # Visual C++ runtime installer for the players' Windows
    $uatArgs += '-prereqs'
}
Write-Host "[HC] Starte BuildCookRun (dauert beim ersten Mal 10-40 Minuten) ..." -ForegroundColor Cyan
& cmd /c "`"$runUat`" $($uatArgs -join ' ')" 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw "BuildCookRun fehlgeschlagen (Exit $LASTEXITCODE). Log: $log" }

$startFile = if ($Platform -eq 'Linux') { 'HowCouldItBeYou.sh' } else { 'HowCouldItBeYou.exe' }
if (-not (Test-Path (Join-Path $staged $startFile))) { throw "Paket nicht gefunden unter $staged" }

if ($SkipZip) { Write-Host "[HC] Paket liegt in $staged" -ForegroundColor Green; return }

$releaseDir = Join-Path $repo 'Build\Release'
New-Item -ItemType Directory -Force $releaseDir | Out-Null
$zip = Join-Path $releaseDir "HowCouldItBeYou-$Platform-v$version.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Write-Host "[HC] Packe $zip ..." -ForegroundColor Cyan
if ($Platform -eq 'Linux') {
    # Python writes the Unix "executable" flag into the zip (HowCouldItBeYou.sh, Binaries\Linux); a Windows zip can't
    & python -B (Join-Path $PSScriptRoot 'LinuxPack.py') game $staged $zip
    if ($LASTEXITCODE -ne 0) { throw "Linux-Zip fehlgeschlagen (ist Python installiert?)" }
} else {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($staged, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)
}

$sizeMb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host ""
Write-Host "PAKET FERTIG: $zip ($sizeMb MB)" -ForegroundColor Green
Write-Host "Naechster Schritt: auf GitHub unter Releases -> 'Draft a new release' den Tag v$version anlegen,"
Write-Host "die ZIP-Datei(en) hochladen (Windows und Linux in dasselbe Release) und veroeffentlichen."
Write-Host "Die Launcher finden das Update dann automatisch."
