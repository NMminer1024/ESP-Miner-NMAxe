# Focused test for the cooldown -> auto hand-over cushion.
# Much shorter than fan_verify.ps1: only the path that regressed.
#
#   0s     set 555/1170, auto, target $TargetVcore  -> settle
#   240s   manual 100 % (what a soak cooldown does)
#   330s   auto again  <- the hand-over under test
#   570s   restore
#
# Pass criteria (checked by the caller on the CSV):
#   - fan duty never falls below ~ the settled duty of phase A
#   - Vcore temperature peak stays at or below the target
#
# Usage: .\fan_test_handover.ps1 -Ip 192.168.124.18 -Out <csv>
param(
    [string]$Ip = '192.168.124.18',
    [string]$Out = "$PSScriptRoot\fan_handover.csv",
    [double]$TargetVcore = 67.0,
    [int]$IntervalMs = 250,
    [double]$VcoreAbortC = 100.0
)
$ErrorActionPreference = 'Continue'

function Get-Info { try { return Invoke-RestMethod "http://$Ip/api/system/info" -TimeoutSec 3 } catch { return $null } }
function Set-Fan1 {
    param([int]$Auto, [int]$Speed)
    $body = @{ fans = @(@{ id = 1; auto = $Auto; target = $TargetVcore; speed = $Speed }) } | ConvertTo-Json -Depth 4
    try { Invoke-RestMethod "http://$Ip/api/setting/preference" -Method Patch -ContentType 'application/json' -Body $body -TimeoutSec 5 | Out-Null } catch {}
}
function Apply-Point {
    $body = @{ freq = 555; vcore = 1170 } | ConvertTo-Json
    try { Invoke-RestMethod "http://$Ip/api/benchmark/apply" -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 10 | Out-Null } catch {}
}

$schedule = @(
    @{ t =   0; phase = 'A_SETTLE';   action = { Apply-Point; Set-Fan1 -Auto 1 -Speed 100 } },
    @{ t = 240; phase = 'B_MANUAL100'; action = { Set-Fan1 -Auto 0 -Speed 100 } },
    @{ t = 330; phase = 'C_HANDOVER';  action = { Set-Fan1 -Auto 1 -Speed 100 } },
    @{ t = 570; phase = 'D_RESTORE';   action = { Set-Fan1 -Auto 1 -Speed 100 } }
)
$endSec = 600
$idx = 0
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$fs = [System.IO.File]::Open($Out, 'Create', 'Write', 'ReadWrite')
$w = New-Object System.IO.StreamWriter($fs, (New-Object System.Text.UTF8Encoding($false)))
$w.AutoFlush = $true
$w.WriteLine('t_s,iso,vcore_c,asic_c,fan0_pct,fan1_pct,fan1_rpm,pwr_w,hr_ghs,phase')

try {
    while ($sw.Elapsed.TotalSeconds -lt $endSec) {
        $t = [math]::Round($sw.Elapsed.TotalSeconds, 3)
        while ($idx -lt $schedule.Count -and $schedule[$idx].t -le $t) {
            Write-Host ("[{0,6:N1}s] {1}" -f $t, $schedule[$idx].phase)
            $null = & $schedule[$idx].action
            $idx++
        }
        $phase = 'DONE'
        for ($i = $schedule.Count - 1; $i -ge 0; $i--) { if ($schedule[$i].t -le $t) { $phase = $schedule[$i].phase; break } }

        $i = Get-Info
        if ($null -ne $i) {
            $f0 = ($i.fans | Where-Object { $_.id -eq 0 } | Select-Object -First 1)
            $f1 = ($i.fans | Where-Object { $_.id -eq 1 } | Select-Object -First 1)
            $w.WriteLine(('{0:F3},{1},{2:F2},{3:F2},{4},{5},{6},{7:F2},{8:F1},{9}' -f `
                $t, (Get-Date -Format 'HH:mm:ss.fff'), $i.temps.vcore, $i.temps.asic,
                $f0.speed, $f1.speed, $f1.rpm, $i.power.power, $i.miner.hashRate, $phase))
            if ($i.temps.vcore -gt $VcoreAbortC) {
                Write-Host "SAFETY ABORT: vcore $($i.temps.vcore) C"
                Set-Fan1 -Auto 0 -Speed 100
                break
            }
        }
        Start-Sleep -Milliseconds $IntervalMs
    }
}
finally {
    Write-Host 'cleanup: auto fan, restore 725/1250'
    Set-Fan1 -Auto 1 -Speed 100
    try { Invoke-RestMethod "http://$Ip/api/benchmark/apply" -Method Post -ContentType 'application/json' -Body '{"freq":725,"vcore":1250}' -TimeoutSec 10 | Out-Null } catch {}
    $w.Close()
    Write-Host "done -> $Out"
}
