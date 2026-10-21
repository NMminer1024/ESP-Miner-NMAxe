# Build a PlatformIO env, OTA-flash it to the board, wait until it boots and hashes.
# Start ws_log.ps1 for this IP BEFORE running this (capture must survive the reboot).
param(
    [string]$Env = $(throw 'Env required (NMAxe|NMAxeGamma|NMQAxePP|NMQAxePPRev61|NMQAxePPNexus)'),
    [string]$Ip = $(throw 'Ip required'),
    [string]$Root = (Resolve-Path "$PSScriptRoot\..\..\..\.."),
    [double]$MinHr = 100,          # hashrate considered "hashing" when waiting for boot
    [int]$BootTimeoutSec = 300
)
$pio = 'C:\Users\bitpony\AppData\Local\Programs\Python\Python311\Scripts\platformio.exe'
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
$out = & $pio run --environment $Env 2>&1
$out | Select-String -Pattern 'error:|\[SUCCESS\]|\[FAILED\]' | ForEach-Object { $_.Line }
if (-not ($out -match '\[SUCCESS\]')) { Pop-Location; exit 1 }
curl.exe -s -m 180 -F "file=@.pio\build\$Env\firmware.bin;filename=firmware.bin" "http://$Ip/api/update/firmware"; ''
Pop-Location
Start-Sleep -Seconds 30
$deadline = (Get-Date).AddSeconds($BootTimeoutSec)
do {
    Start-Sleep -Seconds 5
    $i = try { Invoke-RestMethod "http://$Ip/api/system/info" -TimeoutSec 4 } catch { $null }
} until (($i -and $i.miner.uptimeSeconds -lt 300 -and $i.miner.hashRate -gt $MinHr) -or (Get-Date) -gt $deadline)
if ($i) {
    "booted: uptime=$($i.miner.uptimeSeconds) hr=$([math]::Round($i.miner.hashRate,1)) sha=$($i.identity.appSha256)"
} else {
    'device not answering or not hashing within timeout'
    exit 2
}
