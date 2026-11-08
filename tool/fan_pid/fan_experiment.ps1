# Fan-loop identification experiment for NMQAxe++ (two fans: 0 = ASIC, 1 = Vcore).
#
# Runs a fixed, time-scheduled sequence while sampling /api/system/info at 4 Hz:
#   A) baseline auto PID at the current (hot) operating point
#   B) switch to a low-power benchmark point to get thermal headroom
#   C) manual Vcore-fan duty steps -> plant step response (gain / time constant)
#   D) hand back to auto at two target temperatures -> observe limit cycling
#   E) restore the original mining point and fan policy
#
# Safety: if Vcore exceeds $VcoreAbortC (or ASIC exceeds $AsicAbortC) the script
# forces both fans to 100 % and aborts.
#
# Usage:
#   .\fan_experiment.ps1 -Ip 192.168.124.18 -Out D:\...\fan_base.csv
param(
    [string]$Ip = '192.168.124.18',
    [string]$Out = "$PSScriptRoot\fan_experiment.csv",
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
    try {
        Invoke-RestMethod "http://$Ip/api/setting/preference" -Method Patch -ContentType 'application/json' -Body $body -TimeoutSec 5 | Out-Null
        return $true
    } catch { Write-Host "  ! fan set failed: $($_.Exception.Message)"; return $false }
}
function Set-FanPolicy {
    param([int]$Auto, [double]$Target)
    $body = @{ fans = @(@{ id = 0; auto = $Auto; target = 40.0; speed = 80 },
                         @{ id = 1; auto = $Auto; target = $Target; speed = 100 }) } | ConvertTo-Json -Depth 4
    try {
        Invoke-RestMethod "http://$Ip/api/setting/preference" -Method Patch -ContentType 'application/json' -Body $body -TimeoutSec 5 | Out-Null
        return $true
    } catch { Write-Host "  ! policy set failed: $($_.Exception.Message)"; return $false }
}
function Apply-Point {
    param([int]$Freq, [int]$Vcore)
    $body = @{ freq = $Freq; vcore = $Vcore } | ConvertTo-Json
    try {
        Invoke-RestMethod "http://$Ip/api/benchmark/apply" -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 10 | Out-Null
        return $true
    } catch { Write-Host "  ! apply failed: $($_.Exception.Message)"; return $false }
}

# ---- experiment timeline (seconds) ----
$POINT_LOW_F = 555; $POINT_LOW_V = 1170
$schedule = @(
    @{ t =    0; phase = 'A_AUTO_HOT';        action = { } },
    @{ t =  150; phase = 'B_APPLY_LOW';       action = { Apply-Point $POINT_LOW_F $POINT_LOW_V | Out-Null } },
    @{ t =  150; phase = 'B_AUTO_LOW';        action = { } },
    @{ t =  480; phase = 'C_STEP_100';        action = { Set-Fan -Id 1 -Auto 0 -Target 80 -Speed 100 | Out-Null } },
    @{ t =  720; phase = 'C_STEP_75';         action = { Set-Fan -Id 1 -Auto 0 -Target 80 -Speed 75  | Out-Null } },
    @{ t =  960; phase = 'C_STEP_55';         action = { Set-Fan -Id 1 -Auto 0 -Target 80 -Speed 55  | Out-Null } },
    @{ t = 1200; phase = 'C_STEP_75B';        action = { Set-Fan -Id 1 -Auto 0 -Target 80 -Speed 75  | Out-Null } },
    @{ t = 1440; phase = 'D_AUTO_80';         action = { Set-FanPolicy -Auto 1 -Target 80 | Out-Null } },
    @{ t = 1800; phase = 'D_AUTO_65';         action = { Set-Fan -Id 1 -Auto 1 -Target 65 -Speed 100 | Out-Null } },
    @{ t = 2280; phase = 'E_RESTORE';         action = { Set-FanPolicy -Auto 1 -Target 80 | Out-Null; Apply-Point 725 1250 | Out-Null } }
)
$endSec = 2400
$idx = 0
$t0 = [Environment]::TickCount64
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$fs = [System.IO.File]::Open($Out, 'Create', 'Write', 'ReadWrite')
$w = New-Object System.IO.StreamWriter($fs, (New-Object System.Text.UTF8Encoding($false)))
$w.AutoFlush = $true
$w.WriteLine('t_s,iso,asic_c,vcore_c,fan0_pct,fan1_pct,fan0_rpm,fan1_rpm,vbus_v,ibus_a,pwr_w,hr_ghs,freq_req,vcore_req,vcore_real,mode,protection,phase')

$aborted = $false
try {
    Write-Host "start -> $Out  (Ctrl-C aborts and restores the fan policy)"
    while ($sw.Elapsed.TotalSeconds -lt $endSec) {
        $t = [math]::Round($sw.Elapsed.TotalSeconds, 3)

        while ($idx -lt $schedule.Count -and $schedule[$idx].t -le $t) {
            $ev = $schedule[$idx]
            Write-Host ("[{0,6:N1}s] {1}" -f $t, $ev.phase)
            $null = & $ev.action
            $idx++
            # allow the phase label to be recorded from the next sample on
        }
        $phase = 'E_DONE'
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
                Write-Host "SAFETY ABORT: asic=$a C vcore=$v C -> both fans 100%"
                Set-FanPolicy -Auto 0 -Target 80 | Out-Null
                $aborted = $true
                break
            }
        }
        Start-Sleep -Milliseconds $IntervalMs
    }
}
finally {
    Write-Host "cleanup: restoring auto fan policy and original mining point"
    Set-FanPolicy -Auto 1 -Target 80 | Out-Null
    Apply-Point 725 1250 | Out-Null
    $w.Close()
    Write-Host ("done -> {0} (aborted={1})" -f $Out, $aborted)
}
