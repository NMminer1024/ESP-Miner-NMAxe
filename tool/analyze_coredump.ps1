# Symbolize an ESP32-S3 coredump against the firmware that produced it.
#
# A coredump answers "where did it die" only if it is paired with the exact ELF
# that crashed. The dump carries the app's ELF SHA256, and the ELF's own SHA is
# computed over its contents, so a mismatch silently produces wrong function
# names and line numbers. This script therefore refuses to guess: it resolves
# the SHA to a local artifact and stops when nothing matches.
#
# Usage
#   .\tool\analyze_coredump.ps1 -IncidentDir docs\baseline\web_crash_capture\incident_192_168_124_18_...
#   .\tool\analyze_coredump.ps1 -Dump docs\baseline\trigger_20261113\coredump_78.bin -Sha 21dc69fe5c07a802
#   .\tool\analyze_coredump.ps1 -Dump <file> -Elf .pio\build\NMQAxePP\firmware.elf
#   .\tool\analyze_coredump.ps1 -Dump <file> -Sha <sha> -AllThreads
#
# Where ELFs are looked for (first match wins):
#   1. -Elf
#   2. artifacts/elf/<sha>_*.elf          (produced by archive_elf.ps1)
#   3. .pio/build/<env>/firmware.elf      (only if its SHA equals the crash SHA)

param(
    [string]$Dump,
    [string]$IncidentDir,
    [string]$Elf,
    [string]$Sha,
    [switch]$AllThreads,
    [int]$BacktraceDepth = 40,
    [string]$OutFile
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Get-ElfSha {
    param([string]$BinPath)
    if (-not (Test-Path $BinPath)) { return $null }
    $bytes = [System.IO.File]::ReadAllBytes($BinPath)
    # esp_app_desc starts with magic 0xABCD5432 (little-endian: 32 54 CD AB).
    for ($i = 0; $i -le $bytes.Length - 4; $i++) {
        if ($bytes[$i] -eq 0x32 -and $bytes[$i + 1] -eq 0x54 -and
            $bytes[$i + 2] -eq 0xCD -and $bytes[$i + 3] -eq 0xAB) {
            $shaLen = 8   # first 8 bytes of app_elf_sha256 -> 16 hex chars
            if ($i + 144 + $shaLen -gt $bytes.Length) { return $null }
            $hex = -join ($bytes[($i + 144)..($i + 144 + $shaLen - 1)] | ForEach-Object { $_.ToString('x2') })
            return $hex
        }
    }
    return $null
}

# ── Resolve the inputs ──────────────────────────────────────────────────────
$shaSource = 'none'
if ($IncidentDir) {
    if (-not $Dump) {
        $candidate = Join-Path $IncidentDir 'coredump.bin'
        if (Test-Path $candidate) { $Dump = $candidate }
    }
    if (-not $Sha) {
        $infoPath = Join-Path $IncidentDir 'api_coredump_info.json'
        if (Test-Path $infoPath) {
            $info = Get-Content -Raw -Encoding UTF8 $infoPath | ConvertFrom-Json
            if ($info.appSha256) {
                $Sha = ([string]$info.appSha256).Substring(0, [Math]::Min(16, ([string]$info.appSha256).Length))
                $shaSource = 'incident api_coredump_info.json (authoritative)'
            }
        }
    }
} elseif ($Sha) {
    $shaSource = 'caller-supplied (must equal /api/coredump/info appSha256)'
}

if (-not $Dump -or -not (Test-Path $Dump)) {
    throw 'No coredump file. Pass -Dump <path> or -IncidentDir <dir> containing coredump.bin.'
}
$Dump = (Resolve-Path $Dump).Path

Write-Host ''
Write-Host '=== coredump analysis ===' -ForegroundColor Cyan
Write-Host ("  dump : {0}  ({1:N0} bytes)" -f $Dump, (Get-Item $Dump).Length)

# A download from /api/coredump/download already starts at the ELF magic; a raw
# partition read may still carry the 20-byte IDF image header, so verify.
$head = [System.IO.File]::ReadAllBytes($Dump)[0..3]
if (-not ($head[0] -eq 0x7F -and $head[1] -eq 0x45 -and $head[2] -eq 0x4C -and $head[3] -eq 0x46)) {
    throw ('Not an ELF core (starts with {0}). Re-download via /api/coredump/download.' -f `
        (($head | ForEach-Object { $_.ToString('x2') }) -join ' '))
}

# ── Resolve the ELF ─────────────────────────────────────────────────────────
$resolvedElf = $null
$resolvedSha = $null

if ($Elf) {
    if (-not (Test-Path $Elf)) { throw "ELF not found: $Elf" }
    $resolvedElf = (Resolve-Path $Elf).Path
    $binSibling = [System.IO.Path]::ChangeExtension($resolvedElf, '.bin')
    $resolvedSha = Get-ElfSha $binSibling
    if ($Sha -and $resolvedSha -and $Sha -ne $resolvedSha) {
        Write-Host ("  WARNING: the supplied ELF has SHA {0} but the crash is {1}." -f $resolvedSha, $Sha) -ForegroundColor Red
        Write-Host '           Symbol names below will be WRONG.' -ForegroundColor Red
    }
} else {
    if (-not $Sha) { throw 'Cannot identify the firmware. Pass -Sha <16 hex> or -Elf <path>.' }
    $Sha = $Sha.ToLower()

    # 1. archived artifacts
    $archiveDir = Join-Path $root 'artifacts\elf'
    if (Test-Path $archiveDir) {
        $hit = Get-ChildItem $archiveDir -Filter ("{0}_*.elf" -f $Sha) -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hit) { $resolvedElf = $hit.FullName; $resolvedSha = $Sha }
    }

    # 2. current build tree, verified by SHA (never assume the folder matches)
    if (-not $resolvedElf) {
        foreach ($bin in Get-ChildItem (Join-Path $root '.pio\build') -Recurse -Filter 'firmware.bin' -ErrorAction SilentlyContinue) {
            if ((Get-ElfSha $bin.FullName) -eq $Sha) {
                $elfCandidate = Join-Path $bin.DirectoryName 'firmware.elf'
                if (Test-Path $elfCandidate) { $resolvedElf = $elfCandidate; $resolvedSha = $Sha; break }
            }
        }
    }
}

if (-not $resolvedElf) {
    Write-Host ''
    Write-Host ("  NO MATCHING ELF for crash SHA {0}." -f $Sha) -ForegroundColor Red
    Write-Host '  This dump cannot be symbolized. Rebuild that exact source revision, or restore' -ForegroundColor Yellow
    Write-Host '  the archived artifact. Without the ELF only the raw PC is known.' -ForegroundColor Yellow
    Write-Host '  (Rebuilding later revisions will NOT help - the SHA would differ.)' -ForegroundColor Yellow
    exit 2
}

Write-Host ("  elf  : {0}" -f $resolvedElf)
if ($Sha -and $Sha -ne $resolvedSha) {
    Write-Host ("  sha  : {0}  (mismatch!)" -f $resolvedSha) -ForegroundColor Red
} else {
    Write-Host ("  sha  : {0}" -f $resolvedSha)
    Write-Host ("         from: {0}" -f $shaSource) -ForegroundColor DarkGray
}

if ($shaSource -like 'caller-supplied*') {
    Write-Host '  NOTE: a wrong SHA produces plausible but entirely incorrect function' -ForegroundColor Yellow
    Write-Host '        names - gdb does not detect it. Take the SHA from the device' -ForegroundColor Yellow
    Write-Host '        endpoint api/coredump/info, or prefer -IncidentDir.' -ForegroundColor Yellow
}

# ── Run GDB ─────────────────────────────────────────────────────────────────
$gdb = Join-Path $root '.pio\packages\toolchain-xtensa-esp32s3\bin\xtensa-esp32s3-elf-gdb.exe'
if (-not (Test-Path $gdb)) {
    throw "Xtensa GDB not found: $gdb  (run a build once so PlatformIO downloads the toolchain)"
}

if (-not $OutFile) {
    $OutFile = [System.IO.Path]::ChangeExtension($Dump, '.gdb.txt')
}

$args = @(
    '--batch', '-nx',
    '-ex', 'set pagination off',
    '-ex', 'set print frame-arguments none',
    '-ex', ("core-file `"$Dump`""),
    # The exception frame is what names the fault: PC plus the first arguments.
    # Dumping all ~60 registers buries it, so only these are requested.
    '-ex', 'p/x $pc',
    '-ex', 'info registers a0 a1 a2 a3',
    '-ex', 'bt'
)
if ($AllThreads) {
    # Every task stack is in the dump. Useful when the crash task is not the
    # culprit, but long output - it goes to the file, not the console.
    $args += @('-ex', 'thread apply all bt')
}

Write-Host ''
Write-Host ("  running gdb -> {0}" -f $OutFile) -ForegroundColor DarkGray

# GDB always writes a benign "Couldn't determine a path for the index cache"
# warning to stderr, and under $ErrorActionPreference='Stop' PowerShell turns
# any native stderr into a terminating NativeCommandError. Capture stdout and
# stderr without aborting, then restore the strict preference.
$previousEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    $output = & $gdb @args $resolvedElf 2>&1 | Out-String
} finally {
    $ErrorActionPreference = $previousEap
}
$output | Set-Content -Encoding UTF8 -Path $OutFile

# ── Show the part that matters ──────────────────────────────────────────────
$lines = $output -split "`r?`n"

Write-Host ''
Write-Host '--- exception frame ---' -ForegroundColor Cyan
$show = $false
foreach ($line in $lines) {
    $t = $line.TrimEnd()
    if ($t -match '^\$\d+\s*=\s*0x') { Write-Host ('  ' + $t); continue }
    if ($t -match '^(a[0-3])\s+0x') { Write-Host ('  ' + $t); continue }
    if ($t -match '^#\d') { $show = $true }
    if ($show -and $t -match '^#\d') { Write-Host ('  ' + $t) }
    elseif ($show -and $t -match '^\s+at\s|^\s+from\s') { Write-Host ('    ' + $t.Trim()) -ForegroundColor DarkGray }
}

# A frame printed as "?? () at <path>:line" means the ELF does not belong to this
# dump: the addresses resolve to nothing usable.
if ($output -match 'in \?\? \(\)') {
    Write-Host ''
    Write-Host '  NOTE: some frames show ?? () - the ELF does not match this dump,' -ForegroundColor Yellow
    Write-Host '        or those addresses are in ROM (run addr2line against the ROM ELF).' -ForegroundColor Yellow
}

# Stronger signal than ?? frames: mismatched symbols usually still resolve to a
# *name*, just the wrong one, so cross-check that the resolved names are in
# modules this firmware plausibly has. A backtrace whose frames all land in
# unrelated units is the classic fingerprint of an ELF/dump mismatch.
if ($shaSource -like 'caller-supplied*') {
    Write-Host ''
    Write-Host '  VERIFY the frame names below match what the device was actually running.' -ForegroundColor Yellow
    Write-Host '  A mismatched ELF yields confident-looking but wrong symbols.' -ForegroundColor Yellow
}

Write-Host ''
Write-Host ("Full output: {0}" -f $OutFile) -ForegroundColor DarkGray
if (-not $AllThreads) {
    Write-Host 'Add -AllThreads to dump every task stack (useful when the crash task is not the culprit).' -ForegroundColor DarkGray
}
