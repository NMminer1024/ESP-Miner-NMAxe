# Reproduce fan_pid_tuning_from_plant() for every board preset and report what
# the v3.2.02u duty-hysteresis change does to each fan loop.
#
# This mirrors src/drivers/fan/fan.cpp exactly (lambda = 2L, Kc = tau/(K*3L),
# noise cap = max_dither/sensor_noise, x0.6 when !identified, floor 0.05,
# duty_hyst = 0.5*Kc*sensor_lsb, <2 -> 0). Keep in sync with the C code.
#
# sensor_lsb is the QUANTISATION STEP of the reported reading, not its noise.
#   TMP102 / TPS53647 : 1/16 C  -> hyst switches itself off
#   TPS546D24A (Nexus): 1 C     -> hyst = 2 points
#
# Usage: .\fan_hyst_matrix.ps1

# name, K, L, tau, noise, lsb, dither, identified, board-env
$plants = @(
    @{ name = 'NMAxe fixed blower';       K=1.000; L=1.0;  tau=10.0; noise=1.00; lsb=1.0;    dither=1.0; id=$true;  env='NMAxe' },
    @{ name = 'generic ASIC (fallback)';  K=0.200; L=6.0;  tau=35.0; noise=0.25; lsb=0.0625; dither=4.0; id=$false; env='-' },
    @{ name = 'generic Vcore (fallback)'; K=0.050; L=10.0; tau=60.0; noise=1.00; lsb=0.0625; dither=4.0; id=$false; env='-' },
    @{ name = 'QAxe++ 2ph ASIC';          K=0.310; L=8.6;  tau=45.0; noise=0.20; lsb=0.0625; dither=4.0; id=$true;  env='NMQAxePP' },
    @{ name = 'QAxe++ 2ph Vcore';         K=0.070; L=9.0;  tau=58.0; noise=1.00; lsb=0.0625; dither=4.0; id=$true;  env='NMQAxePP' },
    @{ name = 'Rev6.1/8.1 ASIC';          K=0.300; L=6.0;  tau=45.0; noise=0.20; lsb=0.0625; dither=4.0; id=$false; env='NMQAxePPRev61' },
    @{ name = 'Rev6.1/8.1 Vcore';         K=0.030; L=11.0; tau=70.0; noise=1.00; lsb=0.0625; dither=4.0; id=$false; env='NMQAxePPRev61' },
    @{ name = 'Nexus ASIC';               K=0.263; L=5.0;  tau=45.0; noise=0.15; lsb=0.0625; dither=4.0; id=$true;  env='NMQAxePPNexus' },
    @{ name = 'Nexus Vcore';              K=0.107; L=12.0; tau=50.0; noise=1.00; lsb=1.0;    dither=4.0; id=$true;  env='NMQAxePPNexus' },
    @{ name = 'Gamma ASIC';               K=0.220; L=6.0;  tau=50.0; noise=0.20; lsb=0.0625; dither=4.0; id=$true;  env='NMAxeGamma' }
)

$out = foreach ($p in $plants) {
    # sanitise exactly like the C code
    $K = if ($p.K -gt 1e-4) { $p.K } else { 1e-4 }
    $L = if ($p.L -gt 0.5) { $p.L } else { 0.5 }
    $T = if ($p.tau -gt (2.0 * $L)) { $p.tau } else { 2.0 * $L }
    $noise  = if ($p.noise  -gt 0.01) { $p.noise  } else { 0.01 }
    $dither = if ($p.dither -gt 0.5)  { $p.dither } else { 0.5 }

    $lambda = 2.0 * $L
    $Kc = $T / ($K * ($lambda + $L))       # = tau / (K * 3L)

    $cap = $dither / $noise
    if (-not $p.id) { $cap *= 0.6 }
    if ($Kc -gt $cap) { $Kc = $cap }
    if ($Kc -lt 0.05) { $Kc = 0.05 }

    $Ki      = $Kc / $T
    $db      = [math]::Max(0.5 * $noise, 0.15)
    $filt    = [math]::Max(0.5 * $L, 1.0)
    $hystRaw = 0.5 * $Kc * $p.lsb
    $hyst    = if ($hystRaw -lt 2.0) { 0.0 } else { $hystRaw }
    # how much static error a full hyst step can hold, in sensor degrees
    $hystC   = if ($Kc -gt 0) { $hyst / $Kc } else { 0 }

    [pscustomobject]@{
        plant   = $p.name
        env     = $p.env
        id      = if ($p.id) { 'yes' } else { 'no ' }
        'Kp'    = [math]::Round($Kc, 2)
        'Ki'    = [math]::Round($Ki, 3)
        'db_C'  = [math]::Round($db, 2)
        'filt_s'= [math]::Round($filt, 1)
        'lsb_C' = $p.lsb
        'hyst_raw' = [math]::Round($hystRaw, 2)
        'hyst_%'   = [math]::Round($hyst, 0)
        'hyst_C'   = [math]::Round($hystC, 2)
        status  = if ($hyst -ge 1) { 'HYSTERESIS ON' } else { 'disabled' }
    }
}
"=== fan_pid_tuning_from_plant() reproduced for every preset (v3.2.02u) ==="
''
$out | Format-Table -AutoSize | Out-String -Width 200
''
'Legend:'
'  lsb_C    = quantisation step of the REPORTED reading (TMP102/TPS53647 1/16 C, TPS546D24A 1 C)'
'  hyst_raw = 0.5 * Kp * sensor_lsb  (the derived value)'
'  hyst_%   = what actually applies: 0 when hyst_raw < 2 points'
'  hyst_C   = static error a full hysteresis step is worth (hyst / Kp)'
''
'Loops affected by the new term:'
$out | Where-Object { $_.'hyst_%' -ge 1 } | ForEach-Object { '  {0}  ->  {1} %' -f $_.plant, $_.'hyst_%' }
''
'Loops whose compiled behaviour is unchanged (hyst = 0):'
$out | Where-Object { $_.'hyst_%' -lt 1 } | ForEach-Object { '  {0}' -f $_.plant }
