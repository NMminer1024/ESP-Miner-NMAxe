# async_tcp panic inside ROM strlen (v3.2.02z) — WebSocket client-list race

## 2026-11-13 correction: a complete stack identifies chart-history JSON

The next field recurrence on NMQAxe++ `192.168.124.18` materially changes the
diagnosis. The board ran `v3.2.02d` / app SHA `fc5b5c1ca6c20353` for **90,880 s
(25 h 14 m 40 s)**, then panicked when the dashboard was refreshed. The raw ELF
coredump was preserved under
`docs/baseline/incident_124_18_20261113_212128/coredump.bin` (SHA256
`b919d90204dfbaee5b561c79e1398669918575f13de3cd43e32804bc04cf18db`).

Unlike the earlier one-frame summaries, GDB can unwind this dump. The crash task
is `async_tcp`, with `a2=0xbaa2c2ab` (wild pointer), and the call chain is:

```text
ROM strlen
ArduinoJson::ZeroTerminatedRamString::size
ArduinoJson::CollectionData::getSlot / getOrAddMember
get_status_history() at src/web/http_server.cpp
AsyncCallbackWebHandler::handleRequest
AsyncWebServerRequest::_onData
AsyncClient::_recv
_async_service_task
```

The fault occurs while `get_status_history()` adds metadata after constructing a
large ArduinoJson DOM for up to 2,000 rows x 18 columns. This is direct evidence
that the dashboard chart request is the active crash path; the earlier statement
below that the chart was not the source is superseded. The WebSocket client-list
and close-reason fixes remain valid hardening, but they do not fix this recurrence.

The endpoint now snapshots at most 2,000 fixed-size `history_node_t` records into
PSRAM while holding the history mutex, releases the mutex, and serializes JSON
directly into a bounded PSRAM buffer. It no longer creates the large ArduinoJson
slot graph in `async_tcp`. The response buffer is owned by the callback via a
`shared_ptr` custom deleter, so an aborted browser refresh also frees it.

### Cross-board reproduction: 6/6 panics on the same line

A single `GET /api/dashboard/chart/history?interval=1` (no dashboard UI, no
WebSocket) panicked **every** long-uptime board on the first attempt. Each dump
was symbolized against the ELF whose `app_elf_sha256` matched the dump's
`appSha256`, so the frames are exact, not approximate:

| IP | model | appSha256 | uptime at panic | exc pc | a2 |
|---|---|---|---|---|---|
| .18 | NMQAxe++ | `fc5b5c1ca6c20353` | 90 880 s | 0x400556b1 | `0xbaa2c2ab` |
| .78 | NMQAxe++ (Nexus) | `21dc69fe5c07a802` | 95 001 s | 0x400556b1 | `0xdf275f83` |
| .10 | NMAxeGamma | `ce50326497d8392a` | 162 918 s | 0x400556b1 | `0xffffffff` |
| .11 | NMAxeGamma | `ce50326497d8392a` | 162 940 s | 0x400556bd | `0xaaaaa6aa` |
| .12 | NMAxeGamma | `ce50326497d8392a` | 162 961 s | 0x400556bd | `0xaa242aaa` |
| .13 | NMAxeGamma | `ce50326497d8392a` | 162 981 s | 0x400556bd | `0xb02aeefe` |

All six resolve to `get_status_history()` at `http_server.cpp:1345`, inside
ArduinoJson `getOrAddMember`/`set<int>` (the result-metadata write that follows
the row loop), with the ROM `strlen` argument a garbage pointer and
`a2 & ~3 == exc_vaddr`. Two models + three firmware SHAs + six boards rules out
model-specific and board-specific causes; the shared variable is the size of the
JSON document built in the `async_tcp` task, which grows with the history deque
and therefore with uptime.

Reproduction also means the earlier "chart path is bounded and does not leak"
conclusion was measured on the wrong axis: the buffers were bounded in *size*,
yet constructing the large DOM in the network task was itself the fault. Bounded
memory does not make a 2,000-row `BasicJsonDocument` safe.

Evidence: `docs/baseline/trigger_20261113/` (pre/post snapshots, six raw dumps,
GDB logs, `RESULTS.md`). All boards rebooted and resumed mining on their own.

`tool/capture_web_crash.ps1` provides non-destructive field capture. It polls only
`/probe`, `/api/system/info`, and coredump metadata; it never opens the dashboard,
chart endpoint, or WebSocket. On reachability changes, uptime reset, or a new
coredump it saves reboot metadata and downloads the raw dump automatically.

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

**Re-confirmed on v3.10.3 (the version we bumped to).** The bump to v3.10.3 added
`_ws_clients_lock` but did **not** touch this line — it is still
`strlen(reasonString)` at `AsyncWebSocket.cpp:697` in the tree we now build. So the
library upgrade closed the `_clients` list race but left the strlen over-read open.

**Correction (2026-11-11): an earlier draft of this file claimed the strlen argument
was a valid pointer `0x3fcd5258`. That was a misread.** `0x3fcd5258` is word[6] of the
crash-task CORE note, which is the **TCB handle**, not `a2`: it equals the base of that
task's stack `PT_LOAD` (`0x3fcd4b90`) plus `0x6c8`, and the TCB is exactly 344 B
(`0x158`) — matching the 344-byte `PT_LOAD` pairs. The real argument is in the
`EXTRA_INFO` note and is **wild** (`a2 = 0x8a4faeaa`, see the table below). So the
"unterminated-but-real buffer" reading is NOT supported; the argument itself is a bad
pointer.

**What the notes actually show (symbol-independent).** For `coredump_124.10.elf` the
crash-task CORE note has PC at word[18] = `0x400556c0`, with neighbouring ROM PCs
`0x400556d5 / 0x400556e5`; all three symbolize to **ROM `strlen`**
(`esp32s3_rev0_rom.elf`). The `EXTRA_INFO` note gives `exc_cause = 28 (LoadProhibited)`,
`exc_vaddr = 0x8a4faea8`, `a2 = 0x8a4faeaa` (the strlen argument — wild), and
`a0 = 0x82033a14`. Stripping the Xtensa window-base bits from `a0`
(`0x82033a14 & 0x3FFFFFFF | 0x40000000`) gives **`0x42033a14`, a valid IROM
(instruction-window) address** — so the *caller* is a real function and the current
frame is intact. The argument, not the frame, is corrupt.

**The decisive arithmetic: `a2 & ~3 == exc_vaddr`.** On ESP32-S3 the
instruction/data-mapped external window is `0x42000000-0x43FFFFFF` / `0x3C000000-
0x3DFFFFFF`; `0x8a4faeaa` is in **no** mapped range at all, so `exc_cause = 28
(LoadProhibited)` is a plain unmapped read. And `0x8a4faeaa & ~3 = 0x8a4faea8`, which
is *exactly* `exc_vaddr`. So the load that faulted is strlen's word-aligned load
**at the argument itself** — not a scan that walked off the end of a neighbouring
buffer. `a2` is a garbage pointer value, not the address of an unterminated string.
This single equality is what rules out the close-frame hypothesis below (there,
`reasonString = data + 2` is always a valid DRAM offset and can never be `0x8a4faeaa`).

**Reachability caveat (why it is hard to reproduce).** The strlen only runs when
`reasonCode > 1001`. A normal browser reload sends close code **1000** (normal) or
**1001** (going away) — neither is `> 1001`, so the common "open the dashboard"
action does *not* hit this path. It needs an abnormal close (1002/1003/1007/1008/…)
with no reason text, which is why the 7-session raw-socket probe below did not panic.

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

## "Does data accumulate until a web hit crashes it?" — chart path audited (2026-11-11)

Hypothesis tested: some cache grows with uptime so that opening the dashboard
(the chart pulls the whole history deque) eventually crashes the device. Audited
every buffer the dashboard touches and measured on hardware. **Conclusion: the
chart path is bounded and does not leak; it is not the crash source.**

Buffers the dashboard touches, and their caps:

| buffer | allocator | cap | grows with uptime? |
|---|---|---|---|
| `status_history.deque` | `PsramAllocator` (PSRAM) | `MINER_HISTORY_MAX_SIZE=20000` **and** 24 h window | plateaus at 20 000 nodes ≈ 27.8 h (true 5 s cadence) |
| chart JSON doc | `PsramJsonAllocator` (PSRAM) | `estimated_samples ≤ MAX_DATA_POINTS=2000` | **no** — capped at 2000 samples regardless of deque size |
| chart output buffer | `heap_caps_malloc(PSRAM)` | `measureJson` of the ≤2000-sample doc | **no** — response saturates ≈48 KB |
| `proximity_history.deque` | PSRAM | 36 entries (`add_share_diff_history`) | no |
| `_hr_deque`, `_pool_job_cache`, `_submit_queue`, `_msg_rsp_map` | PSRAM | time-window / fixed-size trims | no |

Hardware measurements (124.10, the weakest board — lowest internal heap):

* **60 full `interval=1` chart loads**: `freeHeap 59612 → 64324` (rose, not fell),
  `minFreeHeap` flat at **36464** the whole time, response steady ≈48 KB. No leak,
  no crash.
* **40 aborted downloads** (connect, read 512 B, RST mid-stream): internal heap
  unchanged. The PSRAM output buffer is freed by the response callback's last chunk;
  an aborted request deletes the response object (`WebRequest.cpp` `delete _response`)
  and the callback filler is not re-entered, so no PSRAM leak either.
* deque on a freshly rebooted board: `size=378`, `maxDataPoints=2000` — confirms the
  server-side cap independent of uptime.

So neither the deque nor the chart response grows past a fixed ceiling, and repeated
dashboard access does not erode the heap. The ~18 h correlation is **not** explained
by chart data accumulation.

## Was the uptime fix (713167d5) the regression? — No

Commit `713167d5` (2026-11-03) "derive uptime from a monotonic clock" changed
`st.uptime_session++` (loop iterations, ran at ~70 % of real time) to
`esp_timer_get_time()` (true seconds), and moved two modulo timers
(`uptime_session % 5`, `% 2`) to `millis()` elapsed-time accumulators.

Checked whether it could be the regression:

* **Type/overflow:** `uptime_session`/`uptime_ever` were already `uint64_t` before
  the fix (`git show 713167d5^:src/mining/mining_types.h`). No `uint16_t` per-second
  counter exists anywhere in the uptime path. The `2^16 = 65536 s` coincidence with
  the crash uptimes (69954 / 74953 / 76122 s) is **not** a truncation — nothing wraps
  at 65536. `millis()` itself wraps at ~49.7 d, far beyond these uptimes.
* **Sampling cadence:** the fix made history sampling fire at a true 5 s instead of
  ~7 s effective, so the deque fills ~40 % faster and hits the 20 000 cap at ~27.8 h
  instead of ~38.9 h. But the cap is a *plateau*, not a cliff — past it the deque
  stays 20 000 and the chart response stays capped at 2000 samples. A faster plateau
  does not create a crash threshold at 18 h.
* **Interaction with async_tcp:** none. `uptime_session` is read only for display
  (`/probe`, `/api/system/info`, LCD) and as a subtraction base (NTP interval, NVS
  save interval). It never feeds a WS broadcast, a buffer size, or an allocation.

Verdict: the uptime fix is **not** the regression. It changed *when* the deque
plateaus, not *whether* anything unbounded grows. The crash predates it in kind
(same strlen-in-ROM signature) and its trigger is the WS close-frame path, which the
uptime fix does not touch.

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

5. **Bound the close-reason `strlen` — applied 2026-11-11.** v3.10.3 still ships
   the unbounded scan (`strlen(reasonString)` on a non-NUL-terminated frame), so a
   build-time patch was added instead of waiting for an upstream release:

   * `patch_libdeps.py` (repo root) rewrites the call in the unpacked library to
     `strnlen(reasonString, datalen - 2)` with a `datalen > 2` guard.
   * wired in `platformio.ini` as `post:patch_libdeps.py` (alongside the existing
     `pre:build_web.py`), and re-applied from a `buildprog` pre-action so a fresh
     checkout / fresh libdeps install is covered too.
   * idempotent (marker check), and it **fails the build loudly** if the upstream
     call shape changes, so we can never silently ship the unbounded version.
   * safe for us: `webSocketEvent()` ignores `data`/`len` for `WS_EVT_ERROR`
     (prints only `error`), so the shorter bounded length changes no behaviour.
   * verified: regex hits exactly 1 site in v3.6.0 and v3.10.3; `pio run -e NMQAxePP`
     SUCCESS with the patched file compiled; re-running does not double-insert.

   **Note for other machines / stale builds — how the patch reaches the binary.**
   The patch edits the *unpacked library copy* at
   `.pio/libdeps/<env>/ESPAsyncWebServer/src/AsyncWebSocket.cpp`, which is a build
   artifact (`gitignored`), so `git status` stays clean and you will not see it in
   a diff. It reaches the compiled object by one of two routes:

   * **Normal pull (library already in `.pio/libdeps`, upstream form).** The
     `extra_scripts` load-time call rewrites the file; SCons then sees a newer
     mtime and recompiles `AsyncWebSocket.cpp.o` in the same build. Verified:
     restoring the library to upstream form and rebuilding printed
     `[patch_libdeps] patched ... (1 site)` and SUCCESS.
   * **No `.pio/libdeps` yet** (fresh clone, `rm -rf .pio`, or after a `lib_deps`
     change forces a re-download). The script loads *before* PlatformIO installs
     the libraries, so the file may not exist at that moment; the
     `env.AddPreAction("buildprog", ...)` hook patches it after the install and
     before compiling. Verified on 2026-11-11 by deleting the library directory
     and rebuilding: PlatformIO re-installed `ESPAsyncWebServer @ 3.10.3+sha.1d17fae`,
     the log printed `[patch_libdeps] patched ... (1 site)` and the build
     succeeded. Cross-check on the object file (upstream has only `strlen` in
     this translation unit, never `strnlen`):

     ```powershell
     & "$env:USERPROFILE\.platformio\packages\toolchain-xtensa-esp32s3\bin\xtensa-esp32s3-elf-nm.exe" `
       .pio\build\<env>\lib*\ESPAsyncWebServer\AsyncWebSocket.cpp.o | Select-String strnlen
     # patched -> "U strnlen" is present
     ```

   **Trap found while verifying this:** PlatformIO treats every directory inside
   `.pio/libdeps/<env>/` as a library, so do **not** park a backup copy there
   (e.g. `ESPAsyncWebServer.bak`) while testing — it gets compiled *instead of*
   the real library, silently producing an unpatched build.

   How to confirm on any machine — one of these must show the patched form:

   ```powershell
   # 1) the build log mentions the patch
   pio run -e <env> | Select-String patch_libdeps
   # 2) the library source is bounded (expect "reason_len", not "strlen(reasonString)")
   Select-String -Path .pio\libdeps\<env>\ESPAsyncWebServer\src\AsyncWebSocket.cpp -Pattern "reason_len|strlen\(reasonString\)"
   ```

   `pio run -t clean` only deletes `.pio/build/<env>`, **not** `.pio/libdeps`, so
   the patched copy survives and is simply recompiled — no manual re-patch needed.
   If a build ever reports `could not find the unbounded strlen(reasonString) call`
   the script exits non-zero on purpose: the library's call shape changed upstream
   and the patch must be re-audited before shipping.

   Caveat: this is a **hardening** fix. The field panic's `a2` was a wild pointer
   (`a2 & ~3 == exc_vaddr`), which the close-frame path cannot produce, so this
   line is not the proven trigger of the recorded panics — see the forensic notes
   above. It removes one network-reachable over-read; it does not by itself close
   the `_clients` use-after-free investigation.

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

## Forensic note: a fixed caller passes a corrupt argument (NOT a smashed frame)

**Correction (2026-11-11): an earlier draft called `a0` "garbage" and concluded the
stack frame was overwritten. That was wrong — it ignored the Xtensa window ABI.**
On Xtensa, `a0` returned from a `call8`/`call12`/`call4` encodes the window base in
bits 31:29 and the callinc in bit 6. `a0 = 0x82033a14` has window base `0b100` (=4)
in the top bits; stripping them (`0x82033a14 & 0x03FFFFFF | 0x40000000`) gives
**`0x42033a14`, a valid DROM (flash) code address**. So the *return address is real*
and the current register frame is intact.

The decisive cross-board fact:

| board | fw / hw | `a0` raw | `a0` window-stripped |
|---|---|---|---|
| 124.10 | v3.2.02z / NMAxeGamma | `0x82033a14` | `0x42033a14` |
| 124.12 | v3.2.02z / NMAxeGamma | `0x82033a14` | `0x42033a14` (identical) |
| 124.18 | v3.2.02z / NMQAxe++ | `0x82037140` | `0x42037140` (different layout) |

Two boards on the **same firmware + same board model** have the **identical** `a0`;
the third (different board model → different link layout) differs. That is the
signature of a **specific, fixed function calling `strlen`**, not a random wild jump
(a wild jump would give a different `a0` every time). What is corrupt is the
**argument** `a2` (`0x8a4faeaa` / `0xbaaa8ad4` / `0x80000000` — all wild), and
`exc_vaddr ≈ a2` (differ only by the strlen word-alignment step), i.e. `strlen`
faulted while scanning *at* the bad pointer.

So the model is: a real function reads a `char*` field **out of a heap object that
has already been corrupted/freed**, and hands it to `strlen`. The register set is
fine (coredump saves it separately); the *object* the pointer was loaded from is not.

`btCorrupted=true` + depth 1 is still consistent: the IDF backtrace walker reads the
window-spill area on the stack, and that spill area can be clobbered even while the
live registers are clean. It does **not** imply the strlen caller's frame was smashed.

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
was being written at the moment of the panic — the corruption happened earlier.
`a0` is **not** garbage: window-stripped it is a valid DROM address, and it is
**identical across the two same-fw/same-model boards** (see the Forensic note). The
invariant is therefore "a fixed real caller + a wild `a2`", i.e. a corrupt pointer
loaded from a heap object — not a smashed frame.

## Uptime correlation (the strongest remaining lead)

Every recorded crash happened **after 65536 s (18.2 h) of uptime**:

| board | crash uptime |
|---|---|
| 124.10 | 69 954 s = 19.4 h |
| 124.18 | 70 416 s = 19.6 h |
| 124.12 | 74 953 s = 20.8 h |

65536 s is exactly one `uint16_t` second-counter wrap, so a 16-bit seconds field
is an attractive suspect. It was swept for and **not found**: no `uint16_t` is
assigned from `millis()`/uptime in the web or WS path (`mdns txid` uses only the
low 16 bits *by design*; `bm_time`/`stab_time` are config seconds; `uptime_session`
was `uint64_t` both before and after `713167d5`). So there is no mechanism at 65536 s.

**Better explanation — selection bias, not a threshold.** The crash is triggered by
*web interaction* (page load / reload → WS disconnect + reconnect + log flood).
Bench boards sit idle for long stretches and are visited by hand only occasionally,
so "uptime at crash" is really "time since boot until the next human visit". Three
events clustering at 19-21 h is consistent with **the operator's visit cadence**,
not with a counter wrap. With n = 3, the 18.2 h "threshold" is not evidence and
should not be used to steer the root-cause search. Note also that all the
short-uptime reproduction attempts below were *inherently* unable to hit a
long-uptime-only condition, so they neither confirm nor refute it.

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
