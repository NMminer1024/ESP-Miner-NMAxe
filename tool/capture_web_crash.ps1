param(
    # Leave empty to auto-discover every Axe-family board on the subnet.
    [string[]]$Ip,

    [int]$IntervalSeconds = 30,

    # Rescan for boards this often (minutes). 0 disables rediscovery.
    # A hardcoded IP list silently stops covering a board that DHCP moved or
    # that was plugged in later, so the list is refreshed periodically.
    [int]$RediscoverMinutes = 10,

    [string]$OutputDir = (Join-Path $PSScriptRoot '..\docs\baseline\web_crash_capture'),

    [switch]$Once
)

$ErrorActionPreference = 'Stop'
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$state = @{}

# Axe-family models served by this firmware. NMMiner (cyd2.8 / cyd3.5) is a
# different product with no coredump endpoint and no app SHA, so it is excluded.
$AxeModelPattern = 'NMAxe|NMQAxe'

function Invoke-JsonGet {
    param(
        [string]$Address,
        [string]$Path,
        [int]$TimeoutSeconds = 5
    )

    try {
        return Invoke-RestMethod -Uri "http://$Address$Path" -TimeoutSec $TimeoutSeconds
    } catch {
        return $null
    }
}

# Probe the /24 of this machine (or of the first seed IP) for Axe boards.
# /probe is the cheapest endpoint the firmware exposes, so discovery does not
# perturb anything it is measuring.
#
# Hosts are discovered with async TCP connects, not one job per address: spawning
# 254 PowerShell processes takes minutes and hundreds of MB, while 254 parallel
# ConnectAsync calls settle in about a second on a LAN.
function Find-AxeBoards {
    param([string]$SeedIp, [int]$ConnectTimeoutMs = 1200)

    $prefix = if ($SeedIp -and $SeedIp -match '^(\d+\.\d+\.\d+)\.') { $Matches[1] } else { '192.168.124' }
    Write-Host "[$(Get-Date -Format T)] discovering $prefix.0/24 ..."

    $attempts = New-Object System.Collections.ArrayList
    foreach ($n in 1..254) {
        $addr = "$prefix.$n"
        $client = New-Object System.Net.Sockets.TcpClient
        try {
            $task = $client.ConnectAsync($addr, 80)
            [void]$attempts.Add([pscustomobject]@{ Ip = $addr; Client = $client; Task = $task })
        } catch {
            $client.Dispose()
        }
    }

    # WaitAll throws on a faulted connect, which is the normal case for an unused
    # address, so wait per-task with a deadline instead of using the aggregate.
    $deadline = (Get-Date).AddMilliseconds($ConnectTimeoutMs)
    foreach ($a in $attempts) {
        $remaining = [int][math]::Max(0, ($deadline - (Get-Date)).TotalMilliseconds)
        try { [void]$a.Task.Wait($remaining) } catch { }
    }

    $found = @()
    foreach ($a in $attempts) {
        $reachable = $false
        try { $reachable = $a.Task.IsCompleted -and -not $a.Task.IsFaulted -and $a.Client.Connected } catch { }
        if (-not $reachable) { $a.Client.Dispose(); continue }

        # Port 80 is open - ask what it is. Only Axe firmware answers /probe.
        try {
            $body = & curl.exe --silent --max-time 3 ("http://" + $a.Ip + "/probe") 2>$null
            if ($body -match '"model"') {
                $probe = $body | ConvertFrom-Json
                if ($probe.model -match $AxeModelPattern) {
                    $found += [pscustomobject]@{ ip = $a.Ip; model = $probe.model; hostname = $probe.hostname }
                }
            }
        } catch { }
        $a.Client.Dispose()
    }

    return @($found | Sort-Object { [int]($_.ip -replace '.*\.', '') })
}

function Save-Json {
    param(
        [object]$Value,
        [string]$Path
    )

    if ($null -ne $Value) {
        $Value | ConvertTo-Json -Depth 12 | Set-Content -Encoding UTF8 -Path $Path
    }
}

function Save-IncidentEvidence {
    param(
        [string]$Address,
        [string]$Reason,
        [object]$SystemInfo,
        [object]$Probe
    )

    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $safeIp = $Address -replace '\.', '_'
    $incidentDir = Join-Path $OutputDir ("incident_${safeIp}_${stamp}_${Reason}")
    New-Item -ItemType Directory -Force -Path $incidentDir | Out-Null

    Save-Json $SystemInfo (Join-Path $incidentDir 'api_system_info.json')
    Save-Json $Probe (Join-Path $incidentDir 'probe.json')
    Save-Json (Invoke-JsonGet $Address '/api/reboot/last') (Join-Path $incidentDir 'api_reboot_last.json')
    Save-Json (Invoke-JsonGet $Address '/api/reboot/list') (Join-Path $incidentDir 'api_reboot_list.json')

    $coreInfo = Invoke-JsonGet $Address '/api/coredump/info' 15
    Save-Json $coreInfo (Join-Path $incidentDir 'api_coredump_info.json')
    if ($coreInfo -and $coreInfo.present) {
        try {
            Invoke-WebRequest -Uri "http://$Address/api/coredump/download" -TimeoutSec 60 -OutFile (Join-Path $incidentDir 'coredump.bin')
        } catch {
            $_.Exception.Message | Set-Content -Encoding UTF8 -Path (Join-Path $incidentDir 'coredump_download_error.txt')
        }
    }

    "$(Get-Date -Format o) $Address $Reason $incidentDir" | Add-Content -Encoding UTF8 -Path (Join-Path $OutputDir 'incidents.log')
    Write-Host "[$(Get-Date -Format T)] $Address evidence saved: $Reason"
}

function Append-Sample {
    param(
        [string]$Address,
        [object]$SystemInfo,
        [object]$Probe,
        [string]$Status
    )

    $safeIp = $Address -replace '\.', '_'
    $csvPath = Join-Path $OutputDir ("samples_$safeIp.csv")
    $identity = $SystemInfo.identity
    $miner = $SystemInfo.miner
    $sample = [pscustomobject]@{
        timestamp = (Get-Date -Format o)
        ip = $Address
        status = $Status
        model = $identity.hwModel
        hostname = $identity.hostName
        firmware = $identity.fwVersion
        appSha256 = $identity.appSha256
        uptimeSeconds = $miner.uptimeSeconds
        uptimeEver = $miner.uptimeEver
        freeHeap = $miner.freeHeap
        minFreeHeap = $miner.minFreeHeap
        hashRate = $miner.hashRate
        probeUptime = $Probe.ut
    }

    if (Test-Path $csvPath) {
        $sample | Export-Csv -NoTypeInformation -Append -Encoding UTF8 -Path $csvPath
    } else {
        $sample | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $csvPath
    }
}

Write-Host "Read-only capture started. No dashboard, chart-history, or WebSocket requests will be made."
Write-Host "Output: $OutputDir"

$lastDiscovery = [datetime]::MinValue

# Build the target list: explicit -Ip wins, otherwise discover the Axe fleet.
if (-not $Ip -or @($Ip).Count -eq 0) {
    $discovered = Find-AxeBoards -SeedIp $(if ($Ip) { @($Ip)[0] } else { $null })
    if ($discovered.Count -eq 0) {
        throw 'No Axe boards found on the subnet. Pass -Ip <address> to target one explicitly.'
    }
    $Ip = @($discovered | ForEach-Object { $_.ip })
    Write-Host ("Targets ({0}): {1}" -f $discovered.Count, (($discovered | ForEach-Object { "$($_.hostname)@$($_.ip)" }) -join ', '))
    $lastDiscovery = Get-Date
} else {
    Write-Host ("Targets ({0}): {1}" -f @($Ip).Count, (@($Ip) -join ', '))
}

do {
    # Refresh the target list so a board that DHCP moved, or one that was
    # plugged in after startup, is picked up instead of being silently dropped.
    if (-not $Once -and $RediscoverMinutes -gt 0 -and
        ((Get-Date) - $lastDiscovery).TotalMinutes -ge $RediscoverMinutes) {
        $current = @(Find-AxeBoards -SeedIp @($Ip)[0] | ForEach-Object { $_.ip })
        if ($current.Count -gt 0) {
            $added   = @($current | Where-Object { $Ip -notcontains $_ })
            $dropped = @($Ip | Where-Object { $current -notcontains $_ })
            if ($added.Count -gt 0)   { Write-Host ("[$(Get-Date -Format T)] discovered new: {0}" -f ($added -join ', ')) }
            if ($dropped.Count -gt 0) { Write-Host ("[$(Get-Date -Format T)] no longer answering: {0}" -f ($dropped -join ', ')) }
            $Ip = $current
        }
        $lastDiscovery = Get-Date
    }

    foreach ($address in $Ip) {
        $probe = Invoke-JsonGet $address '/probe' 3
        $systemInfo = Invoke-JsonGet $address '/api/system/info' 8

        if (-not $probe -or -not $systemInfo) {
            Append-Sample $address $systemInfo $probe 'unreachable'
            if (-not $state.ContainsKey($address) -or $state[$address].reachable) {
                Save-IncidentEvidence $address 'unreachable' $systemInfo $probe
            }
            $state[$address] = @{ reachable = $false; uptime = $null; corePresent = $false }
            continue
        }

        $uptime = [int64]$systemInfo.miner.uptimeSeconds
        $coreInfo = Invoke-JsonGet $address '/api/coredump/info' 8
        $corePresent = [bool]($coreInfo -and $coreInfo.present)
        $reason = $null

        if ($state.ContainsKey($address)) {
            $previous = $state[$address]
            if (-not $previous.reachable) {
                $reason = 'recovered'
            } elseif ($null -ne $previous.uptime -and $uptime -lt $previous.uptime) {
                $reason = 'uptime_reset'
            } elseif ($corePresent -and -not $previous.corePresent) {
                $reason = 'new_coredump'
            }
        } elseif ($corePresent) {
            $reason = 'existing_coredump'
        }

        Append-Sample $address $systemInfo $probe 'ok'
        if ($reason) {
            Save-IncidentEvidence $address $reason $systemInfo $probe
        }

        $state[$address] = @{ reachable = $true; uptime = $uptime; corePresent = $corePresent }
        Write-Host ("[{0}] {1} up={2}s heap={3}/{4} sha={5}" -f (Get-Date -Format T), $address, $uptime, $systemInfo.miner.freeHeap, $systemInfo.miner.minFreeHeap, $systemInfo.identity.appSha256)
    }

    if (-not $Once) {
        [System.Threading.ManualResetEvent]::new($false).WaitOne($IntervalSeconds * 1000) | Out-Null
    }
} while (-not $Once)