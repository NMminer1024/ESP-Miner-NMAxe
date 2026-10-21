# WS log capture for one board. Keeps retrying so it survives device reboots
# (OTA / watchdog) and grabs the stream as soon as WiFi is back.
# Run in a dedicated terminal BEFORE flashing; do not stop it across reboots.
param(
    [string]$Ip = $(throw 'Ip required'),
    [string]$Out = "$PSScriptRoot\ws_$($Ip -replace '\.','_').log",
    [int]$RetrySec = 3
)
$ErrorActionPreference = 'Continue'
$fs = [System.IO.File]::Open($Out, 'Append', 'Write', 'ReadWrite')
$w = New-Object System.IO.StreamWriter($fs, (New-Object System.Text.UTF8Encoding($false)))
$w.AutoFlush = $true
$ct = [System.Threading.CancellationToken]::None
Write-Host "capturing ws://$Ip/ws -> $Out (auto-reconnect every ${RetrySec}s; Ctrl-C to stop)"
while ($true) {
    $ws = New-Object System.Net.WebSockets.ClientWebSocket
    try {
        $ws.ConnectAsync([Uri]"ws://$Ip/ws", $ct).Wait()
        $w.WriteLine("# ---- ws connected $(Get-Date -Format 'MM-dd HH:mm:ss') ----")
        $buf = New-Object byte[] 8192
        while ($ws.State -eq 'Open') {
            $seg = New-Object System.ArraySegment[byte] -ArgumentList @(,$buf)
            $res = $ws.ReceiveAsync($seg, $ct).Result
            if ($res.MessageType -eq 'Close') { break }
            $txt = [System.Text.Encoding]::UTF8.GetString($buf, 0, $res.Count)
            foreach ($line in ($txt -split "`r?`n")) {
                if ($line.Trim()) {
                    $w.WriteLine(("{0:MM-dd HH:mm:ss.fff} {1}" -f (Get-Date), ($line -replace "\x1b\[[0-9;]*m", '')))
                }
            }
        }
    } catch { } finally {
        $ws.Dispose()
    }
    $w.WriteLine("# ---- ws dropped $(Get-Date -Format 'MM-dd HH:mm:ss'), reconnecting ----")
    Start-Sleep -Seconds $RetrySec
}
