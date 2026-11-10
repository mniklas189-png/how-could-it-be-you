param([Parameter(Mandatory)][string]$ReportDir)
# Reads the Unreal automation report (index.json) and writes HC_TestSummary.txt.
$index = Join-Path $ReportDir 'index.json'
$summaryFile = Join-Path $ReportDir 'HC_TestSummary.txt'
$lines = @()

if (-not (Test-Path $index)) {
    $lines += 'KEIN TESTBERICHT GEFUNDEN – Unreal ist vermutlich vor den Tests abgebrochen.'
    $lines += "Erwartet: $index"
    $lines | Set-Content -Encoding utf8 $summaryFile
    $lines | ForEach-Object { Write-Host $_ -ForegroundColor Red }
    exit 2
}

# UE writes the report with a BOM / sometimes UTF-16; let PowerShell detect it.
$report = Get-Content $index -Raw | ConvertFrom-Json
$tests = @($report.tests)
$failed = @($tests | Where-Object { $_.state -ne 'Success' })

if ($tests.Count -eq 0) {
    $lines += 'KEINE TESTS GEFUNDEN (Filter "HowCouldItBeYou").'
    $code = 3
} elseif ($failed.Count -eq 0) {
    $lines += "ALLE TESTS GRÜN ($($tests.Count) Tests)"
    $code = 0
} else {
    $lines += "$($failed.Count) TESTS FEHLGESCHLAGEN (von $($tests.Count))"
    foreach ($t in $failed) {
        $lines += " - $($t.fullTestPath) [$($t.state)]"
        foreach ($e in @($t.entries)) {
            if ($e.event.type -eq 'Error') { $lines += "     $($e.event.message)" }
        }
    }
    $code = 1
}

$lines | Set-Content -Encoding utf8 $summaryFile
Write-Host ''
$color = if ($code -eq 0) { 'Green' } else { 'Red' }
$lines | ForEach-Object { Write-Host $_ -ForegroundColor $color }
exit $code
