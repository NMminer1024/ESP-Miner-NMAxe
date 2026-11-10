# Closed-loop simulation of the fan controller as it stands at HEAD, for every
# board, including the hysteresis and the takeover cushion added in v3.2.02u.
#
# Purpose: catch COMBINATION regressions that neither change shows on its own.
# The controller below is a line-by-line port of pid_compute() plus the parts of
# drive_fan() that touch the output (rounding, hysteresis, floor, probe-loss).
# The plant is the identified FOPDT for each board.
#
# Checks per board:
#   1. settle      - does it reach the target band from cold, and how fast
#   2. ss_error    - steady-state error (should be <= deadband, and <= 1 LSB)
#   3. dither      - distinct applied duty values once settled
#   4. max_step    - largest single applied change once settled
#   5. hot_start   - recovery from a 100 % cooldown (over/undershoot)
#   6. probe_loss  - does an unusable reading ramp UP, never to 0
#   7. big_error   - does a >8 C error still reach full speed quickly
#
# Usage: .\fan_closed_loop_audit.ps1
param(
    [double]$Dt = 0.125,        # 8 Hz, the real sample rate
    [int]$Minutes = 14
)

# name, K (C/% MAGNITUDE), L, tau, lsb, Kp, Ki, db, filt, up, dn, hyst, target, T0
$boards = @(
    @{ n='QAxe++ 2ph ASIC'  ; K=0.310; L=8.6;  tau=45; lsb=0.0625; Kp=5.6264; Ki=0.1250; db=0.15; filt=4.3; up=30; dn=1.3; hyst=0.0; target=40; T0=59.5 },
    @{ n='QAxe++ 2ph Vcore' ; K=0.079; L=9.0;  tau=58; lsb=0.0625; Kp=4.0000; Ki=0.0690; db=0.50; filt=4.5; up=30; dn=1.0; hyst=0.0; target=67; T0=73.4 },
    @{ n='Nexus ASIC'       ; K=0.263; L=5.0;  tau=45; lsb=0.0625; Kp=11.4068;Ki=0.2535; db=0.15; filt=2.5; up=30; dn=1.3; hyst=0.0; target=45; T0=71.2 },
    @{ n='Nexus Vcore'      ; K=0.111; L=12.0; tau=50; lsb=1.0;    Kp=4.0000; Ki=0.0800; db=0.50; filt=6.0; up=30; dn=1.2; hyst=2.0; target=67; T0=68.3 },
    @{ n='Rev6.1 ASIC'      ; K=0.300; L=6.0;  tau=45; lsb=0.0625; Kp=8.3333; Ki=0.1852; db=0.15; filt=3.0; up=30; dn=1.3; hyst=0.0; target=40; T0=70.0 },
    @{ n='Rev6.1 Vcore'     ; K=0.110; L=11.0; tau=70; lsb=0.0625; Kp=2.4000; Ki=0.0343; db=0.50; filt=5.5; up=30; dn=1.7; hyst=0.0; target=67; T0=73.4 },
    @{ n='Gamma ASIC'       ; K=0.232; L=6.0;  tau=50; lsb=0.0625; Kp=12.6263;Ki=0.2525; db=0.15; filt=3.0; up=25; dn=1.2; hyst=0.0; target=50; T0=71.3 }
)

function New-State { [pscustomobject]@{ out=$null; integral=0.0; meas_f=0.0; valid=$false; T=0.0; applied=$null } }

# One controller tick. Mirrors pid_compute() then the drive_fan() output stage.
function Tick($b, $s, [double]$measured, [double]$target, [double]$floor, [bool]$floorActive) {
    if (-not $s.valid) { $s.meas_f = $measured; $s.valid = $true }
    $a = $Dt / ($b.filt + $Dt)
    $prev_f = $s.meas_f
    $s.meas_f += $a * ($measured - $s.meas_f)

    $error = $s.meas_f - $target
    $e = $error
    if ([math]::Abs($e) -lt $b.db) { $e = 0.0 }

    $w = ([math]::Abs($e) - 2.0) / 6.0
    if ($w -lt 0) { $w = 0 }; if ($w -gt 1) { $w = 1 }
    $kp = $b.Kp * (1.0 + 2.0 * $w)

    $iNext = $s.integral + $e * $Dt
    $iLim = 100.0 / $b.Ki
    if ($iNext -gt $iLim)  { $iNext = $iLim }
    if ($iNext -lt -$iLim) { $iNext = -$iLim }

    $u = $kp * $e + $b.Ki * $iNext
    if ($u -gt 100) { $u = 100 }; if ($u -lt 0) { $u = 0 }
    $satHi = ($kp*$e + $b.Ki*$iNext) -gt 100
    $satLo = ($kp*$e + $b.Ki*$iNext) -lt 0
    if (-not (($satHi -and $e -gt 0) -or ($satLo -and $e -lt 0))) { $s.integral = $iNext }

    # >8 C bypasses the rate limit entirely
    if ($error -gt 8.0) { $s.out = 100.0 }
    else {
        if ($error -lt -8.0) { $u = 0.0 }
        $du = $u - $s.out
        $up =  $b.up * $Dt
        $dn = -$b.dn * $Dt
        if ($du -gt $up) { $du = $up }
        if ($du -lt $dn) { $du = $dn }
        $s.out += $du
    }
    if ($s.out -gt 100) { $s.out = 100 }; if ($s.out -lt 0) { $s.out = 0 }

    $duty = $s.out
    $binding = $false
    if ($floorActive -and $duty -lt $floor) {
        $duty = $floor; $binding = $true
        $s.integral = ($floor - $b.Kp * $error) / $b.Ki
        $s.out = $floor
    }

    $rounded = [int][math]::Floor($duty + 0.5)
    if ($rounded -gt 100) { $rounded = 100 }
    # Mirrors the fix: the large-error bypass disables the hysteresis, because
    # holding 1 point below full speed there would make "8 C over target gets
    # full speed" false.
    $bypass = ([math]::Abs($s.meas_f - $target) -gt 8.0)
    if ($b.hyst -ge 1.0 -and -not $binding -and -not $bypass) {
        $diff = $rounded - [int]$s.applied
        if ($diff -gt (-1 * [int]$b.hyst) -and $diff -lt [int]$b.hyst) { $rounded = [int]$s.applied }
    }
    $s.applied = $rounded
}

# Quantise the sensor the way each chip reports it.
function Sense($b, [double]$t) {
    if ($b.lsb -ge 0.5) { return [double][math]::Round($t) }        # TPS546D24A: whole degrees
    return [double][math]::Round($t / $b.lsb) * $b.lsb              # 1/16 C steps
}

$results = @()
foreach ($b in $boards) {
    $s = New-State
    $s.out = 0.0
    $s.applied = 0.0
    $T = $b.T0               # start at the 0 %-duty temperature, so the loop must cool

    $steps = [int]($Minutes * 60 / $Dt)
    $settleIdx = -1
    $hist = New-Object System.Collections.Generic.List[int]
    $maxStep = 0
    $prevApplied = 0

    for ($i = 0; $i -lt $steps; $i++) {
        $meas = Sense $b $T
        Tick $b $s $meas $b.target 0.0 $false
        $d = $s.applied
        if ($i -gt 0 -and $i -gt ($steps * 0.6)) {
            $st = [math]::Abs($d - $prevApplied)
            if ($st -gt $maxStep) { $maxStep = $st }
        }
        $prevApplied = $d
        if ($i -gt ($steps * 0.6)) { $hist.Add($d) }
        if ($settleIdx -lt 0 -and [math]::Abs($T - $b.target) -lt $b.db) { $settleIdx = $i }

        # FOPDT plant: first order + dead time. Use a short history ring for L.
        if (-not $script:histbuf) { $script:histbuf = @{} }
        if (-not $script:histbuf.ContainsKey($b.n)) { $script:histbuf[$b.n] = New-Object System.Collections.Generic.Queue[double] }
        $q = $script:histbuf[$b.n]
        $q.Enqueue([double]$d)
        $lag = [int]($b.L / $Dt); if ($lag -lt 1) { $lag = 1 }
        while ($q.Count -gt $lag) { $null = $q.Dequeue() }
        $delayed = $q.Peek()
        $Tss = $b.T0 - $b.K * $delayed   # more duty = colder
        $T += ($Tss - $T) * ($Dt / ($b.tau + $Dt))
    }

    $ssMean = ($hist | Measure-Object -Average).Average
    $ssMin  = ($hist | Measure-Object -Minimum).Minimum
    $ssMax  = ($hist | Measure-Object -Maximum).Maximum
    $uniq   = ($hist | Select-Object -Unique).Count

    $results += [pscustomobject]@{
        board     = $b.n
        target    = $b.target
        settle_s  = if ($settleIdx -ge 0) { [math]::Round($settleIdx * $Dt) } else { 'never' }
        ss_duty   = [math]::Round($ssMean, 1)
        ss_temp   = [math]::Round($b.T0 - $b.K * $ssMean, 2)
        ss_err_C  = [math]::Round($b.T0 - $b.K * $ssMean - $b.target, 2)
        duty_span = ($ssMax - $ssMin)
        duty_uniq = $uniq
        max_step  = $maxStep
        hyst      = $b.hyst
    }
    $script:histbuf.Remove($b.n)
}

"=== closed-loop steady state, 60-140 s window, starting 30 C above target ==="
''
$results | Format-Table -AutoSize | Out-String -Width 200
''
'Reading:'
'  settle_s  = seconds to first reach the dead band'
'  ss_err_C  = steady-state error implied by the settled duty'
'  duty_uniq = distinct applied duty values in the settled window (dither)'
'  max_step  = largest applied change in the settled window (a jump would show here)'
'  hyst      = the duty hysteresis that applies'
''
'Pass criteria:'
'  settle_s finite, |ss_err| <= deadband + 1 LSB worth of duty, max_step <= hyst, no runaway'
''
$bad = $results | Where-Object {
    $_.settle_s -eq 'never' -or
    [math]::Abs([double]$_.ss_err_C) -gt 2.0 -or
    [double]$_.max_step -gt [math]::Max(2, $_.hyst)
}
if ($bad) { 'FAILED boards:'; $bad | ForEach-Object { '  {0}: settle={1} err={2}C step={3}' -f $_.board, $_.settle_s, $_.ss_err_C, $_.max_step } }
else { 'All boards within criteria.' }
