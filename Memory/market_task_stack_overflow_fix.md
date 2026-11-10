# Market task crash fix (v3.2.02u -> v3.2.02x)

## Symptom
- Customer report + self-test: NMAxe Gamma repeatedly coredumps, crash task `(market)`;
  crashes stop completely once the Watchlist is emptied.
- Coredump (v3.2.02u, sha fd6dfc96b1c3c507): pc 0x4037ac5e, btCorrupted=true; all 6
  backtrace frames land in panic/coredump machinery (panic_abort / esp_system_abort /
  abort / esp_core_dump_get_stack) with zero application frames -> the stack was already
  corrupted at panic time, the classic signature of a stack overflow.

## Root cause (two stacked defects)
1. **Stack overflow (primary)**: the `(market)` task had only a 4 KB stack. Measured with
   `-fstack-usage`: `refresh_watchlist` 672 B / `get_coin_ticker_24hr` 384 B /
   `log_emit` 640 B (msg_buffer[512]); on top of that the HTTPClient/WiFiClient connect
   chain, ArduinoJson Filter recursive deserialization, and IDF interrupt frames ->
   overflow. With an empty watchlist `refresh_watchlist` returns on its first line, so the
   deep path never runs -> explains "clearing the watchlist fixes it".
2. **Unlocked concurrent access (secondary)**: the UI iterated `get_sorted_watchlist()`
   references every second while the market task cleared/rebuilt the same containers every
   30 s -> dangling-read hazard (would crash in the LVGL task, not the direct cause of
   this dump).

## Fix (implemented)
- `src/market/market.h/.cpp`: added `_wl_mutex`; refresh_watchlist builds the snapshot
  outside the lock, `publish_watchlist()` performs only two moves inside it; UI reads via
  `watchlist_size()` + `copy_watchlist_window(offset,count,WatchlistRow*)` (POD rows);
  removed the public `get_watchlist_pairs/get_sorted_watchlist/sort_watchlist_by_price` API.
- `StaticJsonDocument<128> filter` made static (one less stack frame).
- `src/app/application.cpp`: `(market)` task stack 4 KB -> 8 KB.

## Verification (closed loop on Gamma @192.168.124.10)
- Temporary `uxTaskGetStackHighWaterMark` probe: 5628 B still free after the deepest path
  (under the old 4 KB config this was the overflow point).
- Non-empty watchlist (8 coins) soak: zero crash markers, no new coredump,
  reboot/last = planned only.
- NMAxeGamma / NMAxe / NMQAxePP all build clean; v3.2.02x in multi-day soak (user side).

## Reusable lessons
- Gamma is an **ESP32-S3**: addr2line must use `xtensa-esp32s3-elf-*`; the esp32 toolchain
  yields wrong symbols.
- A coredump whose backtrace contains only panic-machinery frames with
  btCorrupted=true -> suspect stack overflow first, do not try to localize from the BT.
- `-fstack-usage` targeted compiles: xtensa GCC 8.4 does not support @response files
  (quotes/size both fail); `$env:PLATFORMIO_BUILD_FLAGS="-fstack-usage"` through a full
  PlatformIO build is the simplest path.
- The `v3.2.02u` version string has been unchanged since ad25c58f and market code had zero
  commits in between -> old dumps can be symbolized with the current ELF directly.
