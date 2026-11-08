# Sample the single-fan Gamma loop at a fixed auto target, old vs new firmware.
# No schedule beyond "hold this target": the point is to measure the duty motion
# of the closed loop itself.
#
# Usage:
#   .\gamma_loop_sample.ps1 -Ip 192.168.124.11 -Target 55 -Minutes 8 -Out <csv>
param(
    [string]$Ip = '192.168.124.11',
    [string]$Out = "$PSScriptRoot\gamma_loop.csv",
    [double]$Target = 55.0,
    [int]$Minutes = 8,
    [int]$IntervalMs = 250,
    [double]$AsicAbortC = 69.0
)
$ErrorActionPreference = 'Continue'

function Get-Info { try { return Invoke-RestMethod "http://$Ip/api/system/info" -TimeoutSec 3 } catch { return $null } }

# Arm the auto loop at the requested target.
$body = @{ fans = @(@{ id = 0; auto = 1; target = $Target; speed = 100 }) } | ConvertTo-Json -Depth 4
Invoke-RestMethod "http://$Ip/api/setting/preference" -Method Patch -ContentType 'application/json' -Body $body -TimeoutSec 5 | Out-Null
Write-Host ("armed auto fan, target {0} C, sampling {1} min -> {2}" -f $Target, $Minutes, $Out)

$endSec = $Minutes * 60
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$fs = [System.IO.File]::Open($Out, 'Create', 'Write', 'ReadWrite')
$w = New-Object System.IO.StreamWriter($fs, (New-Object System.Text.UTF8Encoding($false)))
$w.AutoFlush = $true
$w.WriteLine('t_s,iso,asic_c,vcore_c,fan0_pct,fan1_pct,fan0_rpm,fan1_rpm,pwr_w,hr_ghs,freq_req,phase')

try {
    while ($sw.Elapsed.TotalSeconds -lt $endSec) {
        $t = [math]::Round($sw.Elapsed.TotalSeconds, 3)
        $i = Get-Info
        if ($null -ne $i) {
            $f0 = ($i.fans | Where-Object { $_.id -eq 0 } | Select-Object -First 1)
            $f1 = ($i.fans | Where-Object { $_.id -eq 1 } | Select-Object -First 1)
            $w.WriteLine(('{0:F3},{1},{2:F2},{3:F2},{4},{5},{6},{7},{8:F2},{9:F1},{10},{11}' -f `
                $t, (Get-Date -Format 'HH:mm:ss.fff'), $i.temps.asic, $i.temps.vcore,
                $f0.speed, $f1.speed, $f0.rpm, $f1.rpm, $i.power.power,
                $i.miner.hashRate, $i.asic.freqReq, 'LOOP'))
            if ($i.temps.asic -gt $AsicAbortC) {
                Write-Host "SAFETY ABORT: ASIC $($i.temps.asic) C"
                $b2 = @{ fans = @(@{ id = 0; auto = 0; target = $Target; speed = 100 }) } | ConvertTo-Json -Depth 4
                Invoke-RestMethod "http://$Ip/api/setting/preference" -Method Patch -ContentType 'application/json' -Body $b2 -TimeoutSec 5 | Out-Null
                break
            }
        }
        Start-Sleep -Milliseconds $IntervalMs
    }
}
finally {
    $w.Close()
    Write-Host "done -> $Out"
}
