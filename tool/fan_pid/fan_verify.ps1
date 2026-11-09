# Post-flash verification for the smoothed fan loop. ONE script, per board.
#
# The schedule is always the same six phases (setup / auto / cooldown / handover /
# load step / restore); only the numbers differ per board, because each board has
# its own band where the fan actually has authority. Those numbers are parameters
# with the QAxe++ 2-phase defaults, so an existing invocation keeps working.
#
# Choosing a point (this is the whole trick, and it was learned the hard way):
#   The schedule must sit where the loop can move the fan over a real range. A
#   point where the fan is pinned at 0 % or 100 % makes the A/B compare two flat
#   lines and proves nothing.
#
#     QAxe++ 2-phase (124.7)  555/1170  Vcore 65.5-73 C at 69 W, 67 in the middle
#     Nexus          (124.2)  700/1150  fan 22-100 % moves Vcore 68 -> 57 C
#     Rev6.1         (124.6)  725/1250  Vcore 73.4 C at 0 %, 62.4 C at 100 %
#                             NB: 555/1170 is CLAMPED UP to 650 on this board and
#                             lands at 64 C with the fan already at 0 %; the
#                             board's own 800/1290 point saturates both fans.
#
# The SAME invocation is run once on the old firmware and once on the new one; the
# two CSVs are the A/B pair.
#
# Usage:
#   # QAxe++ 2-phase (defaults)
#   .\fan_verify.ps1 -Ip 192.168.124.7 -Out ..\tmp\old.csv
#
#   # Nexus
#   .\fan_verify.ps1 -Ip 192.168.124.2 -Out ..\tmp\nexus.csv `
#       -Freq 700 -Vcore 1150 -TargetAsic 52 -AsicAbortC 66 -VcoreAbortC 76 `
#       -StepFreq 650 -StepVcore 1120 -RestoreFreq 775 -RestoreVcore 1200 `
#       -RestoreTargetAsic 45 -RestoreTargetVcore 67
#
#   # Rev6.1
#   .\fan_verify.ps1 -Ip 192.168.124.6 -Out ..\tmp\rev61.csv `
#       -Freq 725 -Vcore 1250 -AsicAbortC 65 -VcoreAbortC 92 `
#       -StepFreq 800 -StepVcore 1290 `
#       -SetupSec 240 -RestoreTargetAsic 40 -RestoreTargetVcore 75
#
#   # Check the schedule without touching any hardware:
#   .\fan_verify.ps1 -Ip 192.168.124.7 -DryRun
param(
    [Parameter(Mandatory = $true)][string]$Ip,
    [string]$Out = "$PSScriptRoot\fan_verify.csv",
    [int]$IntervalMs = 250,

    # --- the operating point the six phases run at -------------------------
    [int]$Freq = 555,
    [int]$Vcore = 1170,

    # --- the step at phase E. Up for a load-step, down to show recovery. ---
    [int]$StepFreq = 725,
    [int]$StepVcore = 1250,

    # --- what the run leaves behind. Defaults to the QAxe++ production point. ---
    [int]$RestoreFreq = 725,
    [int]$RestoreVcore = 1250,
    [double]$RestoreTargetAsic = 40.0,
    [double]$RestoreTargetVcore = 80.0,

    # --- fan policy used during the run ------------------------------------
    [double]$TargetAsic = 40.0,
    [double]$TargetVcore = 67.0,

    # --- safety. Keep both BELOW the board limits; see board.cpp. ----------
    [double]$AsicAbortC = 62.0,
    [double]$VcoreAbortC = 100.0,
    [switch]$AbortOnProtection,     # Nexus turns this on; see the note below

    # --- timing ------------------------------------------------------------
    [int]$SetupSec = 180,           # A_SETUP
    [int]$AutoSec = 300,            # B_AUTO
    [int]$CoolSec = 90,             # C_COOLDOWN
    [int]$HandSec = 240,            # D_HANDOVER
    [int]$StepSec = 240,            # E_LOADSTEP
    [int]$RestoreSec = 60,          # F_RESTORE

    # --- print the schedule and exit without touching the device ----------
    [switch]$DryRun
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

# Phase boundaries, derived from the durations so the two stay in sync.
$tAuto    = $SetupSec
$tCool    = $tAuto  + $AutoSec
$tHand    = $tCool  + $CoolSec
$tStep    = $tHand  + $HandSec
$tRestore = $tStep  + $StepSec
$endSec   = $tRestore + $RestoreSec

$schedule = @(
    @{ t = 0;         phase = 'A_SETUP';    action = { Apply-Point $Freq $Vcore | Out-Null; Set-Policy 1 1 $TargetAsic $TargetVcore 80 100 | Out-Null } },
    @{ t = $tAuto;    phase = 'B_AUTO';     action = { } },
    @{ t = $tCool;    phase = 'C_COOLDOWN'; action = { Set-Fan -Id 1 -Auto 0 -Target $TargetVcore -Speed 100 | Out-Null } },
    @{ t = $tHand;    phase = 'D_HANDOVER'; action = { Set-Fan -Id 1 -Auto 1 -Target $TargetVcore -Speed 100 | Out-Null } },
    @{ t = $tStep;    phase = 'E_LOADSTEP'; action = { Apply-Point $StepFreq $StepVcore | Out-Null } },
    @{ t = $tRestore; phase = 'F_RESTORE';  action = { Apply-Point $RestoreFreq $RestoreVcore | Out-Null; Set-Policy 1 1 $RestoreTargetAsic $RestoreTargetVcore 80 100 | Out-Null } }
)

if ($DryRun) {
    # Proves an invocation produces the intended schedule without touching the
    # board. Diff this against the old per-board script before deleting anything.
    "=== DryRun: no request will be sent to $Ip ==="
    ''
    'run point     : {0} MHz / {1} mV' -f $Freq, $Vcore
    'step point    : {0} MHz / {1} mV' -f $StepFreq, $StepVcore
    'restore point : {0} MHz / {1} mV' -f $RestoreFreq, $RestoreVcore
    'targets       : asic {0} C, vcore {1} C' -f $TargetAsic, $TargetVcore
    'restore fan   : asic {0} C, vcore {1} C' -f $RestoreTargetAsic, $RestoreTargetVcore
    'abort         : asic > {0} C, vcore > {1} C{2}' -f $AsicAbortC, $VcoreAbortC, $(if ($AbortOnProtection) { ', or protection != none' } else { '' })
    'total         : {0} s ({1:N1} min)' -f $endSec, ($endSec / 60)
    ''
    'phase schedule:'
    foreach ($ev in $schedule) { '  {0,7} s  {1}' -f $ev.t, $ev.phase }
    ''
    'resolved actions:'
    '  A_SETUP    Apply-Point {0} {1}; Set-Policy 1 1 {2} {3} 80 100' -f $Freq, $Vcore, $TargetAsic, $TargetVcore
    '  C_COOLDOWN Set-Fan id=1 auto=0 target={0} speed=100' -f $TargetVcore
    '  D_HANDOVER Set-Fan id=1 auto=1 target={0} speed=100' -f $TargetVcore
    '  E_LOADSTEP Apply-Point {0} {1}' -f $StepFreq, $StepVcore
    '  F_RESTORE  Apply-Point {0} {1}; Set-Policy 1 1 {2} {3} 80 100' -f $RestoreFreq, $RestoreVcore, $RestoreTargetAsic, $RestoreTargetVcore
    '  cleanup    Set-Policy 1 1 {0} {1} 80 100; Apply-Point {2} {3}' -f $RestoreTargetAsic, $RestoreTargetVcore, $RestoreFreq, $RestoreVcore
    ''
    'CSV columns:'
    '  t_s,iso,asic_c,vcore_c,fan0_pct,fan1_pct,fan0_rpm,fan1_rpm,vbus_v,ibus_a,pwr_w,hr_ghs,freq_req,vcore_req,vcore_real,mode,protection,phase'
    return
}

$idx = 0
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$fs = [System.IO.File]::Open($Out, 'Create', 'Write', 'ReadWrite')
$w = New-Object System.IO.StreamWriter($fs, (New-Object System.Text.UTF8Encoding($false)))
$w.AutoFlush = $true
$w.WriteLine('t_s,iso,asic_c,vcore_c,fan0_pct,fan1_pct,fan0_rpm,fan1_rpm,vbus_v,ibus_a,pwr_w,hr_ghs,freq_req,vcore_req,vcore_real,mode,protection,phase')

$aborted = $false
try {
    Write-Host ("start -> {0}" -f $Out)
    Write-Host ("  point {0}/{1} mV, step {2}/{3} mV, targets asic {4} / vcore {5} C, {6} s" -f `
        $Freq, $Vcore, $StepFreq, $StepVcore, $TargetAsic, $TargetVcore, $endSec)
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
            $w.WriteLine(('{0:F3},{1},{2:F2},{3:F2},{4},{5},{6},{7},{8:F2},{9:F3},{10:F2},{11:F2},{12},{13},{14},{15},{16},{17}' -f `
                $t, (Get-Date -Format 'HH:mm:ss.fff'), $a, $v,
                $f0.speed, $f1.speed, $f0.rpm, $f1.rpm,
                ($i.power.vbus / 1000.0), ($i.power.ibus / 1000.0), $i.power.power,
                $i.miner.hashRate, $i.asic.freqReq, $i.asic.vcoreReq, $i.asic.vcoreReal,
                $i.miner.bmMode, $i.miner.protection, $phase))

            $trip = ($v -gt $VcoreAbortC) -or ($a -gt $AsicAbortC)
            if ($AbortOnProtection -and $i.miner.protection -ne 'none') { $trip = $true }
            if ($trip) {
                Write-Host ("SAFETY ABORT: asic={0} C vcore={1} C protection={2} -> Vcore fan 100%" -f `
                    $a, $v, $i.miner.protection)
                Set-Fan -Id 1 -Auto 0 -Target 65 -Speed 100 | Out-Null
                $aborted = $true
                break
            }
        }
        Start-Sleep -Milliseconds $IntervalMs
    }
}
finally {
    Write-Host ("cleanup: restoring auto fan policy ({0}/{1} C) and the {2}/{3} mV point" -f `
        $RestoreTargetAsic, $RestoreTargetVcore, $RestoreFreq, $RestoreVcore)
    Set-Policy 1 1 $RestoreTargetAsic $RestoreTargetVcore 80 100 | Out-Null
    Apply-Point $RestoreFreq $RestoreVcore | Out-Null
    $w.Close()
    Write-Host ("done -> {0} (aborted={1})" -f $Out, $aborted)
}
