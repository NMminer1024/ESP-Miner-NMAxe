# Fan takeover cushion: what it is for, and the one condition that never fires

Context: `src/thread/hw_threads.cpp` `drive_fan()`. Applies to every fan, and to
two edges — benchmark/manual override → auto, and manual → auto.

## What the cushion does

After a takeover releases, the loop re-seeds itself from the applied duty
(bumpless transfer) and then must not walk BELOW `floor_duty` for up to 60 s
(`floor_until_ms`). Reason: the heatsink is still cold, so the PI would integrate
that cold away and cut the fan below the steady need; the thermal lag then carries
the temperature past the target. Measured on 124.18: without it the fan slid
100 % → 4 % on a point needing ~58 %, peaking 71.3 °C instead of 69.4 °C.

## Where the floor comes from (changed 2026-11-15)

It used to be `ovr_saved` — the duty at the moment the takeover STARTED. During a
benchmark that is 100 % (the sweep pins the fans), so an applied point could sit at
100 % for the whole window with nothing asking for it.

It is now `last_auto_duty` — the duty the loop itself settled at — and only when
the operating point **matches** the one it was measured at (`last_auto_freq` /
`last_auto_vcore`). A duty that is right for 600 MHz is not a safe lower bound at
785 MHz, so any mismatch keeps the old conservative behaviour. See commit
`fix(fan): hand the cushion the duty this point settled at, not a takeover snapshot`.

## The release condition that never fires

Two ways out of the cushion:

```cpp
const bool at_target = meas_f >= (target - fan_cfg.pid.deadband);
const bool expired   = (int32_t)(now_ms - floor_until_ms[fan_index]) >= 0;
```

`at_target` is **unreachable in steady state**: a correctly cooled chip sits just
below target by design. Observed on Nexus at 785/1210:

```
Fan[0] takeover cushion released (60s timeout, 46.3C vs target 47.0C) at 85%.
Fan[1] takeover cushion released (temperature reached the band, 61.0C vs target 60.0C) at 100%.
```

Fan[0] always burns the full 60 s; Fan[1] released properly because its rail was
still hot and crossed the band.

Harmless while the floor equals the steady duty (holding the steady duty costs no
cooling), which is why it is **not** a priority. It only becomes visible when the
floor is too high — exactly the case the `last_auto_duty` change removes.

Do not "fix" this by loosening the band without a soak: the band exists to delay
release until the temperature has actually arrived, and releasing early is the
100 % → 5 % undershoot that made the cushion necessary.

## Reading the log

| Line | Meaning |
|---|---|
| `Fan[N] takeover cushion released (temperature reached the band, …)` | normal, released when the rail reached target |
| `Fan[N] takeover cushion released (60s timeout, …)` | the usual case for a rail that settles slightly BELOW target — not an error |
| `Fan[N] temperature reading recovered after Ns` | probe glitch; the loop re-seeds from the applied duty |
| `Fan[N] temperature reading unusable — ramping duty from X% to Y%` | probe lost; ramps toward 100 %, never to 0 |
