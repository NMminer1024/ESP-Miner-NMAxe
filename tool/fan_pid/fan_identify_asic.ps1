# Identify the ASIC-fan plant (fan 0 -> TMP102) on a 2-fan board.
#
# Manual duty steps at a fixed low-power operating point, 4 min per step, sampled
# at 4 Hz. From the result the tuner needs three numbers per step:
#   K   = dT/dDuty   (°C per %)
#   L   = dead time  (first visible response)
#   tau = 63 % point of the remaining change
#
# Usage:
#   .\fan_identify_asic.ps1 -Ip 192.168.124.18 -Out D:\...\fan_asic_ident.csv
param(
    [string]$Ip = '192.168.124.18',
    [string]$Out = "$PSScriptRoot\fan_asic_ident.csv",
    [int]$Freq = 555,
    [int]$Vcore = 1170,
    [int]$IntervalMs = 250,
    [double]$AsicAbortC = 62.0
)
$ErrorActionPreference = 'Continue'

function Get-Info { try { return Invoke-RestMethod "http://$Ip/api/system/info" -TimeoutSec 3 } catch { return $null } }
function Set-Fan0 {
    param([int]$Speed)
    $body = @{ fans = @(@{ id = 0; auto = 0; target = 40.0; speed = $Speed }) } | ConvertTo-Json -Depth 4
    try { Invoke-RestMethod "http://$Ip/api/setting/preference" -Method Patch -ContentType 'application/json' -Body $body -TimeoutSec 5 | Out-Null } catch {}
}
function Apply-Point {
    param([int]$F, [int]$V)
    $body = @{ freq = $F; vcore = $V } | ConvertTo-Json
    try { Invoke-RestMethod "http://$Ip/api/benchmark/apply" -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 10 | Out-Null } catch {}
}

# 20 -> 80 -> 40 -> 60 %: rising, falling and a re-rise, so the gain can be read
# near the duty the loop will actually use.
$steps = @(
    @{ t =    0; duty = 20; name = 'S20' },
    @{ t =  300; duty = 80; name = 'S80' },
    @{ t =  600; duty = 40; name = 'S40' },
    @{ t =  900; duty = 60; name = 'S60' }
)
$endSec = 1140
$idx = 0
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$fs = [System.IO.File]::Open($Out, 'Create', 'Write', 'ReadWrite')
$w = New-Object System.IO.StreamWriter($fs, (New-Object System.Text.UTF8Encoding($false)))
$w.AutoFlush = $true
$w.WriteLine('t_s,iso,asic_c,vcore_c,fan0_pct,fan1_pct,fan0_rpm,fan1_rpm,pwr_w,hr_ghs,phase')

$aborted = $false
try {
    Apply-Point $Freq $Vcore
    Write-Host "start -> $Out  ($Freq MHz / $Vcore mV)"
    while ($sw.Elapsed.TotalSeconds -lt $endSec) {
        $t = [math]::Round($sw.Elapsed.TotalSeconds, 3)
        while ($idx -lt $steps.Count -and $steps[$idx].t -le $t) {
            $s = $steps[$idx]
            Write-Host ("[{0,6:N1}s] ASIC fan -> {1} %% ({2})" -f $t, $s.duty, $s.name)
            Set-Fan0 -Speed $s.duty
            $idx++
        }
        $phase = 'DONE'
        for ($i = $steps.Count - 1; $i -ge 0; $i--) { if ($steps[$i].t -le $t) { $phase = $steps[$i].name; break } }

        $i = Get-Info
        if ($null -ne $i) {
            $f0 = ($i.fans | Where-Object { $_.id -eq 0 } | Select-Object -First 1)
            $f1 = ($i.fans | Where-Object { $_.id -eq 1 } | Select-Object -First 1)
            $w.WriteLine(('{0:F3},{1},{2:F2},{3:F2},{4},{5},{6},{7},{8:F2},{9:F2},{10}' -f `
                $t, (Get-Date -Format 'HH:mm:ss.fff'), $i.temps.asic, $i.temps.vcore,
                $f0.speed, $f1.speed, $f0.rpm, $f1.rpm, $i.power.power, $i.miner.hashRate, $phase))
            if ($i.temps.asic -gt $AsicAbortC) {
                Write-Host "SAFETY ABORT: ASIC $($i.temps.asic) C -> ASIC fan 100%"
                Set-Fan0 -Speed 100
                $aborted = $true
                break
            }
        }
        Start-Sleep -Milliseconds $IntervalMs
    }
}
finally {
    Write-Host 'cleanup: ASIC fan back to auto, restore 725/1250'
    $body = @{ fans = @(@{ id = 0; auto = 1; target = 40.0; speed = 80 }) } | ConvertTo-Json -Depth 4
    try { Invoke-RestMethod "http://$Ip/api/setting/preference" -Method Patch -ContentType 'application/json' -Body $body -TimeoutSec 5 | Out-Null } catch {}
    Apply-Point 725 1250
    $w.Close()
    Write-Host ("done -> {0} (aborted={1})" -f $Out, $aborted)
}
