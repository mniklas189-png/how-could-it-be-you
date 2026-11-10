param([Parameter(Mandatory)][string]$GameDir)
$GameDir = (Resolve-Path $GameDir).Path
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$tmp = Join-Path $env:TEMP "HC_Logs_$stamp"
New-Item -ItemType Directory -Force $tmp | Out-Null

foreach ($sub in 'Saved\Logs', 'Saved\TestReports', 'Saved\HCBotRun', 'Saved\Crashes') {
    $p = Join-Path $GameDir $sub
    if (Test-Path $p) { Copy-Item $p (Join-Path $tmp ($sub -replace '\\', '_')) -Recurse -Force }
}
$pkg = Join-Path $env:LOCALAPPDATA 'HowCouldItBeYou\Saved\Logs'
if (Test-Path $pkg) { Copy-Item $pkg (Join-Path $tmp 'Packaged_Logs') -Recurse -Force }

if (-not (Get-ChildItem $tmp)) { 'Keine Logs gefunden.' | Set-Content (Join-Path $tmp 'EMPTY.txt') }
$zip = Join-Path ([Environment]::GetFolderPath('Desktop')) "HC_Logs_$stamp.zip"
Compress-Archive -Path (Join-Path $tmp '*') -DestinationPath $zip -Force
Remove-Item $tmp -Recurse -Force
Write-Host "[HC] Logs gesammelt: $zip" -ForegroundColor Green
