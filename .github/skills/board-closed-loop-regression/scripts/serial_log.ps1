# Serial capture for pre-WiFi boot logs (ROM, bootloader, early init, panics).
# Requires the user to plug USB into the board and provide the COM port.
# NOTE: opening the port asserts DTR/RTS off; on many boards that itself
# triggers a chip reset -> you also capture the boot from scratch.
param(
    [string]$Port = $(throw 'COM port required (ask the user), e.g. COM8'),
    [string]$Out = "$PSScriptRoot\serial_$Port.log",
    [int]$Baud = 115200
)
$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.DtrEnable = $false
$sp.RtsEnable = $false
$sp.ReadTimeout = 1000
$sp.NewLine = "`n"
$sp.Encoding = [System.Text.Encoding]::UTF8
$sp.Open()
$fs = [System.IO.File]::Open($Out, 'Append', 'Write', 'ReadWrite')
$w = New-Object System.IO.StreamWriter($fs, (New-Object System.Text.UTF8Encoding($false)))
$w.AutoFlush = $true
Write-Host "logging $Port -> $Out (Ctrl-C to stop)"
while ($true) {
    try {
        $line = $sp.ReadLine()
        $line = [regex]::Replace($line, "\x1b\[[0-9;]*m", '').TrimEnd("`r")
        $w.WriteLine(("{0:MM-dd HH:mm:ss.fff} {1}" -f (Get-Date), $line))
    } catch [System.TimeoutException] {
    } catch {
        Write-Host "serial error: $_"
        Start-Sleep -Milliseconds 500
        if (-not $sp.IsOpen) { try { $sp.Open() } catch {} }
    }
}
