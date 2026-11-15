param(
    [Parameter(Mandatory = $true)]
    [string]$Ip,

    [int]$IntervalMinutes = 5,

    [string]$OutputDir = (Join-Path $PSScriptRoot '..\docs\baseline\soak_chart_history')
)

# Long-run stress for GET /api/dashboard/chart/history.
#
# The chart-history panic only appears once the status_history deque is large
# (observed at 18k-20k entries, i.e. ~20-25 h of uptime). Right after a reboot
# the same request is harmless, so a soak has to keep poking the endpoint while
# uptime grows. This script does exactly that, and keeps enough evidence that a
# crash can be attributed to the exact firmware image:
#
#  - records the app SHA in every sample, so the ELF used for symbolization is
#    unambiguous
#  - records the reboot index and uptime, so a restart is detected even when the
#    board still holds a stale coredump from an earlier incident
#  - on restart it snapshots reboot records and downloads the raw ELF dump
#
# It never opens the dashboard UI and never establishes a WebSocket: the only
# request it makes is the chart-history GET.

$ErrorActionPreference = 'Stop'
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$safeIp = $Ip -replace '\.', '_'
$csvPath = Join-Path $OutputDir "soak_$safeIp.csv"
$statePath = Join-Path $OutputDir "state_$safeIp.json"

function Invoke-JsonGet {
    param([string]$Path, [int]$TimeoutSeconds = 10)
    try { return Invoke-RestMethod -Uri "http://$Ip$Path" -TimeoutSec $TimeoutSeconds }
    catch { return $null }
}

function Save-Incident {
    param([string]$Reason, [object]$SystemInfo)

    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $dir = Join-Path $OutputDir ("incident_${safeIp}_${stamp}_${Reason}")
    New-Item -ItemType Directory -Force -Path $dir | Out-Null

    if ($SystemInfo) {
        $SystemInfo | ConvertTo-Json -Depth 12 | Set-Content -Encoding UTF8 -Path (Join-Path $dir 'api_system_info.json')
    }
    foreach ($pair in @(@('/api/reboot/last', 'api_reboot_last.json'), @('/api/reboot/list', 'api_reboot_list.json'))) {
        $value = Invoke-JsonGet $pair[0]
        if ($value) { $value | ConvertTo-Json -Depth 12 | Set-Content -Encoding UTF8 -Path (Join-Path $dir $pair[1]) }
    }

    $core = Invoke-JsonGet '/api/coredump/info' 20
    if ($core) {
        $core | ConvertTo-Json -Depth 12 | Set-Content -Encoding UTF8 -Path (Join-Path $dir 'api_coredump_info.json')
        if ($core.present) {
            try {
                Invoke-WebRequest -Uri "http://$Ip/api/coredump/download" -TimeoutSec 90 `
                    -OutFile (Join-Path $dir 'coredump.bin')
            } catch {
                $_.Exception.Message | Set-Content -Encoding UTF8 -Path (Join-Path $dir 'download_error.txt')
            }
        }
    }

    "$(Get-Date -Format o) $Reason $dir" | Add-Content -Encoding UTF8 -Path (Join-Path $OutputDir 'incidents.log')
    Write-Host "[$(Get-Date -Format T)] incident captured: $Reason -> $dir"
}

# Restore the last known state so a restart of this script does not produce a
# spurious "uptime reset" incident.
$previous = $null
if (Test-Path $statePath) {
    try { $previous = Get-Content -Raw -Encoding UTF8 $statePath | ConvertFrom-Json } catch { $previous = $null }
}

Write-Host "Chart-history soak started against $Ip every $IntervalMinutes min."
Write-Host "Firmware is NOT modified. Output: $OutputDir"

$requests = 0
$failures = 0

do {
    $info = Invoke-JsonGet '/api/system/info'
    $realtime = Invoke-JsonGet '/api/dashboard/chart/realtime'
    $reboot = Invoke-JsonGet '/api/reboot/last'

    if (-not $info) {
        Write-Host "[$(Get-Date -Format T)] ${Ip}: unreachable"
        if (-not $previous -or $previous.reachable) {
            Save-Incident 'unreachable' $null
        }
        $previous = [pscustomobject]@{ reachable = $false; uptime = $null; rebootIdx = $null; sha = $null }
        $previous | ConvertTo-Json | Set-Content -Encoding UTF8 -Path $statePath
        [System.Threading.ManualResetEvent]::new($false).WaitOne($IntervalMinutes * 60000) | Out-Null
        continue
    }

    $uptime = [int64]$info.miner.uptimeSeconds
    $rebootIdx = if ($reboot) { [int64]$reboot.idx } else { -1 }
    $reason = $null

    if ($previous) {
        if (-not $previous.reachable) {
            $reason = 'recovered'
        } elseif ($null -ne $previous.uptime -and $uptime -lt $previous.uptime) {
            $reason = 'uptime_reset'
        } elseif ($null -ne $previous.rebootIdx -and $rebootIdx -gt $previous.rebootIdx) {
            $reason = 'new_reboot_record'
        } elseif ($null -ne $previous.sha -and $previous.sha -ne $info.identity.appSha256) {
            $reason = 'firmware_changed'
        }
    }

    if ($reason) { Save-Incident $reason $info }

    # The actual stress: one chart-history request, exactly what the dashboard
    # issues when it opens the chart tab.
    #
    # The body is written to one reused file so the metadata at its tail can be
    # read back. Only the tail is scanned: parsing a 2 000-row payload with
    # ConvertFrom-Json would cost far more than the request it measures.
    $bodyPath = Join-Path $OutputDir "body_$safeIp.json"
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $code = & curl.exe --silent --max-time 30 --output $bodyPath --write-out '%{http_code}' `
        ("http://$Ip/api/dashboard/chart/history?interval=1") 2>$null
    $stopwatch.Stop()
    $requests++
    if ($code -ne '200') { $failures++ }

    # dequeSize is the variable that actually drives the failure: the chart
    # document stops growing once the response caps at 2 000 rows, but the
    # deque keeps filling until the 24 h retention window is full. Recording it
    # per sample is what turns the prediction into something testable.
    $dequeSize = $null
    $sampledSize = $null
    $bodyBytes = 0
    if (Test-Path $bodyPath) {
        $bodyBytes = (Get-Item $bodyPath).Length
        $text = Get-Content -Raw -Encoding UTF8 $bodyPath
        # "size" ends the metadata object; the rows themselves are numeric
        # arrays, so these two patterns cannot match anything else in the body.
        if ($text -match '"size":(\d+)') { $dequeSize = [int]$Matches[1] }
        if ($text -match '"sampledSize":(\d+)') { $sampledSize = [int]$Matches[1] }
    }

    $row = [pscustomobject]@{
        timestamp     = (Get-Date -Format o)
        ip            = $Ip
        firmware      = $info.identity.fwVersion
        appSha256     = $info.identity.appSha256
        uptimeSeconds = $uptime
        rebootIdx     = $rebootIdx
        freeHeap      = $info.miner.freeHeap
        minFreeHeap   = $info.miner.minFreeHeap
        freePsramKB   = if ($realtime) { $realtime.statistics[0][12] } else { $null }
        dequeSize     = $dequeSize
        sampledSize   = $sampledSize
        # Wall-clock seconds per stored sample. Drift here silently moves the
        # moment the retention window fills, so it is worth tracking directly.
        # Wall-clock seconds per stored sample. The deque only holds the
        # retention window, so past 24 h the covered span is the window, not the
        # full uptime - dividing uptime by the deque size would report a growing
        # "interval" for a run whose cadence never changed (at 33 h it claimed
        # 8.3 s when the true interval was still 6.2 s).
        cadenceSec    = if ($dequeSize -and $dequeSize -gt 0) {
                            $span = [math]::Min([double]$uptime, 86400.0)
                            [math]::Round($span / [double]$dequeSize, 3)
                        } else { $null }
        bodyBytes     = $bodyBytes
        httpCode      = $code
        latencyMs     = [math]::Round($stopwatch.Elapsed.TotalMilliseconds)
        requestsSoFar = $requests
        failuresSoFar = $failures
    }

    # A previous version of this script wrote CSVs without the deque columns.
    # Those rows cannot be back-filled, so rotate them out instead of appending
    # rows under a mismatched header.
    if ((Test-Path $csvPath) -and -not (Select-String -Path $csvPath -Pattern 'dequeSize' -Quiet)) {
        Move-Item -Force $csvPath ($csvPath -replace '\.csv$', '.pre-deque.csv')
    }

    if (Test-Path $csvPath) {
        $row | Export-Csv -NoTypeInformation -Append -Encoding UTF8 -Path $csvPath
    } else {
        $row | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $csvPath
    }

    # Update the in-memory baseline as well as the file. Writing only the file
    # left $previous frozen at the pre-change values, so after an OTA or a reboot
    # every subsequent round re-detected the same "uptime_reset"/"firmware_changed"
    # and captured a duplicate incident (observed every 5 min for over an hour).
    $previous = [pscustomobject]@{
        reachable = $true
        uptime    = $uptime
        rebootIdx = $rebootIdx
        sha       = $info.identity.appSha256
    }
    $previous | ConvertTo-Json | Set-Content -Encoding UTF8 -Path $statePath

    Write-Host ("[{0}] up={1}s deque={2} http={3} {4}ms heap={5} psram={6}KB sha={7}" -f `
        (Get-Date -Format T), $uptime, $dequeSize, $code, $row.latencyMs, $row.freeHeap, $row.freePsramKB, $row.appSha256)

    # If the request itself killed the board, the next iteration sees it as
    # unreachable and captures the dump; no extra probing needed here.
    [System.Threading.ManualResetEvent]::new($false).WaitOne($IntervalMinutes * 60000) | Out-Null
} while ($true)
