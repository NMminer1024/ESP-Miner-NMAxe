# Finish the Nexus VRM (fan1) identification that nexus_identify.ps1 left open.
#
# The full run was interrupted during V50 (207 s of a 240 s segment), so V0 and
# V100B were never sampled. This script replays only the VRM tail, with the ASIC
# fan pinned at 100 % (the same condition the original sweep used while measuring
# fan1) so the die cannot run away and the two channels stay independent.
#
#   V50  0-180 s   (re-settle the point the interrupted run stopped on)
#   V0   180-420 s
#   V100 420-660 s  (return leg: tau on the cooling direction)
#
# Safety: every abort forces BOTH fans to 100 % and restores auto on exit.
#
# Usage:
#   .\nexus_identify_vcore_tail.ps1 -Ip 192.168.124.2 -Freq 700 -Vcore 1150 -Out <csv>
param(
    [string]$Ip = '192.168.124.2',
    [string]$Out = "$PSScriptRoot\nexus_ident_vcore_tail.csv",
    [int]$Freq = 700,
    [int]$Vcore = 1150,
    [int]$IntervalMs = 250,
    [double]$AsicAbortC = 67.0,
    [double]$VcoreAbortC = 73.0
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
function Apply-Point {
    param([int]$F, [int]$V)
    $body = @{ freq = $F; vcore = $V } | ConvertTo-Json
    try { Invoke-RestMethod "http://$Ip/api/benchmark/apply" -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 10 | Out-Null }
    catch { Write-Host "  ! apply failed: $($_.Exception.Message)" }
}

$steps = @(
    @{ t =   0; duty =  50; name = 'V50'   },
    @{ t = 180; duty =   0; name = 'V0'    },
    @{ t = 420; duty = 100; name = 'V100B' }
)
$endSec = 660
$idx = 0
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$fs = [System.IO.File]::Open($Out, 'Create', 'Write', 'ReadWrite')
$w = New-Object System.IO.StreamWriter($fs, (New-Object System.Text.UTF8Encoding($false)))
$w.AutoFlush = $true
$w.WriteLine('t_s,iso,asic_c,vcore_c,fan0_pct,fan1_pct,fan0_rpm,fan1_rpm,vbus_v,ibus_a,pwr_w,hr_ghs,freq_req,vcore_req,vcore_real,mode,protection,phase')

$aborted = $false
$reason  = ''
try {
    Apply-Point $Freq $Vcore
    Set-Both 0 100 0 50
    Write-Host "start -> $Out  ($Freq MHz / $Vcore mV, asic abort $AsicAbortC C, vrm abort $VcoreAbortC C)"
    while ($sw.Elapsed.TotalSeconds -lt $endSec) {
        $t = [math]::Round($sw.Elapsed.TotalSeconds, 3)
        while ($idx -lt $steps.Count -and $steps[$idx].t -le $t) {
            $s = $steps[$idx]
            Write-Host ("[{0,6:N1}s] fan1 -> {1} % ({2})" -f $t, $s.duty, $s.name)
            Set-Both 0 100 0 $s.duty
            $idx++
        }
        $phase = 'DONE'
        for ($i = $steps.Count - 1; $i -ge 0; $i--) { if ($steps[$i].t -le $t) { $phase = $steps[$i].name; break } }

        $i = Get-Info
        if ($null -ne $i) {
            $f0 = ($i.fans | Where-Object { $_.id -eq 0 } | Select-Object -First 1)
            $f1 = ($i.fans | Where-Object { $_.id -eq 1 } | Select-Object -First 1)
            $w.WriteLine(('{0:F3},{1},{2:F2},{3:F2},{4},{5},{6},{7},{8:F2},{9:F3},{10:F2},{11:F1},{12},{13},{14},{15},{16},{17}' -f `
                $t, (Get-Date -Format 'HH:mm:ss.fff'), $i.temps.asic, $i.temps.vcore,
                $f0.speed, $f1.speed, $f0.rpm, $f1.rpm,
                ($i.power.vbus / 1000.0), ($i.power.ibus / 1000.0), $i.power.power,
                $i.miner.hashRate, $i.asic.freqReq, $i.asic.vcoreReq, $i.asic.vcoreReal,
                $i.protection, $i.protection, $phase))
            if ($i.temps.asic -gt $AsicAbortC)  { $aborted = $true; $reason = "ASIC $($i.temps.asic) C" }
            if ($i.temps.vcore -gt $VcoreAbortC) { $aborted = $true; $reason = "VRM $($i.temps.vcore) C" }
            if ($i.protection -and $i.protection -ne 'none') { $aborted = $true; $reason = "protection=$($i.protection)" }
            if ($aborted) {
                Write-Host "SAFETY ABORT: $reason -> both fans 100%"
                Set-Both 0 100 0 100
                break
            }
        }
        Start-Sleep -Milliseconds $IntervalMs
    }
}
finally {
    Write-Host 'cleanup: both fans back to auto, restore the production point'
    Set-Both 1 100 1 100
    Apply-Point 775 1200
    $w.Close()
    Write-Host ("done -> {0} (aborted={1}{2})" -f $Out, $aborted, $(if ($reason) { ", $reason" } else { '' }))
}
