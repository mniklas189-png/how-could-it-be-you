param(
    # The release zip just made, e.g. Build\Release\HowCouldItBeYou-Win64-v0.12.1.zip
    [Parameter(Mandatory = $true)][string]$Zip,
    [ValidateSet('Win64', 'Linux')]
    [string]$Platform = 'Win64',
    # Manifest of the previous release: auto (newest GitHub release that has one), none, or a .manifest.json file
    [string]$Base = 'auto',
    # Package (zip) of that previous release, for the binary patches of the program files.
    # Default: PatchTool picks it from the zip's folder (Build\Release keeps the older zips).
    [string]$BasePackage
)
# Small updates for the launcher (since launcher 0.5.0): writes
#   HowCouldItBeYou-<Platform>-v<version>.manifest.json  (all files as chunks, where each chunk can be fetched)
#   HowCouldItBeYou-<Platform>-v<version>.chunks         (the chunks and binary patches new in this release)
# next to the zip. Upload BOTH into the same GitHub release as the zip; never delete an older release that has
# a .chunks file, later manifests still point into it. Takes about 6 minutes (two binary diffs of ~350 MB files).
$ErrorActionPreference = 'Stop'

$zipItem = Get-Item $Zip
if ($zipItem.Name -notmatch "^HowCouldItBeYou-$Platform-(v.+)\.zip$") {
    throw "Zip-Name passt nicht zu HowCouldItBeYou-$Platform-v<version>.zip: $($zipItem.Name)"
}
$tag = $Matches[1]

$project = Join-Path $PSScriptRoot 'PatchTool\PatchTool.csproj'
Write-Host "[HC] Baue PatchTool ..." -ForegroundColor Cyan
& dotnet build $project -c Release -nologo -v q | Out-Host
if ($LASTEXITCODE -ne 0) { throw "PatchTool liess sich nicht bauen (ist das .NET SDK 9 installiert?)" }
$exe = Join-Path $PSScriptRoot 'PatchTool\bin\Release\net9.0\PatchTool.exe'

$toolArgs = @('make', '--package', $zipItem.FullName, '--platform', $Platform, '--tag', $tag,
              '--out', $zipItem.DirectoryName, '--base', $Base)
if ($BasePackage) { $toolArgs += @('--base-package', (Resolve-Path $BasePackage).Path) }
Write-Host "[HC] Kleine Updates fuer $tag ($Platform) ..." -ForegroundColor Cyan
& $exe @toolArgs | Out-Host
if ($LASTEXITCODE -ne 0) { throw "PatchTool make fehlgeschlagen (Exit $LASTEXITCODE). Das Zip ist trotzdem fertig." }
