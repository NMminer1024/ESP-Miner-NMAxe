# Summarise a Nexus identification CSV: per-phase steady-state temperature
# distribution, to separate sensor noise from residual thermal drift.
#
# Usage: .\nexus_noise.ps1 -Csv <csv> [-TailSec 60]
param(
    [Parameter(Mandatory = $true)][string]$Csv,
    [int]$TailSec = 60
)
$rows = Import-Csv $Csv | ForEach-Object {
    [pscustomobject]@{
        t     = [double]$_.t_s
        phase = $_.phase
        asic  = [double]$_.asic_c
        vcore = [double]$_.vcore_c
        f0    = [int]$_.fan0_pct
        f1    = [int]$_.fan1_pct
    }
} | Sort-Object t

$phases = $rows | Group-Object phase
foreach ($g in $phases) {
    $seg = $g.Group
    $tEnd = ($seg | Select-Object -Last 1).t
    $tail = $seg | Where-Object { $_.t -ge ($tEnd - $TailSec) }
    if ($tail.Count -lt 5) { continue }

    foreach ($sig in @('asic', 'vcore')) {
        $a = @($tail | ForEach-Object { $_.$sig })
        $st = $a | Measure-Object -Average -Minimum -Maximum
        $sd = [math]::Sqrt((($a | ForEach-Object { [math]::Pow($_ - $st.Average, 2) }) | Measure-Object -Sum).Sum / ($a.Count - 1))
        $uniq = ($a | Select-Object -Unique | Sort-Object)
        '{0,-7} {1,-5} n={2,3} mean={3,6:F2} sd={4,5:F3} min={5,5:F2} max={6,5:F2} uniq={7}' -f `
            $g.Name, $sig, $a.Count, $st.Average, $sd, $st.Minimum, $st.Maximum, ($uniq -join '/')
    }
}
