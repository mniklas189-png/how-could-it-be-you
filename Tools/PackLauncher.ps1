param(
    # Where the files go (default Build\Release)
    [string]$OutDir,
    # Default: <Version> in Launcher\HowCouldItBeYouLauncher.csproj. Another value only for tests.
    [string]$Version,
    # Only the Windows launcher (the json then offers no Linux update)
    [switch]$NoLinux
)
# Builds the launcher files for a GitHub release from clean publish folders:
#   HCIBY-Launcher.zip            Windows: HowCouldItBeYouLauncher.exe + launcher.json (for new players)
#   HCIBY-Launcher-Linux.tar.gz   Linux: folder HowCouldItBeYou-Launcher (via Tools\LinuxPack.py, keeps the exec bit)
#   HCIBY-Launcher.json           version, size and SHA-256 of both: launchers from 0.5.0 on update themselves from it
# Upload all three into every release. Raise <Version> in the .csproj whenever the launcher changes,
# otherwise installed launchers don't pick the new one up.
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$project = Join-Path $repo 'Launcher\HowCouldItBeYouLauncher.csproj'
if (-not $OutDir) { $OutDir = Join-Path $repo 'Build\Release' }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
if (-not $Version) {
    $Version = ([xml](Get-Content $project -Raw)).Project.PropertyGroup.Version | Where-Object { $_ } | Select-Object -First 1
}
if ($Version -notmatch '^\d+\.\d+(\.\d+)?$') { throw "Launcher-Version nicht lesbar: '$Version'" }
Write-Host "[HC] Launcher $Version" -ForegroundColor Cyan

$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_NOLOGO = '1'
$env:AVALONIA_TELEMETRY_OPTOUT = '1'
$work = Join-Path ([IO.Path]::GetTempPath()) ('hciby-launcher-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force $work | Out-Null

function Publish([string]$rid, [string]$dir) {
    & dotnet publish $project -c Release -r $rid -o $dir "-p:Version=$Version" -nologo -v q | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "dotnet publish $rid fehlgeschlagen" }
}

function Describe([string]$path, [string]$program) {
    [ordered]@{
        asset   = Split-Path $path -Leaf
        size    = (Get-Item $path).Length
        sha256  = (Get-FileHash -Algorithm SHA256 $path).Hash.ToLowerInvariant()
        program = $program
    }
}

try {
    $files = [ordered]@{}

    # Windows: exe + launcher.json at the top of the zip
    Publish 'win-x64' (Join-Path $work 'win')
    $stage = Join-Path $work 'winzip'
    New-Item -ItemType Directory -Force $stage | Out-Null
    Copy-Item (Join-Path $work 'win\HowCouldItBeYouLauncher.exe'), (Join-Path $work 'win\launcher.json') $stage
    $zip = Join-Path $OutDir 'HCIBY-Launcher.zip'
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)
    $files['win-x64'] = Describe $zip 'HowCouldItBeYouLauncher.exe'

    if (-not $NoLinux) {
        Publish 'linux-x64' (Join-Path $work 'linux')
        $tar = Join-Path $OutDir 'HCIBY-Launcher-Linux.tar.gz'
        & python -B (Join-Path $PSScriptRoot 'LinuxPack.py') launcher (Join-Path $work 'linux') $tar
        if ($LASTEXITCODE -ne 0) { throw "Linux-Archiv fehlgeschlagen (ist Python installiert?)" }
        $files['linux-x64'] = Describe $tar 'HowCouldItBeYou-Launcher/HowCouldItBeYouLauncher'
    }

    $json = Join-Path $OutDir 'HCIBY-Launcher.json'
    $text = [ordered]@{ version = $Version; files = $files } | ConvertTo-Json -Depth 4
    [IO.File]::WriteAllText($json, $text, (New-Object Text.UTF8Encoding $false))
}
finally {
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "LAUNCHER $Version FERTIG in $OutDir" -ForegroundColor Green
foreach ($f in $files.Values) { Write-Host ("  {0}  ({1:0.0} MB)" -f $f.asset, ($f.size / 1MB)) }
Write-Host "  HCIBY-Launcher.json"
Write-Host "Alle drei ins Release hochladen; installierte Launcher ab 0.5.0 holen sich die neue Version selbst."
