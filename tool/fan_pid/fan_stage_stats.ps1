# Per-phase jitter statistics for a fan_verify CSV (old vs new A/B pair).
#
# Reports, per phase: fan0/fan1 min-avg-max, number of jumps >= threshold,
# largest single sample-to-sample step, and the temperature range. These are the
# numbers the fan-loop change set is judged on, so old and new runs are compared
# with the same rule.
#
# Usage: .\fan_stage_stats.ps1 -Csv <csv> [-JumpThr 15]
param(
    [Parameter(Mandatory = $true)][string]$Csv,
    [int]$JumpThr = 15
)
$rows = Import-Csv $Csv | ForEach-Object {
    [pscustomobject]@{
        t     = [double]$_.t_s
        phase = $_.phase
        asic  = [double]$_.asic_c
        vcore = [double]$_.vcore_c
        f0    = [int]$_.fan0_pct
        f1    = [int]$_.fan1_pct
        pwr   = [double]$_.pwr_w
    }
} | Sort-Object t

'{0}   (jump threshold = {1} duty points)' -f (Split-Path $Csv -Leaf), $JumpThr
$out = @()
foreach ($g in ($rows | Group-Object phase)) {
    $s = $g.Group
    if ($s.Count -lt 3) { continue }
    $dur = $s[-1].t - $s[0].t

    $res = [ordered]@{ phase = $g.Name; dur_s = [math]::Round($dur) }
    foreach ($fan in @('f0', 'f1')) {
        $d = @($s | ForEach-Object { $_.$fan })
        $st = $d | Measure-Object -Minimum -Maximum -Average
        $jumps = 0; $maxStep = 0
        for ($i = 1; $i -lt $d.Count; $i++) {
            $step = [math]::Abs($d[$i] - $d[$i - 1])
            if ($step -ge $JumpThr) { $jumps++ }
            if ($step -gt $maxStep) { $maxStep = $step }
        }
        $res["${fan}_min"] = $st.Minimum
        $res["${fan}_avg"] = [math]::Round($st.Average, 0)
        $res["${fan}_max"] = $st.Maximum
        $res["${fan}_span"] = $st.Maximum - $st.Minimum
        $res["${fan}_jumps"] = $jumps
        $res["${fan}_maxstep"] = $maxStep
    }
    foreach ($sig in @('asic', 'vcore')) {
        $t = @($s | ForEach-Object { $_.$sig })
        $st = $t | Measure-Object -Minimum -Maximum -Average
        $res["${sig}_min"] = [math]::Round($st.Minimum, 1)
        $res["${sig}_max"] = [math]::Round($st.Maximum, 1)
    }
    $res['pwr_avg'] = [math]::Round(($s | ForEach-Object { $_.pwr } | Measure-Object -Average).Average, 1)
    $out += [pscustomobject]$res
}

# Two compact lines per phase: the wide one-table form wraps on a 120-col
# terminal and hides the fan1 / temperature columns.
foreach ($r in $out) {
    '{0,-11} {1,4}s  asic {2,5}..{3,5}C  vcore {4,5}..{5,5}C  {6,6}W' -f `
        $r.phase, $r.dur_s, $r.asic_min, $r.asic_max, $r.vcore_min, $r.vcore_max, $r.pwr_avg
    '            fan0 {0,3}..{1,3}% avg {2,3}  span {3,3}  jumps>={4} {5,3}  maxstep {6,2}' -f `
        $r.f0_min, $r.f0_max, $r.f0_avg, $r.f0_span, $JumpThr, $r.f0_jumps, $r.f0_maxstep
    '            fan1 {0,3}..{1,3}% avg {2,3}  span {3,3}  jumps>={4} {5,3}  maxstep {6,2}' -f `
        $r.f1_min, $r.f1_max, $r.f1_avg, $r.f1_span, $JumpThr, $r.f1_jumps, $r.f1_maxstep
}
