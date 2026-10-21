# Build a PlatformIO env and OTA-flash it to a board, then wait for it to come
# back up. Start ws_log.ps1 for this IP BEFORE running this (capture must
# survive the reboot). Health check is generic: fresh uptime + caller-supplied
# metric threshold (default: just "device answering with fresh uptime").
param(
    [string]$Env = $(throw 'PlatformIO env required (see platformio.ini [env:*])'),
    [string]$Ip = $(throw 'Board IP required'),
    [string]$Root = (Resolve-Path "$PSScriptRoot\..\..\..\.."),
    [string]$HealthPath = '/api/system/info',
    [string]$UptimeField = 'miner.uptimeSeconds',   # dotted path to uptime seconds
    [double]$MinMetric = 0,                          # optional extra health metric
    [string]$MetricField = '',                       # dotted path, e.g. 'miner.hashRate'
    [int]$BootTimeoutSec = 300,
    [string]$Pio = ''                                # optional: path to platformio.exe; default: resolve from PATH
)
# Resolve the PlatformIO CLI without hardcoding any machine-specific path:
# -Pio override > "platformio"/"pio" on PATH > "python/python3 -m platformio".
$pioCmd = $null
if ($Pio) {
    if (-not (Test-Path $Pio)) { throw "-Pio path not found: $Pio" }
    $pioCmd = @($Pio)
} else {
    foreach ($name in 'platformio', 'pio') {
        $c = Get-Command $name -ErrorAction SilentlyContinue
        if ($c) { $pioCmd = @($c.Source); break }
    }
    if (-not $pioCmd) {
        foreach ($py in 'python', 'python3') {
            $c = Get-Command $py -ErrorAction SilentlyContinue
            if ($c) {
                & $c.Source -m platformio --version *> $null
                if ($LASTEXITCODE -eq 0) { $pioCmd = @($c.Source, '-m', 'platformio'); break }
            }
        }
    }
    if (-not $pioCmd) { throw 'platformio CLI not found on PATH (pass -Pio <path>, or install it: pip install platformio)' }
}
function Get-Dotted($obj, [string]$path) {
    $cur = $obj
    foreach ($seg in $path.Split('.')) { $cur = $cur.$seg; if ($null -eq $cur) { return $null } }
    return $cur
}
Push-Location $Root
# strip UTF-8 BOM from changed sources (edit-tool quirk breaks some builds)
foreach ($p in (git diff --name-only)) {
    $full = Join-Path $Root $p
    if (-not (Test-Path $full)) { continue }
    $b = [System.IO.File]::ReadAllBytes($full)
    if ($b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF) {
        [System.IO.File]::WriteAllBytes($full, $b[3..($b.Length - 1)]); "BOM stripped: $p"
    }
}
$pioArgs = @('run', '--environment', $Env)
if ($pioCmd.Count -gt 1) { $pioArgs = $pioCmd[1..($pioCmd.Count - 1)] + $pioArgs }
$out = & $pioCmd[0] @pioArgs 2>&1
$out | Select-String -Pattern 'error:|\[SUCCESS\]|\[FAILED\]' | ForEach-Object { $_.Line }
if (-not ($out -match '\[SUCCESS\]')) { Pop-Location; exit 1 }
curl.exe -s -m 180 -F "file=@.pio\build\$Env\firmware.bin;filename=firmware.bin" "http://$Ip/api/update/firmware"; ''
Pop-Location
Start-Sleep -Seconds 30
$deadline = (Get-Date).AddSeconds($BootTimeoutSec)
do {
    Start-Sleep -Seconds 5
    $i = try { Invoke-RestMethod "http://$Ip$HealthPath" -TimeoutSec 4 } catch { $null }
    $up = if ($i) { Get-Dotted $i $UptimeField } else { $null }
    $met = if ($i -and $MetricField) { Get-Dotted $i $MetricField } else { if ($MetricField) { $null } else { 1 } }
} until (($up -ne $null -and $up -lt 300 -and $met -ne $null -and [double]$met -ge $MinMetric) -or (Get-Date) -gt $deadline)
if ($i -and $up -ne $null) {
    "booted: uptime=$up metric=$met sha=$($i.identity.appSha256)"
} else {
    'device not answering or health check failed within timeout'
    exit 2
}
