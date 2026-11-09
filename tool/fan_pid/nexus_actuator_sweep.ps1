# Measure the actuator: does the Vcore fan's RPM actually respond between 40 %
# and 60 % duty?
#
# Why this comes first: the loop cannot be made "stable" if the band it works in
# has no actuator authority. If RPM is flat across 45-55 %, the continuous PI is
# chasing a dead zone and the correct fix is a coarse duty grid, not a gain change.
# If RPM is smooth there, the residual motion is pure measurement dither and a
# small duty hysteresis removes it.
#
# The ASIC fan is pinned to a fixed duty so the heatsink boundary condition does
# not move during the sweep.
#
# Usage: .\nexus_actuator_sweep.ps1 -Ip 192.168.124.2 -Out <csv>
param(
    [string]$Ip = '192.168.124.2',
    [string]$Out = "$PSScriptRoot\nexus_actuator_sweep.csv",
    [int]$HoldSec = 75,
    [int]$IntervalMs = 500,
    [int]$PinFan0 = 80,
    [double]$AsicAbortC = 65.0,
    [double]$VcoreAbortC = 74.0
)
$ErrorActionPreference = 'Continue'

function Get-Info { try { return Invoke-RestMethod "http://$Ip/api/system/info" -TimeoutSec 3 } catch { return $null } }
function Set-Both {
    param([int]$A0, [int]$S0, [int]$A1, [int]$S1)
    $body = @{ fans = @(
        @{ id = 0; auto = $A0; target = 45.0; speed = $S0 },
        @{ id = 1; auto = $A1; target = 67.0; speed = $S1 }
    ) } | ConvertTo-Json -Depth 4
    try { Invoke-RestMethod "http://$Ip/api/setting/preference" -Method Patch -ContentType 'application/json' -Body $body -TimeoutSec 5 | Out-Null }
    catch { Write-Host "  ! policy set failed: $($_.Exception.Message)" }
}

$duties = @(40, 45, 48, 50, 52, 55, 60)
$fs = [System.IO.File]::Open($Out, 'Create', 'Write', 'ReadWrite')
$w = New-Object System.IO.StreamWriter($fs, (New-Object System.Text.UTF8Encoding($false)))
$w.AutoFlush = $true
$w.WriteLine('t_s,duty,fan1_pct,fan1_rpm,fan0_rpm,asic_c,vcore_c,pwr_w,hr_ghs')

$aborted = $false
$reason  = ''
$sw = [System.Diagnostics.Stopwatch]::StartNew()
try {
    Set-Both 0 $PinFan0 0 50
    Write-Host "start -> $Out (fan0 pinned $PinFan0 %, fan1 swept 40..60 %)"
    foreach ($d in $duties) {
        if ($aborted) { break }
        Set-Both 0 $PinFan0 0 $d
        $tStep = $sw.Elapsed.TotalSeconds
        Write-Host ("[{0,6:N0}s] fan1 -> {1} %" -f $tStep, $d)
        # Discard the first 15 s so each row is a settled point.
        while ($sw.Elapsed.TotalSeconds -lt ($tStep + $HoldSec)) {
            $i = Get-Info
            if ($null -ne $i) {
                $f0 = ($i.fans | Where-Object { $_.id -eq 0 } | Select-Object -First 1)
                $f1 = ($i.fans | Where-Object { $_.id -eq 1 } | Select-Object -First 1)
                if (($sw.Elapsed.TotalSeconds - $tStep) -gt 15) {
                    $w.WriteLine(('{0:F2},{1},{2},{3},{4},{5:F2},{6:F2},{7:F2},{8:F1}' -f `
                        $sw.Elapsed.TotalSeconds, $d, $f1.speed, $f1.rpm, $f0.rpm,
                        $i.temps.asic, $i.temps.vcore, $i.power.power, $i.miner.hashRate))
                }
                if ($i.temps.asic -gt $AsicAbortC)  { $aborted = $true; $reason = "ASIC $($i.temps.asic)C" }
                if ($i.temps.vcore -gt $VcoreAbortC) { $aborted = $true; $reason = "VRM $($i.temps.vcore)C" }
                if ($aborted) { Write-Host "SAFETY ABORT: $reason"; Set-Both 0 $PinFan0 0 100; break }
            }
            Start-Sleep -Milliseconds $IntervalMs
        }
    }
}
finally {
    Write-Host 'cleanup: both fans back to auto'
    Set-Both 1 100 1 100
    $w.Close()
    Write-Host ("done -> {0} (aborted={1}{2})" -f $Out, $aborted, $(if ($reason) { ", $reason" } else { '' }))
}
