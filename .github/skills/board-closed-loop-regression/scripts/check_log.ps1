# Grep a captured ws log for the closed-loop evidence markers.
# -FromHHmm / -ToHHmm restrict to a wall-clock window (e.g. since the flash).
param(
    [string]$Log = $(throw 'Log file required'),
    [string]$FromHHmm = '',      # e.g. '13:58'
    [string]$ToHHmm = '',        # e.g. '14:12'
    [switch]$ShowLines
)
$lines = Get-Content $Log
if ($FromHHmm -or $ToHHmm) {
    $from = if ($FromHHmm) { [datetime]::ParseExact("2000-01-01 $FromHHmm", 'yyyy-MM-dd HH:mm', $null) } else { [datetime]::MinValue }
    $to = if ($ToHHmm) { [datetime]::ParseExact("2000-01-01 $ToHHmm", 'yyyy-MM-dd HH:mm', $null) } else { [datetime]::MaxValue }
    $lines = $lines | Where-Object {
        if ($_ -match '^(\d\d-\d\d (\d\d:\d\d))') {
            $t = [datetime]::ParseExact("2000-$($matches[1])", 'yyyy-MM-dd HH:mm:ss', $null)
            $t -ge $from -and $t -le $to
        } else { $false }
    }
}
"window lines: $($lines.Count)"
$markers = [ordered]@{
    'ready gate opened'      = 'miner ready gate opened'
    'Change asic diff'       = 'Change asic diff'
    'Vcore ready pre-reinit' = 'Vcore ready .* before ASIC reinit'
    'warmup (NMAxe only)'    = 'ASIC cold|warming up|raising PLL'
    'PLL verify (BM1366)'    = 'PLL0\(0x08\)|REG 0x'
    'DOMAIN (BM1366)'        = '\[DOMAIN\]'
    'HCN frozen/empty'       = 'HCN: \(\)|HCN: \(\d+(, \d+)?\) = 0\.00'
    'ASIC-REINIT watchdog'   = 'ASIC-REINIT'
    'NONCE-fallback'         = 'NONCE-fallback'
    'rx timeout'             = 'Asic response timeout'
    'rx invalid'             = 'Invalid asic response'
    'panic/abort'            = 'Guru Meditation|Panicking|abort\(\)'
}
foreach ($k in $markers.Keys) {
    $m = $lines | Select-String -Pattern $markers[$k]
    "{0,-24} {1}" -f $k, $m.Count
    if ($ShowLines -and $m.Count) { $m | ForEach-Object { "  $($_.Line)" } }
}
