# Side-by-side comparison of two fan_verify.ps1 runs (old PID vs new).
# Reports, per phase: duration, duty range, temperature range, how often the duty
# jumped by >= 15 points in one 250 ms sample, and the largest single step.
param(
    [Parameter(Mandatory = $true)][string]$Old,
    [Parameter(Mandatory = $true)][string]$New,
    [ValidateSet('vcore', 'asic')][string]$Signal = 'vcore'
)
$duty = if ($Signal -eq 'asic') { 'fan0_pct' } else { 'fan1_pct' }

function Load($path) {
    Import-Csv $path | ForEach-Object {
        [pscustomobject]@{
            t     = [double]$_.t_s
            asic  = [double]$_.asic_c
            vcore = [double]$_.vcore_c
            fan0  = [int]$_.fan0_pct
            fan1  = [int]$_.fan1_pct
            pwr   = [double]$_.pwr_w
            phase = $_.phase
        }
    }
}
function Stats($rows, $phaseName, $dutyCol, $sig) {
    $s = @($rows | Where-Object { $_.phase -eq $phaseName } | Sort-Object t)
    if ($s.Count -lt 5) { return $null }
    $jumps = 0; $dsum = 0.0; $dmax = 0.0
    for ($i = 1; $i -lt $s.Count; $i++) {
        $a = $s[$i].$dutyCol
        $b = $s[$i - 1].$dutyCol
        $d = [math]::Abs($a - $b)
        $dsum += $d
        if ($d -gt $dmax) { $dmax = $d }
        if ($d -ge 15) { $jumps++ }
    }
    $dur = ($s[-1].t - $s[0].t) / 60.0
    [pscustomobject]@{
        duration_min = [math]::Round($dur, 1)
        duty_min     = ($s.$dutyCol | Measure-Object -Minimum).Minimum
        duty_avg     = [math]::Round((($s.$dutyCol | Measure-Object -Average).Average), 0)
        duty_max     = ($s.$dutyCol | Measure-Object -Maximum).Maximum
        temp_min     = [math]::Round((($s.$sig | Measure-Object -Minimum).Minimum), 1)
        temp_max     = [math]::Round((($s.$sig | Measure-Object -Maximum).Maximum), 1)
        jumps_15     = $jumps
        jumps_permin = [math]::Round($jumps / [math]::Max($dur, 0.01), 2)
        largest_step = [math]::Round($dmax, 0)
        mean_step    = [math]::Round($dsum / [math]::Max($s.Count - 1, 1), 1)
    }
}

$oldRows = Load $Old
$newRows = Load $New
$phases  = @('B_AUTO', 'C_COOLDOWN', 'D_HANDOVER', 'E_LOADSTEP')

"===== $Signal fan loop: OLD = $Old ====="
foreach ($p in $phases) { $r = Stats $oldRows $p $duty $Signal; if ($r) { "[$p]"; $r | Format-List | Out-String } }
"===== $Signal fan loop: NEW = $New ====="
foreach ($p in $phases) { $r = Stats $newRows $p $duty $Signal; if ($r) { "[$p]"; $r | Format-List | Out-String } }

'===== summary ====='
$rowsOut = @()
foreach ($p in $phases) {
    $o = Stats $oldRows $p $duty $Signal
    $n = Stats $newRows $p $duty $Signal
    if (-not $o -and -not $n) { continue }
    $rowsOut += [pscustomobject]@{
        phase        = $p
        old_jumps15  = if ($o) { $o.jumps_15 } else { '-' }
        new_jumps15  = if ($n) { $n.jumps_15 } else { '-' }
        old_maxstep  = if ($o) { $o.largest_step } else { '-' }
        new_maxstep  = if ($n) { $n.largest_step } else { '-' }
        old_duty_rng = if ($o) { "$($o.duty_min)-$($o.duty_max)" } else { '-' }
        new_duty_rng = if ($n) { "$($n.duty_min)-$($n.duty_max)" } else { '-' }
        old_temp_rng = if ($o) { "$($o.temp_min)-$($o.temp_max)" } else { '-' }
        new_temp_rng = if ($n) { "$($n.temp_min)-$($n.temp_max)" } else { '-' }
    }
}
$rowsOut | Format-Table -AutoSize | Out-String -Width 200
