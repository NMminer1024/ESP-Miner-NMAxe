# Multi-board fan audit: capture every board at the same time.
#
# Why parallel: the boards are independent, and a serial pass over five boards
# would take 5x as long for no benefit. Each board gets its own WS log and its
# own steady-state CSV, captured simultaneously, so the comparison is also
# fairer (same ambient conditions, similar uptime).
#
# Usage:
#   .\fan_multi_capture.ps1 -Boards 192.168.124.2,192.168.124.6 -Minutes 6
#   .\fan_multi_capture.ps1 -Boards @('192.168.124.88') -Minutes 6 -Tag before
param(
    [Parameter(Mandatory = $true)][string[]]$Boards,
    [int]$Minutes = 6,
    [string]$Tag = 'run',
    [string]$OutDir = "$PSScriptRoot\..\..\tmp\fan_multi",
    [int]$IntervalMs = 500,
    [switch]$SkipWs
)
$ErrorActionPreference = 'Continue'

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$wsScript = Join-Path $PSScriptRoot '..\..\.github\skills\board-closed-loop-regression\scripts\ws_log.ps1'
$wsScript = (Resolve-Path $wsScript -EA SilentlyContinue).Path

$stamp = Get-Date -Format 'HHmmss'
$jobs = @()

# ── one WS capture per board (survives reboots, grabs pre-WiFi? no - post only) ──
if (-not $SkipWs -and $wsScript) {
    foreach ($ip in $Boards) {
        $out = Join-Path $OutDir "ws_${Tag}_$($ip -replace '\.','_').log"
        Remove-Item $out -Force -EA SilentlyContinue
        $p = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList @(
            '-NoProfile','-ExecutionPolicy','Bypass','-File',$wsScript,'-Ip',$ip,'-Out',$out)
        $jobs += [pscustomobject]@{ kind='ws'; ip=$ip; proc=$p; file=$out }
        Write-Host ("ws capture started for {0} (pid {1})" -f $ip, $p.Id)
    }
}

# ── one steady-state sampler per board, running concurrently as jobs ─────────
Write-Host ("`ncapturing {0} min on {1} board(s) in parallel..." -f $Minutes, $Boards.Count)
$sampler = {
    param($Ip, $Out, $Minutes, $IntervalMs)
    $fs = [System.IO.File]::Open($Out, 'Create', 'Write', 'ReadWrite')
    $w = New-Object System.IO.StreamWriter($fs, (New-Object System.Text.UTF8Encoding($false)))
    $w.AutoFlush = $true
    $w.WriteLine('t_s,iso,asic_c,vcore_c,fan0_pct,fan1_pct,fan0_rpm,fan1_rpm,vbus_v,ibus_a,pwr_w,hr_ghs,freq_req,vcore_req,vcore_real,mode,protection,phase')
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $end = $Minutes * 60
    $fail = 0
    while ($sw.Elapsed.TotalSeconds -lt $end) {
        try {
            $i = Invoke-RestMethod "http://$Ip/api/system/info" -TimeoutSec 3
            $fail = 0
            $f0 = ($i.fans | Where-Object { $_.id -eq 0 } | Select-Object -First 1)
            $f1 = ($i.fans | Where-Object { $_.id -eq 1 } | Select-Object -First 1)
            $w.WriteLine(('{0:F3},{1},{2:F2},{3:F2},{4},{5},{6},{7},{8:F2},{9:F3},{10:F2},{11:F1},{12},{13},{14},{15},{16},{17}' -f `
                $sw.Elapsed.TotalSeconds, (Get-Date -Format 'HH:mm:ss.fff'),
                $i.temps.asic, $i.temps.vcore,
                $f0.speed, $(if($null -ne $f1){$f1.speed}else{''}),
                $f0.rpm,   $(if($null -ne $f1){$f1.rpm}else{''}),
                ($i.power.vbus/1000.0), ($i.power.ibus/1000.0), $i.power.power,
                $i.miner.hashRate, $i.asic.freqReq, $i.asic.vcoreReq, $i.asic.vcoreReal,
                $i.miner.bmMode, $i.miner.protection, 'STEADY'))
        } catch { $fail++ }
        if ($fail -gt 30) { break }   # board gone; stop burning time
        Start-Sleep -Milliseconds $IntervalMs
    }
    $w.Close()
}

$samplers = @()
foreach ($ip in $Boards) {
    $out = Join-Path $OutDir "steady_${Tag}_$($ip -replace '\.','_').csv"
    Remove-Item $out -Force -EA SilentlyContinue
    $samplers += Start-Job -ScriptBlock $sampler -ArgumentList $ip, $out, $Minutes, $IntervalMs
}

$samplers | Wait-Job | Out-Null
$samplers | Receive-Job | Out-Null
$samplers | Remove-Job -Force -EA SilentlyContinue

# ── stop the WS captures ────────────────────────────────────────────────────
foreach ($j in ($jobs | Where-Object kind -eq 'ws')) {
    if ($j.proc -and -not $j.proc.HasExited) { Stop-Process -Id $j.proc.Id -Force -EA SilentlyContinue }
}

Write-Host "`n=== results in $OutDir ==="
Get-ChildItem $OutDir -Filter "*${Tag}*" | Sort-Object Name |
    Select-Object Name, @{n='KB';e={[math]::Round($_.Length/1KB,1)}} |
    Format-Table -AutoSize | Out-String -Width 120
