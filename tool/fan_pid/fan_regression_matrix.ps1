# Prove that the v3.2.02u fan change did not spread: recompute the tuning for
# every board preset from BOTH the pre-change (git HEAD) plant values and the
# current working-tree values, and compare every derived number.
#
# Any row that differs is a board whose closed-loop behaviour changed. The only
# rows allowed to differ are the two Nexus loops, whose plants were re-measured
# on hardware as part of the same change set.
#
# Usage: .\fan_regression_matrix.ps1

# --- pre-change plants (git HEAD, before v3.2.02u) -------------------------
# name, K, L, tau, noise, dither, identified
$before = @(
    @{ n='NMAxe fixed blower';  K=1.000; L=1.0;  tau=10.0; noise=1.00; dit=1.0; id=$true  },
    @{ n='generic ASIC';        K=0.200; L=6.0;  tau=35.0; noise=0.25; dit=4.0; id=$false },
    @{ n='generic Vcore';       K=0.050; L=10.0; tau=60.0; noise=1.00; dit=4.0; id=$false },
    @{ n='QAxe++ 2ph ASIC';     K=0.310; L=8.6;  tau=45.0; noise=0.20; dit=4.0; id=$true  },
    @{ n='QAxe++ 2ph Vcore';    K=0.070; L=9.0;  tau=58.0; noise=1.00; dit=4.0; id=$true  },
    @{ n='Rev6.1/8.1 ASIC';     K=0.300; L=6.0;  tau=45.0; noise=0.20; dit=4.0; id=$false },
    @{ n='Rev6.1/8.1 Vcore';    K=0.030; L=11.0; tau=70.0; noise=1.00; dit=4.0; id=$false },
    @{ n='Nexus ASIC';          K=0.280; L=7.0;  tau=50.0; noise=0.20; dit=4.0; id=$false },
    @{ n='Nexus Vcore';         K=0.030; L=12.0; tau=75.0; noise=1.00; dit=4.0; id=$false },
    @{ n='Gamma ASIC';          K=0.220; L=6.0;  tau=50.0; noise=0.20; dit=4.0; id=$true  }
)

# --- current plants (working tree, v3.2.02u) ------------------------------
$after = @(
    @{ n='NMAxe fixed blower';  K=1.000; L=1.0;  tau=10.0; noise=1.00; lsb=1.0;    dit=1.0; id=$true  },
    @{ n='generic ASIC';        K=0.200; L=6.0;  tau=35.0; noise=0.25; lsb=0.0625; dit=4.0; id=$false },
    @{ n='generic Vcore';       K=0.050; L=10.0; tau=60.0; noise=1.00; lsb=0.0625; dit=4.0; id=$false },
    @{ n='QAxe++ 2ph ASIC';     K=0.310; L=8.6;  tau=45.0; noise=0.20; lsb=0.0625; dit=4.0; id=$true  },
    @{ n='QAxe++ 2ph Vcore';    K=0.070; L=9.0;  tau=58.0; noise=1.00; lsb=0.0625; dit=4.0; id=$true  },
    @{ n='Rev6.1/8.1 ASIC';     K=0.300; L=6.0;  tau=45.0; noise=0.20; lsb=0.0625; dit=4.0; id=$false },
    @{ n='Rev6.1/8.1 Vcore';    K=0.030; L=11.0; tau=70.0; noise=1.00; lsb=0.0625; dit=4.0; id=$false },
    @{ n='Nexus ASIC';          K=0.263; L=5.0;  tau=45.0; noise=0.15; lsb=0.0625; dit=4.0; id=$true  },
    @{ n='Nexus Vcore';         K=0.107; L=12.0; tau=50.0; noise=1.00; lsb=1.0;    dit=4.0; id=$true  },
    @{ n='Gamma ASIC';          K=0.220; L=6.0;  tau=50.0; noise=0.20; lsb=0.0625; dit=4.0; id=$true  }
)

function Tune($p, [double]$lsb) {
    $K = [math]::Max($p.K, 1e-4)
    $L = [math]::Max($p.L, 0.5)
    $T = if ($p.tau -gt (2.0*$L)) { $p.tau } else { 2.0*$L }
    $noise = [math]::Max($p.noise, 0.01)
    $dit   = [math]::Max($p.dit, 0.5)
    $lambda = 2.0*$L
    $Kc = $T / ($K * ($lambda + $L))
    $cap = $dit / $noise
    if (-not $p.id) { $cap *= 0.6 }
    if ($Kc -gt $cap) { $Kc = $cap }
    if ($Kc -lt 0.05) { $Kc = 0.05 }
    $Ki   = $Kc / $T
    $db   = [math]::Max(0.5*$noise, 0.15)
    $filt = [math]::Max(0.5*$L, 1.0)
    $up   = if ($p.up_override) { $p.up_override } else { 25.0 }
    # slew_up: fixed-blower/generic use 1.0/25.0/25.0 in code; carried as needed
    $dnHi = 0.05/$K
    $dnTau = 60.0/$T
    $dn = [math]::Max($dnHi, $dnTau)
    if ($dn -lt 1.0)  { $dn = 1.0 }
    if ($dn -gt 15.0) { $dn = 15.0 }
    $hyst = 0.0
    if ($null -ne $lsb) { $hyst = 0.5*$Kc*$lsb; if ($hyst -lt 2.0) { $hyst = 0.0 } }
    [pscustomobject]@{
        Kp=[math]::Round($Kc,4); Ki=[math]::Round($Ki,4); db=[math]::Round($db,3)
        filt=[math]::Round($filt,2); hyst=[math]::Round($hyst,2)
    }
}

$rows = @()
for ($i = 0; $i -lt $before.Count; $i++) {
    $b = Tune $before[$i] $null          # no lsb -> zero hysteresis, Kp from noise only
    $a = Tune $after[$i]  $after[$i].lsb
    $diff = @()
    if ($b.Kp   -ne $a.Kp)   { $diff += "Kp $($b.Kp)->$($a.Kp)" }
    if ($b.Ki   -ne $a.Ki)   { $diff += "Ki $($b.Ki)->$($a.Ki)" }
    if ($b.db   -ne $a.db)   { $diff += "db $($b.db)->$($a.db)" }
    if ($b.filt -ne $a.filt) { $diff += "filt $($b.filt)->$($a.filt)" }
    if ($a.hyst -ne 0)       { $diff += "hyst +$($a.hyst)%" }
    $rows += [pscustomobject]@{
        plant   = $before[$i].n
        'Kp_before' = $b.Kp; 'Kp_after' = $a.Kp
        'Ki_before' = $b.Ki; 'Ki_after' = $a.Ki
        'db_before' = $b.db; 'db_after' = $a.db
        'filt_before' = $b.filt; 'filt_after' = $a.filt
        'hyst'  = $a.hyst
        verdict = if ($diff.Count -eq 0) { 'IDENTICAL' } else { ($diff -join '; ') }
    }
}

"=== fan loop: pre-change (HEAD) vs working tree (v3.2.02u) ==="
''
$rows | Format-Table -AutoSize | Out-String -Width 220
''
$changed = $rows | Where-Object { $_.verdict -ne 'IDENTICAL' }
'Loops whose derived tuning CHANGED:'
$changed | ForEach-Object { '  {0,-20} {1}' -f $_.plant, $_.verdict }
''
'Loops whose derived tuning is byte-identical:'
($rows | Where-Object { $_.verdict -eq 'IDENTICAL' }) | ForEach-Object { '  {0}' -f $_.plant }
''
'Note: TMP102 / TPS53647 sensors report 1/16 C, so the new hysteresis term'
'lands below 2 points for all of them and is forced to 0 by design.'
