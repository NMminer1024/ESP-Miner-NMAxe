# Steady-state capture at the production operating point, for dither metrics.
#
# Records the live point for N minutes at ~4 Hz and writes the same column layout
# fan_verify already uses, so fan_wander_corr / fan_stage_stats can read it.
#
# Usage: .\steady_capture.ps1 -Ip 192.168.124.2 -Minutes 5 -Out <csv>
param(
    [string]$Ip = '192.168.124.2',
    [int]$Minutes = 5,
    [string]$Out = "$PSScriptRoot\steady.csv",
    [int]$IntervalMs = 250
)
$ErrorActionPreference = 'Continue'
function Get-Info { try { return Invoke-RestMethod "http://$Ip/api/system/info" -TimeoutSec 3 } catch { return $null } }

$fs = [System.IO.File]::Open($Out, 'Create', 'Write', 'ReadWrite')
$w = New-Object System.IO.StreamWriter($fs, (New-Object System.Text.UTF8Encoding($false)))
$w.AutoFlush = $true
$w.WriteLine('t_s,iso,asic_c,vcore_c,fan0_pct,fan1_pct,fan0_rpm,fan1_rpm,vbus_v,ibus_a,pwr_w,hr_ghs,freq_req,vcore_req,vcore_real,mode,protection,phase')

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$end = $Minutes * 60
Write-Host "capturing $Minutes min -> $Out"
while ($sw.Elapsed.TotalSeconds -lt $end) {
    $i = Get-Info
    if ($null -ne $i) {
        $f0 = ($i.fans | Where-Object { $_.id -eq 0 } | Select-Object -First 1)
        $f1 = ($i.fans | Where-Object { $_.id -eq 1 } | Select-Object -First 1)
        $w.WriteLine(('{0:F3},{1},{2:F2},{3:F2},{4},{5},{6},{7},{8:F2},{9:F3},{10:F2},{11:F1},{12},{13},{14},{15},{16},{17}' -f `
            $sw.Elapsed.TotalSeconds, (Get-Date -Format 'HH:mm:ss.fff'),
            $i.temps.asic, $i.temps.vcore, $f0.speed, $f1.speed, $f0.rpm, $f1.rpm,
            ($i.power.vbus / 1000.0), ($i.power.ibus / 1000.0), $i.power.power,
            $i.miner.hashRate, $i.asic.freqReq, $i.asic.vcoreReq, $i.asic.vcoreReal,
            $i.miner.paused, $i.miner.protection, 'STEADY'))
    }
    Start-Sleep -Milliseconds $IntervalMs
}
$w.Close()
Write-Host "done -> $Out"
