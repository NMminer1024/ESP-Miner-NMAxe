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

#### 🪪 Board Identity — `identity`

| Field | Type | Description |
|-------|------|-------------|
| `identity.fwVersion` | string | Firmware version (e.g. `"v2.9.31"`) |
| `identity.hwModel` | string | Hardware model (e.g. `"NMAxe"`) |
| `identity.hostName` | string | Device hostname |
| `identity.ssid` | string | Connected Wi-Fi SSID |
| `identity.rssi` | int | Wi-Fi signal strength (dBm) |

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
    "sAccepted": 1024,
    "sRejected": 3,
    "uptimeSeconds": 86400,
    "uptimeEver": 2592000
  },
  "identity": {
    "fwVersion": "v2.9.31",
    "hwModel": "NMAxe",
    "hostName": "nmaxe-001",
    "ssid": "MyHomeWifi",
    "rssi": -55
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
  "screensaverEnable": 1, "screensaverTimeout": 300,
  "hwModel": "NMAxe",
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
  "screensaverEnable": 1, "screensaverTimeout": 300,
  "fans": [
    { "id": 0, "auto": 1, "target": 55.0, "speed": 60 },
    { "id": 1, "auto": 1, "target": 85.0, "speed": 80 }
  ]
}
```

> `fans[].speed` takes effect only when `auto=0`. `fans[1]` (Vcore fan, `id=1`) is NMQAxe++ only.

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
