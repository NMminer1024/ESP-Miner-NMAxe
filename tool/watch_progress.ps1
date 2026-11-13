# Live progress view for the chart-history soak and the fleet monitor.
#
# Reads the CSV files the two background tools already write, so watching costs
# nothing: no extra HTTP requests to the miners, no interference with the run
# being observed. Refreshes in place until Ctrl+C.
#
#   .\tool\watch_progress.ps1                 # 3 s refresh, all targets
#   .\tool\watch_progress.ps1 -RefreshSeconds 10
#   .\tool\watch_progress.ps1 -StopOnDone     # exit as soon as a soak is ready
#
# The goal of the soak is the 24 h retention window: the history deque keeps
# growing until MINER_HISTORY_SAMPLE_DEEPTH (24 h) is full, and that transition
# is the only moment the original crash conditions are reproduced. So the two
# numbers worth watching are uptime (how close to 24 h) and dequeSize (whether
# it keeps growing at the expected cadence).

param(
    [int]$RefreshSeconds = 3,
    [int]$SoakIntervalMinutes = 5,
    [switch]$StopOnDone
)

$ErrorActionPreference = 'Stop'

$root      = Split-Path -Parent $PSScriptRoot
$soakDir   = Join-Path $root 'docs\baseline\soak_chart_history'
$fleetDir  = Join-Path $root 'docs\baseline\web_crash_capture_20261113'

# Window size from src/mining/mining_types.h (MINER_HISTORY_SAMPLE_DEEPTH).
$windowSeconds = 24 * 3600

function Read-CsvSafe {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return $null }
    # The writer appends while this runs, so a read can land mid-line; retry
    # rather than showing a parse error for a transient state.
    for ($i = 0; $i -lt 3; $i++) {
        try { return @(Import-Csv $Path) }
        catch { [System.Threading.Thread]::Sleep(60) }
    }
    return $null
}

function Format-Duration {
    param([double]$Seconds)
    if ($Seconds -le 0) { return '0m' }
    $ts = [TimeSpan]::FromSeconds($Seconds)
    if ($ts.TotalDays -ge 1) { return ('{0}d {1}h' -f [int]$ts.Days, $ts.Hours) }
    if ($ts.TotalHours -ge 1) { return ('{0}h {1}m' -f [int]$ts.Hours, $ts.Minutes) }
    return ('{0}m {1}s' -f $ts.Minutes, $ts.Seconds)
}

function Format-Bytes {
    param([double]$Bytes)
    if (-not $Bytes) { return '-' }
    if ($Bytes -ge 1MB) { return ('{0:N1} MB' -f ($Bytes / 1MB)) }
    if ($Bytes -ge 1KB) { return ('{0:N1} KB' -f ($Bytes / 1KB)) }
    return ('{0:N0} B' -f $Bytes)
}

function Write-Bar {
    param([double]$Fraction, [int]$Width = 28)
    if ($Fraction -lt 0) { $Fraction = 0 }
    if ($Fraction -gt 1) { $Fraction = 1 }
    $filled = [int][math]::Round($Fraction * $Width)
    return ('[' + ('#' * $filled) + ('.' * ($Width - $filled)) + ']')
}

while ($true) {
    Clear-Host
    $now = Get-Date

    Write-Host '=== chart-history soak ===' -ForegroundColor Cyan
    Write-Host ("  {0}" -f $now.ToString('yyyy-MM-dd HH:mm:ss')) -ForegroundColor DarkGray

    $soakFiles = @(Get-ChildItem $soakDir -Filter 'soak_*.csv' -ErrorAction SilentlyContinue |
                   Where-Object { $_.Name -notmatch '\.pre-deque\.csv$' })

    if ($soakFiles.Count -eq 0) {
        Write-Host '  (no soak data yet)' -ForegroundColor Yellow
    }

    $ready = $false
    foreach ($file in $soakFiles) {
        $rows = Read-CsvSafe $file.FullName
        if (-not $rows -or $rows.Count -eq 0) { continue }

        $last = $rows[-1]
        $ip   = $last.ip
        $up   = [double]$last.uptimeSeconds
        $deque = [double]$last.dequeSize
        $cad   = [double]$last.cadenceSec
        $ageS  = ($now - [datetime]$last.timestamp).TotalSeconds

        $frac = $up / $windowSeconds
        if ($frac -ge 1) { $ready = $true }

        # Expected deque once the retention window is full.
        $expect = if ($cad -gt 0) { [math]::Round($windowSeconds / $cad) } else { 0 }
        $eta    = [math]::Max(0, $windowSeconds - $up)

        # The soak samples every SoakIntervalMinutes, so a gap of up to that long
        # is normal. Only flag staleness past two intervals, otherwise a healthy
        # run looks broken for most of every cycle.
        $intervalS = $SoakIntervalMinutes * 60
        $nextIn    = [math]::Max(0, $intervalS - $ageS)
        $color = if ($ageS -gt $intervalS * 2) { 'Red' } elseif ($ageS -gt $intervalS * 1.5) { 'Yellow' } else { 'Gray' }

        Write-Host ''
        Write-Host ("  {0,-16} fw {1}  sha {2}" -f $ip, $last.firmware, $last.appSha256) -ForegroundColor White
        Write-Host ("    window  {0} {1,5:N1}%   eta {2}" -f (Write-Bar $frac), ($frac * 100), (Format-Duration $eta))
        Write-Host ("    uptime  {0,-12} deque {1,-6} / ~{2} expected   cadence {3:N2} s" -f `
            (Format-Duration $up), $deque, $expect, $cad)
        Write-Host ("    heap {0}  psram {1}  latency {2} ms  http {3}" -f `
            (Format-Bytes $last.freeHeap), (Format-Bytes ([double]$last.freePsramKB * 1KB)),
            $last.latencyMs, $last.httpCode)
        Write-Host ("    samples {0} recorded   last {1:N0}s ago   next in {2:N0}s" -f `
            $rows.Count, $ageS, $nextIn) -ForegroundColor $color
        if ($ageS -gt $intervalS * 2) {
            Write-Host '    <-- overdue: the soak process may have died, check its terminal' -ForegroundColor Red
        }
    }

    Write-Host ''
    Write-Host '=== fleet monitor ===' -ForegroundColor Cyan

    $fleetFiles = @(Get-ChildItem $fleetDir -Filter 'samples_*.csv' -ErrorAction SilentlyContinue |
                    Where-Object { $_.Name -notmatch '_78\.' })   # .78 is the retired IP of the Nexus board

    foreach ($file in $fleetFiles) {
        $rows = Read-CsvSafe $file.FullName
        if (-not $rows -or $rows.Count -eq 0) { continue }
        $last = $rows[-1]
        $ageS = ($now - [datetime]$last.timestamp).TotalSeconds
        $color = if ($last.status -ne 'ok') { 'Red' } elseif ($ageS -gt 120) { 'Yellow' } else { 'Gray' }
        Write-Host ("  {0,-16} up {1,-11} heap {2,-9} {3,-12} {4:N0}s ago" -f `
            $last.ip, (Format-Duration ([double]$last.uptimeSeconds)), (Format-Bytes $last.freeHeap),
            $last.status, $ageS) -ForegroundColor $color
    }

    # Incidents are the only thing worth interrupting the operator for.
    $incidentLog = Join-Path $soakDir 'incidents.log'
    $incCount = 0
    if (Test-Path $incidentLog) { $incCount = @(Get-Content $incidentLog).Count }
    Write-Host ''
    Write-Host ("  incidents logged: {0}   (see soak_chart_history\incidents.log)" -f $incCount) -ForegroundColor DarkGray
    Write-Host '  Ctrl+C to stop. This view only reads the CSV files.' -ForegroundColor DarkGray

    if ($StopOnDone -and $ready) {
        Write-Host ''
        Write-Host '  A soak target has passed the 24 h window. Reproduction window is open.' -ForegroundColor Green
        return
    }

    [System.Threading.Thread]::Sleep($RefreshSeconds * 1000)
}
