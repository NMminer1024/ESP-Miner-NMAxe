# bm_abort_race_test.ps1 - stop the sweep while the PLL hot-switch is in flight.
#
# Regression guard for two coupled bugs in the in-place sweep:
#   1. bm_apply_point used to block through the vcore settle + PLL wait, so a Stop
#      issued mid-switch was only honoured up to ~15 s later.
#   2. bm_apply_point's "already at target" fast path returned success while a
#      pending request still pointed at the sweep frequency, so the restore could
#      claim to be done and then get overwritten by the stale target.
#
# The test starts short sweeps and stops them at increasing offsets chosen to land
# inside the switch window, then requires the ASIC to converge back to the
# pre-test frequency with no reboot.
param(
    [Parameter(Mandatory = $true)][string]$Ip,
    [Parameter(Mandatory = $true)][int]$TestFreq,
    [Parameter(Mandatory = $true)][int]$Vcore,
    [int[]]$DelaysMs = @(400, 600, 800, 1000, 1500),
    [string]$LogFile = "tmp\bm-abort-race.log"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$wsScript = Join-Path $root '.github\skills\board-closed-loop-regression\scripts\ws_log.ps1'
Push-Location $root
try {
    function Get-Info { (curl.exe -s --max-time 8 "http://$Ip/api/system/info") | ConvertFrom-Json }

    # Reuse the repo's own WS log capture (auto-reconnects across reboots).
    function Start-WsLog($ip, $out) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $out) | Out-Null
        Remove-Item $out -ErrorAction SilentlyContinue
        return Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $wsScript,
            '-Ip', $ip, '-Out', $out
        )
    }
    function Stop-WsLog($proc) {
        if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
        Start-Sleep -Milliseconds 500
    }

    $b = Get-Info
    $baseFreq = [int]$b.asic.freqReq
    "BASELINE fw=$($b.identity.fwVersion) asic=$baseFreq/$($b.asic.vcoreReq) hr=$([math]::Round($b.miner.hashRate,1)) uptime=$($b.miner.uptimeSeconds)"
    if ($baseFreq -eq $TestFreq) { throw "Baseline freq equals TestFreq ($TestFreq) - pick another target." }

    $logger = Start-WsLog $Ip $LogFile
    Start-Sleep -Seconds 2

    $pass = 0; $fail = 0
    foreach ($d in $DelaysMs) {
        $body = '{"freqMin":' + $TestFreq + ',"freqMax":' + $TestFreq + ',"freqStep":25,"vcoreMin":' + $Vcore +
                ',"vcoreMax":' + $Vcore + ',"vcoreStep":25,"sampleIntv":2,"bmTime":15,"stabTime":15}'
        curl.exe -s --max-time 10 -X POST "http://$Ip/api/benchmark/start" -H "Content-Type: application/json" -d $body | Out-Null
        Start-Sleep -Milliseconds $d
        curl.exe -s --max-time 10 -X POST "http://$Ip/api/benchmark/stop" | Out-Null

        $converged = $false; $last = $null
        for ($i = 0; $i -lt 40; $i++) {
            Start-Sleep -Milliseconds 500
            $s = Get-Info
            $last = $s
            if ($s.miner.bmMode -eq 0 -and [int]$s.asic.freqReq -eq $baseFreq) { $converged = $true; break }
        }
        if ($converged) { $pass++; $tag = 'PASS' } else { $fail++; $tag = 'FAIL' }
        "{0} stop@{1}ms -> asic={2}/{3} bmMode={4} hr={5} uptime={6}" -f `
            $tag, $d, $last.asic.freqReq, $last.asic.vcoreReq, $last.miner.bmMode,
            [math]::Round($last.miner.hashRate, 1), $last.miner.uptimeSeconds
    }

    $f = Get-Info
    "SUMMARY pass=$pass fail=$fail finalAsic=$($f.asic.freqReq)/$($f.asic.vcoreReq) uptime=$($f.miner.uptimeSeconds) (baseline uptime $($b.miner.uptimeSeconds))"
    "REBOOT-LAST " + (curl.exe -s --max-time 8 "http://$Ip/api/reboot/last")

    Stop-WsLog $logger
    "=== DEVICE LOG (BM/switch only) ==="
    Get-Content $LogFile -ErrorAction SilentlyContinue |
        Select-String -Pattern '\[BM\]|hot-switch|PLL' | ForEach-Object { $_.Line }
}
finally {
    Pop-Location
}
