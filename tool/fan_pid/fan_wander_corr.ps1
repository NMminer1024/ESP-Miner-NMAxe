# Is the residual Vcore-fan wander load tracking or loop dither?
#
# Correlates fan1 duty against measured power / hashrate inside one steady phase.
# If the fan follows power, the "wander" is the loop doing its job and damping it
# only trades temperature for acoustics. If it does not correlate, it is dither
# and can be removed by gain / deadband / duty hysteresis without any cost.
#
# Usage: .\fan_wander_corr.ps1 -Csv <csv> -Phase B_AUTO
param(
    [Parameter(Mandatory = $true)][string]$Csv,
    [string]$Phase = 'B_AUTO',
    [double]$SkipSec = 60
)

$rows = Import-Csv $Csv | Where-Object { $_.phase -eq $Phase }
if ($rows.Count -lt 10) { throw "phase '$Phase' not found or too short in $Csv" }

# Drop the settling head so the statistics describe steady state, not the step in.
$t0   = [double]$rows[0].t_s
$rows = $rows | Where-Object { ([double]$_.t_s - $t0) -ge $SkipSec }
$n    = $rows.Count

function Col($name) { return @($rows | ForEach-Object { [double]$_.$name }) }
$f1  = Col 'fan1_pct'
$f0  = Col 'fan0_pct'
$pwr = Col 'pwr_w'
$vc  = Col 'vcore_c'
$as  = Col 'asic_c'
$hr  = Col 'hr_ghs'

function Stats($a) {
    $s = $a | Measure-Object -Average -Minimum -Maximum
    $var = 0.0
    foreach ($v in $a) { $var += [math]::Pow($v - $s.Average, 2) }
    $sd = [math]::Sqrt($var / ($a.Count - 1))
    return [pscustomobject]@{
        mean = $s.Average; min = $s.Minimum; max = $s.Maximum
        span = $s.Maximum - $s.Minimum; sd = $sd
        uniq = ($a | Select-Object -Unique).Count
    }
}
function Corr($a, $b) {
    $ma = ($a | Measure-Object -Average).Average
    $mb = ($b | Measure-Object -Average).Average
    $sab = 0.0; $saa = 0.0; $sbb = 0.0
    for ($i = 0; $i -lt $a.Count; $i++) {
        $da = $a[$i] - $ma; $db = $b[$i] - $mb
        $sab += $da * $db; $saa += $da * $da; $sbb += $db * $db
    }
    if ($saa -le 0 -or $sbb -le 0) { return [double]::NaN }
    return $sab / [math]::Sqrt($saa * $sbb)
}

"=== $([System.IO.Path]::GetFileName($Csv))  phase=$Phase  n=$n (head ${SkipSec}s dropped) ==="
foreach ($pair in @(@('fan1_pct',$f1), @('fan0_pct',$f0), @('pwr_w',$pwr), @('vcore_c',$vc), @('asic_c',$as), @('hr_ghs',$hr))) {
    $st = Stats $pair[1]
    '{0,-9} mean={1,9:F3} min={2,8:F3} max={3,8:F3} span={4,7:F3} sd={5,6:F3} uniq={6,3}' -f `
        $pair[0], $st.mean, $st.min, $st.max, $st.span, $st.sd, $st.uniq
}

''
'=== correlation (does the fan move with the load?) ==='
'corr(fan1, pwr)   = {0,7:F3}' -f (Corr $f1 $pwr)
'corr(vcore, pwr)  = {0,7:F3}' -f (Corr $vc $pwr)
'corr(fan1, vcore) = {0,7:F3}' -f (Corr $f1 $vc)
'corr(fan1, asic)  = {0,7:F3}' -f (Corr $f1 $as)
'corr(asic, pwr)   = {0,7:F3}' -f (Corr $as $pwr)
'corr(fan1, fan0)  = {0,7:F3}' -f (Corr $f1 $f0)

''
'=== how much of fan1 variance would a "hold" policy give up? ==='
# If the fan were pinned at its mean duty, the temperature would drift by roughly
# K * (duty span). Report that so the trade-off is explicit.
if (($vc | Select-Object -Unique).Count -gt 1) {
    $vcSpan = (Stats $vc).span
    'measured vcore span = {0:F2} C; fan1 span = {1:F0} points' -f $vcSpan, (Stats $f1).span
} else {
    'vcore reading never left one LSB -> the fan is moving without any measurable temperature signal (pure search/dither).'
}
