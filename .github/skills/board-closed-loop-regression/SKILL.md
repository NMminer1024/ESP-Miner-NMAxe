---
name: board-closed-loop-regression
description: "Closed-loop hardware regression for NMAxe-family boards (NMAxe/Gamma/QAxe++/Rev6.1/Rev8.1/Nexus) over the web log stream. USE WHEN: the user asks to regression-test, closed-loop verify, flash-and-verify, or 'run the loop' on one or more miner boards after a firmware change; or when a board shows hashrate/vcore/HCN anomalies and needs an A/B attribution test. Log capture uses ws://{ip}/ws (NOT serial) so all board types can be tested in parallel. DO NOT USE FOR: pure code edits, benchmark sweeps (use benchmark API), or release packaging."
---

# Board Closed-Loop Regression (web log stream, parallel-safe)

Verify every firmware change on real hardware with a closed loop:
**baseline -> flash -> capture boot logs -> exercise pause/resume -> grep evidence -> A/B if anomalous -> report + rollback path**.
All interaction is over HTTP/WS, so **multiple boards of different types can be looped in parallel** (one log-capture process per board). Never use serial for this workflow.

## Board / environment map

| Board | PlatformIO env | ASIC | Power IC | Expected HR (GH/s) at stock |
|---|---|---|---|---|
| NMAxe | `NMAxe` | BM1366 x1 | TPS53355 | small_cores x MHz / 1000 (894x575=514) |
| NMAxeGamma | `NMAxeGamma` | BM1370 x1 | TPS53647 | ~1200 @ stock |
| QAxe++ | `NMQAxePP` | BM1370 x4 | TPS53647 | ~4800 @ 595MHz |
| QAxe++ Rev6.1 | `NMQAxePPRev61` | BM1370 x4 | TPS53647 | ~6100 @ 760MHz |
| QAxe++ Nexus | `NMQAxePPNexus` | BM1373 x2 | TPS546D24A | ~10000 @ 725MHz |

Identify the env from `GET /api/system/info` -> `identity.hwModel`/`display_name` (no Rev suffix => base `NMQAxePP`). `asic.model` tells the ASIC family — this drives which log markers are expected (see markers table).

## API endpoints (all boards)

- `GET /api/system/info` — hashRate, uptimeSeconds, temps.asic, asic.vcoreReal/vcoreReq/freqReq, miner.sAccepted/sRejected, identity.appSha256, miner.freeHeap
- `PATCH /api/mining/state` body `{"paused":true|false}` — pause/resume (goes through full vcore-off + ASIC reinit path)
- `POST /api/update/firmware` multipart `file=@firmware.bin` — OTA (device reboots; WiFi drops for ~30-60s)
- `WS ws://{ip}/ws` — every `LOG_*` line broadcast as UTF-8 text frames (docs/API.md)

## Critical: WS capture must survive the reboot

The WS stream depends on the board's WiFi. When the device reboots (OTA, watchdog, power-cycle reinit), the socket dies **and the earliest boot logs — exactly the ones we need — are emitted before WiFi is back**. Therefore:

1. Start the capture process **before** flashing and **keep it running through the reboot**.
2. The capture script must auto-reconnect in a retry loop (connect attempts every ~3s) so it grabs the log stream the instant WiFi comes up, with no gap at first boot output.
3. Never kill and manually restart the capture around a reboot — that is how boot-window evidence gets lost.

Use `scripts/ws_log.ps1` (bundled, already implements the reconnect loop). Run one per board, in async terminals, writing to a per-board log file.

## Loop procedure (per board)

### 1. Baseline (before any change)
```powershell
scripts/get_info.ps1 -Ip <ip>     # record fw, appSha256, hr, vcore, temp, acc/rej, heap
```
Record it in the report. Note `appSha256` — it is the identity of what is actually running.

### 2. Start log capture, then flash
```powershell
# async terminal, keep alive across reboots:
scripts/ws_log.ps1 -Ip <ip> -Out <logdir>\ws_<ip>.log
# then:
scripts/build_flash.ps1 -Env <env> -Ip <ip>   # builds, OTA, waits for boot+hash, prints new sha
```
Build takes ~1-2 min, OTA + boot ~1 min. The capture process must already be connected (or retrying) when the device reboots.

### 3. Verify cold boot from the captured log
Grep the window after the reboot for the expected markers (see table). Expect: `ready gate opened` for both tx/rx, one `Change asic diff`, zero `Asic response timeout` / `Invalid asic response` / panics, and board-type-appropriate markers only (no BM1366-only markers on BM1370/1373 boards).

### 4. Exercise pause/resume cycles
```powershell
scripts/pause_resume.ps1 -Cycles 5 -Ip <ip> -PauseSec @(20,60,20,120,20) -WarmSec 35 -SampleSec 50 -GoodHr <threshold> -Tag <board> -Csv <csv>
```
Include at least one long pause (>=60s): the vcore TOCTOU regression only shows when a pause lands inside the ~50ms `get_vcore()` sample window. Verdict per cycle: GOOD if avg hr >= GoodHr, BAD if >= 50, DEAD otherwise. Set `GoodHr` ~90% of expected steady hashrate for that board.

**Warmup caveat (NMAxe only):** after a resume with the die below `warmup_temp` (42C), the chip deliberately runs at `warmup_frq` (500MHz => ~447 GH/s) until warm, so the sample window may legitimately read ~87% of target and be flagged BAD. Confirm via log markers (`ASIC cold ... start at 500MHz` + `raising PLL 500MHz -> 575MHz`) before calling it a regression — see marker table.

### 5. Grep evidence from the log (not just API numbers)
```powershell
scripts/check_log.ps1 -Log <file> -FromHHmm <start>   # counts + key lines for all markers
```
Required evidence per resume: one `Change asic diff from [0.0]` (ticket mask re-applied after init), one `Vcore ready ... before ASIC reinit`, HCN line back to expected within ~60s, vcoreReal staying within ~±20mV of req (no slam to max).

### 6. A/B attribution when a cycle is BAD/DEAD
Do NOT guess. Flash the previous release bin (`release/vX.Y.ZZ/<board>/firmware.bin` via the same OTA endpoint), run the **identical** pause/resume schedule, compare BAD counts. If old=0 BAD and new>=2 BAD, the change caused it; bisect the suspect write/register by driver (see `bm_hal.h` `rearm_hcn_after_init()` precedent: a per-init broadcast 0x90 write is required on BM1366/BM1370 but freezes BM1373 chain counters — vendor "once before first poll" semantics are not "after every init"). After attribution, re-flash the fix and re-run >=8 cycles to confirm.

### 7. Report + rollback
Report a closed-loop table per board: baseline sha -> flashed sha -> cycle results -> marker evidence -> rollback command (`OTA release/<prev>/<board>/firmware.bin`). Update `identity.appSha256` comparison so the reader can see which image produced which result.

## Log marker reference

| Marker | Meaning | Expected |
|---|---|---|
| `(asic_rx/tx) miner ready gate opened` | rx/tx threads gated until init done | every boot, all boards |
| `Change asic diff from [0.0] to [N]` | ticket mask re-applied after an init | once per boot + once per resume/reinit, all boards |
| `Vcore ready at X (req Y) before ASIC reinit` | 1s rail-settle on resume path | every resume, all boards |
| `ASIC cold (...): start at 500MHz` / `warming up` / `raising PLL` | NMAxe warmup active | **NMAxe only**; must be 0 elsewhere |
| `PLL0(0x08) verified` / `REG 0x` | BM1366 PLL readback verify | **BM1366 only** |
| `[DOMAIN] d0=..` | BM1366 domain counters | **BM1366 only** |
| `HCN: (a, b) = N GH/s` | per-chip HCN hashrate; `(0)` or missing channel = that chip's 0x90 counter frozen/stale | channels present & ~equal on healthy chains |
| `[ASIC-REINIT] ... no progress / reject streak` | watchdog fast power-cycle reinit | 0 in a clean loop; occasional + recovery on BM1373 is pre-existing (verify against old fw before blaming the diff) |
| `Hashrate source switched to NONCE-fallback` | HCN temporarily unusable, hashrate falls back to nonce rate | transient OK, sustained = investigate |
| `Asic response timeout` / `Invalid asic response` | UART rx health | 0 in loop windows |
| `Guru Meditation` / `Panicking` / `abort()` | crash | 0 always |

## Parallel multi-board runs

One `(ws_log capture + pause_resume cycle)` pair per board can run simultaneously — they are independent HTTP clients. Keep each board's log file and CSV separate (`ws_<ip>.log`, shared `pr_results.csv` is append-safe per row but prefer one CSV per board when parallel). Flash sequentially if the build machine is the bottleneck; captures may all be running at once.

## Scripts (bundled)

- `scripts/ws_log.ps1` — WS log capture with reconnect loop that survives device reboots
- `scripts/build_flash.ps1` — BOM-strip, build env, OTA, wait-for-hash, print new sha
- `scripts/pause_resume.ps1` — parameterized pause/resume cycles with GOOD/BAD/DEAD verdict
- `scripts/check_log.ps1` — marker grep + counts over a captured log window
- `scripts/get_info.ps1` — one-shot baseline snapshot

All scripts are Windows PowerShell 5.1 compatible.
