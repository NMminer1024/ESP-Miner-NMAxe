# Serial capture for pre-WiFi boot logs (ROM, bootloader, early init, panics).
# Requires the user to plug USB into the board and provide the COM port.
#
# ⚠️ READ THIS BEFORE USING IT ON A BOARD THAT MATTERS
#
# Opening an ESP32-S3 USB-Serial-JTAG port RESETS THE CHIP (rst:0x15). That is
# hardware: the peripheral acts on the host's modem-control state. Worse, this
# script re-opens the port whenever the handle dies, and every open re-applies
# those lines -- so a reboot caused by the capture itself triggers the next
# re-open, which causes the next reboot. Measured: 6 of 6 `opened` markers were
# immediately followed by rst:0x15.
#
# That makes this tool able to FABRICATE the very reboots it is supposed to be
# observing, and it will destroy the uptime/reboot-history evidence of the board
# it is attached to. Therefore:
#   - default MaxReopen = 0: open once, stop on disconnect (no reopen storm);
#   - a `usb_uart` / `unknown+external` reboot record next to a capture `opened`
#     marker is the capture's fault, not the firmware's;
#   - for a live-mining board prefer the WS log stream (ws_log.ps1), which does
#     not touch USB at all;
#   - only ever attach USB when the question is about pre-WiFi boot output.
param(
    [string]$Port = $(throw 'COM port required (ask the user), e.g. COM8'),
    [string]$Out = "$PSScriptRoot\serial_$Port.log",
    [int]$Baud = 115200,
    [int]$ReopenDelayMs = 1000,
    [int]$ErrorLogIntervalMs = 15000,
    # Number of re-opens allowed after the handle dies. 0 = never re-open
    # (recommended: avoids resetting the board over and over).
    [int]$MaxReopen = 0
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

# On ESP32-S3 the USB-Serial-JTAG peripheral RESETS THE CHIP when the host
# asserts RTS (measured: DTR/RTS = 0/1 and 1/1 both produce
# rst:0x15 USB_UART_CHIP_RESET, while 0/0 and 1/0 do not). The serial port
# properties set before Open() are not reliably applied by the driver, so the
# host can open the port with the modem lines high and reboot the board.
# Re-assert both lines low immediately after Open() as well -- this does not
# prevent the reset (that signal is already sent at open time) but it stops the
# lines staying asserted for the whole session.
function Set-ModemLinesLow {
    param($Port)
    if ($null -eq $Port -or -not $Port.IsOpen) { return }
    try { $Port.DtrEnable = $false } catch {}
    try { $Port.RtsEnable = $false } catch {}
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
Write-Host "logging $Port -> $Out (MaxReopen=$MaxReopen; Ctrl-C to stop)"
if ($MaxReopen -eq 0) {
    Write-Host "note: opening the port resets an ESP32-S3 once (rst:0x15); re-open is disabled so it cannot reset it again"
}

$sp = $null
$lastErrMsg = ''
$lastErrTick = 0
$reopens = 0
$everOpened = $false

while ($true) {
    # ---- (re)attach ----
    if ($null -eq $sp -or -not $sp.IsOpen) {
        Close-CapturePort $sp
        $sp = $null
        if ($everOpened -and $reopens -ge $MaxReopen) {
            # Do NOT reopen: every open re-applies the modem lines and resets the
            # chip, which would both destroy the evidence and manufacture a new
            # reboot (see the header comment).
            $w.WriteLine(("# ---- serial {0} closed, MaxReopen={1} reached, stopping {2} ----" -f `
                          $Port, $MaxReopen, (Get-Date -Format 'MM-dd HH:mm:ss')))
            Write-Host "serial $Port closed; not reopening (MaxReopen=$MaxReopen). Exiting."
            break
        }
        $reopens++
        $candidate = $null   # must start null so the catch never closes a stale object
        try {
            $candidate = New-CapturePort $Port $Baud
            $candidate.Open()
            Set-ModemLinesLow $candidate   # see Set-ModemLinesLow: RTS high resets an S3
            $sp = $candidate
            $everOpened = $true
            $lastErrMsg = ''
            $w.WriteLine(("# ---- serial {0} opened {1} (open #{2}) ----" -f `
                          $Port, (Get-Date -Format 'MM-dd HH:mm:ss'), $reopens))
            Write-Host ("serial {0} opened {1}" -f $Port, (Get-Date -Format 'HH:mm:ss'))
        } catch {
            Close-CapturePort $candidate
            $msg = $_.Exception.Message
            $now = [Environment]::TickCount64
            if ($msg -ne $lastErrMsg -or ($now - $lastErrTick) -ge $ErrorLogIntervalMs) {
                $w.WriteLine(("# ---- serial open failed: {0} {1} ----" -f $msg, (Get-Date -Format 'MM-dd HH:mm:ss')))
                Write-Host ("serial open failed: {0}" -f $msg)
                $lastErrMsg  = $msg
                $lastErrTick = $now
            }
            if ($everOpened -and $reopens -gt $MaxReopen) {
                Write-Host "giving up after $reopens open attempt(s)"
                break
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
