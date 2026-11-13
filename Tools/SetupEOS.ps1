# Writes the EOS ClientSecret into Game/Config/Windows/WindowsEngine.ini (git-ignored, docs/03 section 6.4).
# Started by Tools\SetupEOS.bat. The secret is never printed, logged or sent anywhere.
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Target = Join-Path $Root 'Game\Config\Windows\WindowsEngine.ini'
$DefaultIni = Join-Path $Root 'Game\Config\DefaultEngine.ini'

Write-Host ''
Write-Host 'EOS einrichten: ClientSecret lokal speichern' -ForegroundColor Cyan
Write-Host "Ziel: $Target"
Write-Host ''

# Must be ignored by git, otherwise stop: the secret must never land in a commit
Push-Location $Root
try {
    & git check-ignore -q -- 'Game/Config/Windows/WindowsEngine.ini'
    $Ignored = ($LASTEXITCODE -eq 0)
} catch {
    $Ignored = $true
} finally {
    Pop-Location
}
if (-not $Ignored) {
    Write-Host 'ABBRUCH: Git ignoriert Game/Config/Windows/WindowsEngine.ini nicht (.gitignore pruefen).' -ForegroundColor Red
    exit 1
}

if (Test-Path $Target) {
    $Existing = Get-Content $Target -Raw
    if ($Existing -match '(?m)^\s*ClientSecret=(?!HIER_EINTRAGEN)\S+') {
        $Answer = Read-Host 'Es ist schon ein ClientSecret eingetragen. Ersetzen? (j/n)'
        if ($Answer -notmatch '^[jJyY]') {
            Write-Host 'Nichts geaendert.'
            exit 0
        }
    }
}

$Secure = Read-Host 'ClientSecret aus dem Epic Developer Portal (Eingabe bleibt unsichtbar, Einfuegen mit Rechtsklick)' -AsSecureString
$Ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($Secure)
try {
    $Secret = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($Ptr).Trim()
} finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($Ptr)
}
if ([string]::IsNullOrWhiteSpace($Secret) -or $Secret -match '[\s"'';]') {
    Write-Host 'Das sieht nicht wie ein ClientSecret aus (leer oder mit Leerzeichen/Anfuehrungszeichen). Nichts geaendert.' -ForegroundColor Red
    exit 1
}

$Lines = @(
    '; Local EOS secret (docs/03 section 6.4). Git-ignored: never commit, never send this file. Written by Tools\SetupEOS.ps1.',
    '[EOSSDK.Platform.HCDev]',
    "ClientSecret=$Secret"
)
New-Item -ItemType Directory -Force -Path (Split-Path $Target) | Out-Null
[IO.File]::WriteAllLines($Target, $Lines, (New-Object Text.UTF8Encoding($false)))
$Secret = $null
Write-Host ''
Write-Host 'ClientSecret gespeichert.' -ForegroundColor Green

# Hint if the non-secret IDs are still empty
$Ids = Get-Content $DefaultIni -Raw
foreach ($Key in 'ProductId', 'SandboxId', 'DeploymentId', 'ClientId') {
    if ($Ids -match "(?m)^$Key=\s*$") {
        Write-Host "Hinweis: $Key ist in Game/Config/DefaultEngine.ini noch leer (die IDs traegt Claude ein)." -ForegroundColor Yellow
    }
}
exit 0
