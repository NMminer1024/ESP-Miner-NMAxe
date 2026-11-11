# AxeOS API Reference

Base URL: `http://{device-ip}`

All endpoints are served on **port 80**. Response bodies are JSON unless noted otherwise. `PATCH`/`POST` request bodies must use `Content-Type: application/json` (except multipart file uploads).

> 💡 An interactive version of this document is also available on-device at `http://{device-ip}/api-doc` after flashing.

---

## Table of Contents

- [System](#system)
- [Dashboard Data](#dashboard-data)
- [Settings](#settings)
- [OTA Update](#ota-update)
- [Swarm](#swarm)
- [Probe & Alive](#probe--alive-swarm-internal)
- [Theme](#theme)
- [Log & WebSocket](#log--websocket)
- [Reboot History](#reboot-history)
- [NVS Diagnostics](#nvs-diagnostics)
- [Coredump](#coredump)
- [Benchmark](#benchmark)

---

## System

| Method | Endpoint | Description |
|:------:|:---------|:------------|
| GET  | `/api/system/info`      | Dashboard summary: power, temperatures, ASIC status, mining stats, board identity |
| POST | `/api/system/restart`   | Trigger a graceful soft reboot |
| POST | `/api/system/clearhits` | Reset block-hit counter to zero (RAM + NVS) |
| PATCH | `/api/mining/state`    | Pause/resume mining at runtime; pause powers off ASIC Vcore |
| GET  | `/api/wakeup`           | Reset screensaver idle timer and wake the display |

### `GET /api/system/info` — Response Fields

The response is structured into nested sub-objects.

#### ⚡ Power — `power`

| Field | Type | Unit | Description |
|-------|------|------|-------------|
| `power.power` | float | W | Real-time power consumption (vbus × ibus) |
| `power.vbus` | int | mV | Input voltage |
| `power.ibus` | int | mA | Input current |

#### 🌡 Temperatures — `temps`

| Field | Type | Unit | Description |
|-------|------|------|-------------|
| `temps.vcore` | float | °C | VCORE regulator temperature |
| `temps.asic` | float | °C | ASIC die temperature |

#### 🔧 ASIC Status — `asic`

| Field | Type | Unit | Description |
|-------|------|------|-------------|
| `asic.count` | int | — | Number of active ASICs |
| `asic.model` | string | — | ASIC model name (e.g. `"BM1366"`) |
| `asic.vcoreReq` | int | mV | ASIC rated core voltage |
| `asic.vcoreReal` | int | mV | ASIC actual measured core voltage |
| `asic.freqReq` | int | MHz | ASIC active/requested target frequency; hot-applied when changed via mining settings |
| `asic.smallCoreCnt` | int | — | Total small-core count across all ASICs |

#### ⛏ Mining Stats — `miner`

| Field | Type | Unit | Description |
|-------|------|------|-------------|
| `miner.state` | string | — | Runtime mining state: `running`, `pausing`, `paused`, `resuming`, or `error` |
| `miner.paused` | bool | — | `true` while user pause/resume control is active |
| `miner.pauseReason` | string | — | Pause reason, currently `"user"` for API/UI-triggered pause |
| `miner.protection` | string | — | Latched hardware protection: `none`, `overcurrent`, or `overtemperature`. Independent of `paused` — a protection shutdown reports `paused: false` |
| `miner.hashRate` | float | GH/s | 3-minute average hashrate |
| `miner.bestDiffEver` | string | — | All-time best share difficulty (e.g. `"1.23M"`) |
| `miner.bestDiffSession` | string | — | Session best share difficulty |
| `miner.networkDiff` | string | — | Current network difficulty |
| `miner.poolDiff` | string | — | Pool-assigned difficulty for this worker |
| `miner.lastDiff` | string | — | Difficulty of the most recently submitted share |
| `miner.blkhits` | int | — | Block-hit counter (valid shares meeting network difficulty) |
| `miner.sAccepted` | int | — | Cumulative accepted shares |
| `miner.sRejected` | int | — | Cumulative rejected shares |
| `miner.uptimeSeconds` | int | s | Session uptime (resets on reboot). Derived from a monotonic clock (`esp_timer_get_time()`), not loop iterations |
| `miner.uptimeEver` | int | s | Total cumulative uptime across all sessions |
| `miner.bmMode` | int | — | Benchmark flag: `1` while a benchmark sweep is active, `0` in normal mining |
| `miner.freeHeap` | int | bytes | ESP32 free heap memory |
| `miner.minFreeHeap` | int | bytes | ESP32 minimum free heap since boot (watermark) |

#### 🪪 Board Identity — `identity`

| Field | Type | Description |
|-------|------|-------------|
| `identity.fwVersion` | string | Firmware version (e.g. `"v2.9.31"`) |
| `identity.hwModel` | string | Functional board ID — stable across hardware revisions (e.g. `"NMQAxe+"`) |
| `identity.displayName` | string | Human-readable variant label including revision (e.g. `"NMQAxe++Rev6.1"`) |
| `identity.hostName` | string | Device hostname |
| `identity.mac` | string | Factory station MAC, uppercase colon-separated hex (e.g. `"FC:9E:53:4A:8B:43"`). Read from eFuse, so it is present even when the radio is down. |
| `identity.ssid` | string | Connected Wi-Fi SSID |
| `identity.rssi` | int | Wi-Fi signal strength (dBm) |
| `identity.appSha256` | string | First 16 hex chars of the running app image SHA256. Pairs a device with the exact build artifact when triaging crash reports. |

#### 🌐 Stratum — `stratum`

Active pool connection — read-only snapshot.

| Field | Type | Description |
|-------|------|-------------|
| `stratum.url` | string | Pool URL (e.g. `"stratum+tcp://pool.example.com:3333"`) |
| `stratum.user` | string | Worker username |
| `stratum.pwd` | string | Worker password |

#### 🌀 Fans — `fans[]`

| Field | Type | Description |
|-------|------|-------------|
| `fans[].id` | int | Fan index (0-based) |
| `fans[].speed` | int | Speed percentage (0–100) |
| `fans[].rpm` | int | Actual RPM |

> `fans[]` length is the authoritative fan count for the board (1 on NMAxe/NMAxeGamma, 2 on NMQAxe++ variants — ASIC fan `id 0`, Vcore fan `id 1`). The chart endpoints mirror it as the `fanCount` column; the web UI reads this to decide how many fan curves to draw, so no board-specific logic is needed.

<details>
<summary>Full Example Response</summary>

```json
{
  "power": {
    "power": 12.5,
    "vbus": 5000,
    "ibus": 2500
  },
  "temps": {
    "vcore": 45.2,
    "asic": 62.7
  },
  "asic": {
    "count": 1,
    "model": "BM1370",
    "vcoreReq": 1200,
    "vcoreReal": 1198,
    "freqReq": 490,
    "smallCoreCnt": 672
  },
  "miner": {
    "state": "running",
    "paused": false,
    "pauseReason": "",
    "protection": "none",
    "bmMode": 0,
    "hashRate": 720.5,
    "bestDiffEver": "1.23M",
    "bestDiffSession": "456.7K",
    "networkDiff": "88.55T",
    "poolDiff": "65536",
    "lastDiff": "131072",
    "blkhits": 0,
    "freeHeap": 187320,
    "minFreeHeap": 163840,
    "sAccepted": 1024,
    "sRejected": 3,
    "uptimeSeconds": 86400,
    "uptimeEver": 2592000
  },
  "identity": {
    "fwVersion": "v2.9.31",
    "hwModel": "NMAxe",
    "displayName": "NMAxe",
    "hostName": "nmaxe-001",
    "ssid": "MyHomeWifi",
    "rssi": -55,
    "appSha256": "ef970b389312276b"
  },
  "fans": [
    { "id": 0, "speed": 60, "rpm": 2800 }
  ],
  "stratum": {
    "url": "stratum+tcp://pool.example.com:3333",
    "user": "bc1q...xyz.worker1",

### `PATCH /api/mining/state` — Pause/Resume Mining

Controls mining at runtime without rebooting the ESP32. Pausing is a user action: the firmware powers off ASIC Vcore, stops ASIC job TX/RX, clears ASIC job/hashrate caches, and suppresses low-power/low-hashrate/ASIC-alive restart monitors while paused. Resuming restores Vcore, reinitializes the ASIC, refreshes miner timeout timestamps, and applies a short recovery grace period before activity monitors resume.

**Request**
```json
{ "paused": true }
```

Set `paused` to `false` to start mining again:
```json
{ "paused": false }
```

**Response 200**
```json
{
  "status": "ok",
  "state": "pausing",
  "paused": true,
  "reason": "user",
  "protection": "none",
  "vcoreEnabled": false
}
```

`409 Conflict` is returned while Benchmark mode or OTA update is running. The pause state is runtime-only and is not saved to NVS; after reboot, normal mining starts as usual.

> **`protection` vs `paused`**: `paused` only reflects a user-requested pause. When the firmware shuts the rail down on overcurrent or overtemperature, `paused` stays `false` while `protection` is set — monitor `protection` (or the near-zero `power` reading) rather than inferring a shutdown from `paused` alone. The latch is cleared by a restart.
    "pwd": "x"
  }
}
```

</details>

---

## Dashboard Data

| Method | Endpoint | Description |
|:------:|:---------|:------------|
| GET | `/api/dashboard/topdiffs`       | Lifetime Top-10 best share difficulties (descending, NVS-persisted) |
| GET | `/api/dashboard/gauge/limits`   | Min/max ranges for power, heat, and performance gauges |
| GET | `/api/dashboard/chart/history`  | Full miner status time-series (up to 2000 records, ~24 h) |
| GET | `/api/dashboard/chart/realtime` | Single latest data point for live chart update |
| GET | `/api/dashboard/luck/history`   | Block-proximity history for luck/difficulty chart |

### Chart series schema — `labels[]` / `statistics[][]`

Both chart endpoints return position-aligned rows: `statistics[i][j]` is the value of column `labels[j]`. **Always resolve columns by label, never by hard-coded index** — the schema is append-only. Current fixed 18-column order (identical on both endpoints):

| idx | label | unit | notes |
|----:|:------|:-----|:------|
| 0 | `hashRate` | GH/s | |
| 1 | `shareRate` | n/s | |
| 2 | `asicTemp` | °C | |
| 3 | `vcoreTemp` | °C | |
| 4 | `Pbus` | W | |
| 5 | `Vbus` | V | |
| 6 | `Ibus` | A | |
| 7 | `Vcore` | mV | |
| 8 | `fanspeed` | % | ASIC fan (id 0) duty — kept for backward compatibility |
| 9 | `fanrpm` | RPM | ASIC fan (id 0) RPM — kept for backward compatibility |
| 10 | `wifiRSSI` | dBm | |
| 11 | `freeHeap` | KB | |
| 12 | `freePsram` | KB | |
| 13 | `latency` | ms | `0` = not measured yet |
| 14 | `epoch` | ms | |
| 15 | `fanCount` | — | fans this board has (1 or 2); drives how many fan curves to plot |
| 16 | `fan1speed` | % | Vcore fan (id 1) duty; `0` on single-fan boards |
| 17 | `fan1rpm` | RPM | Vcore fan (id 1) RPM; `0` on single-fan boards |

> The schema is fixed regardless of board: single-fan boards still send 18 columns with `fanCount=1` and the `fan1*` columns zeroed. Older firmware returns 15 columns — treat a missing `fanCount` as `1`.

---

## Settings

Each endpoint supports `GET` (read current values) and `PATCH` (save changes to NVS). All `PATCH` fields are optional.

### Network — `/api/setting/network`

**GET**
```json
{ "hostName": "NMAxe", "mdnsName": "nmaxepp81-06ea1", "ssid": "MyWifi", "status": "connected", "ip": "192.168.1.100" }
```

**PATCH**
```json
{ "hostname": "...", "mdnsName": "...", "ssid": "...", "wifiPass": "..." }
```

> `mdnsName`: mDNS `.local` name — the miner is reachable on the LAN as `http://<mdnsName>.local`. Lowercase `[a-z0-9-]` only, max 20 chars, no leading/trailing hyphen. Independent of `hostname` (which also sets the AP SSID). Default: `<board-mdns-base>-<5-hex-device-code>`, e.g. `nmaxepp81-06ea1`. Takes effect after reboot.

### Time — `/api/setting/time`

**GET**
```json
{ "timeZone": "8.0", "timeFormat": 24, "dateFormat": "YYYY/MM/DD" }
```

**PATCH**
```json
{ "timezone": "8.0", "timeFormat": 24, "dateFormat": "YYYY/MM/DD" }
```

> `timeFormat`: `24` = 24-hour, `12` = 12-hour. All three fields apply in real time after `PATCH` — no restart required. `timeFormat`/`dateFormat` are read live by the display render loop; a changed `timezone` (numeric UTC offset, e.g. `"8.0"`) is re-applied by the monitor thread (`setenv("TZ")` + `tzset()`) within ~1 second via `SYS_EVENT_TZ_CHANGED`, instead of waiting for the next hourly NTP sync.

### Mining — `/api/setting/mining`

**GET** — stratum config (primary / fallback / in-use), ASIC freq/vcore, and dropdown options
```json
{
  "vcoreReq": 1200, "freqReq": 550, "asic": "BM1366",
  "stratum": {
    "used":     { "url": "stratum+tcp://...", "user": "...", "pwd": "..." },
    "primary":  { "url": "...", "user": "...", "pwd": "..." },
    "fallback": { "url": "...", "user": "...", "pwd": "..." }
  },
  "overclock": { "options": [{ "name": "550MHz", "value": 550 }] },
  "vcore":     { "options": [{ "name": "1200mV", "value": 1200 }] }
}
```

**PATCH**
```json
{
  "stratum": {
    "primary":  { "url": "...", "user": "...", "pwd": "..." },
    "fallback": { "url": "...", "user": "...", "pwd": "..." }
  },
  "asicVcoreReq": 1200,
  "asicFreqReq": 550
}
```

> `asicVcoreReq` and `asicFreqReq` both take effect in real time after `PATCH /api/setting/mining`. Vcore is applied through the power regulator; frequency is hot-switched through the ASIC PLL. Both values are saved to NVS for the next boot. Values outside the selectable dropdown range (`overclock.options` / `vcore.options`) are clamped to that range before saving. Stratum pool changes are saved to NVS and applied hot: the stratum thread re-reads NVS within a few seconds and reconnects when the *active* pool (primary/fallback in use) changed; an update to the idle pool applies at the next failover. Malformed pool URLs are rejected with `400 {"error":"invalid pool url"}` before anything is written to NVS.
>
> **Storage failures are reported, not swallowed.** If a stratum value cannot be written to NVS the request fails and nothing else is applied (the ASIC is *not* re-tuned):
> ```json
> { "error": "nvs_write_failed", "code": 4357,
>   "detail": "ESP_ERR_NVS_NOT_ENOUGH_SPACE", "key": "stratumurl1",
>   "hint": "NVS is full or fragmented - clear benchmark results / top-10 or factory reset, then retry" }
> ```
> HTTP status `500`. Before giving up, the firmware reclaims unreachable benchmark result slots (rows no ring index points at — invisible in the UI but still occupying NVS) and retries once, so a full-but-littered partition does not block a pool change. Use [`GET /api/system/nvs`](#get-apisystemnvs--response-fields) to see what is actually stored.

### Market — `/api/setting/market`

**GET** — coin display settings and all available USDT pairs fetched from Binance
```json
{ "mainprice": "BTC", "coinWatchlist": "BTC,ETH,SOL", "pairs": ["BTCUSDT", "ETHUSDT"] }
```

**PATCH**
```json
{ "mainprice": "BTC", "coinWatchlist": "BTC,ETH,SOL" }
```

### Preference — `/api/setting/preference`

**GET**
```json
{
  "screenFlip": 0, "Brightness": 80, "screenAutoRoll": 1, "ledIndicator": 1,
  "screensaverEnable": 1, "screensaverTimeout": 300, "screensaverMode": 0,
  "hwModel": "NMAxe",
  "displayName": "NMAxe",
  "fans": [
    { "id": 0, "speed": 60, "rpm": 3600, "auto": 1, "target": 55.0, "maxTarget": 70.0 },
    { "id": 1, "speed": 80, "rpm": 4200, "auto": 1, "target": 85.0, "maxTarget": 125.0 }
  ]
}
```

> `fans[1]` (Vcore fan) is only present on NMQAxe++.
> `fans[].maxTarget` is the board-specific safe ceiling for the auto-mode target temperature: the over-temperature trip point minus a 5 °C margin. Values sent above it are clamped on save.

**PATCH**
```json
{
  "Brightness": 80, "screenFlip": 0,
  "ledIndicator": 1, "screenAutoRoll": 1,
  "screensaverEnable": 1, "screensaverTimeout": 300, "screensaverMode": 0,
  "fans": [
    { "id": 0, "auto": 1, "target": 55.0, "speed": 60 },
    { "id": 1, "auto": 1, "target": 85.0, "speed": 80 }
  ]
}
```

> `fans[].speed` takes effect only when `auto=0`. `fans[1]` (Vcore fan, `id=1`) is NMQAxe++ only.
> `fans[].target` is clamped to `[0, maxTarget]` before being stored, so an auto-fan target can never reach the over-temperature protection threshold.

> `screensaverMode`: `0` = animated GIF, `1` = black (backlight off while screensaver is active). Only effective when `screensaverEnable=1` and `screensaverTimeout > 0`.

### Screensaver — `POST /api/update/screensaver`

Upload a custom animated GIF as screensaver.

- Content-Type: `multipart/form-data`, field name: `screensaver`
- Only `.gif` files accepted (400 otherwise)
- Saved as `screen_saver_240x135.gif` (NMAxe / NMAxeGamma) or `screen_saver_320x240.gif` (NMQAxe++)

---

## OTA Update

| Method | Endpoint | Description |
|:------:|:---------|:------------|
| GET  | `/api/update/progress`    | Poll upload/flash progress (0–100 %) |
| GET  | `/api/update/last-result` | Snapshot of the latest OTA/upload outcome |
| POST | `/api/update/firmware`    | Upload `firmware.bin` (multipart/form-data) |
| POST | `/api/update/spiffs`      | Upload `spiffs.bin` (multipart/form-data) |

> Legacy aliases (kept for compatibility): `/api/system/OTA`, `/api/system/OTAWWW`

**Progress response**
```json
{ "running": true, "progress": 45, "filename": "firmware.bin" }
```

**Last-result response**
```json
{
  "valid": false, "success": false, "rebootPending": false,
  "httpStatus": 0, "bytes": 0, "tsMs": 0,
  "target": "firmware", "filename": "firmware.bin", "detail": "",
  "running": false, "progress": 0
}
```

> `target` is one of `firmware` / `spiffs` / `screensaver` / `unknown`. `rebootPending` is set after a successful firmware or SPIFFS flash. `valid` is `false` when no upload has ever completed.

---

## Swarm

| Method | Endpoint | Description |
|:------:|:---------|:------------|
| POST | `/api/swarm/scan` | Run an mDNS query now; start a background ICMP re-scan only if the last one is stale (5 min, or 60 min while mDNS fully covers the Axe peers). `?force=1` always starts a re-scan. Never clears the `/alive` list |
| POST | `/api/swarm/find` | Locate a specific device by blinking its screen |
| GET | `/api/swarm/summary` | Firmware-aggregated swarm totals (the same `SwarmState` the LCD shows): `workers`, `hr` (H/s), `sbd`, `ebd` |

### `POST /api/swarm/find`

Sets the `FIND_NEIGHBOR` event on the target device, causing its display to flash. The user can dismiss the effect by tapping the screen or pressing the Boot button. Available in both normal and recovery mode.

**Request body:** empty or `{}`

**Response 200**
```json
{ "status": "ok" }
```

---

## Probe & Alive (Swarm Internal)

| Method | Endpoint | Description |
|:------:|:---------|:------------|
| GET | `/probe` | Lightweight probe — returns model, hostname, firmware version, hashrate, best difficulty and uptime |
| GET | `/alive` | Returns this device's own IP and all known alive neighbor IPs |

### `GET /probe` — Response Fields

| Field | Type | Description |
|-------|------|-------------|
| `model` | string | Board model name (e.g. `"NMAxe"`) |
| `hostname` | string | Device hostname |
| `ver` | string | Firmware version |
| `sw` | int | TFT screen width in pixels |
| `sh` | int | TFT screen height in pixels |
| `hr` | float | 3-minute average hashrate (H/s) |
| `sbd` | float | Best session difficulty |
| `ebd` | float | Best ever difficulty |
| `ut` | int | Session uptime (seconds) |

### `GET /alive` — Response Fields

| Field | Type | Description |
|-------|------|-------------|
| `self` | string | This device's own IP address |
| `scanning` | bool | `true` while a subnet scan is in progress |
| `progress` | int | Current scan progress (0–254 hosts probed so far) |
| `total` | int | Total hosts to probe (always 254 for a /24 subnet) |
| `next_scan_in` | int | Seconds until the next automatic ICMP scan, taken from the scan thread's schedule (5 min, or 60 min while mDNS is healthy; 0 while scanning) |
| `mdns_active` | bool | `true` when mDNS discovery is fresh (< 25 s old); UI shows live auto-discovery instead of the scan-progress ring |
| `mdns_next_in` | int | Seconds until the next mDNS query (10 s cycle) |
| `ips` | array | Self plus the de-duplicated union of ICMP-alive hosts and mDNS peers seen within the last 60 s |

---

## Theme

| Method | Endpoint | Description |
|:------:|:---------|:------------|
| GET  | `/api/theme` | Current color scheme name and accent color map |
| POST | `/api/theme` | Save `colorScheme`, `theme` name and `accentColors` to NVS |

---

## Log & WebSocket

| Method | Endpoint | Description |
|:------:|:---------|:------------|
| GET | `/api/log`       | Trigger probe only — real logs are pushed over WebSocket |
| WS  | `ws://{ip}/ws`   | Persistent WebSocket — every `LOG_*` line is broadcast as a UTF-8 text frame. Outgoing text from clients is echoed back. |

---

## Reboot History

| Method | Endpoint | Description |
|:------:|:---------|:------------|
| GET    | `/api/reboot/last` | Newest reboot record only (for dashboard banner) |
| GET    | `/api/reboot/list` | Up to 10 reboot records, newest first |
| DELETE | `/api/reboot/list` | Wipe stored reboot history (boot index is preserved) |

### Reboot Record Fields

| Field | Type | Description |
|-------|------|-------------|
| `idx` | int | Monotonic boot index (increments each boot, survives resets) |
| `ts` | int | Unix timestamp at the time of reboot (0 if RTC not synced) |
| `uptime` | int | Uptime in seconds of the previous session |
| `heapMin` | int | Minimum free internal heap (bytes) during the previous session |
| `reset` | string | ESP32 reset reason (e.g. `"panic"`, `"watchdog"`, `"power_on"`) |
| `intent` | string | Software reboot intent (e.g. `"ota_finished"`, `"user_web"`, `"none"`) |
| `class` | string | Reboot class: `"cold"`, `"normal"`, `"crash"`, `"ota"` |
| `fw` | string | Firmware version that was running when the reboot occurred |
| `detail` | string | Additional detail string (e.g. panic reason, uploaded filename) |

**`GET /api/reboot/list` example response**
```json
[
  {
    "idx": 142, "ts": 1716000000, "uptime": 11520, "heapMin": 49152,
    "reset": "panic", "intent": "none", "class": "crash",
    "fw": "v3.0.11", "detail": "panic"
  }
]
```

> `GET /api/reboot/last` returns a single object (not an array). If no history exists it returns `{"class":"cold","intent":"none"}`.

---

## NVS Diagnostics

| Method | Endpoint | Description |
|:------:|:---------|:------------|
| GET    | `/api/system/nvs` | Read-only inventory of the `nvs` partition (occupancy + per-key sizes) |

Read-only. Touches nothing but flash reads; safe to call at any time.

### `GET /api/system/nvs` — Response Fields

| Field | Description |
|-------|-------------|
| `partition.size_bytes` | NVS partition size read **from the partition table** (`24576` on a current build). A smaller value means the board was flashed with an older partition table — an OTA updates the app only, never the partition table. |
| `partition.total_entries` | Total 32-byte NVS entries (6 × 126 = 756 for a 24 KB partition) |
| `partition.used_entries` / `free_entries` | Live / free entries as reported by `nvs_get_stats()` |
| `partition.namespaces` | Number of namespaces present |
| `entries[]` | `{ns, key, type, bytes}` for every stored key (`type` = `u8`/`u16`/`u32`/`u64`/`str`/`blob`) |
| `namespaces[]` | `{ns, keys, bytes}` — key count and payload bytes per namespace |
| `largest[]` | The 10 biggest values, descending (these are what block page reclaim) |

```json
{
  "partition": { "ok": true, "size_bytes": 24576, "total_entries": 756,
                 "used_entries": 214, "free_entries": 542, "namespaces": 5 },
  "entries": [ { "ns": "main", "key": "stratumurl1", "type": "str", "bytes": 36 } ],
  "namespaces": [ { "ns": "main", "keys": 25, "bytes": 757 } ],
  "largest": [ { "ns": "phy", "key": "cal_data", "bytes": 1904 } ],
  "entry_bytes": 32, "page_bytes": 4096
}
```

> **Why payload bytes ≠ partition usage.** NVS is log-structured: a string of N bytes costs one 32-byte item header plus `ceil((N+1)/28)` 32-byte chunks, and reclaiming space requires an entire 4 KB page to be evacuated first. So `free_entries > 0` does **not** guarantee a write will succeed, and deletes themselves need a free entry (for the tombstone). A device reporting `ESP_ERR_NVS_NOT_ENOUGH_SPACE` with a seemingly empty benchmark table is usually holding **unreachable** `bm_r_*` rows — result slots outside the `bm_r_start`/`bm_r_cnt` window, invisible to `GET /api/benchmark`. `DELETE /api/benchmark/results` removes them; the firmware also reclaims them automatically at boot and when a pool-config write hits a full partition.
>
> **Write amplification.** Because every overwrite appends a new entry, a periodic save that rewrites unchanged values burns the partition for nothing. The lifetime counters are therefore persisted only when the value actually differs (they are read back and compared first): a quiet miner now costs ~1–2 entries per hour instead of ~51, and the Top-10 connection snapshot collapses to a single `@pool|user` pair when all ranks share one pool and wallet.

---

## Coredump

| Method | Endpoint | Description |
|:------:|:---------|:------------|
| GET    | `/api/coredump/info` | Coredump presence and summary metadata |
| DELETE | `/api/coredump`      | Erase the coredump partition (irreversible) |

### `GET /api/coredump/info` — Response Fields

| Field | Type | Description |
|-------|------|-------------|
| `present` | bool | `true` if a coredump exists in flash |
| `size` | int | Coredump size in bytes (only when `present=true`) |
| `crcOk` | bool | `false` if the dump may be truncated or corrupted |
| `task` | string | Name of the crashing task (if parseable) |
| `pc` | int | Program counter at crash (hex address as integer) |
| `bt` | array | Backtrace addresses (array of integers) |
| `btCorrupted` | bool | `true` if the backtrace could not be fully decoded |
| `appSha256` | string | First 16 hex chars of the firmware SHA256 at crash time |

```json
{ "present": true, "size": 12480, "crcOk": true,
  "task": "miner", "pc": 1075871436,
  "bt": [1075871436, 1075869312], "btCorrupted": false,
  "appSha256": "ef970b389312276b" }
```

> When no coredump is present: `{ "present": false }`.

---

## Benchmark

| Method | Endpoint | Description |
|:------:|:---------|:------------|
| GET    | `/api/benchmark`         | Config, sweep state and stored results |
| PATCH  | `/api/benchmark`         | Update sweep parameters (does NOT start) |
| POST   | `/api/benchmark/start`   | Start (or resume) benchmark sweep — runs in place, no reboot |
| POST   | `/api/benchmark/stop`    | Stop benchmark; sweep aborts and the Normal-mode point is restored, no reboot |
| DELETE | `/api/benchmark/results` | Clear results and reset all sweep config to board defaults |
| DELETE | `/api/benchmark/results?freq=N` | Delete only that frequency's result row; sweep config and other rows untouched. `400` invalid freq (`0`), `404` no result for that frequency, `500` NVS delete failed |
| POST   | `/api/benchmark/reset`   | Stop sweep and reset state/config to defaults (results preserved); no reboot |
| POST   | `/api/benchmark/apply`   | Apply a benchmark result as normal ASIC settings; always hot-switches freq+vcore immediately (a running sweep aborts in place), never reboots |

### `GET /api/benchmark` — Response Fields

| Field | Type | Description |
|-------|------|-------------|
| `mode` | int | `0` = normal mining, `1` = benchmark sweep active |
| `freqMin` | int | MHz — sweep start frequency |
| `freqMax` | int | MHz — sweep end frequency |
| `freqStep` | int | MHz — frequency increment per step |
| `vcoreMin` | int | mV — sweep start core voltage |
| `vcoreMax` | int | mV — sweep end core voltage |
| `vcoreStep` | int | mV — voltage increment per step |
| `sampleIntv` | int | Seconds — sampling interval between hash-rate measurements |
| `bmTime` | int | Seconds — measurement window per step |
| `stabTime` | int | Seconds — stabilisation delay before each measurement |
| `curFreq` | int | MHz — current sweep position (frequency) |
| `curVcore` | int | mV — current sweep position (voltage) |
| `startTs` | int | Unix timestamp when the current sweep started |
| `totalSec` | int | Accumulated sweep time across all sessions (seconds) |
| `results` | array | Array of result objects stored in NVS (see below) |

#### Result row fields (`results[]`)

Results are keyed by **frequency alone**: the latest run at a frequency overwrites the previous row, and a FAILED row (`fail:1`) invalidates any earlier "qualified" row for the same frequency. Temperature fields are `null` when that sensor never reported (never `0` or `NaN`).

| Field | Type | Description |
|-------|------|-------------|
| `freq` | int | MHz — the swept frequency (row key) |
| `vcore` | int | mV — the voltage that PASSED at this frequency (lower attempts that went unstable are not stored) |
| `expHR` | float | GH/s — expected hashrate from freq × small-core count |
| `avgHR` | float | GH/s — measured 3-min-average hashrate (`0` on a FAILED row) |
| `avgAsicTemp` / `avgVcoreTemp` | float\|null | °C — average temperatures, `null` = never measured |
| `effJTH` | float | J/GH — efficiency (`0` on a FAILED row) |
| `avgPwr` | float | W — average input power (vbus × ibus) |
| `maxIout` | float | A — peak input current during sampling |
| `maxPwr` | float | W — peak input power during sampling |
| `maxAsicTemp` / `maxVcoreTemp` | float\|null | °C — peak temperatures, `null` = never measured |
| `maxPhaseIout` | float | A — peak current of the HEAVIEST VRM phase (the per-phase OC margin is judged on this) |
| `phases` | int | Runtime-detected phase count (`0` on boards without per-phase telemetry) |
| `maxIbus` | float | A — peak input current (the adapter current budget is judged on this) |
| `maxPwrIn` | float | W — peak input power (the adapter watt budget is judged on this) |
| `vFail` | int\|absent | mV — **highest** voltage that came in UNSTABLE at this frequency. Absent when the first voltage tried already passed, i.e. the floor was never probed |
| `vFailPct` | float\|absent | % of expected hashrate that `vFail` reached (e.g. `97.85`) — only the failing round measured it, so it is stored rather than derived from `avgHR`/`expHR` |
| `fanAsic` | int\|absent | % — peak ASIC-fan duty while sampling; `100` on fixed-speed boards; absent when no such fan or on older results |
| `fanVcore` | int\|absent | % — peak VRM-fan duty; two-fan models only |
| `fw` | string\|absent | Firmware build that measured this row (results outlive an upgrade; verdicts are not comparable across builds). Absent on older results |
| `q` | int\|absent | Qualification: `1` = passed the OC/OT/adapter safety margin, `0` = a peak was too close to a limit. **Absent on results written before the margin rule existed — treat those as unverified, never as passing** |
| `fail` | int | `1` = protection tripped at this point (tombstone); invalidates earlier same-freq results |
| `soak` | int\|absent | `1` = the point also passed the post-sweep soak under the real fan policy and is safe to auto-apply. Absent until soaked |
| `fanNeed` | int\|absent | % — ASIC-fan duty the point settled at during the soak (present with `soak`) |
| `fanNeedVcore` | int\|absent | % — VRM-fan duty at soak settle; omitted on single-fan boards |
| `ts` | int | Unix timestamp when the row was written |

### `PATCH /api/benchmark` / `POST /api/benchmark/start` — Request Body

All fields are optional. `POST /api/benchmark/start` also accepts `{"resume": true}` to continue from a paused sweep.

```json
{
  "freqMin": 400, "freqMax": 625, "freqStep": 25,
  "vcoreMin": 1000, "vcoreMax": 1250, "vcoreStep": 25,
  "sampleIntv": 2, "bmTime": 120, "stabTime": 30
}
```

> `POST /api/benchmark/start` returns **`409`** `{"error":"an OTA update is in progress - benchmark cannot start until it finishes"}` while an OTA upload is running — a sweep started then would be killed mid-round and could race the flash erase.

### `POST /api/benchmark/apply` — Request Body

```json
{ "freq": 575, "vcore": 1200 }
```

> Both fields are required. `vcore` is clamped to the board's user-facing safety range (`min_vcore` … vcore-list max) and `freq` to the selectable overclock range before anything is saved.
>
> The point is always applied without a reboot, with a direction-dependent switch order so the ASIC never hashes at a frequency the rail cannot support: on a **frequency down-shift** the PLL steps down first (awaited), then the rail is lowered; on an **up-shift** the rail is raised first, then the PLL. If a benchmark sweep is still running (`bm_mode == 1`), it aborts in place and its sweep position is kept for a later resume. Response is always `{"status":"ok","reboot":false}`.
>
> This endpoint is preferred over `PATCH /api/setting/mining` when applying a result from the benchmark table, as it also clears the `bm_mode` flag.
