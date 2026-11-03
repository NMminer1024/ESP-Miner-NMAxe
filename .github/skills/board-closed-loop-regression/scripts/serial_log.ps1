# Serial capture for pre-WiFi boot logs (ROM, bootloader, early init, panics).
# Requires the user to plug USB into the board and provide the COM port.
# NOTE: opening the port asserts DTR/RTS off; on many boards that itself
# triggers a chip reset -> you also capture the boot from scratch.
#
# Reconnect behaviour: when the device reboots (OTA, watchdog, escalation) the
# COM handle dies. The previous version only retried `Open()` inside the catch
# block and swallowed the failure, so it spun forever printing
# "serial error: 端口被关闭" without ever reattaching. Now every failure path
# disposes the dead handle and loops on reopen, with rate-limited logging.
param(
    [string]$Port = $(throw 'COM port required (ask the user), e.g. COM8'),
    [string]$Out = "$PSScriptRoot\serial_$Port.log",
    [int]$Baud = 115200,
    [int]$ReopenDelayMs = 1000,
    [int]$ErrorLogIntervalMs = 15000
)

function New-CapturePort {
    param([string]$Name, [int]$BaudRate)
    $p = New-Object System.IO.Ports.SerialPort $Name, $BaudRate, 'None', 8, 'One'
    $p.DtrEnable = $false
    $p.RtsEnable = $false
    $p.ReadTimeout = 1000
    $p.NewLine = "`n"
    $p.Encoding = [System.Text.Encoding]::UTF8
    return $p
}

function Close-CapturePort {
    param($p)
    if ($null -eq $p) { return }
    # The handle is often already dead when we get here; every step is best-effort.
    try { if ($p.IsOpen) { $p.Close() } } catch {}
    try { $p.Dispose() } catch {}
}

$fs = [System.IO.File]::Open($Out, 'Append', 'Write', 'ReadWrite')
$w = New-Object System.IO.StreamWriter($fs, (New-Object System.Text.UTF8Encoding($false)))
$w.AutoFlush = $true
Write-Host "logging $Port -> $Out (auto-reconnect on device reboot; Ctrl-C to stop)"

$sp = $null
$lastErrMsg = ''
$lastErrTick = 0

while ($true) {
    # ---- (re)attach ----
    if ($null -eq $sp -or -not $sp.IsOpen) {
        Close-CapturePort $sp
        $sp = $null
        $candidate = $null   # must start null so the catch never closes a stale object
        try {
            $candidate = New-CapturePort $Port $Baud
            $candidate.Open()
            $sp = $candidate
            $lastErrMsg = ''
            $w.WriteLine(("# ---- serial {0} opened {1} ----" -f $Port, (Get-Date -Format 'MM-dd HH:mm:ss')))
            Write-Host ("serial {0} opened {1}" -f $Port, (Get-Date -Format 'HH:mm:ss'))
        } catch {
            Close-CapturePort $candidate
            $msg = $_.Exception.Message
            $now = [Environment]::TickCount64
            if ($msg -ne $lastErrMsg -or ($now - $lastErrTick) -ge $ErrorLogIntervalMs) {
                $w.WriteLine(("# ---- serial open failed: {0} {1} ----" -f $msg, (Get-Date -Format 'MM-dd HH:mm:ss')))
                Write-Host ("serial open failed: {0} (retrying every {1}ms)" -f $msg, $ReopenDelayMs)
                $lastErrMsg  = $msg
                $lastErrTick = $now
            }
            Start-Sleep -Milliseconds $ReopenDelayMs
            continue
        }
    }

    # ---- read ----
    try {
        $line = $sp.ReadLine()
        $line = [regex]::Replace($line, "\x1b\[[0-9;]*m", '').TrimEnd("`r")
        $w.WriteLine(("{0:MM-dd HH:mm:ss.fff} {1}" -f (Get-Date), $line))
        $lastErrMsg = ''
    } catch [System.TimeoutException] {
        # No data within ReadTimeout: normal idle, keep the handle.
    } catch {
        # Port closed / handle invalid (device rebooted). Drop it and reattach.
        $msg = $_.Exception.Message
        $now = [Environment]::TickCount64
        if ($msg -ne $lastErrMsg -or ($now - $lastErrTick) -ge $ErrorLogIntervalMs) {
            $w.WriteLine(("# ---- serial error: {0} {1} ----" -f $msg, (Get-Date -Format 'MM-dd HH:mm:ss')))
            Write-Host ("serial error: {0} -> reconnecting" -f $msg)
            $lastErrMsg  = $msg
            $lastErrTick = $now
        }
        Close-CapturePort $sp
        $sp = $null
        Start-Sleep -Milliseconds $ReopenDelayMs
    }
}
