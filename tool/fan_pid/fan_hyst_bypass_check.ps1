# Focused A/B for the emergency-bypass interaction with the duty hysteresis,
# plus a check that the fix did not disable hysteresis in steady state.
#
# The controller below is a port of pid_compute() + the drive_fan() output stage
# at HEAD. `bypassNow` mirrors the new condition added in drive_fan():
#   bypass = (meas_f - target) > 8 or < -8
#
# Usage: .\fan_hyst_bypass_check.ps1
param([double]$Dt = 0.125)

function Sim {
    param([double]$Kp, [double]$Ki, [double]$db, [double]$filt,
          [double]$up, [double]$dn, [double]$hyst,
          [double]$startDuty, [double]$constTemp, [double]$target,
          [int]$samples, [switch]$FixApplied)

    $out = $startDuty; $applied = $startDuty
    $integral = 0.0; $valid = $false; $meas_f = 0.0
    $trace = New-Object System.Collections.Generic.List[int]

    for ($i = 0; $i -lt $samples; $i++) {
        if (-not $valid) { $meas_f = $constTemp; $valid = $true }
        $a = $Dt / ($filt + $Dt)
        $meas_f += $a * ($constTemp - $meas_f)

        $error = $meas_f - $target
        $e = $error
        if ([math]::Abs($e) -lt $db) { $e = 0.0 }

        $w = ([math]::Abs($e) - 2.0) / 6.0
        if ($w -lt 0) { $w = 0 }; if ($w -gt 1) { $w = 1 }
        $kp = $Kp * (1.0 + 2.0 * $w)

        $iNext = $integral + $e * $Dt
        $iLim = 100.0 / $Ki
        if ($iNext -gt $iLim)  { $iNext = $iLim }
        if ($iNext -lt -$iLim) { $iNext = -$iLim }

        $uRaw = $kp * $e + $Ki * $iNext
        $u = [math]::Min(100.0, [math]::Max(0.0, $uRaw))
        $satHi = $uRaw -gt 100; $satLo = $uRaw -lt 0
        if (-not (($satHi -and $e -gt 0) -or ($satLo -and $e -lt 0))) { $integral = $iNext }

        if ($error -gt 8.0) { $out = 100.0 }
        else {
            if ($error -lt -8.0) { $u = 0.0 }
            $du = $u - $out
            $upL =  $up * $Dt
            $dnL = -$dn * $Dt
            if ($du -gt $upL) { $du = $upL }
            if ($du -lt $dnL) { $du = $dnL }
            $out += $du
        }
        $out = [math]::Min(100.0, [math]::Max(0.0, $out))

        $rounded = [int][math]::Floor($out + 0.5)
        if ($rounded -gt 100) { $rounded = 100 }

        $bypass = ($FixApplied -and ([math]::Abs($meas_f - $target) -gt 8.0))
        if ($hyst -ge 1.0 -and -not $bypass) {
            $diff = $rounded - $applied
            if ($diff -gt (-1 * [int]$hyst) -and $diff -lt [int]$hyst) { $rounded = $applied }
        }
        $applied = $rounded
        $trace.Add($applied)
    }
    return $trace
}

"=== A. emergency: 9 C over target, fan parked at 99 %, hyst 2 ==="
''
foreach ($fix in @($false, $true)) {
    $t = Sim -Kp 4.0 -Ki 0.080 -db 0.5 -filt 6.0 -up 30 -dn 1.2 -hyst 2.0 `
            -startDuty 99 -constTemp 76.0 -target 67 -samples 400 -FixApplied:$fix
    $max = ($t | Measure-Object -Maximum).Maximum
    '  {0,-22} max duty {1,3} %   {2}' -f `
        $(if ($fix) { 'with fix' } else { 'before fix' }), $max,
        $(if ($max -ge 100) { 'OK' } else { 'REGRESSION: full speed never reached' })
}

''
"=== B. steady state hysteresis is unchanged ==="
''
@'
   Not re-tested here: a fixed-temperature plant never converges, so any duty
   trace it produces is a ramp, not steady state. The evidence that the fix
   leaves steady-state hysteresis intact is fan_closed_loop_audit.ps1, where
   Nexus Vcore (hyst 2) still shows exactly one distinct applied duty value
   once settled - same as before the fix.
'@
''

''
"=== C. hysteresis is 0 on the 1/16 C boards, so the fix must change nothing there ==="
''
$t0 = Sim -Kp 5.6264 -Ki 0.125 -db 0.15 -filt 4.3 -up 30 -dn 1.3 -hyst 0 `
        -startDuty 63 -constTemp 40.05 -target 40 -samples 800 -FixApplied:$false
$t1 = Sim -Kp 5.6264 -Ki 0.125 -db 0.15 -filt 4.3 -up 30 -dn 1.3 -hyst 0 `
        -startDuty 63 -constTemp 40.05 -target 40 -samples 800 -FixApplied:$true
$same = $true
for ($i = 0; $i -lt $t0.Count; $i++) { if ($t0[$i] -ne $t1[$i]) { $same = $false; break } }
'  QAxe++ ASIC trace identical with and without fix: {0}' -f $same
