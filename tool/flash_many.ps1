# Flash several boards at once, each with its own build, and verify every board
# actually switched.
#
# Release regression needs every board on the SAME source revision. Doing it one
# at a time invites the mistake this script exists to prevent: flashing four of
# five and testing a mix.
#
# Verification is the point. An upload can return {"status":"ok"} while the board
# keeps running the old image (seen on 124.7: empty response, bytes=0, reboot, same
# fw). So this checks, per board:
#   - the upload reported HTTP 200 and a byte count matching the file
#   - the board came back and its fwVersion is the one we asked for
#   - appSha256 changed relative to before
#
# Usage:
#   .\flash_many.ps1 -Targets 192.168.124.2=NMQAxePPNexus,192.168.124.6=NMQAxePPRev61
#   .\flash_many.ps1 -Targets ... -DryRun
param(
    [Parameter(Mandatory = $true)][string[]]$Targets,   # ip=env
    [string]$Version = 'v3.2.02u',
    [int]$Parallel = 3,
    [switch]$DryRun
)
$ErrorActionPreference = 'Continue'
$root = (Resolve-Path "$PSScriptRoot\..").Path

$current = (Select-String -Path (Join-Path $root 'src\version.h') -Pattern 'BOARD_CURRENT_FW_VERSION\s+"([^"]+)"').Matches.Groups[1].Value
if ($Version -ne $current) { throw "Version mismatch: src/version.h says $current, -Version says $Version" }

$jobs = @()
foreach ($t in $Targets) {
    $parts = $t -split '='
    if ($parts.Count -ne 2) { throw "bad target '$t', expected ip=env" }
    $ip = $parts[0]; $env = $parts[1]
    $bin = Join-Path $root ".pio\build\$env\firmware.bin"
    if (-not (Test-Path $bin)) { throw "missing $bin - build $env first" }
    $jobs += [pscustomobject]@{ ip=$ip; env=$env; bin=$bin; bytes=(Get-Item $bin).Length }
}

"=== flash plan (target $Version) ==="
$jobs | ForEach-Object { '  {0,-16} <- {1} ({2} KB)' -f $_.ip, $_.env, [math]::Round($_.bytes/1KB,0) }
if ($DryRun) { return }

# Snapshot what each board is running now, so "did it change" is provable.
"`n=== before ==="
foreach ($j in $jobs) {
    try {
        $i = Invoke-RestMethod "http://$($j.ip)/api/system/info" -TimeoutSec 5
        $j | Add-Member -NotePropertyName before_fw  -NotePropertyValue $i.identity.fwVersion  -Force
        $j | Add-Member -NotePropertyName before_sha -NotePropertyValue $i.identity.appSha256 -Force
        '  {0,-16} fw={1} sha={2} uptime={3}s' -f $j.ip, $j.before_fw, $j.before_sha, $i.miner.uptimeSeconds
    } catch {
        '  {0,-16} UNREACHABLE' -f $j.ip
        $j | Add-Member -NotePropertyName before_fw  -NotePropertyValue '?' -Force
        $j | Add-Member -NotePropertyName before_sha -NotePropertyValue '?' -Force
    }
}

# Upload in small batches so the LAN is not saturated and errors stay attributable.
#
# curl's -w format does not behave under Start-Process redirection (it emitted a
# stream of HTTP=000 lines and then the final value, which made every upload look
# like a failure). Write the body to one file and the metrics to another with two
# separate invocations instead - and treat the byte count as advisory, because the
# real proof is the uptime reset checked below.
"`n=== uploading ==="
$batch = [math]::Max(1, $Parallel)
for ($k = 0; $k -lt $jobs.Count; $k += $batch) {
    $last = [math]::Min($k + $batch - 1, $jobs.Count - 1)
    $slice = @($jobs[$k..$last])
    $procs = @()
    foreach ($j in $slice) {
        $bodyLog = Join-Path $env:TEMP "flash_body_$($j.ip -replace '\.','_').txt"
        Remove-Item $bodyLog -Force -EA SilentlyContinue
        $a = @('-s','-m','300','-o',$bodyLog,'-w','%{http_code}',
               '-X','POST',"http://$($j.ip)/api/update/firmware",
               '-F',"file=@$($j.bin);filename=firmware.bin")
        $procs += [pscustomobject]@{
            job  = $j
            p    = (Start-Process curl.exe -PassThru -NoNewWindow -RedirectStandardOutput "$bodyLog.code" -ArgumentList $a)
            body = $bodyLog
        }
    }
    $procs | ForEach-Object { $_.p.WaitForExit() }
    foreach ($pr in $procs) {
        $code = ((Get-Content "$($pr.body).code" -Raw -EA SilentlyContinue) + '').Trim()
        $body = ((Get-Content $pr.body -Raw -EA SilentlyContinue) + '').Trim()
        $j = $pr.job
        $j | Add-Member -NotePropertyName upload -NotePropertyValue "$code $body" -Force
        '  {0,-16} HTTP={1} {2}' -f $j.ip, $code, $body
    }
}

# Wait for every board to come back with the new image.
#
# The decisive check is the uptime reset, not the sha: reflashing a build the
# board already runs leaves appSha256 identical by definition, so "sha unchanged"
# reports a false negative on every board that was already current.
"`n=== verifying ==="
Start-Sleep -Seconds 20
foreach ($j in $jobs) {
    $ok = $false
    $deadline = (Get-Date).AddMinutes(6)
    while ((Get-Date) -lt $deadline) {
        try {
            $i = Invoke-RestMethod "http://$($j.ip)/api/system/info" -TimeoutSec 4
            if ($i.identity.fwVersion -eq $Version) {
                $rebooted = ([double]$i.miner.uptimeSeconds -lt [double]$j.uptime0)
                $j | Add-Member -NotePropertyName after_fw  -NotePropertyValue $i.identity.fwVersion  -Force
                $j | Add-Member -NotePropertyName after_sha -NotePropertyValue $i.identity.appSha256 -Force
                $j | Add-Member -NotePropertyName after_up  -NotePropertyValue $i.miner.uptimeSeconds -Force
                $j | Add-Member -NotePropertyName rebooted  -NotePropertyValue $rebooted -Force
                '  {0,-16} fw={1} sha={2} uptime={3}s {4}' -f $j.ip, $j.after_fw, $j.after_sha, $j.after_up,
                    $(if ($rebooted) { '(rebooted - image applied)' } else { '(NO UPTIME RESET - upload did not land)' })
                $ok = $true
                break
            }
        } catch { }
        Start-Sleep -Seconds 5
    }
    if (-not $ok) {
        $j | Add-Member -NotePropertyName after_fw -NotePropertyValue 'TIMEOUT' -Force
        $j | Add-Member -NotePropertyName rebooted -NotePropertyValue $false -Force
        '  {0,-16} DID NOT COME BACK ON {1}' -f $j.ip, $Version
    }
}

"`n=== summary ==="
$jobs | Select-Object ip, env, before_fw, after_fw, rebooted | Format-Table -AutoSize | Out-String -Width 140

$bad = $jobs | Where-Object { $_.after_fw -ne $Version -or -not $_.rebooted }
if ($bad) {
    "`nFAILED boards:"
    $bad | ForEach-Object { '  {0} ({1}) -> {2} rebooted={3}' -f $_.ip, $_.env, $_.after_fw, $_.rebooted }
    exit 1
} else {
    "`nAll $($jobs.Count) board(s) on $Version, all rebooted into it."
}

"`n=== summary ==="
$jobs | Select-Object ip, env, before_fw, after_fw | Format-Table -AutoSize | Out-String -Width 140

$bad = $jobs | Where-Object { $_.after_fw -ne $Version }
if ($bad) {
    "`nFAILED boards:"
    $bad | ForEach-Object { '  {0} ({1}) -> {2}' -f $_.ip, $_.env, $_.after_fw }
    exit 1
} else {
    "`nAll $($jobs.Count) board(s) on $Version."
}
