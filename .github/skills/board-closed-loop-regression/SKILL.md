---
name: board-closed-loop-regression
description: "Closed-loop firmware debugging method for ESP-Miner boards: edit code -> build -> OTA flash -> capture logs (web WS stream, plus USB serial when pre-WiFi boot logs are needed) -> verify with evidence -> A/B attribution -> report with rollback. USE WHEN: the user asks to verify a firmware change on real hardware, '闭环/flash and check', regression after a diff, or diagnosing a device-side anomaly that needs flash-log-iterate cycles. Works for ANY subsystem (mining, power, network, web, NVS, UI), not just ASIC. DO NOT USE FOR: changes needing JTAG, hardware rework, or pure host-side tests without a device."
---

# Closed-Loop Firmware Debugging (flash -> observe -> iterate)

The unit of work is one **loop iteration**: change -> build -> flash -> observe -> decide.
Never conclude from code reading alone; never flash without a capture running; never blame a change without an A/B.

## The loop

### 0. Baseline first — always
Before touching code, snapshot the device's current verified state:
- `scripts/get_info.ps1 -Ip <ip>` — firmware version, running image sha (`identity.appSha256`), key metrics.
- Record which release/bin produced this state (`release/<ver>/<board>/` + its sha marker file).
This is the control group for every later comparison.

### 1. Minimal change + clean diff
- Change only what the current hypothesis requires; keep unrelated work out of the diff (commit or stash it separately so the loop tests exactly one variable).
- The build script strips UTF-8 BOM from changed files automatically (known edit-tool quirk in this repo).

### 2. Start log capture BEFORE flashing — mandatory
Two channels, chosen by what evidence is needed:

| Evidence needed | Channel | How |
|---|---|---|
| Anything logged after WiFi connects | **WS stream** `ws://{ip}/ws` (every `LOG_*` line, see docs/API.md) | `scripts/ws_log.ps1 -Ip <ip> -Out <file>` — runs forever, auto-reconnects every ~3s, survives device reboots. Start it in an async terminal BEFORE the flash and leave it running through every reboot. |
| Anything **before WiFi is up**: ROM/bootloader, early `begin()`, boot reset reason, panic backtrace at boot, WiFi-join failures | **USB serial** | Ask the user to plug USB into that board and reply with the COM port, then `scripts/serial_log.ps1 -Port COM<n> -Out <file>`. Opening the port may reset the board (DTR) — that is acceptable and even useful: it triggers the boot you want to capture. Tell the user first if the board is live-mining. |

Rules:
- One capture process per board; captures for many boards run fully in parallel (independent HTTP/serial clients). This is why WS is preferred: no per-board human action needed.
- **Never stop the capture around a reboot.** The WS socket dies when WiFi drops during OTA/reboot; the reconnect loop reattaches the moment WiFi is back. Manually restarting capture loses exactly the boot window you need.
- If the question involves pre-network behavior, serial is the only option — ask for the COM port up front instead of discovering this after a WS-only attempt misses the window.
- Captures timestamp every line and write `# ws connected/dropped` markers so later time-window analysis is unambiguous.

### 3. Build + OTA flash
`scripts/build_flash.ps1 -Env <platformio_env> -Ip <ip>`
- Map board -> env from `identity.hwModel`/`asic.model` in `GET /api/system/info`, or from platformio.ini `[env:*]` build flags.
- OTAs via `POST /api/update/firmware` (multipart `file=@firmware.bin`), then waits until the device reports fresh `uptimeSeconds` and a healthy metric, and prints the new `appSha256`.
- The printed sha is the identity of what actually runs — quote it in every conclusion; a board that was not flashed has no opinion.

### 4. Observe: drive the behavior, then grep evidence
- Exercise the behavior under test through the device's own API surface (state transitions, config writes, reboots), sampling status endpoints on a timer. Wrap each exercise in a small parameterized script so runs are repeatable and comparable across builds.
- Cover worst cases, not just happy path: long idle, cold start, repeated cycles, boundary configs.
- Then verify **log evidence**, not just metric deltas: count/inspect the lines proving the new code path ran, and the lines that must be absent (errors, timeouts, fallbacks, crashes): `scripts/check_log.ps1 -Log <file> -Pattern '<regex>' [-FromHHmm .. -ToHHmm ..]`.
- Before flashing, write down an expectation table: which markers/behaviors must appear, which must not, per board type or config. After flashing, diff against that table.

### 5. Anomaly -> A/B attribution, never guessing
If a metric or marker regressed:
1. Flash the **previous known-good bin** (`release/<prev>/<board>/firmware.bin`) through the same OTA path.
2. Run the **identical** exercise schedule (same script, same parameters).
3. Compare counts side by side: old 0 bad vs new N bad = the diff caused it; both bad = pre-existing, out of scope for this change.
4. If only some targets fail, bisect along hardware/config dimensions (chip model, board revision, power IC, NVS settings). When correct behavior differs per target, encode the difference as an explicit per-driver/per-board opt-in/opt-out — never implicit.
5. Re-flash the fix and re-run with **more** cycles than the failing run had before declaring resolved.

### 6. Report + rollback
Every loop closes with a table: baseline sha -> flashed sha -> exercise results -> evidence lines -> rollback command (OTA the previous release bin; note any NVS changes and how to revert them). State explicitly which boards/configs were NOT covered.

## Practical constraints (apply to every loop)

- OTA reboots the board and drops WiFi for ~30-60s: budget capture windows accordingly; `uptimeSeconds < 300` confirms "this is the new image".
- WS stream carries only `LOG_*` output after network bring-up. Anything earlier requires serial (see step 2).
- Opening a serial port can itself reset the board — treat as a free boot capture, but warn the user on a working device.
- Parallel loops across boards are safe and encouraged: build once per env, flash sequentially if the build machine is the bottleneck, keep all captures running.
- Keep per-board artifacts separate: `ws_<ip>.log`, `serial_COM<n>.log`, one results CSV per board.

## Worked example (illustration of the method only)

A hashrate regression appeared only after pause/resume cycles, and only on one ASIC family:
1. **Baseline/control**: previous release bin, 5/5 exercise cycles clean.
2. **New build, same schedule**: 4/10 cycles degraded — anomaly confirmed by metrics.
3. **WS log evidence**: per-cycle markers localized it to one chip-side counter state on the failing boards only; automatic recovery events visible in the same log.
4. **A/B**: re-flashed old bin, identical schedule, 0/5 bad — attributed to the diff, not the hardware.
5. **Fix** placed behind an explicit per-driver opt-out (the write was required on some chips, harmful on others); re-ran 13 cycles clean across all board types **in parallel** (one WS capture + one exercise script each).
6. **Report**: sha before/after, per-board cycle table, OTA rollback command.

The transferable parts are: control group first; capture-before-flash; evidence from logs not metrics alone; identical-schedule A/B; explicit per-target opt-outs when behavior differs by hardware; more cycles after the fix than during the failure. The registers/markers themselves are ASIC-specific and intentionally not part of this method.

## Scripts (bundled, Windows PowerShell 5.1)

- `scripts/ws_log.ps1` — WS log capture, infinite reconnect loop, survives reboots
- `scripts/serial_log.ps1` — serial capture for pre-WiFi boot logs (user plugs USB, gives COM)
- `scripts/build_flash.ps1` — BOM-strip, PlatformIO build, OTA, wait-for-boot, print new sha
- `scripts/get_info.ps1` — baseline snapshot (version, sha, metrics)
- `scripts/check_log.ps1` — regex evidence counts/lines over a time window of a capture file
