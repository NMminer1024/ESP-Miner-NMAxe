# Monitor chart: one curve per physical fan (v3.2.02z)

## What changed
`history_node_t` no longer stores a single `fanspeed`/`fanrpm` pair. It stores
`fan_count` plus `fan_speed[2]` / `fan_rpm[2]` (`MINER_HISTORY_MAX_FANS = 2`:
index 0 = ASIC fan, index 1 = Vcore fan). The chart API appends three columns so
clients can plot one curve per fan the board actually has, while the web UI stays
one universal SPIFFS image across every board.

## Chart schema is APPEND-ONLY, 18 columns
`/api/dashboard/chart/history` and `/api/dashboard/chart/realtime` share one order:

```
0 hashRate   1 shareRate  2 asicTemp  3 vcoreTemp  4 Pbus   5 Vbus
6 Ibus       7 Vcore      8 fanspeed  9 fanrpm    10 wifiRSSI
11 freeHeap 12 freePsram 13 latency  14 epoch
15 fanCount 16 fan1speed 17 fan1rpm        <- appended, never reorder 0-14
```

- `epoch` is fixed at index 14 — it is **not** the last element any more.
- Single-fan boards still send all 18 columns with `fanCount=1` and `fan1*` = 0.
- Firmware older than this schema returns 15 columns; treat a missing `fanCount`
  as `1` (that is what the web UI does).

## Files that must stay in sync when the schema changes
1. `src/mining/miner.h` — struct + the column-order comment above it.
2. `src/thread/system_threads.cpp` — history sampling loop (samples every fan).
3. `src/web/http_server.cpp` — **both** `get_status_history` and
   `get_status_realtime`: `labels`, `dataPoint` order, and JSON capacity
   (`per_sample_size` 256->320, realtime `json_size_max` 1024->1536).
4. `docs/API.md` — "Chart series schema" table + the `fans[]` note.
5. `src/web/axe-os/src/assets/api-doc.html` — the API docs page shipped inside
   SPIFFS (two `labels` lists, two example rows, one explainer paragraph).
   Easy to forget, and it only takes effect after a **spiffs** OTA.
6. `monitor.component.ts` — resolves columns by label (`columnIndexMap`), so an
   append-only change needs no edit here.
7. `monitor-chart.component.ts` — its legacy fallback used to assume "last
   element = epoch". Now uses `dataPoint[14]` with a `length >= 15` guard.
8. `home.component.ts` — `latency$` hard-codes index 13; still valid only
   because the schema is append-only.

## UI decisions (approved on hardware)
- One y-axis only. A second (RPM) axis was tried and rejected.
- ASIC fan = solid indigo `#3F51B5` (speed) / cyan `#00BCD4` (RPM).
  Vcore fan = dashed pink `#E91E63` (speed) / orange `#FF9800` (RPM).
  Near-neighbour hues of the ASIC colour were rejected as unreadable.
- Legend is hidden on single-fan boards (`fanCount == 1`) and shows only the
  fan curves — the `Avg` lines are filtered out of the legend but still appear
  as a column in the tooltip.
- Tooltip label column is 16 characters; 10 truncated "ASIC Fan Speed".

## Verification (closed loop)
| board | fw sha (16) | fanCount | result |
|---|---|---|---|
| 192.168.124.18 NMQAxe++ | `1869f77a3ccc3faf` | 2 | 4 curves, 2 legend entries (no Avg) |
| 192.168.124.10 NMAxeGamma | `e2f9a325a13cd5a7` | 1 | ASIC-only curves, legend hidden |

Both rebooted as `planned / ota_finished`. No NVS keys were added or changed.

## Reminder
A UI change needs its own **spiffs** OTA (`POST /api/update/spiffs`, multipart
field `update`, filename exactly `spiffs.bin`); the firmware OTA does not touch
the web partition.
