# async_tcp panic inside ROM strlen (v3.2.02z) — WebSocket client-list race

## Symptom

Two boards, same firmware `v3.2.02z`, both panic while the operator is
interacting with (or reloading) the web UI:

| Board | IP | uptime at panic | appSha256 | crash task | exc_pc |
|---|---|---|---|---|---|
| NMAxeGamma | 192.168.124.10 | 69 954 s | `3b13cb96dfefe1c6` | `async_tcp` | 0x400556bd |
| NMQAxePP | 192.168.124.18 | 70 416 s | `061593a1dfc8689e` | `async_tcp` | 0x400556d2 |

Coredump metadata for both: `crcOk=true`, `btCorrupted=true`, **bt depth = 1**
(only the PC itself). `heapMin` ≈ 7.4 MB free, so this is not memory exhaustion.
`/api/reboot/last` classifies them as `crash / panic / intent=none`.

## Symbolizing that PC

`0x40055xxx` is **ROM**, not our application, so
`addr2line -e .pio/build/<env>/firmware.elf` answers `??`. Resolve it against
the ROM ELF shipped with the toolchain:

```powershell
$tc  = "$env:USERPROFILE\.platformio\packages\toolchain-xtensa-esp32s3\bin\xtensa-esp32s3-elf-addr2line.exe"
$rom = "$env:USERPROFILE\.platformio\packages\tool-esp-rom-elfs\esp32s3_rev0_rom.elf"
& $tc -f -C -i -e $rom 0x400556bd 0x400556d2      # -> strlen
```

Both PCs sit inside **ROM `strlen`** (the 4-byte-at-a-time loop reads past the
end of a buffer). `btCorrupted=true` + depth 1 means the stack walk failed, so
the coredump summary cannot name the caller — only the USB serial panic
backtrace can.

A fault *inside* `strlen` always means the same class of bug: **the pointer was
bad, or the "string" had no NUL and the scan ran into unmapped memory.**

## Root cause: ESPAsyncWebServer 3.6.0 WebSocket layer

`platformio.ini` pins `ESP32Async/ESPAsyncWebServer#v3.6.0` (AsyncTCP 3.3.x).
In that version `AsyncWebSocket::_clients` (`std::list<AsyncWebSocketClient>`) is
accessed by several threads **with no lock at all**:

```cpp
// libdeps/.../ESPAsyncWebServer/src/AsyncWebSocket.cpp (v3.6.0)
AsyncWebSocket::SendStatus AsyncWebSocket::textAll(AsyncWebSocketSharedBuffer buffer) {
  size_t hit = 0, miss = 0;
  for (auto& c : _clients)                 // no lock
    if (c.status() == WS_CONNECTED && c.text(buffer)) hit++;
    ...
}
void AsyncWebSocket::cleanupClients(uint16_t maxClients) {
  ...
  for (auto iter = _clients.begin(); iter != _clients.end();) {
    if (iter->shouldBeDeleted()) iter = _clients.erase(iter);   // no lock, destroys + frees the node
    else iter++;
  }
}
```

`AsyncWebSocket` does have a `_lock` member, but every `lock_guard` in that file
belongs to `AsyncWebSocketClient`; the server-side list is unlocked.

Who hits it in this firmware:

* `cleanupClients()` — `webserver_thread_entry` (`src/thread/net_threads.cpp`),
  every 250 ms.
* `textAll()` — `dbg::log_emit()` (`src/utils/logger/logger.cpp`), i.e. **every
  LOG_* line from every task**, including `async_tcp` itself, while it holds the
  logger mutex.
* `_newClient()` — the WS handshake, executed in `async_tcp`.

Sequence that matches the reports: the page is reloaded -> the browser drops the
old WebSocket -> `cleanupClients()` erases that client (destructor + `free` of the
list node) while a logging task is inside the `for (auto& c : _clients)` loop ->
use-after-free on the client object and on its `_messageQueue` ->
heap corruption -> the next `strlen` / `String` on the corrupted heap faults.
`v3.6.0` also defaults `closeWhenFull = true`, so a client that cannot drain the
log flood is closed from *inside* the very iteration that is walking the list,
widening the window.

Upstream history of exactly this defect:

* **v3.10.3** — "Fix race conditions in the SSE and WS code" (PR #414): adds
  `_ws_clients_lock`, every `_clients` access becomes locked.
* **v3.11.0** — locking leftovers (PR #427/#429); `CloseClientOnQueueFull`
  default becomes `false` (queue full drops the message instead of killing the
  connection).
* **v3.12.0** — major WebSocket refactor: torn frames, **use-after-free**,
  ack computation for long fragmented messages. Requires AsyncTCP ^3.5.0.

## Secondary (latent) bug in the same area

`AsyncWebSocketClient::_onData()` computes the close reason length with
`strlen(reasonString)` although `data[datalen]` is **never NUL-terminated**
before that call:

```cpp
char* reasonString = (char*)(data + 2);
if (reasonCode > 1001)
  _server->_handleEvent(this, WS_EVT_ERROR, &reasonCode,
                        (uint8_t*)reasonString, strlen(reasonString));
```

Any close frame with code > 1001 and no reason text makes `strlen` scan past the
frame. Still present in v3.12.1; fixed on `main` only
(`strnlen(reasonString, datalen - 2)`).

## What was tried on hardware (2026-11-11)

Raw-socket WS probes against 124.10 / 124.18: close frames with code 1007 and
1002, with and without a 1200-byte padded text frame in front, 7 sessions in
total. The server echoed the text frames (so the WS data path works) and **did
not panic** — the close-frame `strlen` is real but not reliably exploitable
(whether it faults depends on the byte that happens to follow the frame).
Conclusion: it is a latent bug, not the trigger seen by the operator.

## Two measurement traps hit during this investigation

1. **Opening the USB serial port RESETS the board.** Attaching
   `serial_log.ps1` to a board that had been up for 76 122 s produced
   `rst:0x15 (USB_UART_CHIP_RESET)` and `reboot/last` classified it as
   `reset=unknown, class=external, intent=none`. The capture script sets
   `DtrEnable=false`/`RtsEnable=false`, which is not enough on ESP32-S3 USB-CDC.
   Two consequences: never attach serial to a unit whose long uptime is the
   evidence under test without accepting that loss, and do not mistake the
   resulting reboot for a crash (`coredump.present` stays false and
   `class` is `external`, not `crash`).

2. **A PowerShell confirmation prompt can fake a latency measurement.**
   `Invoke-WebRequest` in this shell raised "脚本执行风险 … 是否要继续?" and waited
   for input, so a wall-clock timer around it reported 303 s for a request that
   actually takes 0.68 s. Always pass `-UseBasicParsing` (or drive the device
   from python) before drawing any performance conclusion.

## `/api/dashboard/chart/history` behaviour (measured on a 21 h uptime board)

`interval` is parsed and clamped to **1..100**, so `interval=1000/500/200` all
return the same `sampleInterval=100`. With 12 945 samples in the deque:

| request | samples returned | elapsed |
|---|---|---|
| `interval=100` | 130 | 0.084 s |
| `interval=10` | 1295 | 0.682 s |
| `interval=1` | 2000 (capped by `maxDataPoints`) | — |

So the endpoint is roughly linear and not a latency problem. Response size does
saturate once the deque exceeds ~2000 samples (≈2.8 h of uptime), which means
response size alone cannot explain a crash threshold at ~18 h.

## Fix

1. Bump the WebSocket library to the **smallest** release that fixes it:

   ```ini
   https://github.com/ESP32Async/AsyncTCP.git#v3.4.10
   https://github.com/ESP32Async/ESPAsyncWebServer.git#v3.10.3
   ```

   v3.10.3 is the first release with `_ws_clients_lock` and it still accepts
   AsyncTCP ^3.4.10, so no AsyncTCP / toolchain change was needed. v3.12.0 would
   need AsyncTCP ^3.5.0 and changes `abort()` to run its callbacks synchronously
   in the caller's task — a much bigger blast radius for no extra benefit here.

   Note the dependency URL also had to change owner: `mathieucarbou/` only ever
   published v3.6.0; everything from v3.9.0 on lives at `ESP32Async/`.

2. `webSocketEvent()` / `WS_EVT_CONNECT`: `client->setCloseClientOnQueueFull(false)`
   (the API is on `AsyncWebSocketClient`, not `AsyncWebSocket`).

   Every `LOG_*` line is broadcast with `textAll()` from whichever task logged
   it, so a browser that stops draining (background tab, slow Wi-Fi, a page stuck
   loading) fills the 32-message queue fast. The default then closes that client
   from *inside* the loop walking `_clients`, and the page reconnects at once —
   a reconnect storm precisely while the list is being modified. Dropping a log
   line costs nothing.

3. Keep the log flood away from the WS from arbitrary tasks (defence in depth):
   one low-priority task should own the `textAll()` calls, so a slow browser can
   never block `log_emit()` (which is called from the miner/WiFi/stratum tasks
   while holding the logger mutex). Not done yet.

4. Instrumentation for the next occurrence: keep the **USB serial** console open
   while refreshing the page — the panic handler prints the full backtrace with
   symbol names, which the 1-frame coredump summary cannot.

## Coredump download endpoint (added, works)

`GET /api/coredump/download` streams the raw image so a real post-mortem is
possible. Two implementation traps, both hit on hardware:

1. `esp_core_dump_image_get()` reports an **absolute flash address**; 
   `esp_partition_read()` wants an **offset inside the partition**. Passing the
   absolute address makes every read fail and the endpoint streams 0 bytes
   (HTTP 200 with an empty body — easy to miss).
2. The stored image **starts with the IDF header**, not with the ELF: measured
   20 bytes on IDF 4.4 / ESP32-S3 (first u32 = image size, ELF magic at +0x14).
   gdb refuses such a file. The endpoint now locates the ELF magic inside the
   first 64 bytes and serves from there.

Verify with: `head -c 4 coredump.elf` → `7f 45 4c 46`.

OTA does **not** erase the coredump partition, so the dump from a crash on the
previous firmware is still downloadable after the upgrade — that is how the
evidence below was recovered.

## Closed-loop verification (2026-11-11, after the fix)

| Board | IP | before | flashed | after |
|---|---|---|---|---|
| NMAxeGamma | 192.168.124.10 | v3.2.02z, sha `3b13cb96…` | v3.2.02a, sha `08e40ed7512deecc` | 1354 GH/s, running, prot none |
| NMQAxe++ | 192.168.124.18 | v3.2.02z, sha `061593a1…` | v3.2.02a, sha `909fc0f7cf1431b3` | 5870 GH/s, running, prot none |

Exercise: 560 total "refresh storm" cycles (280 per board) — each cycle does
`GET /`, `/styles.css`, `/api/system/info`, `/api/dashboard/chart/history?interval=1`,
`/api/system/nvs`, then a WS handshake + text frame + **hard RST** (no clean
close, so the client lingers in `_clients` for the reaper). That deliberately
puts a new client insertion next to a reaper erase.

Result:
- **0 reboots, 0 panics** in 560 cycles (uptime only ever increased; the only
  `reset=sw` entry is my own OTA, `intent=ota_finished, class=planned`).
- Serial captures (COM4 / COM17) and the WS capture ran across every reboot and
  contain no `Guru Meditation` / `panic` / `Backtrace` lines.
- The coredump partition still holds the **old** v3.2.02z dump on both boards
  (`appSha256` unchanged), which is the positive control that no new crash
  overwrote it.

## Forensic note: the old dump confirms a smashed stack, not a bad string

With `coredump_124.10.elf` + a rebuild of the crashing commit loaded in
`xtensa-esp32s3-elf-gdb`:

- crashing thread PC = ROM `strlen`;
- **`a0` (return address) = `0x82033a14`, `a2` (the argument) = `0x8a4faeaa`** —
  neither is a valid ESP32-S3 code address (code lives in `0x4000_0000`–`0x4005_ffff`
  ROM / `0x4200_0000`+ flash / `0x4037_0000`+ IRAM);
- 400 words of the crashing task's stack around `sp` contain **zero** values in
  the flash code range.

So the task's stack had already been overwritten with heap/PSRAM data before the
panic. That is the signature of a wild write through a dangling pointer, not of
the firmware passing a bad-but-otherwise-intact string. It also explains
`btCorrupted=true` with a depth-1 trace.

Caveat: the ELF could not be reproduced bit-exactly (sha `1f459f3f…` vs the
recorded `3b13cb96…`; that build came from a dirty working tree), so per-frame
symbol names for the old dump are not trustworthy. The register/stack
observations above do not depend on symbol matching.

### Decoded from the ELF notes (symbol-independent)

The `EXTRA_INFO` note carries the Xtensa `esp_core_dump_summary_extra_info_t`:

| dump | exc_cause | exc_vaddr | a0 | a2 (strlen arg) |
|---|---|---|---|---|
| 124.10 | **28 = LoadProhibited** | `0x8a4faea8` | `0x82033a14` | `0x8a4faeaa` |
| 124.12 | **28 = LoadProhibited** | `0xbaaa8ad4` | `0x82033a14` | `0xbaaa8ad4` |
| 124.18 | 28 (same layout) | `0x80000000` | `0x82037140` | `0x80000000` |

All three are **read** faults (LoadProhibited, never StoreProhibited), so nothing
was being written at the moment of the panic — the write that smashed the stack
happened earlier. `a0` is a different garbage value per board, so it is not a
fixed constant to chase; the invariant is only "a0 and a2 are both non-code
addresses", i.e. the frame was overwritten.

## Uptime correlation (the strongest remaining lead)

Every recorded crash happened **after 65536 s (18.2 h) of uptime**:

| board | crash uptime |
|---|---|
| 124.10 | 69 954 s = 19.4 h |
| 124.18 | 70 416 s = 19.6 h |
| 124.12 | 74 953 s = 20.8 h |

65536 s is exactly one `uint16_t` second-counter wrap, so a 16-bit seconds field
is the obvious suspect. A sweep of `uint16_t` fields assigned from
`millis()`/uptime found no such counter in the web path (`mdns txid` uses only the
low 16 bits by design; `bm_time`/`stab_time` are config seconds), so this is not
yet proven — it is, however, the one numerical threshold that matches all three
data points.

## Attempted reproduction on the OLD firmware (2026-11-11, 124.13, v3.2.02z)

All of the following ran against the broken library with a serial capture
attached, and **none reproduced the crash** (uptime only increased):

| exercise | scale | result |
|---|---|---|
| faithful page load (parallel `/`,`/runtime.js`,`/main.js` 493 KB,`/styles.css` + live WS + dashboard polling) | 15 sessions | no crash |
| abort mid-transfer (read 4 KB then RST, 3 concurrent, rotating targets) | 60 rounds | no crash |
| WS starvation (4 sockets that never read, tiny SO_RCVBUF, continuous log traffic) | 120 s, 160 handshakes | no crash |
| earlier "refresh storm" (WS handshake + text + hard RST) | 560 cycles | no crash |

Because all of these were run at short uptime, they neither confirm nor refute
the uptime correlation. They do rule out "any single page load triggers it".

## Reusable lessons

* `0x4005xxxx` is ROM on ESP32-S3: symbolize with `esp32s3_rev0_rom.elf`, not the
  app ELF. Do this before concluding "the backtrace is empty".
* A fault inside `strlen` is a *data* bug (unterminated / dangling pointer), not
  a string-library bug.
* `btCorrupted == true` + depth 1 in a coredump summary means "the stack was
  already smashed"; do not try to localise from the backtrace, look for the
  concurrency/UAF instead.
* The coredump summary is available over `GET /api/coredump/info`, but there is
  no download endpoint — the raw dump cannot be pulled off the device yet.
