# Offline replay of the Vcore fan loop against a RECORDED temperature stream.
#
# Purpose: quantify how much of the residual fan wander is removable, WITHOUT
# flashing a new build. The recorded vcore_c column is what the loop actually saw
# (raw quantised sensor value, 4 Hz). Feeding the same stream through candidate
# controllers isolates "how stable would the fan be against this measurement".
#
# Caveat: this is an open-loop replay. In closed loop the fan also moves the
# temperature, so a stiffer controller would additionally shrink the thermal
# excursion. The numbers here are therefore a lower bound on the improvement and
# an exact measure of the dither component.
#
# The controller below mirrors pid_compute() in src/drivers/fan/fan.cpp
# (measurement LPF, dead band, gain scheduling, conditional anti-windup,
# asymmetric slew, dt clamp, >8 C bypass) plus two candidate extras:
#   - duty hysteresis: only move the applied duty once the internal demand has
#     drifted H points away from what the fan is really doing
#   - a fixed-duty floor, to show what "no regulation" would look like
#
# Usage: .\fan_replay_tuning.ps1 -Csv <csv> -Phase B_AUTO
param(
    [Parameter(Mandatory = $true)][string]$Csv,
    [string]$Phase = 'B_AUTO',
    [double]$SkipSec = 60,
    [double]$Target = 67.0
)

$rows = Import-Csv $Csv | Where-Object { $_.phase -eq $Phase }
if ($rows.Count -lt 20) { throw "phase '$Phase' not found or too short" }
$t0   = [double]$rows[0].t_s
$rows = $rows | Where-Object { ([double]$_.t_s - $t0) -ge $SkipSec }
$meas = @($rows | ForEach-Object { [double]$_.vcore_c })
$fanRec = @($rows | ForEach-Object { [double]$_.fan1_pct })
$n = $meas.Count

# Recorded cadence is 4 Hz; the real PID runs at 8 Hz, so run two 125 ms sub-steps
# per recorded sample with the measurement held (it is piecewise constant anyway).
function Simulate($name, $Kp, $Ki, $db, $filt, $slewUp, $slewDown, $hyst, $fixed) {
    $dt = 0.125
    $out = if ($null -ne $fixed) { $fixed } else { $fanRec[0] }
    $applied = $out
    $valid = $false
    $meas_f = 0.0

    # Seed the integrator so the FIRST output equals the duty the real loop was
    # actually holding. Starting it at zero is wrong: a PI with e=0 and I=0 asks
    # for 0 % and simply slews the fan to the bottom, which is not the state the
    # recording was taken in.
    $e0 = $meas[0] - $Target
    if ([math]::Abs($e0) -lt $db) { $e0 = 0.0 }
    $integral = if ($Ki -gt 0) { ($out - $Kp * $e0) / $Ki } else { 0.0 }
    if ($Ki -gt 0) {
        $iLim = 100.0 / $Ki
        if ($integral -gt $iLim)  { $integral = $iLim }
        if ($integral -lt -$iLim) { $integral = -$iLim }
    }

    $hist = New-Object System.Collections.Generic.List[double]
    for ($i = 0; $i -lt $n; $i++) {
        $mv = $meas[$i]
        for ($sub = 0; $sub -lt 2; $sub++) {
            if (-not $valid) { $meas_f = $mv; $valid = $true }
            $a = $dt / ($filt + $dt)
            $meas_f += $a * ($mv - $meas_f)

            $error = $meas_f - $Target
            $e = $error
            if ([math]::Abs($e) -lt $db) { $e = 0.0 }

            $w = ([math]::Abs($e) - 2.0) / 6.0
            if ($w -lt 0) { $w = 0 }; if ($w -gt 1) { $w = 1 }
            $kp = $Kp * (1.0 + 2.0 * $w)

            $iNext = $integral + $e * $dt
            if ($Ki -gt 0) {
                $iLim = 100.0 / $Ki
                if ($iNext -gt $iLim)  { $iNext = $iLim }
                if ($iNext -lt -$iLim) { $iNext = -$iLim }
            }
            $u = $kp * $e + $Ki * $iNext
            if ($u -gt 100) { $u = 100 }; if ($u -lt 0) { $u = 0 }
            # conditional integration: only accumulate when not driving deeper into the rail
            if (-not (($u -ge 100 -and $e -gt 0) -or ($u -le 0 -and $e -lt 0))) { $integral = $iNext }

            if ($error -gt 8.0) { $out = 100 } else {
                if ($error -lt -8.0) { $u = 0 }
                $du = $u - $out
                $up = $slewUp * $dt
                $dn = -$slewDown * $dt
                if ($du -gt $up) { $du = $up }
                if ($du -lt  $dn) { $du = $dn }
                $out = $out + $du
            }
            if ($out -gt 100) { $out = 100 }; if ($out -lt 0) { $out = 0 }

            if ($null -ne $fixed) { $applied = $fixed }
            elseif ($hyst -le 0) { $applied = $out }
            elseif ([math]::Abs($out - $applied) -ge $hyst) { $applied = [math]::Round($out) }
        }
        $hist.Add($applied)
    }

    $st = $hist | Measure-Object -Average -Minimum -Maximum
    $sd = 0.0
    foreach ($v in $hist) { $sd += [math]::Pow($v - $st.Average, 2) }
    $sd = [math]::Sqrt($sd / ($hist.Count - 1))
    $jumps = 0; $maxStep = 0
    for ($i = 1; $i -lt $hist.Count; $i++) {
        $s = [math]::Abs($hist[$i] - $hist[$i - 1])
        if ($s -ge 3) { $jumps++ }
        if ($s -gt $maxStep) { $maxStep = $s }
    }
    return [pscustomobject]@{
        policy = $name
        min = [math]::Round($st.Minimum); max = [math]::Round($st.Maximum)
        span = [math]::Round($st.Maximum - $st.Minimum)
        mean = [math]::Round($st.Average, 1); sd = [math]::Round($sd, 2)
        uniq = ($hist | Select-Object -Unique).Count
        moves = $jumps; maxStep = [math]::Round($maxStep)
    }
}

$res = @()
$res += Simulate 'recorded (as flashed)'      4.00 0.080 0.50 6.0 30 1.20 0 $null
$res += Simulate 'A deadband -> 1 LSB'        4.00 0.080 1.00 6.0 30 1.20 0 $null
$res += Simulate 'B Kp -> 2 (noise cap)'      2.00 0.040 0.50 6.0 30 1.20 0 $null
$res += Simulate 'C A+B'                      2.00 0.040 1.00 6.0 30 1.20 0 $null
$res += Simulate 'D C + duty hyst 3'          2.00 0.040 1.00 6.0 30 1.20 3 $null
$res += Simulate 'G Kp4 + hyst 3'             4.00 0.080 0.50 6.0 30 1.20 3 $null
$res += Simulate 'H Kp4 db1 + hyst 3'         4.00 0.080 1.00 6.0 30 1.20 3 $null
$res += Simulate 'J Kp4 + hyst 2'             4.00 0.080 0.50 6.0 30 1.20 2 $null
$res += Simulate 'K Kp4 + hyst 1'             4.00 0.080 0.50 6.0 30 1.20 1 $null
$res += Simulate 'I Kp3 db1 + hyst 2'         3.00 0.060 1.00 6.0 30 1.20 2 $null
$res += Simulate 'E C + slower down 0.5'      2.00 0.040 1.00 6.0 30 0.50 3 $null
$res += Simulate 'F no regulation (hold 51)'  2.00 0.040 1.00 6.0 30 1.20 0 51

"=== replay: $([System.IO.Path]::GetFileName($Csv))  phase=$Phase  n=$n samples (== $([math]::Round($n*0.25,0)) s) ==="
"    target = $Target C, measurement = recorded raw sensor stream (1 C LSB)"
''
$res | Format-Table -AutoSize | Out-String -Width 200
''
'notes:'
'  span/sd/moves refer to the APPLIED fan duty (what the fan actually does).'
'  "moves" counts changes >= 3 duty points between logged samples.'
'  F is the acoustic floor: it shows how much motion is pure regulation.'
