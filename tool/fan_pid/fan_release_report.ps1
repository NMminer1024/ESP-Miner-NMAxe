# Release regression report: steady state only, with an explicit warm-up discard.
#
# The multi-report default drops 45 s, which is right for a board that has been
# running for hours. Right after a flash the loop is still pulling the rail back
# to target, so the first minutes look like a huge span. In closed loop the fan
# and the temperature are coupled, so a big temperature span early on is the loop
# doing its job, not a fault.
#
# This variant reports two windows side by side:
#   ALL   - everything after the discard (shows the settling transient)
#   LAST  - the final third only (the actual steady state)
# and the pass/fail is judged on LAST.
#
# Usage: .\fan_release_report.ps1 -Tag release
param(
    [string]$Tag = 'release',
    [string]$Dir = "$PSScriptRoot\..\..\tmp\fan_multi",
    [int]$SkipSec = 60
)
$files = Get-ChildItem $Dir -Filter "steady_${Tag}_*.csv" -EA SilentlyContinue | Sort-Object Name
if (-not $files) { throw "no steady_${Tag}_*.csv in $Dir" }

function Stats($a) {
    $s = $a | Measure-Object -Average -Minimum -Maximum
    $v = 0.0; foreach ($x in $a) { $v += [math]::Pow($x - $s.Average, 2) }
    [pscustomobject]@{
        min=$s.Minimum; max=$s.Maximum; avg=$s.Average
        span=($s.Maximum - $s.Minimum)
        sd=[math]::Sqrt($v / [math]::Max(1, $a.Count - 1))
        uniq=($a | Select-Object -Unique).Count
    }
}
function MaxStep($a) {
    $m = 0
    for ($i = 1; $i -lt $a.Count; $i++) { $d = [math]::Abs($a[$i] - $a[$i-1]); if ($d -gt $m) { $m = $d } }
    return $m
}

$rows = @()
foreach ($f in $files) {
    $leaf = $f.BaseName -replace '^steady_' + [regex]::Escape($Tag) + '_', ''
    $ip = $leaf -replace '_', '.'
    $r = Import-Csv $f.FullName
    if ($r.Count -lt 60) { continue }
    $t0 = [double]$r[0].t_s
    $r = @($r | Where-Object { ([double]$_.t_s - $t0) -ge $SkipSec })
    if ($r.Count -lt 60) { continue }
    $lastIdx = [int]($r.Count * 2 / 3)
    $last = @($r[$lastIdx..($r.Count-1)])

    $f0 = @($r | ForEach-Object { [double]$_.fan0_pct })
    $f1 = @($r | Where-Object { $_.fan1_pct -ne '' } | ForEach-Object { [double]$_.fan1_pct })
    $as = @($r | ForEach-Object { [double]$_.asic_c })
    $vc = @($r | ForEach-Object { [double]$_.vcore_c })

    $lf0 = @($last | ForEach-Object { [double]$_.fan0_pct })
    $lf1 = @($last | Where-Object { $_.fan1_pct -ne '' } | ForEach-Object { [double]$_.fan1_pct })
    $las = @($last | ForEach-Object { [double]$_.asic_c })
    $lvc = @($last | ForEach-Object { [double]$_.vcore_c })

    $useAll = if ($f1.Count -gt 0 -and (Stats $f1).span -gt (Stats $f0).span) { @{ d=$f1; t=$vc; n='fan1' } } else { @{ d=$f0; t=$as; n='fan0' } }
    $useLast = if ($useAll.n -eq 'fan1') { @{ d=$lf1; t=$lvc } } else { @{ d=$lf0; t=$las } }

    $dAll = Stats $useAll.d; $dLast = Stats $useLast.d
    $tAll = Stats $useAll.t; $tLast = Stats $useLast.t

    $rows += [pscustomobject]@{
        ip        = $ip
        fan       = $useAll.n
        all_span  = [math]::Round($dAll.span)
        all_tspan = [math]::Round($tAll.span, 2)
        last_duty = [math]::Round($dLast.avg, 1)
        last_span = [math]::Round($dLast.span)
        last_uniq = $dLast.uniq
        last_step = [math]::Round((MaxStep $useLast.d))
        last_temp = [math]::Round($tLast.avg, 2)
        last_tspan= [math]::Round($tLast.span, 2)
        pass      = if ((MaxStep $useLast.d) -le 2) { 'PASS' } else { 'FAIL' }
    }
}

"=== release regression, tag=$Tag ==="
"Warm-up: first ${SkipSec}s discarded. ALL = settling visible, LAST = final third (steady state)."
''
$rows | Format-Table -AutoSize | Out-String -Width 200
''
'Pass criterion: no single applied-duty change above 2 points in the steady window.'
''
$fail = $rows | Where-Object pass -eq 'FAIL'
if ($fail) { 'FAILED:'; $fail | ForEach-Object { '  {0}: max step {1} points' -f $_.ip, $_.last_step } }
else { "All $($rows.Count) board(s) PASS (no jump above 2 points in steady state)." }
