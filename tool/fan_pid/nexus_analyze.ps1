# Analyse a Nexus identification run (nexus_identify.ps1 output).
#
# The generic analyze_steps.ps1 groups on phase names (C_STEP_*/S*); this board
# sweeps two fans in one file, so the segments are derived from the ACTUAL duty
# changes of the fan being analysed instead. That keeps the step boundaries
# correct even when the second fan keeps running through the first fan's steps.
#
# Usage:
#   .\nexus_analyze.ps1 -Csv <csv> -Signal asic
#   .\nexus_analyze.ps1 -Csv <csv> -Signal vcore
param(
    [Parameter(Mandatory = $true)][string]$Csv,
    [ValidateSet('asic', 'vcore')][string]$Signal = 'asic',
    [double]$AsicNoise  = 0.20,
    [double]$VcoreNoise = 1.00
)
$rows = Import-Csv $Csv | ForEach-Object {
    [pscustomobject]@{
        t     = [double]$_.t_s
        asic  = [double]$_.asic_c
        vcore = [double]$_.vcore_c
        f0    = [int]$_.fan0_pct
        f1    = [int]$_.fan1_pct
        pwr   = [double]$_.pwr_w
    }
} | Sort-Object t

$sig  = $Signal
$duty = if ($Signal -eq 'asic') { 'f0' } else { 'f1' }
$noise = if ($Signal -eq 'asic') { $AsicNoise } else { $VcoreNoise }

function Avg($a) { if (-not $a -or $a.Count -eq 0) { return [double]::NaN }; return (($a | Measure-Object -Average).Average) }

# Split into segments of constant duty on the analysed fan.
$segments = @()
$cur = @()
$curDuty = $null
foreach ($r in $rows) {
    $d = $r.$duty
    if ($null -eq $curDuty) {
        $curDuty = $d
        $cur = @($r)
        continue
    }
    if ($d -ne $curDuty) {
        if ($cur.Count -gt 0) { $segments += , @{ duty = $curDuty; rows = $cur } }
        $curDuty = $d
        $cur = @($r)
    } else {
        $cur += $r
    }
}
if ($cur.Count -gt 0) { $segments += , @{ duty = $curDuty; rows = $cur } }

"=== segments ($Signal sensor, duty = $duty) ==="
$sum = @()
for ($s = 0; $s -lt $segments.Count; $s++) {
    $seg  = $segments[$s]
    $sr   = $seg.rows
    $n    = $sr.Count
    $dur  = $sr[-1].t - $sr[0].t
    $headN = [math]::Min(8, $n)
    $tailN = [math]::Max(20, [int]($n * 0.25)); if ($tailN -gt $n) { $tailN = $n }
    $T0  = Avg ($sr[0..($headN - 1)] | ForEach-Object { $_.$sig })
    $Tss = Avg ($sr[($n - $tailN)..($n - 1)] | ForEach-Object { $_.$sig })
    $dT  = $Tss - $T0
    # t10 / t90 relative to the first sample of the segment
    $t10 = $null; $t90 = $null
    if ([math]::Abs($dT) -gt 1e-6) {
        foreach ($r in $sr) {
            $rel = [math]::Abs($r.$sig - $T0)
            if ($null -eq $t10 -and $rel -ge [math]::Abs($dT) * 0.10) { $t10 = $r.t - $sr[0].t }
            if ($null -eq $t90 -and $rel -ge [math]::Abs($dT) * 0.90) { $t90 = $r.t - $sr[0].t; break }
        }
    }
    # dead time: first excursion larger than the sensor noise
    $L = $null
    foreach ($r in $sr) {
        if ([math]::Abs($r.$sig - $T0) -gt $noise) { $L = $r.t - $sr[0].t; break }
    }
    $tau = if ($null -ne $t10 -and $null -ne $t90 -and $t90 -gt $t10) { ($t90 - $t10) / 2.2 } else { [double]::NaN }
    $sum += [pscustomobject]@{
        idx   = $s
        duty  = $seg.duty
        dur_s = [math]::Round($dur, 0)
        T0    = [math]::Round($T0, 2)
        Tss   = [math]::Round($Tss, 2)
        dT    = [math]::Round($dT, 2)
        pwr   = [math]::Round((Avg ($sr | ForEach-Object { $_.pwr })), 1)
        L_s   = if ($null -ne $L) { [math]::Round($L, 1) } else { $null }
        t10   = if ($null -ne $t10) { [math]::Round($t10, 1) } else { $null }
        t90   = if ($null -ne $t90) { [math]::Round($t90, 1) } else { $null }
        tau_s = [math]::Round($tau, 1)
    }
}
$sum | Format-Table -AutoSize | Out-String -Width 200

"=== steady-state gain between adjacent segments (degC per % duty) ==="
for ($i = 1; $i -lt $sum.Count; $i++) {
    $a = $sum[$i - 1]; $b = $sum[$i]
    $dd = $b.duty - $a.duty
    if ($dd -ne 0) {
        '{0,3} -> {1,3}: {2,7:F4} degC/%   (Tss {3} -> {4})' -f $a.duty, $b.duty, (($b.Tss - $a.Tss) / $dd), $a.Tss, $b.Tss
    }
}
