# Analyse every board's steady-state capture in one table.
#
# Reports, per board: fan duty span / distinct values / max step, the temperature
# span, and corr(duty, power). The correlation is the decision maker from earlier
# work: high means the fan is tracking real load (leave it alone), low means the
# motion is dither (removable).
#
# Usage: .\fan_multi_report.ps1 -Tag baseline
param(
    [string]$Tag = 'baseline',
    [string]$Dir = "$PSScriptRoot\..\..\tmp\fan_multi",
    [int]$SkipSec = 45
)
$files = Get-ChildItem $Dir -Filter "steady_${Tag}_*.csv" -EA SilentlyContinue | Sort-Object Name
if (-not $files) { throw "no steady_${Tag}_*.csv in $Dir" }

function Corr($a, $b) {
    $n = [math]::Min($a.Count, $b.Count)
    if ($n -lt 10) { return [double]::NaN }
    $ma = ($a[0..($n-1)] | Measure-Object -Average).Average
    $mb = ($b[0..($n-1)] | Measure-Object -Average).Average
    $sab = 0.0; $saa = 0.0; $sbb = 0.0
    for ($i = 0; $i -lt $n; $i++) {
        $da = $a[$i] - $ma; $db = $b[$i] - $mb
        $sab += $da*$db; $saa += $da*$da; $sbb += $db*$db
    }
    if ($saa -le 0 -or $sbb -le 0) { return [double]::NaN }
    return $sab / [math]::Sqrt($saa*$sbb)
}

$rows = @()
foreach ($f in $files) {
    $ip = ($f.BaseName -replace '^steady_' + $Tag + '_', '') -replace '_','.'
    $r = Import-Csv $f.FullName
    if ($r.Count -lt 20) { continue }
    $t0 = [double]$r[0].t_s
    $r = $r | Where-Object { ([double]$_.t_s - $t0) -ge $SkipSec }
    if ($r.Count -lt 20) { continue }

    $f0 = @($r | ForEach-Object { [double]$_.fan0_pct })
    $f1 = @($r | Where-Object { $_.fan1_pct -ne '' } | ForEach-Object { [double]$_.fan1_pct })
    $vc = @($r | ForEach-Object { [double]$_.vcore_c })
    $as = @($r | ForEach-Object { [double]$_.asic_c })
    $pw = @($r | ForEach-Object { [double]$_.pwr_w })
    $fr = @($r | ForEach-Object { [double]$_.freq_req })

    function StepInfo($a) {
        $mx = 0; $cnt = 0
        for ($i = 1; $i -lt $a.Count; $i++) {
            $d = [math]::Abs($a[$i] - $a[$i-1])
            if ($d -gt $mx) { $mx = $d }
            if ($d -ge 2) { $cnt++ }
        }
        return @{ max = $mx; moves = $cnt }
    }

    # Decide which fan is the "interesting" one: the one that moves.
    $span0 = (($f0 | Measure-Object -Maximum).Maximum - ($f0 | Measure-Object -Minimum).Minimum)
    $span1 = if ($f1.Count -gt 0) { (($f1 | Measure-Object -Maximum).Maximum - ($f1 | Measure-Object -Minimum).Minimum) } else { -1 }
    if ($span1 -gt $span0) { $use = $f1; $which='fan1'; $tUse = $vc } else { $use = $f0; $which='fan0'; $tUse = $as }

    $si = StepInfo $use
    $rows += [pscustomobject]@{
        ip        = $ip
        point     = ('{0}/{1}' -f $fr[0], $r[0].vcore_req)
        fan       = $which
        duty_span = [math]::Round((($use | Measure-Object -Maximum).Maximum - ($use | Measure-Object -Minimum).Minimum))
        duty_uniq = ($use | Select-Object -Unique).Count
        moves     = $si.moves
        max_step  = [math]::Round($si.max)
        temp_span = [math]::Round((($tUse | Measure-Object -Maximum).Maximum - ($tUse | Measure-Object -Minimum).Minimum), 2)
        corr_pwr  = [math]::Round((Corr $use $pw), 3)
        pwr_sd    = [math]::Round((($pw | Measure-Object -Average).Average), 1)
        verdict   = if ([math]::Abs((Corr $use $pw)) -gt 0.4) { 'TRACKING load' } else { 'dither' }
    }
}

"=== multi-board steady state, tag=$Tag, first ${SkipSec}s dropped ==="
''
$rows | Format-Table -AutoSize | Out-String -Width 200
''
'Reading:'
'  duty_span/uniq/moves/max_step  describe the fan that moves most on that board'
'  corr_pwr                       high = the loop is following real load (do not crush it)'
'                                 low  = pure dither (removable without losing anything)'
