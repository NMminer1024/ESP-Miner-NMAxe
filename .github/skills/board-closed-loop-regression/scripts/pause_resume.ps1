# Pause/resume regression cycles over the API. Include long pauses (>=60s) to
# cover the vcore TOCTOU window. Verdict per cycle vs -GoodHr (~90% of expected).
# NMAxe caveat: a resume below warmup_temp legitimately runs warmup_frq first,
# so mid-window averages can be ~87% of target -> check log markers before BAD.
param(
    [int]$Cycles = 5,
    [string]$Ip = $(throw 'Ip required'),
    [int[]]$PauseSec = @(20, 60, 20, 120, 20),
    [int]$WarmSec = 35,        # settle time after resume before sampling
    [int]$SampleSec = 50,      # sampling window length
    [double]$GoodHr = 470,     # GOOD threshold (GH/s)
    [string]$Tag = 'pr',
    [string]$Csv = "$PSScriptRoot\pr_$($Ip -replace '\.','_').csv"
)
$base = "http://$Ip"
if (-not (Test-Path $Csv)) { 'tag,cycle,time,pause_s,temp_at_resume,avg_hr,min_hr,max_hr,avg_pwr,vcore,verdict' | Out-File $Csv -Encoding ascii }
function Set-Paused([bool]$p) {
    $body = @{ paused = $p } | ConvertTo-Json -Compress
    try { (Invoke-RestMethod -Method Patch -Uri "$base/api/mining/state" -Body $body -ContentType 'application/json' -TimeoutSec 5).state } catch { "ERR $_" }
}
$sum = @{ GOOD = 0; BAD = 0; DEAD = 0 }
for ($c = 1; $c -le $Cycles; $c++) {
    $ps = $PauseSec[($c - 1) % $PauseSec.Count]
    $null = Set-Paused $true
    Start-Sleep -Seconds $ps
    $ti = try { Invoke-RestMethod "$base/api/system/info" -TimeoutSec 4 } catch { $null }
    $temp = if ($ti) { [math]::Round([double]$ti.temps.asic, 1) } else { -1 }
    $null = Set-Paused $false
    $t = Get-Date
    Start-Sleep -Seconds $WarmSec
    $hr = @(); $pw = @(); $vc = 0
    $end = (Get-Date).AddSeconds($SampleSec)
    while ((Get-Date) -lt $end) {
        $i = try { Invoke-RestMethod "$base/api/system/info" -TimeoutSec 4 } catch { $null }
        if ($i) { $hr += [double]$i.miner.hashRate; $pw += [double]$i.power.power; $vc = $i.asic.vcoreReal }
        Start-Sleep -Seconds 5
    }
    $avg = if ($hr.Count) { [math]::Round(($hr | Measure-Object -Average).Average, 1) } else { 0 }
    $mn = if ($hr.Count) { [math]::Round(($hr | Measure-Object -Minimum).Minimum, 1) } else { 0 }
    $mx = if ($hr.Count) { [math]::Round(($hr | Measure-Object -Maximum).Maximum, 1) } else { 0 }
    $ap = if ($pw.Count) { [math]::Round(($pw | Measure-Object -Average).Average, 2) } else { 0 }
    $v = if ($avg -ge $GoodHr) { 'GOOD' } elseif ($avg -ge 50) { 'BAD' } else { 'DEAD' }
    $sum[$v]++
    $row = "{0},{1},{2:HH:mm:ss},{3},{4},{5},{6},{7},{8},{9},{10}" -f $Tag, $c, $t, $ps, $temp, $avg, $mn, $mx, $ap, $vc, $v
    $row | Out-File $Csv -Append -Encoding ascii
    Write-Host $row
}
Write-Host ("SUMMARY {0}: GOOD={1} BAD={2} DEAD={3}" -f $Tag, $sum.GOOD, $sum.BAD, $sum.DEAD)
