param(
    # Release text (Markdown): German first, then "---" and "## English - <title>" with the English part
    [Parameter(Mandatory = $true)][string]$Notes,
    # Where the result goes (default: overwrite -Notes)
    [string]$Out
)
# Puts the note for new players (Tools\NewPlayerNote.md: which file to download, the SmartScreen warning
# "Weitere Informationen -> Trotzdem ausfuehren", the firewall question when hosting) into a release text:
# the German note at the end of the German part, the English note at the end of the English part.
# Run it on every release text before "gh release create ... --notes-file".
# A text that already holds the note is left as it is.
$ErrorActionPreference = 'Stop'

$utf8 = New-Object System.Text.UTF8Encoding $false
$NotesPath = (Resolve-Path $Notes).Path
if (-not $Out) { $Out = $NotesPath }

# Same rule as the launcher (LauncherView.EnglishHeading): "## English", "## English - Title", "**English: Title**"
function Test-EnglishHeading([string]$raw) {
    $line = $raw.Trim()
    if (-not ($line.StartsWith('#') -or $line.StartsWith('**'))) { return $false }
    $text = $line.TrimStart('#', ' ').Replace('**', '').Trim()
    if (-not $text.StartsWith('English', [StringComparison]::OrdinalIgnoreCase)) { return $false }
    $dashes = " :-" + [char]0x2013 + [char]0x2014
    return ($text.Length -eq 7 -or $dashes.Contains($text[7]))
}

function Split-Language([string]$text, [string]$what) {
    $lines = $text.Replace("`r", '').Split("`n")
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if (Test-EnglishHeading $lines[$i]) {
            # A "---" right above the heading belongs to the English part
            $start = $i
            $j = $i - 1
            while ($j -ge 0 -and $lines[$j].Trim() -eq '') { $j-- }
            if ($j -ge 0 -and $lines[$j].Trim() -match '^(-{3,}|\*{3,}|_{3,})$') { $start = $j }
            $german = if ($start -gt 0) { ($lines[0..($start - 1)] -join "`n").TrimEnd() } else { '' }
            return [pscustomobject]@{
                German  = $german
                Heading = ($lines[$start..$i] -join "`n").Trim()
                English = if ($i + 1 -lt $lines.Count) { ($lines[($i + 1)..($lines.Count - 1)] -join "`n").Trim() } else { '' }
            }
        }
    }
    throw "$what has no English heading (`"## English - <title>`"). The launcher needs one, see README `"Release-Text in zwei Sprachen`"."
}

$note = Split-Language ([IO.File]::ReadAllText((Join-Path $PSScriptRoot 'NewPlayerNote.md'), $utf8)) 'Tools\NewPlayerNote.md'
$text = [IO.File]::ReadAllText($NotesPath, $utf8)
$firstNoteLine = ($note.German.Split("`n") | Where-Object { $_.Trim() } | Select-Object -First 1).Trim()
if ($text.Replace("`r", '').Contains($firstNoteLine)) {
    Write-Host "[HC] Note for new players is already in $Notes"
    if ($Out -ne $NotesPath) { [IO.File]::WriteAllText($Out, $text, $utf8) }
    return
}

$release = Split-Language $text $Notes
$result = $release.German + "`n`n" + $note.German + "`n`n" + $release.Heading + "`n`n" + $release.English + "`n`n" + $note.English + "`n"
[IO.File]::WriteAllText($Out, $result, $utf8)
Write-Host "[HC] Note for new players added: $Out"
