# Generic evidence grep over a captured log file (ws_*.log or serial_*.log).
# -Pattern: one or more regexes; prints match counts per pattern and, with
# -ShowLines, the matching lines. -FromHHmm/-ToHHmm restrict to a time-of-day
# window (capture lines start with "MM-dd HH:mm:ss.fff").
param(
    [string]$Log = $(throw 'Log file required'),
    [string[]]$Pattern = @(),
    [string]$FromHHmm = '',      # e.g. '13:58'
    [string]$ToHHmm = '',        # e.g. '14:12'
    [switch]$ShowLines
)
function ToMin([string]$hhmm) { [int]$hhmm.Split(':')[0] * 60 + [int]$hhmm.Split(':')[1] }
$lines = Get-Content $Log
if ($FromHHmm -or $ToHHmm) {
    $lo = if ($FromHHmm) { ToMin $FromHHmm } else { [int]::MinValue }
    $hi = if ($ToHHmm) { ToMin $ToHHmm } else { [int]::MaxValue }
    $lines = $lines | Where-Object {
        if ($_ -match '^\d\d-\d\d (\d\d):(\d\d):\d\d') {
            $m = [int]$matches[1] * 60 + [int]$matches[2]
            $m -ge $lo -and $m -le $hi
        } else { $false }
    }
}
"window lines: $($lines.Count)"
if (-not $Pattern) {
    'no -Pattern given; pass one or more regexes, e.g. -Pattern "Guru Meditation","your marker"'
    exit 0
}
foreach ($p in $Pattern) {
    $m = $lines | Select-String -Pattern $p
    "{0,-40} {1}" -f $p, $m.Count
    if ($ShowLines -and $m.Count) { $m | ForEach-Object { "  $($_.Line)" } }
}
