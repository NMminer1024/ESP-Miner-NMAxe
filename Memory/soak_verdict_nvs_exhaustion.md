# Soak verdict lost silently, and the point applied anyway — FIXED

Status: **fixed** (v3.2.02j / v3.2.02k). Field report: a sweep finished with
775 MHz / 1220 mV passing the soak and being applied
("Best qualified result: freq=775MHz ... applying"), yet its row carried no
`soak:1` / `fanNeed` / `fanNeedVcore` and the log had no
`marked as soak-verified` line.

## Root cause: plain NVS exhaustion, not a write-path bug

Reproduced on Nexus by sweeping one point with the partition nearly full
(131 free entries, 27 rows). The mechanism was not a silent code path — it was:

```
[BM] freq=785 passed the soak but its verdict could NOT be stored
     (ESP_ERR_NVS_NOT_ENOUGH_SPACE, 341 bytes)
```

A verdict adds ~45 B to a ~340 B row, i.e. 12 entries become 14, and an
in-place rewrite needs room for the OLD and NEW entry at once. On a 24 KB
partition holding 27 rows there is no such room, so the write fails — and the
old code returned that error without logging anything while `bm_finish()`
applied the row regardless.

**The 24 KB partition cannot hold the table it is asked to hold.** See
`docs/API.md` ("What a benchmark row actually costs") for the arithmetic:
`(756 − 278) / 14 ≈ 34` soaked rows on a configured board.

## What was wrong in the code (three gaps)

1. `bm_result_mark_soak()` returned the `esp_err_t` and `bm_soak_run()`
   discarded it.
2. `bm_result_mark_soak()` logged nothing on failure.
3. `bm_finish()` applied the highest-hashrate qualified row without asking
   whether it had ever been verified.

## Fix (two stages, both verified on hardware)

**Stage 1 — be honest.** Log the failure, and require the verdict before an
automatic apply. If the best row has none, fall back to the best *soaked* row;
only if none exists keep the pre-sweep settings.

**Stage 2 — make room.** Losing the verdict still throws away a whole sweep, and
the rows blocking it are usually worthless (on Nexus they were from an older
build). So on `ESP_ERR_NVS_NOT_ENOUGH_SPACE`, evict the least useful OTHER row
and retry, bounded to two attempts:

```
[BM] NVS full: dropped 525MHz (row from another firmware build) to make room
     for the soak verdict on freq=785.
[BM] NVS full: dropped 535MHz (row from another firmware build) to make room
     for the soak verdict on freq=785.
[BM] freq=785 marked as soak-verified (fanNeed=83% ASIC / 100% VRM)
[BM] Best qualified result: freq=785MHz vcore=1210mV avgHR=10717.5GH/s — applying.
```

Resulting state: `soak=1 / fanNeed=83 / fanNeedVcore=100` on the 785 row, rows
27 → 25, free entries 131 → 154, hash rate resumed at 10.5 TH/s.

### Two guards that only matter on this path

Both are parameters on `bm_r_pick_victim()`, **not** post-hoc filtering of its
result — skipping the winner afterwards returns the second-best of one class
while a cheaper class still had candidates.

- **exclude the row being upgraded** — a full partition must never "make room"
  by deleting the measurement it is trying to preserve;
- **protect rows that already carry a verdict** — a soak is 2 minutes of
  measurement and the one thing a later sweep cannot cheaply recreate, so it
  ranks below even the oldest plain row.

`bm_r_pick_victim()` can now return `BM_R_NO_VICTIM` (0xFFFF) meaning "nothing
may be evicted"; the append path turns that into a loud failure instead of
indexing with 0xFFFF.

## Deployment note: a new version invalidates every row's `fw` tag

The victim ranking treats "row from another firmware build" as cheap to lose.
Bumping `BOARD_CURRENT_FW_VERSION` therefore makes **every existing row
evictable** — which is what made the fix work immediately here (525/535 were
dropped first), but it also means an upgrade can legitimately discard the whole
table under pressure. That is intended (a verdict from different code cannot be
trusted) but worth knowing before flashing a new version onto a nearly full
partition.

## Testing notes

- `BM_TEST_FORCE_SOAK_MARK_FAIL` forces the failure path on demand; nothing in
  `platformio.ini` sets it.
- To reproduce a full partition naturally: sweep a single point on a board whose
  `free_entries` is under ~150.
- Watch `GET /api/system/nvs` for `free_entries`; `free_entries / 14` is the
  approximate number of further soaked rows.
