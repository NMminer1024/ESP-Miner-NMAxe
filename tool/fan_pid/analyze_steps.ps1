# Summarise a fan identification run: steady-state gain / time constant per
# manual-duty step, and hunting statistics for the auto phases.
#
# Works for either sensor. The CSV columns are the same for both scripts
# (fan_identify_asic.ps1 / fan_experiment.ps1), so:
#   .\analyze_steps.ps1 -Csv ..\..\docs\baseline\fan_124_18_experiment.csv -Signal vcore
#   .\analyze_steps.ps1 -Csv ..\..\docs\baseline\fan_124_18_asic_ident.csv -Signal asic
param(
    [string]$Csv = "$PSScriptRoot\..\..\docs\baseline\fan_124_18_experiment.csv",
    [ValidateSet('asic', 'vcore')][string]$Signal = 'vcore'
)
$rows = Import-Csv $Csv | ForEach-Object {
    [pscustomobject]@{
        t     = [double]$_.t_s
        asic  = [double]$_.asic_c
        vcore = [double]$_.vcore_c
        f0    = [int]$_.fan0_pct
        f1    = [int]$_.fan1_pct
        pwr   = [double]$_.pwr_w
        phase = $_.phase
    }
}
$sig  = $Signal
$duty = if ($Signal -eq 'asic') { 'f0' } else { 'f1' }

function Avg($a) { if ($null -eq $a) { return [double]::NaN }; return (($a | Measure-Object -Average).Average) }

# First time the signal has moved $frac of the total change away from T0.
# NOTE: PowerShell variables are case-insensitive, so the local start time must
# NOT be called $t0 - that would overwrite the $T0 parameter (silent zero result).
function Cross-Rel($s, [double]$T0, [double]$delta, [double]$frac) {
    if ([math]::Abs($delta) -lt 1e-6) { return $null }
    $thr = [math]::Abs($delta) * $frac
    $t_start = $s[0].t
    for ($i = 0; $i -lt $s.Count; $i++) {
        $v  = $s[$i].$sig
        $tt = $s[$i].t
        if ([math]::Abs($v - $T0) -ge $thr) { return ($tt - $t_start) }
    }
    return $null
}

"=== manual duty steps ($Signal sensor, duty = $duty) ==="
$steps = @()
foreach ($g in ($rows | Group-Object phase | Where-Object { $_.Name -like 'C_STEP_*' -or $_.Name -like 'S*' })) {
    $s = @($g.Group | Sort-Object t)
    $n = $s.Count
    if ($n -lt 10) { continue }
    $head = @(); for ($i = 0; $i -lt [math]::Min(9, $n); $i++) { $head += $s[$i].$sig }
    $tail = @(); for ($i = [math]::Max(0, $n - 60); $i -lt $n; $i++) { $tail += $s[$i].$sig }
    $T0    = Avg $head
    $Tss   = Avg $tail
    $delta = $Tss - $T0
    $t10 = Cross-Rel $s $T0 $delta 0.10
    $t90 = Cross-Rel $s $T0 $delta 0.90
    $tau = if ($null -ne $t10 -and $null -ne $t90 -and $t90 -gt $t10) { ($t90 - $t10) / 2.2 } else { [double]::NaN }
    $steps += [pscustomobject]@{
        phase = $g.Name
        duty  = $s[0].$duty
        T0    = [math]::Round($T0, 2)
        Tss   = [math]::Round($Tss, 2)
        dT    = [math]::Round($delta, 2)
        pwr   = [math]::Round((Avg $s.pwr), 1)
        t10   = if ($null -ne $t10) { [math]::Round($t10, 1) } else { $null }
        t90   = if ($null -ne $t90) { [math]::Round($t90, 1) } else { $null }
        tau_s = [math]::Round($tau, 1)
    }
}
$steps | Format-Table -AutoSize | Out-String -Width 200

"=== steady-state gain between adjacent steps (degC per % duty) ==="
for ($i = 1; $i -lt $steps.Count; $i++) {
    $a = $steps[$i - 1]; $b = $steps[$i]
    $dd = $b.duty - $a.duty
    if ($dd -ne 0) { '{0,3} -> {1,3}: {2:F3} degC/%' -f $a.duty, $b.duty, (($b.Tss - $a.Tss) / $dd) }
}

"=== auto phases: hunting statistics (duty column = $duty) ==="
foreach ($g in ($rows | Group-Object phase | Where-Object { $_.Name -like 'D_AUTO*' -or $_.Name -like 'B_AUTO*' })) {
    $s = @($g.Group | Sort-Object t)
    if ($s.Count -lt 10) { continue }
    $big = 0; $dsum = 0.0
    for ($i = 1; $i -lt $s.Count; $i++) {
        $d = [math]::Abs($s[$i].$duty - $s[$i - 1].$duty)
        $dsum += $d
        if ($d -ge 15) { $big++ }
    }
    $dur_min = ($s[-1].t - $s[0].t) / 60.0
    $vmin = ($s.$sig | Measure-Object -Minimum).Minimum
    $vmax = ($s.$sig | Measure-Object -Maximum).Maximum
    '{0,-12} dur={1,5:F1}min  duty[min/avg/max]={2}/{3:F0}/{4}  {5}[min/max]={6:F1}/{7:F1}  jumps>=15:{8} ({9:F2}/min)  mean|duty step|={10:F1}' -f `
        $g.Name, $dur_min,
        ($s.$duty | Measure-Object -Minimum).Minimum, (Avg $s.$duty), ($s.$duty | Measure-Object -Maximum).Maximum,
        $sig, $vmin, $vmax, $big, ($big / [math]::Max($dur_min, 0.01)), ($dsum / [math]::Max($s.Count - 1, 1))
}
