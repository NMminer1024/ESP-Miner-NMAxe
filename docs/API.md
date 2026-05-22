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
- [Coredump](#coredump)
- [Benchmark](#benchmark)

---

## System

| Method | Endpoint | Description |
|:------:|:---------|:------------|
| GET  | `/api/system/info`      | Dashboard summary: power, temperatures, ASIC status, mining stats, board identity |
| POST | `/api/system/restart`   | Trigger a graceful soft reboot |
| POST | `/api/system/clearhits` | Reset block-hit counter to zero (RAM + NVS) |
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
| `asic.freqReq` | int | MHz | ASIC target frequency |
| `asic.smallCoreCnt` | int | — | Total small-core count across all ASICs |

#### ⛏ Mining Stats — `miner`

| Field | Type | Unit | Description |
|-------|------|------|-------------|
| `miner.hashRate` | float | GH/s | 3-minute average hashrate |
| `miner.bestDiffEver` | string | — | All-time best share difficulty (e.g. `"1.23M"`) |
| `miner.bestDiffSession` | string | — | Session best share difficulty |
| `miner.networkDiff` | string | — | Current network difficulty |
| `miner.poolDiff` | string | — | Pool-assigned difficulty for this worker |
| `miner.lastDiff` | string | — | Difficulty of the most recently submitted share |
| `miner.blkhits` | int | — | Block-hit counter (valid shares meeting network difficulty) |
| `miner.sAccepted` | int | — | Cumulative accepted shares |
| `miner.sRejected` | int | — | Cumulative rejected shares |
| `miner.uptimeSeconds` | int | s | Session uptime (resets on reboot) |
| `miner.uptimeEver` | int | s | Total cumulative uptime across all sessions |
| `miner.freeHeap` | int | bytes | ESP32 free heap memory |
| `miner.minFreeHeap` | int | bytes | ESP32 minimum free heap since boot (watermark) |

#### 🪪 Board Identity — `identity`

| Field | Type | Description |
|-------|------|-------------|
| `identity.fwVersion` | string | Firmware version (e.g. `"v2.9.31"`) |
| `identity.hwModel` | string | Functional board ID — stable across hardware revisions (e.g. `"NMQAxe+"`) |
| `identity.displayName` | string | Human-readable variant label including revision (e.g. `"NMQAxe++Rev6.1"`) |
| `identity.hostName` | string | Device hostname |
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
    "pwd": "x"
  }
}
```

</details>

---

## Dashboard Data

| Method | Endpoint | Description |
|:------:|:---------|:------------|
| GET | `/api/dashboard/hr/dist`        | Hashrate distribution bar config and sample counts |
| GET | `/api/dashboard/gauge/limits`   | Min/max ranges for power, heat, and performance gauges |
| GET | `/api/dashboard/chart/history`  | Full miner status time-series (up to 2000 records, ~24 h) |
| GET | `/api/dashboard/chart/realtime` | Single latest data point for live chart update |
| GET | `/api/dashboard/luck/history`   | Block-proximity history for luck/difficulty chart |

---

## Settings

Each endpoint supports `GET` (read current values) and `PATCH` (save changes to NVS). All `PATCH` fields are optional.

### Network — `/api/setting/network`

**GET**
```json
{ "hostName": "NMAxe", "ssid": "MyWifi", "status": "connected", "ip": "192.168.1.100" }
```

**PATCH**
```json
{ "hostname": "...", "ssid": "...", "wifiPass": "..." }
```

### Time — `/api/setting/time`

**GET**
```json
{ "timeZone": "8.0", "timeFormat": 24, "dateFormat": "YYYY/MM/DD" }
```

**PATCH**
```json
{ "timezone": "8.0", "timeFormat": 24, "dateFormat": "YYYY/MM/DD" }
```

> `timeFormat`: `24` = 24-hour, `12` = 12-hour.

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

> `asicVcoreReq` is applied immediately (no reboot required). `asicFreqReq` takes effect after reboot.

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
    { "id": 0, "speed": 60, "rpm": 3600, "auto": 1, "target": 55.0 },
    { "id": 1, "speed": 80, "rpm": 4200, "auto": 1, "target": 85.0 }
  ]
}
```

> `fans[1]` (Vcore fan) is only present on NMQAxe++.

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
| GET  | `/api/update/progress`  | Poll upload/flash progress (0–100 %) |
| POST | `/api/update/firmware`  | Upload `firmware.bin` (multipart/form-data) |
| POST | `/api/update/spiffs`    | Upload `spiffs.bin` (multipart/form-data) |

> Legacy aliases (kept for compatibility): `/api/system/OTA`, `/api/system/OTAWWW`

**Progress response**
```json
{ "running": true, "progress": 45, "filename": "firmware.bin" }
```

---

## Swarm

| Method | Endpoint | Description |
|:------:|:---------|:------------|
| POST | `/api/swarm/scan` | Trigger an immediate neighbor IP scan |
| POST | `/api/swarm/find` | Locate a specific device by blinking its screen |

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
| `next_scan_in` | int | Seconds until the next automatic scan (0 if overdue or never scanned) |
| `ips` | array | List of all alive IP addresses on the subnet (including self) |

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
| POST   | `/api/benchmark/start`   | Start (or resume) benchmark sweep; device reboots |
| POST   | `/api/benchmark/stop`    | Stop benchmark and reboot into normal mining |
| DELETE | `/api/benchmark/results` | Clear results and reset all sweep config to board defaults |
| POST   | `/api/benchmark/reset`   | Stop sweep, clear results, reset state; reboots if running |
| POST   | `/api/benchmark/apply`   | Apply a specific freq+vcore as normal ASIC settings; reboots |

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
| `sampleIntv` | int | Minutes — sampling interval between hash-rate measurements |
| `bmTime` | int | Seconds — measurement window per step |
| `stabTime` | int | Milliseconds — stabilisation delay before each measurement |
| `curFreq` | int | MHz — current sweep position (frequency) |
| `curVcore` | int | mV — current sweep position (voltage) |
| `startTs` | int | Unix timestamp when the current sweep started |
| `totalSec` | int | Accumulated sweep time across all sessions (seconds) |
| `results` | array | Array of result objects stored in NVS |

### `PATCH /api/benchmark` / `POST /api/benchmark/start` — Request Body

All fields are optional. `POST /api/benchmark/start` also accepts `{"resume": true}` to continue from a paused sweep.

```json
{
  "freqMin": 400, "freqMax": 625, "freqStep": 25,
  "vcoreMin": 1000, "vcoreMax": 1250, "vcoreStep": 25,
  "sampleIntv": 5, "bmTime": 1000, "stabTime": 200
}
```

### `POST /api/benchmark/apply` — Request Body

```json
{ "freq": 575, "vcore": 1200 }
```

> Both fields are required. `vcore` is clamped to the board's safety limits. The device reboots immediately into normal mining with the new settings.
