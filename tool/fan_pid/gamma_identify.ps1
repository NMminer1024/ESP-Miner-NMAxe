# Identify the single fan plant on NMAxeGamma (fan 0 -> TMP102 ASIC sensor).
#
# Gamma has one small blower and very little thermal headroom: at ~22 W the fan
# already sits near 100 % to hold 50 C. The steps therefore stay in the upper
# range (100 -> 70 -> 40 -> 70) so the die cannot run away while identified, and
# the abort threshold is below the board's 70 C limit.
#
# Usage:
#   .\gamma_identify.ps1 -Ip 192.168.124.11 -Freq 550 -Vcore 1130 -Out <csv>
param(
    [string]$Ip = '192.168.124.11',
    [string]$Out = "$PSScriptRoot\gamma_ident.csv",
    [int]$Freq = 550,
    [int]$Vcore = 1130,
    [int]$IntervalMs = 250,
    [double]$AsicAbortC = 67.0
)
$ErrorActionPreference = 'Continue'

function Get-Info { try { return Invoke-RestMethod "http://$Ip/api/system/info" -TimeoutSec 3 } catch { return $null } }
function Set-Fan0 {
    param([int]$Speed)
    $body = @{ fans = @(@{ id = 0; auto = 0; target = 50.0; speed = $Speed }) } | ConvertTo-Json -Depth 4
    try { Invoke-RestMethod "http://$Ip/api/setting/preference" -Method Patch -ContentType 'application/json' -Body $body -TimeoutSec 5 | Out-Null } catch {}
}
function Apply-Point {
    param([int]$F, [int]$V)
    $body = @{ freq = $F; vcore = $V } | ConvertTo-Json
    try { Invoke-RestMethod "http://$Ip/api/benchmark/apply" -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 10 | Out-Null } catch {}
}

$steps = @(
    @{ t =    0; duty = 100; name = 'S100' },
    @{ t =  240; duty =  70; name = 'S70'  },
    @{ t =  480; duty =  40; name = 'S40'  },
    @{ t =  720; duty =  70; name = 'S70B' }
)
$endSec = 960
$idx = 0
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$fs = [System.IO.File]::Open($Out, 'Create', 'Write', 'ReadWrite')
$w = New-Object System.IO.StreamWriter($fs, (New-Object System.Text.UTF8Encoding($false)))
$w.AutoFlush = $true
$w.WriteLine('t_s,iso,asic_c,vcore_c,fan0_pct,fan1_pct,fan0_rpm,fan1_rpm,vbus_v,ibus_a,pwr_w,hr_ghs,freq_req,vcore_req,vcore_real,phase')

$aborted = $false
try {
    Apply-Point $Freq $Vcore
    Write-Host "start -> $Out  ($Freq MHz / $Vcore mV, abort at $AsicAbortC C)"
    while ($sw.Elapsed.TotalSeconds -lt $endSec) {
        $t = [math]::Round($sw.Elapsed.TotalSeconds, 3)
        while ($idx -lt $steps.Count -and $steps[$idx].t -le $t) {
            $s = $steps[$idx]
            Write-Host ("[{0,6:N1}s] ASIC fan -> {1} % ({2})" -f $t, $s.duty, $s.name)
            Set-Fan0 -Speed $s.duty
            $idx++
        }
        $phase = 'DONE'
        for ($i = $steps.Count - 1; $i -ge 0; $i--) { if ($steps[$i].t -le $t) { $phase = $steps[$i].name; break } }

        $i = Get-Info
        if ($null -ne $i) {
            $f0 = ($i.fans | Where-Object { $_.id -eq 0 } | Select-Object -First 1)
            $f1 = ($i.fans | Where-Object { $_.id -eq 1 } | Select-Object -First 1)
            $w.WriteLine(('{0:F3},{1},{2:F2},{3:F2},{4},{5},{6},{7},{8:F2},{9:F3},{10:F2},{11:F1},{12},{13},{14},{15}' -f `
                $t, (Get-Date -Format 'HH:mm:ss.fff'), $i.temps.asic, $i.temps.vcore,
                $f0.speed, $f1.speed, $f0.rpm, $f1.rpm,
                ($i.power.vbus / 1000.0), ($i.power.ibus / 1000.0), $i.power.power,
                $i.miner.hashRate, $i.asic.freqReq, $i.asic.vcoreReq, $i.asic.vcoreReal, $phase))
            if ($i.temps.asic -gt $AsicAbortC) {
                Write-Host "SAFETY ABORT: ASIC $($i.temps.asic) C -> fan 100%"
                Set-Fan0 -Speed 100
                $aborted = $true
                break
            }
        }
        Start-Sleep -Milliseconds $IntervalMs
    }
}
finally {
    Write-Host 'cleanup: fan back to auto, restore the original point'
    $body = @{ fans = @(@{ id = 0; auto = 1; target = 50.0; speed = 100 }) } | ConvertTo-Json -Depth 4
    try { Invoke-RestMethod "http://$Ip/api/setting/preference" -Method Patch -ContentType 'application/json' -Body $body -TimeoutSec 5 | Out-Null } catch {}
    Apply-Point 600 1125
    $w.Close()
    Write-Host ("done -> {0} (aborted={1})" -f $Out, $aborted)
}
