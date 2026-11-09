# Post-flash verification for the smoothed fan loop, NEXUS variant.
#
# Nexus is a 3-phase board whose two loops are BOTH saturated at the factory
# defaults (ASIC fan 100 % at 45 C target, Vcore fan 0 % at 67 C target), so the
# QAxe++ schedule (555/1170, Vcore target 67) does not exercise anything here.
# This script therefore:
#   - runs at 700 MHz / 1150 mV (~136 W, ~11.3 A of the 15 A adapter), chosen so
#     both sensors sit inside a band the fans can actually move;
#   - uses ASIC target 52 C and Vcore target 67 C, both inside the measured band;
#   - steps the load DOWN (700/1150 -> 650/1120) instead of up, because the
#     production point (775/1200) already draws 14.07 A = 94 % of the adapter and
#     a step up would risk the adapter limit, not the fans.
#
# The SAME script is run once on the old firmware and once on the new one; the
# two CSVs are the A/B pair.
#
# Usage:
#   .\fan_verify_nexus.ps1 -Ip 192.168.124.2 -Out <csv>
param(
    [string]$Ip = '192.168.124.2',
    [string]$Out = "$PSScriptRoot\fan_verify_nexus.csv",
    [double]$TargetAsic  = 52.0,
    [double]$TargetVcore = 67.0,
    [int]$IntervalMs = 250,
    [double]$VcoreAbortC = 76.0,
    [double]$AsicAbortC  = 66.0
)
$ErrorActionPreference = 'Continue'

function Get-Info { try { return Invoke-RestMethod "http://$Ip/api/system/info" -TimeoutSec 3 } catch { return $null } }
function Set-Fan {
    param([int]$Id, [int]$Auto, [double]$Target, [int]$Speed)
    $body = @{ fans = @(@{ id = $Id; auto = $Auto; target = $Target; speed = $Speed }) } | ConvertTo-Json -Depth 4
    try { Invoke-RestMethod "http://$Ip/api/setting/preference" -Method Patch -ContentType 'application/json' -Body $body -TimeoutSec 5 | Out-Null; return $true }
    catch { Write-Host "  ! fan set failed: $($_.Exception.Message)"; return $false }
}
function Set-Policy {
    param([int]$AutoAsic, [int]$AutoVcore, [double]$Ta, [double]$Tv, [int]$Sa, [int]$Sv)
    $body = @{ fans = @(
        @{ id = 0; auto = $AutoAsic;  target = $Ta; speed = $Sa },
        @{ id = 1; auto = $AutoVcore; target = $Tv; speed = $Sv }
    ) } | ConvertTo-Json -Depth 4
    try { Invoke-RestMethod "http://$Ip/api/setting/preference" -Method Patch -ContentType 'application/json' -Body $body -TimeoutSec 5 | Out-Null; return $true }
    catch { Write-Host "  ! policy set failed: $($_.Exception.Message)"; return $false }
}
function Apply-Point {
    param([int]$Freq, [int]$Vcore)
    $body = @{ freq = $Freq; vcore = $Vcore } | ConvertTo-Json
    try { Invoke-RestMethod "http://$Ip/api/benchmark/apply" -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 10 | Out-Null; return $true }
    catch { Write-Host "  ! apply failed: $($_.Exception.Message)"; return $false }
}

$schedule = @(
    @{ t =    0; phase = 'A_SETUP';    action = { Apply-Point 700 1150 | Out-Null; Set-Policy 1 1 $TargetAsic $TargetVcore 80 100 | Out-Null } },
    @{ t =  180; phase = 'B_AUTO';     action = { } },
    @{ t =  480; phase = 'C_COOLDOWN'; action = { Set-Fan -Id 1 -Auto 0 -Target $TargetVcore -Speed 100 | Out-Null } },
    @{ t =  570; phase = 'D_HANDOVER'; action = { Set-Fan -Id 1 -Auto 1 -Target $TargetVcore -Speed 100 | Out-Null } },
    @{ t =  810; phase = 'E_LOADDOWN'; action = { Apply-Point 650 1120 | Out-Null } },
    @{ t = 1050; phase = 'F_RESTORE';  action = { Apply-Point 700 1150 | Out-Null; Set-Policy 1 1 45 67 80 100 | Out-Null } }
)
$endSec = 1110
$idx = 0
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$fs = [System.IO.File]::Open($Out, 'Create', 'Write', 'ReadWrite')
$w = New-Object System.IO.StreamWriter($fs, (New-Object System.Text.UTF8Encoding($false)))
$w.AutoFlush = $true
$w.WriteLine('t_s,iso,asic_c,vcore_c,fan0_pct,fan1_pct,fan0_rpm,fan1_rpm,vbus_v,ibus_a,pwr_w,hr_ghs,freq_req,vcore_req,vcore_real,mode,protection,phase')

$aborted = $false
try {
    Write-Host "start -> $Out  (targets: asic $TargetAsic C, vcore $TargetVcore C; Ctrl-C aborts and restores)"
    while ($sw.Elapsed.TotalSeconds -lt $endSec) {
        $t = [math]::Round($sw.Elapsed.TotalSeconds, 3)
        while ($idx -lt $schedule.Count -and $schedule[$idx].t -le $t) {
            $ev = $schedule[$idx]
            Write-Host ("[{0,6:N1}s] {1}" -f $t, $ev.phase)
            $null = & $ev.action
            $idx++
        }
        $phase = 'G_DONE'
        for ($i = $schedule.Count - 1; $i -ge 0; $i--) { if ($schedule[$i].t -le $t) { $phase = $schedule[$i].phase; break } }

        $i = Get-Info
        if ($null -ne $i) {
            $a = $i.temps.asic; $v = $i.temps.vcore
            $f0 = ($i.fans | Where-Object { $_.id -eq 0 } | Select-Object -First 1)
            $f1 = ($i.fans | Where-Object { $_.id -eq 1 } | Select-Object -First 1)
            $w.WriteLine(('{0:F3},{1},{2:F2},{3:F2},{4},{5},{6},{7},{8:F2},{9:F3},{10:F2},{11:F2},{12},{13},{14},{15},{16},{17}' -f `
                $t, (Get-Date -Format 'HH:mm:ss.fff'), $a, $v,
                $f0.speed, $f1.speed, $f0.rpm, $f1.rpm,
                ($i.power.vbus / 1000.0), ($i.power.ibus / 1000.0), $i.power.power,
                $i.miner.hashRate, $i.asic.freqReq, $i.asic.vcoreReq, $i.asic.vcoreReal,
                $i.miner.bmMode, $i.miner.protection, $phase))

            if ($v -gt $VcoreAbortC -or $a -gt $AsicAbortC -or $i.miner.protection -ne 'none') {
                Write-Host "SAFETY ABORT: asic=$a C vcore=$v C protection=$($i.miner.protection) -> both fans 100%"
                Set-Policy 0 0 45 67 100 100 | Out-Null
                $aborted = $true
                break
            }
        }
        Start-Sleep -Milliseconds $IntervalMs
    }
}
finally {
    Write-Host 'cleanup: auto fan policy (asic 45, vcore 67) and production point 775/1200'
    Set-Policy 1 1 45 67 80 100 | Out-Null
    Apply-Point 775 1200 | Out-Null
    $w.Close()
    Write-Host ("done -> {0} (aborted={1})" -f $Out, $aborted)
}
