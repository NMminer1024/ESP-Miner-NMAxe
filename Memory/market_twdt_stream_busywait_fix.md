# Market task TWDT reset — ArduinoJson stream-parse busy-waits on core 0 (v3.2.02d -> v3.2.02e)

## Symptom
- NMQAxe++ Nexus @192.168.124.2 (v3.2.02d) rebooting via the **5 s task watchdog**
  while the market feature is active. Artifacts: `tmp/market_twdt_fix_20261113-135127/`
  (`*_before.json`, `ws_192_168_124_2.log`).

## Root cause
`get_coin_ticker_24hr()` and `refresh_watchlist()` parsed straight off the socket:

```cpp
deserializeJson(doc, http.getStream());   // ArduinoJson Stream reader
```

ArduinoJson's Stream reader goes through `Stream::timedRead()`, which in the
ESP32 Arduino core is a **pure busy-wait with no yield/delay**:

```cpp
int Stream::timedRead() {
    int c; _startMillis = millis();
    do { c = read(); if (c >= 0) return c; }
    while (millis() - _startMillis < _timeout);
    return -1;
}
```

While the next TCP segment is in flight, the `(market)` task spins at full speed.
It is pinned to **core 0** (`application.cpp` `_create_task(..., 0)`), so it starves
`idle0` and the task watchdog resets the board. Whether a given fetch trips it
depends purely on how Binance/the network segments the response — hence sporadic.

`fetch_available_usdt_pairs()` was never affected: it already reads in 128-byte
chunks with `delay(1)` when the socket is empty and every 512 bytes read.

## Fix (v3.2.02e)
- New `read_http_body()` in `src/market/market.cpp`: polls `available()`, sleeps
  `delay(1)` while empty, hard deadline `MARKET_READ_TIMEOUT_MS` (4 s < TWDT 5 s),
  into a **PSRAM** buffer (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`) capped at
  `MARKET_BODY_MAX_BYTES`.
- Both parse sites now: read full body -> `http.end()` -> `deserializeJson(doc,
  body, body_len)` from RAM -> `heap_caps_free()`. The parser never touches the
  socket again, so the busy-wait window is gone.
- **Cap sizing trap:** the first cut used 8 KB. Measured real bodies: 1 symbol of
  `/api/v3/ticker/24hr` ≈ 559 B, the default 8-coin watchlist ≈ 4.3 KB — but the
  settings UI allows up to **50** watchlist coins ≈ 27 KB. An 8 KB cap would have
  made large watchlists fail every cycle. Cap is now **32 KB** (PSRAM is plentiful:
  `heapMin` ≈ 8 MB free).

## Verification (Nexus @192.168.124.2, fw v3.2.02e, sha `dbacb1dbd51f3911`)
- All 5 envs build clean (NMAxe / NMAxeGamma / NMQAxePP / NMQAxePPRev61 / NMQAxePPNexus).
- Soak harness in the fix folder: `soak_monitor.py` (1/min: uptime, reboot/last,
  coredump/present -> `soak_124_2.jsonl`) + `ws_listen.py` (pure-python /ws client,
  full capture + filtered market/watchdog lines).
- Result: **PASS.** Two 60-min monitor windows (2026-11-13 14:22-16:21) covering
  ~111 min of runtime / ~220 market refresh cycles:
  - uptime monotonic (576 -> 3584, then 3533 -> 3654 after the event below);
    zero `task_wdt` / crash-class reboots; `coredump/present` stayed false.
  - WS capture (6.4k+ lines): zero panic / Guru Meditation / backtrace / wdt
    lines, zero market/Binance warnings or errors (a failing body read would
    LOG_W/LOG_E every 30 s).
  - Positive path check: post-reboot boot log shows "Available USDT pairs on
    Binance: 776" (the unchanged fetch path) and the board resumed normal mining.
  - One mid-soak reboot at 15:13 was `reset=sw, intent=pool_timeout,
    class=planned` (stratum watchdog: "no stratum traffic for 300000ms") — the
    firmware's designed pool-dead path, unrelated to this fix; v3.2.02d history
    shows the same planned reboot kinds. NOTE: shares were still being accepted
    seconds before that reboot, so the pool_timeout traffic criterion looks
    suspect — filed as a separate open question, not addressed here.
  - Soak flag lines in `soak_124_2.jsonl` are all artifacts of that outage window
    (unreachable -> fw=null) or of the planned reboot; none are watchdog/crash.

## Reusable lessons
- Any `deserializeJson(doc, stream)` (or `readBytes`/`timedRead` loop) in a task
  pinned to a core is a TWDT hazard: `Stream::timedRead()` never yields. Read with
  `available()` + `delay(1)`, then parse from RAM.
- Version string alone is ambiguous during a fix iteration — match boards to the
  working tree with `identity.appSha256` (first 8 B of `esp_app_desc.app_elf_sha256`,
  extractable from `firmware.bin` at `esp_app_desc` magic `0xABCD5432` + 144).
- Size "bounded" buffers against the *config maximum*, not the default: the
  watchlist allows 50 entries while the default is 8.
