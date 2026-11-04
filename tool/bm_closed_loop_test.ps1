# bm_closed_loop_test.ps1 — benchmark hot-switch closed-loop regression helper.
#
# Runs one scenario against a real board and records the evidence needed to prove
# the in-place (no-reboot) sweep behaves:
#   - baseline ASIC point + uptime
#   - live device log over the WebSocket (tool/wslog.js)
#   - POST /api/benchmark/start with a narrow window around the test point
#   - either wait for the sweep to complete (default) or stop it mid-sampling
#   - final mode / ASIC point / uptime / last reboot reason
#
# Usage:
#   powershell -File tool\bm_closed_loop_test.ps1 -Ip 192.168.124.88 -Env NMAxeGamma `
#       -FreqMin 650 -FreqMax 650 -Vcore 1125 -Stab 15 -Bm 15 -LogFile tmp\bm-88-hot.log
#   add -StopAfterSec 26 to exercise the stop path instead of completion.
param(
    [Parameter(Mandatory = $true)][string]$Ip,
    [Parameter(Mandatory = $true)][int]$FreqMin,
    [Parameter(Mandatory = $true)][int]$FreqMax,
    [Parameter(Mandatory = $true)][int]$Vcore,
    [int]$FreqStep = 25,
    [int]$VcoreStep = 25,
    [int]$SampleIntv = 2,
    [int]$Stab = 15,
    [int]$Bm = 15,
    [int]$StopAfterSec = 0,
    [int]$WaitTimeoutSec = 300,
    [string]$LogFile = "tmp\bm-test.log"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    function Get-Info { (curl.exe -s --max-time 8 "http://$Ip/api/system/info") | ConvertFrom-Json }
    function Get-Bm { (curl.exe -s --max-time 8 "http://$Ip/api/benchmark") | ConvertFrom-Json }

    Remove-Item $LogFile -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $LogFile) | Out-Null
    $logSec = if ($StopAfterSec -gt 0) { $StopAfterSec + 40 } else { $WaitTimeoutSec + 20 }
    $logger = Start-Process -FilePath node -ArgumentList "tool\wslog.js $Ip $LogFile $logSec" -NoNewWindow -PassThru

    $b = Get-Info
    "BASELINE fw=$($b.identity.fwVersion) board=$($b.identity.hwModel) asic=$($b.asic.freqReq)/$($b.asic.vcoreReq) real=$($b.asic.vcoreReal) hr=$([math]::Round($b.miner.hashRate,1)) uptime=$($b.miner.uptimeSeconds) bmMode=$($b.miner.bmMode)"
    $baseFreq = [int]$b.asic.freqReq
    $baseVcore = [int]$b.asic.vcoreReq
    $baseUptime = [int]$b.miner.uptimeSeconds

    $body = '{"freqMin":' + $FreqMin + ',"freqMax":' + $FreqMax + ',"freqStep":' + $FreqStep +
            ',"vcoreMin":' + $Vcore + ',"vcoreMax":' + $Vcore + ',"vcoreStep":' + $VcoreStep +
            ',"sampleIntv":' + $SampleIntv + ',"bmTime":' + $Bm + ',"stabTime":' + $Stab + '}'
    "START body=$body"
    curl.exe -s --max-time 15 -X POST "http://$Ip/api/benchmark/start" -H "Content-Type: application/json" -d $body
    ""

    $stopAt = if ($StopAfterSec -gt 0) { (Get-Date).AddSeconds($StopAfterSec) } else { $null }
    $stopDone = $false
    $runStart = Get-Date
    $deadline = $runStart.AddSeconds($WaitTimeoutSec)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 3
        $mid = Get-Bm
        $ms = Get-Info
        "  t={0,3}s mode={1} cur={2}/{3} asic={4} hr={5} uptime={6}" -f `
            [int]((Get-Date) - $runStart).TotalSeconds, $mid.mode, $mid.curFreq, $mid.curVcore,
            "$($ms.asic.freqReq)/$($ms.asic.vcoreReq)", [math]::Round($ms.miner.hashRate, 1), $ms.miner.uptimeSeconds

        if ($stopAt -and -not $stopDone -and (Get-Date) -ge $stopAt) {
            $t0 = Get-Date
            curl.exe -s --max-time 10 -X POST "http://$Ip/api/benchmark/stop"
            ""
            while (((Get-Date) - $t0).TotalSeconds -lt 30) {
                Start-Sleep -Milliseconds 500
                $s = Get-Info
                if ($s.miner.bmMode -eq 0 -and [int]$s.asic.freqReq -eq $baseFreq) {
                    "STOP restored in {0}s -> asic={1}/{2} hr={3} uptime={4}" -f `
                        [math]::Round(((Get-Date) - $t0).TotalSeconds, 1), $s.asic.freqReq, $s.asic.vcoreReq,
                        [math]::Round($s.miner.hashRate, 1), $s.miner.uptimeSeconds
                    break
                }
            }
            $stopDone = $true
            break
        }
        if ($mid.mode -eq 0) { break }
    }

    $f = Get-Bm
    $fi = Get-Info
    "FINAL mode=$($f.mode) cur=$($f.curFreq)/$($f.curVcore) results=$($f.results.Count) freqs=$(($f.results | ForEach-Object { $_.freq }) -join ',')"
    "FINAL asic=$($fi.asic.freqReq)/$($fi.asic.vcoreReq) hr=$([math]::Round($fi.miner.hashRate,1)) uptime=$($fi.miner.uptimeSeconds) bmMode=$($fi.miner.bmMode) baseFreq=$baseFreq baseUptime=$baseUptime"
    "REBOOT-LAST " + (curl.exe -s --max-time 8 "http://$Ip/api/reboot/last")

    if (-not $logger.HasExited) { $logger.WaitForExit(($logSec + 20) * 1000) | Out-Null }
    $bmLines = Get-Content $LogFile -ErrorAction SilentlyContinue | Select-String -Pattern '\[BM\]|hot-switch|Fault'
    if ($bmLines) { "=== DEVICE LOG ($($bmLines.Count) lines) ==="; $bmLines | ForEach-Object { $_.Line } }
}
finally {
    Pop-Location
}
