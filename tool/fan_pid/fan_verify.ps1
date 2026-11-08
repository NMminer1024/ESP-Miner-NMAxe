# Post-flash verification for the smoothed fan loop.
# Runs a fixed schedule at a point where the old PID hunted: 555MHz/1170mV with
# the Vcore target at 67 C (the 65.5-73 C controllable band for this board at
# 69 W puts 67 in the middle, so the loop has real authority instead of sitting
# on a saturation rail).
#
# The SAME script is run once on the old firmware and once on the new one; the
# two CSVs are the A/B pair.
#
# Usage:
#   .\fan_verify.ps1 -Ip 192.168.124.18 -Out D:\...\fan_verify_old.csv
param(
    [string]$Ip = '192.168.124.18',
    [string]$Out = "$PSScriptRoot\fan_verify.csv",
    [double]$TargetVcore = 67.0,
    [int]$IntervalMs = 250,
    [double]$VcoreAbortC = 100.0,
    [double]$AsicAbortC  = 62.0
)
$ErrorActionPreference = 'Continue'

function Get-Info {
    try { return Invoke-RestMethod "http://$Ip/api/system/info" -TimeoutSec 3 } catch { return $null }
}
function Set-Fan {
    param([int]$Id, [int]$Auto, [double]$Target, [int]$Speed)
    $body = @{ fans = @(@{ id = $Id; auto = $Auto; target = $Target; speed = $Speed }) } | ConvertTo-Json -Depth 4
    try { Invoke-RestMethod "http://$Ip/api/setting/preference" -Method Patch -ContentType 'application/json' -Body $body -TimeoutSec 5 | Out-Null; return $true }
    catch { Write-Host "  ! fan set failed: $($_.Exception.Message)"; return $false }
}
function Set-Policy {
    param([int]$AutoAsic, [int]$AutoVcore, [double]$TargetAsic, [double]$TargetVcore, [int]$SpeedAsic, [int]$SpeedVcore)
    $body = @{ fans = @(
        @{ id = 0; auto = $AutoAsic;  target = $TargetAsic;  speed = $SpeedAsic },
        @{ id = 1; auto = $AutoVcore; target = $TargetVcore; speed = $SpeedVcore }
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
    @{ t =    0; phase = 'A_SETUP';      action = { Apply-Point 555 1170 | Out-Null; Set-Policy 1 1 40 $TargetVcore 80 100 | Out-Null } },
    @{ t =  180; phase = 'B_AUTO';       action = { } },
    @{ t =  480; phase = 'C_COOLDOWN';   action = { Set-Fan -Id 1 -Auto 0 -Target $TargetVcore -Speed 100 | Out-Null } },
    @{ t =  570; phase = 'D_HANDOVER';   action = { Set-Fan -Id 1 -Auto 1 -Target $TargetVcore -Speed 100 | Out-Null } },
    @{ t =  810; phase = 'E_LOADSTEP';   action = { Apply-Point 725 1250 | Out-Null } },
    @{ t = 1050; phase = 'F_RESTORE';    action = { Apply-Point 555 1170 | Out-Null; Set-Policy 1 1 40 80 80 100 | Out-Null } }
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
    Write-Host "start -> $Out  (Ctrl-C aborts and restores the policy)"
    while ($sw.Elapsed.TotalSeconds -lt $endSec) {
        $t = [math]::Round($sw.Elapsed.TotalSeconds, 3)
        while ($idx -lt $schedule.Count -and $schedule[$idx].t -le $t) {
            $ev = $schedule[$idx]
            Write-Host ("[{0,6:N1}s] {1}" -f $t, $ev.phase)
            $null = & $ev.action
            $idx++
        }
        $phase = 'G_DONE'
        for ($i = $schedule.Count - 1; $i -ge 0; $i--) {
            if ($schedule[$i].t -le $t) { $phase = $schedule[$i].phase; break }
        }

        $i = Get-Info
        if ($null -ne $i) {
            $a = $i.temps.asic; $v = $i.temps.vcore
            $f0 = ($i.fans | Where-Object { $_.id -eq 0 } | Select-Object -First 1)
            $f1 = ($i.fans | Where-Object { $_.id -eq 1 } | Select-Object -First 1)
            $row = '{0:F3},{1},{2:F2},{3:F2},{4},{5},{6},{7},{8:F2},{9:F3},{10:F2},{11:F2},{12},{13},{14},{15},{16},{17}' -f `
                $t, (Get-Date -Format 'HH:mm:ss.fff'), $a, $v,
                $f0.speed, $f1.speed, $f0.rpm, $f1.rpm,
                ($i.power.vbus / 1000.0), ($i.power.ibus / 1000.0), $i.power.power,
                $i.miner.hashRate, $i.asic.freqReq, $i.asic.vcoreReq, $i.asic.vcoreReal,
                $i.miner.bmMode, $i.miner.protection, $phase
            $w.WriteLine($row)

            if ($v -gt $VcoreAbortC -or $a -gt $AsicAbortC) {
                Write-Host "SAFETY ABORT: asic=$a C vcore=$v C -> Vcore fan 100%"
                Set-Fan -Id 1 -Auto 0 -Target 65 -Speed 100 | Out-Null
                $aborted = $true
                break
            }
        }
        Start-Sleep -Milliseconds $IntervalMs
    }
}
finally {
    Write-Host "cleanup: restoring auto fan policy and the original 725/1250 operating point"
    Set-Policy 1 1 40 80 80 100 | Out-Null
    Apply-Point 725 1250 | Out-Null
    $w.Close()
    Write-Host ("done -> {0} (aborted={1})" -f $Out, $aborted)
}
