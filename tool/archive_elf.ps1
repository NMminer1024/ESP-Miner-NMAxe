# Archive a built firmware.elf so a future coredump can be symbolized.
#
# Why this exists: a coredump only carries the crashing image's ELF SHA256, and
# symbolizing it requires that exact ELF. Rebuilding later source produces a new
# SHA, so the artifact is unrecoverable once .pio/build is cleaned or overwritten.
# This already cost us the v3.2.02d dumps - those crashes can no longer be traced.
#
# Run after every build you might ship:
#   .\tool\archive_elf.ps1                     # all envs present in .pio/build
#   .\tool\archive_elf.ps1 -Env NMQAxePPNexus
#   .\tool\archive_elf.ps1 -List               # show what is archived
#
# Files land in artifacts/elf/<sha>_<version>_<env>.elf (+ .bin), named by the
# SHA so analyze_coredump.ps1 can find them from a dump alone.

param(
    [string[]]$Env,
    [string]$ArchiveDir,
    [switch]$List
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $ArchiveDir) { $ArchiveDir = Join-Path $root 'artifacts\elf' }
$ArchiveDir = [System.IO.Path]::GetFullPath($ArchiveDir)
New-Item -ItemType Directory -Force -Path $ArchiveDir | Out-Null

function Get-FirmwareIdentity {
    param([string]$BinPath)
    if (-not (Test-Path $BinPath)) { return $null }
    $bytes = [System.IO.File]::ReadAllBytes($BinPath)

    $sha = $null
    $descOffset = -1
    for ($i = 0; $i -le $bytes.Length - 4; $i++) {
        if ($bytes[$i] -eq 0x32 -and $bytes[$i + 1] -eq 0x54 -and
            $bytes[$i + 2] -eq 0xCD -and $bytes[$i + 3] -eq 0xAB) {
            $descOffset = $i
            if ($i + 144 + 8 -le $bytes.Length) {
                $sha = -join ($bytes[($i + 144)..($i + 151)] | ForEach-Object { $_.ToString('x2') })
            }
            break
        }
    }

    # BOARD_CURRENT_FW_VERSION is compiled in as a plain string literal, not into
    # esp_app_desc, so recover it by scanning for the version pattern. The image
    # also contains the framework version ("v4.4.6"), so match this project's
    # shape specifically: two-digit patch plus an optional revision letter.
    $text = [System.Text.Encoding]::ASCII.GetString($bytes)
    $version = 'unknown'
    if ($text -match 'v\d+\.\d+\.\d{2}[a-z]?') { $version = $Matches[0] }

    if (-not $sha) { return $null }
    return [pscustomobject]@{ Sha = $sha; Version = $version; DescOffset = $descOffset }
}

if ($List) {
    $items = @(Get-ChildItem $ArchiveDir -Filter '*.elf' -ErrorAction SilentlyContinue)
    if ($items.Count -eq 0) { Write-Host 'Archive is empty.'; exit 0 }
    Write-Host ("Archived ELFs in {0}" -f $ArchiveDir)
    $items | Sort-Object LastWriteTime -Descending |
        Select-Object @{n = 'Name'; e = { $_.Name } }, @{n = 'MB'; e = { [math]::Round($_.Length / 1MB, 2) } }, LastWriteTime |
        Format-Table -AutoSize
    exit 0
}

$buildRoot = Join-Path $root '.pio\build'
if (-not (Test-Path $buildRoot)) { throw "No build output at $buildRoot" }

$envs = if ($Env) { $Env } else {
    Get-ChildItem $buildRoot -Directory |
        Where-Object { Test-Path (Join-Path $_.FullName 'firmware.bin') } |
        Select-Object -ExpandProperty Name
}

$archived = 0
foreach ($e in $envs) {
    $dir = Join-Path $buildRoot $e
    $bin = Join-Path $dir 'firmware.bin'
    $elf = Join-Path $dir 'firmware.elf'
    if (-not (Test-Path $bin) -or -not (Test-Path $elf)) {
        Write-Host ("  skip {0}: no firmware.bin/elf" -f $e) -ForegroundColor DarkGray
        continue
    }

    $id = Get-FirmwareIdentity $bin
    if (-not $id) {
        Write-Host ("  skip {0}: could not read the app SHA" -f $e) -ForegroundColor Yellow
        continue
    }

    $base     = "{0}_{1}_{2}" -f $id.Sha, $id.Version, $e
    $elfDest  = Join-Path $ArchiveDir ($base + '.elf')
    $binDest  = Join-Path $ArchiveDir ($base + '.bin')

    if (Test-Path $elfDest) {
        Write-Host ("  ok   {0}  fw {1}  (already archived)" -f $id.Sha, $id.Version) -ForegroundColor DarkGray
        continue
    }

    Copy-Item $elf $elfDest
    Copy-Item $bin $binDest
    $archived++
    Write-Host ("  saved {0}  fw {1}  env {2}" -f $id.Sha, $id.Version, $e) -ForegroundColor Green
}

Write-Host ''
Write-Host ("Archived {0} new ELF(s) in {1}" -f $archived, $ArchiveDir)
Write-Host 'Keep artifacts/elf with the evidence - a dump without its ELF is unreadable.' -ForegroundColor DarkGray
