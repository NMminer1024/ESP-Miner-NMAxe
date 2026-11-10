# Fan PID thermal closed loop: field jitter analysis and smoothing rework

## Field symptoms (v3.2.02k/m, NMQAxe++)
- The Vcore fan showed 28 jumps of >=30 percentage points (0-92 %) over 8.7 h,
  while the TPS53647 Vcore temperature stayed at 60-61 °C the whole time.
- The ASIC fan on the same machine was comparatively smooth; but on cold start /
  load step it also jumped 0 -> 59 % in a single tick.
- When soak cooldown (fans at 100 %) ended and control returned to the auto policy,
  the fan dropped straight to 6 %, the ASIC shot up from a cold state to 57.9 °C,
  and input current peaked at 14.85 A.

## Root cause (code + measurements)
`src/drivers/fan/fan.cpp` old defaults (board.cpp hardcoded the same values for every board):

```
Kp = 50 %/°C, Ki = 1 %/(°C·s), Kd = 0, integral clamp = ±300 °C·s
```

- The plant's steady-state gain is far below 1 °C/%, and the fan/airflow/thermal
  capacitance add tens of seconds of lag. Kp=50 means a 1 °C error demands a 50 %
  duty — this is not a controller, it is a relay; a 0.7 °C reading change alone could
  swing the Vcore fan 3 % <-> 100 %.
- The temperature sensors (TPS53647 / TMP102) actually resolve to 1/16 °C, API shows
  0.1 °C; quantization itself is not the main cause but amplifies the P-term's step feel.
- Integral clamp ±300 °C·s = 1 °C error for 5 straight minutes; the output can stay pinned
  at the saturated end for a long time; once temperature crosses the target, the integral
  takes a long time to unwind -> long-period limit cycle.
- The original PID ran every 125 ms, `fan_status->speed` is an integer percentage;
  any ramp <1 %/tick was eaten by rounding.

## Measured baseline (124.18, v3.2.02o, COM17 log capture + /api sampling at 4 Hz)
Data: `docs/baseline/fan_124_18_experiment.csv`
- Test point 555 MHz / 1170 mV (~69 W), ASIC fan left on auto (~38-40 %).
- **The plant (Vcore fan -> TPS53647 temperature) is FOPDT**:
  - 100 % step: 73.4 °C -> 65.5 °C, dead time L≈9 s, τ≈58 s.
  - Steady-state gain is **strongly nonlinear**: 0->55 % about 0.10 °C/%, 55->75 % about
    0.05 °C/%, 75->100 % only 0.03 °C/%. The 67 °C target sits right in the 55-70 % "knee".
  - At this power 0 % only reaches 73 °C and 100 % only reaches 65.5 °C, so a 65 °C target
    is unreachable; choosing 67 °C gives the fan a real control range.
- Old PID reproduced in the field: as temperature went 82.3 -> 81.9 -> 82.6 -> 84.4,
  the fan went 23 % -> 3 % -> 39 % -> 100 % (±0.7 °C triggering 36-97 point jumps).
- On load 102 W -> 69 W the Vcore fan went **100 % -> 0 % in one tick**, temperature 88.5 -> 73 °C.
- Cold start: the ASIC fan at target 40 °C first sat at 0 %, then at 42.4 °C jumped to 59 %
  in one tick (P=50 × 2.4 °C, plus an integral of −57).

## New control law (`fan_pid_configure` / `pid_compute`)
- Measurement low-pass: ASIC τ=4 s, Vcore τ=6 s (also restores sub-LSB resolution).
- Deadband: ASIC 0.20 °C, Vcore 0.30 °C; integral frozen inside the band.
- Gains: near target Kp=3 (ASIC) / 2.5 (Vcore) %/°C; as |e| goes 2->8 °C it rises
  continuously to 3×. Kp is sized against the **worst-case 1 °C resolution**:
  1 LSB ≤ 2.5-3 % duty.
- Integral: Ki=0.10 / 0.08 %/(°C·s), conditional-integration anti-windup, the integral alone
  can cover a 100 % output (high-power steady state needs 80-100 % fan; the old 60 % cap
  would pin it).
- Asymmetric slew limit: rise 25 %/s (4 s to full), fall 3 %/s (100->20 % about 27 s).
  `pid->out` keeps a float, only the reported/PWM value is rounded, so 3 %/s is not stalled
  by rounding at 8 Hz.
- Safety: error > +8 °C goes straight to 100 %, bypassing the slew limit.
- Takeover / fault handoff: on override end or sensor recovery `fan_pid_reset()`,
  starting from the current actual duty and clearing the integral -> bump-less.
- **Cooldown-handover floor**: after soak's 90 s 100 % cooldown ends, until temperature
  returns into the target band, duty does not drop below the pre-override steady-state value
  (held for at most 180 s). This is the key to avoiding 100->6 % dumping the cold-radiator
  heat straight back onto the chip.
- Sensor loss: first hold the last valid duty for 2 s, then ramp linearly to 100 % over 30 s
  (no longer "hold at 2 %").

## Control model abstraction (per-board / per-fan)

The old code's problem was not just gain magnitude: `Kp/Ki` were **hand-copied constants in
each board's `board.cpp`**, all 5 boards × 10 fans were `Kp=50, Ki=1`, so "this machine's
plant" and "that machine's plant" could not be distinguished.

Now it is **describe the plant first, then derive gains by rule**:

```c
// fan.h
typedef struct {                 // one per board × fan
    float gain;          // K   °C per %  (|dT/dDuty| near the operating point)
    float dead_time_s;   // L   s
    float tau_s;         // tau s
    float sensor_noise;  // °C  worst-case reading jitter/resolution
    float max_dither;    // %   allowed duty dither from sensor noise
    float slew_up;       // %/s actuator capability
    float slew_down;     // %/s allowed cool-down rate (0 = derived from gain)
    bool  identified;    // true = measured
} fan_thermal_plant_t;
```

`fan_pid_tuning_from_plant()` uses **SIMC / lambda tuning (FOPDT)**:

| Step | Formula | Note |
|---|---|---|
| Closed-loop time constant | λ = 2L | For a dead-time-dominated thermal plant this gives ~60° phase margin; λ=L rings |
| Proportional gain | Kp = τ / (K·(λ+L)) | Smaller plant gain -> larger controller gain needed |
| Integral | Ki = Kp / τ | Integral time set to the plant time constant |
| Noise constraint | Kp ≤ max_dither / sensor_noise | **This is the key anti-jitter rule**: 1 °C jitter × Kp is the duty jitter |
| Filter | τ_filter = L/2 | ≥ half the dead time, also serves as sub-LSB averaging |
| Deadband | deadband = sensor_noise/2 | A reading can't tell "on target" from "off by one LSB" |
| Fall rate | slew_down = 0.05 / K (clamped 1-15 %/s) | Cool only 0.05 °C/s, to avoid dumping cold-radiator heat back on soak handover |
| Not measured | Kp × 0.6 | Extra conservative when `identified=false` |

The same rules give (measured/estimated):

| Board/fan | K (°C/%) | L (s) | τ (s) | noise (°C) | measured? | Result Kp / Ki (%/°C, %/(°C·s)) |
|---|---|---|---|---|---|---|
| QAxe++ 2ph ASIC | 0.31 | 8.6 | 45 | 0.20 | **yes** | 5.63 / 0.125 |
| QAxe++ 2ph Vcore | 0.07 | 9.0 | 58 | 1.0 | **yes** | 4.00 / 0.069 |
| Rev6.1/Rev8.1 ASIC | 0.30 | 6.0 | 45 | 0.20 | no | 8.33 / 0.185 |
| Rev6.1/Rev8.1 Vcore | 0.030 | 11.0 | 70 | 1.0 | no | 2.40 / 0.034 |
| Nexus ASIC | 0.263 | 5.0 | 45 | 0.15 | **yes** | 11.41 / 0.254 |
| Nexus Vcore | 0.107 | 12.0 | 50 | 1.0 | **yes** | 4.00 / 0.080 |
| NMAxeGamma ASIC | 0.22 | 6.0* | 50 | 0.20 | **yes** | 12.63 / 0.253 |
| NMAxe (fixed-speed blower) | — | — | — | — | — | not closed-loop |

> This table is recomputed by `make_plant(...)` in `fan.cpp`; keep it in sync when editing code.
> 2026-11-09 correction: the Rev6.1/8.1 ASIC row was previously mis-written as 0.25/8.6/7.0,
> it is actually 0.30/6.0/8.33.

\* Gamma's `L` of 6 s is a **deliberate conservative hold**, not the measured value (measured
first response is only 2.3-3.2 s). Since `λ = 2L`, the real L would be more aggressive
(L=3 s gives SIMC 25 %/C); 6 s is the value A/B-validated on this board, so it is not changed.
See the comment in `fan_plant_gamma_asic()`.

### QAxe++ raw measured data (124.18, 555 MHz/1170 mV, ~70 W)
ASIC fan (TMP102, target 40 °C):

| Manual duty | Steady ASIC temperature |
|---|---|
| 20 % | 53.3 °C |
| 80 % | 34.8 °C |

→ K = (53.3−34.8)/60 = **0.31 °C/%**; step dead time **~9 s**;
heat-up τ≈63 s, cool-down ~40 s (**tune on the faster 40-45 s**, otherwise the Kp computed
from 63 s is too high and rings in the cool-down direction).

Vcore fan (TPS53647, target 67 °C):

| Manual duty | Steady Vcore temperature |
|---|---|
| 100 % | 65.5 °C |
| 75 % | 66.2-66.3 °C |
| 55 % | 67.8 °C |
| 0 % (this power) | 73.4 °C |

→ K is **0.075-0.082 °C/%** over the 55-75 % segment, only 0.034 °C/% over 75-100 %
(nonlinear knee); dead time 9 s, τ 58 s. The 67 °C target sits mid-way in the usable 55-70 % range.
**Note**: at this point 100 % only reaches 65.5 °C, so targets of 65 °C and below are unreachable
for this load; seeing "fan stuck at 100 %" in the field is normal saturation, not a loop problem.

### NMAxeGamma measured (124.11 "Gamma-B", single fan, v3.2.02s)

**Plant identification** (`tool/fan_pid/gamma_identify.ps1`, 550 MHz/1130 mV ≈ 21 W):
data `docs/baseline/gamma_ident.csv`

| Manual duty | Steady ASIC temperature |
|---|---|
| 100 % | 48.1 °C |
| 70 % | 55.1 °C |
| 40 % | 66 °C and still rising -> **safety abort** (threshold 67 °C) |

→ K = 0.232 °C/% (100->70 %), 0.369 °C/% (70->40 %, nonlinear steeper);
first response **2.3-3.2 s** (not t10; t10 is 5.7/11.6/11.1 s including thermal lag),
τ ≈ 45.6 s (100 % step) / 80.8 s (70 % step).

**Two machine characteristics to remember**:
1. **Narrow control range, biased high**: at 21 W 100 % only reaches 48 °C, so the factory
   default target=50 needs ~93 % duty -> the fan is nearly always saturated. This is the
   machine itself, not a loop defect.
2. `maxTarget=65`, very little cooling headroom; below 70 % duty temperature runs away fast,
   so step identification must set the abort threshold below the board-level 70 °C limit
   (67 °C used here, it did trigger).

Model: `make_plant(0.22f, 6.0f, 50.0f, 0.20f, 4.0f, 25.0f, 0.0f, true)`
-> boot log `Fan[0] PID from plant: K=0.220 C/%, L=6.0s, tau=50s -> Kp=12.63 %/C,
Ki=0.253, deadband=0.15C, filter=3.0s, slew +25/-1.2 %/s, lambda=12s`

> `L = 6 s` is a **deliberately held conservative value**, not measured (measured first
> response 2.3-3.2 s). `λ = 2L`, using the real L would be more aggressive (L=3 s -> SIMC
> 25.3 %/C), and 6 s is A/B-validated on this board, so it is not changed.
>
> Side note: `identified=true` **does have a numeric effect here**. The noise cap
> `4.0/0.20 = 20 %/C` just lets through the 12.63 %/C SIMC requests; if it were still marked
> `identified=false`, that cap × 0.6 becomes 12 %/C and the gain would be clipped to **12.00**.
> So of the Kp jump from 8.9 to 12.63, the parameters and the flag each account for part.

**A/B (same target=55 °C, 8-minute steady-state stats, t≥240 s)**:

| Metric | v3.2.02o (Kp=50) | v3.2.02s (Kp=12.6) | Improvement |
|---|---|---|---|
| Fan std dev | 3.57 | **2.91** | −18 % |
| Fan span | 19 points | **11 points** | −42 % |
| Temp std dev | 0.312 °C | **0.177 °C** | −43 % |
| Temp span | 1.5 °C | **0.81 °C** | −46 % |
| Steady-state error (to 55) | 0.25 °C | **0.11 °C** | −56 % |
| Jumps ≥15 | 0 | 0 | — |
| Max single step | 7 | **2** | — |
| Power / hashrate | 23.54 W / 1211 GH/s | 23.57 W / 1209 GH/s | no regression |

Data: `gamma_loop_old_o.csv` vs `gamma_loop_new_s.csv`

**Why Gamma's Kp is 2.2× the QAxe++ ASIC loop (12.63 vs 5.63)**: the SIMC formula
`Kp = τ/(K·3L)`. Gamma's K is smaller (0.22 vs 0.31) and L shorter (6 vs 8.6 s), so it needs
a larger gain. The cost is a more active fan — this is physically necessary, not a tuning error.
To be quieter you can only sacrifice response speed (larger λ).

**Residual behavior (acceptable)**: steady state still has a slow ~11-point, ~160 s period fan
wander. After checking, **corr(fan, power) = 0.51**, `corr(temp, power) = 0.48`, i.e. more than
half is the loop tracking real load fluctuation (power 23.3-24.3 W), not a self-excited limit
cycle. Temp std dev 0.177 °C is only slightly above the 0.15 °C deadband — control quality is already good.

### NMQAxe++Nexus measured (124.2, dual fan, v3.2.02t)

**Identification conditions**: 700 MHz/1150 mV (~135 W, 11.3 A / 15 A adapter), step one fan at
a time with the other fixed at 100 % (script `tool/fan_pid/nexus_identify.ps1`).
Data `nexus_ident.csv` + `nexus_ident_vcore_tail.csv` (the latter re-runs the interrupted V0/V100B).

| Fan | Duty | Steady temperature | Segment gain |
|---|---|---|---|
| ASIC (TMP102) | 100 % | 44.89 °C | — |
| | 70 % | 49.10 °C | 0.140 °C/% |
| | 50 % | 58.05 °C | **0.447 °C/%** |
| Vcore (TPS546D24A) | 100 % | 57.13 °C | — |
| | 50 % | 60.00 °C | 0.057-0.068 °C/% |
| | 0 % | 68.27 °C | **0.146 °C/%** |

ASIC: L 4.3-5.3 s, τ 26.5 s (70 % step) / 52.2 s (50 % step);
Vcore: L 6.5-14.7 s, τ 49-51 s (consistent in both directions).

**The decisive characteristic of this board is the Vcore sensor resolution**: the TPS546D24A's
PMBus temperature reports only **integer Celsius** (readings only 57/58, 68/69), 16× coarser
than QAxe++'s TPS53647 (1/16 °C). The old Kp=50 here is not oscillation but a relay — measured
**80-133 jumps ≥15 points within 4-5 minutes** (see table below).

**The opposite of Gamma**: the Vcore loop's Kp is not set by the plant gain. SIMC requests
13.0 %/C at K=0.107 and 24.4 %/C at K=0.057, but the noise cap `4.0/1.0 = 4.0 %/C` clips them
all -> Kp is constant 4.00. What really changes is `identified=false -> true`: the cap goes from
2.4 up to 4.0.

**ASIC nonlinear value trade-off**: the whole 100-50 % range averages 0.263 °C/%, but 100-70 %
is only 0.140 while 70-50 % is as high as 0.447. Take 0.263 (Kp 11.41) rather than the local
0.447 (Kp 6.71), because the loop's operating range spans all of 50-100 %: the factory 45 °C
target is at the saturated end (100 %), the 52 °C validation target lands at ~60 %. Using 0.140
would give Kp 21 and ring at 60 %; using 0.447 would make the saturated end sluggish.

**Old firmware (v3.2.02m) control group** (`fan_verify.ps1` with Nexus params, target ASIC 52 /
Vcore 67, data `fan_verify_nexus_old_m.csv`):

| Phase | fan1 range | jumps ≥15 | max single step | vcore reading |
|---|---|---|---|---|
| B_AUTO 5 min | 4-100 % | **80** | 51 | 66-68 °C |
| D_HANDOVER 4 min | 0-100 % | **131** | 100 | 64-71 °C |
| E_LOADDOWN 4 min | 0-87 % | **133** | 54 | 0-68 °C |
| F_RESTORE 1 min | 16-100 % | 30 | 51 | 67-68 °C |

In the same data the ASIC loop (fan0) is clean: B_AUTO 55-67 %, jumps 0, max single step 4.
So this board's problem is **the Vcore loop + 1 °C sensor resolution**, not the loop as a whole.

**New firmware (v3.2.02t) A/B** (same script, same operating point, same target; data
`fan_verify_nexus_new_t.csv`):

| Phase | fan1 jumps ≥15 old→new | max single step old→new | fan1 span old→new | vcore peak old→new |
|---|---|---|---|---|
| A_SETUP | 103 → **1** | 100 → 84 | 100 → 100 | 68 → 72 °C |
| **B_AUTO** | **80 → 0** | 51 → **2** | 96 → **9** | 68 → 69 °C |
| C_COOLDOWN | 0 → 0 | 0 → 0 | 0 → 0 | 67 → 67 °C |
| **D_HANDOVER** | **131 → 1** | 100 → 48 | 100 → 50 | **71 → 67 °C** |
| **E_LOADDOWN** | **133 → 0** | 54 → **2** | 87 → 53 | 68 → 70 °C |
| F_RESTORE | 30 → 1 | 51 → 64 | 84 → 83 | 68 → 70 °C |

- `B_AUTO` steady state: fan1 45-54 % (span 9), vcore 67-69 °C, jumps 0, single step 2.
- ASIC loop no regression: `B_AUTO` fan0 56-61 %, jumps 0, single step 2 (old 55-67 %, single step 4).
- **Production point 775/1200 steady state** (after 160 s): fan1 **67 %**, vcore **67 °C**,
  hashrate 10441 GH/s, 164 W. Old firmware at the same point was fan1 87 %, vcore 68 °C and
  continuously jumping — the new firmware uses **20 fewer fan points and still holds temperature
  at target** (the old version's limit cycle averaged a higher duty).
- Performance: 133-141 W segment 10424 GH/s / 163.3 W / 13.76 A, same order as old firmware,
  no rejected shares.

**Residual defect (same root as QAxe++ r version, fixed in v3.2.02u)**: the single 48-point step
in D_HANDOVER happened on the first takeover tick (`t=570.7`, 100 -> 51), dropping straight to the
floor value. The cause is that in `hw_threads.cpp`'s floor branch `pid.out = fl` runs
**unconditionally**: as long as the floor is active, even when the current output (100 %) is above
the floor, it rewrites the slew state to the floor value, bypassing the rate limit. The floor should
only act as a lower bound (`if (duty < fl) duty = fl`), and the back-computed integral is only needed
when actually clamped. Measured temperature did not overshoot (64 -> 67 °C, old 64 -> 71 °C), so it
was kept at the time, but it was a real single-tick jump, changed to a pure lower bound in the u version.

### Residual wander: first distinguish "tracking load" from "quantization dither" (v3.2.02u, 124.2)

The goal is "fan steady, no jumps". Before acting, use `tool/fan_pid/fan_wander_corr.ps1` to
compute correlation coefficients **within a single steady segment** — this step decides whether to
suppress:

| Operating point | corr(fan1, pwr) | corr(fan1, vcore) | power fluctuation | verdict |
|---|---|---|---|---|
| 700/1150, target 67 | **0.089** | 0.046 | ±0.6 % (sd 0.21 W) | **pure dither**, suppress at zero cost |
| 775/1200 production point | **−0.834** | −0.741 | 163-166 W, hashrate 9.1-12.0 TH/s | **tracking real load**, must not suppress |

Two boards (Gamma/Nexus), two operating points, different conclusions each — **so measure before tuning**.
Gamma is 0.51 (half tracking, half dither), Nexus low-power point is 0.09, Nexus production point is −0.83.

**Verify actuator authority first** (`tool/fan_pid/nexus_actuator_sweep.ps1`, 600/1150, fan0 fixed 80 %):

| duty | rpm | Δrpm/point | vcore |
|---|---|---|---|
| 40 % | 1275 | — | 63.3 °C |
| 45 % | 1416 | +28 | 60.5 °C |
| 50 % | 1580 | +33 | 58.5 °C |
| 55 % | 1708 | +26 | 57.0 °C |
| 60 % | 1833 | +25 | 56.0 °C |

1 point ≈ 28 rpm, rpm measurement sd ≈ 13 rpm -> resolution about 2× noise, **no dead zone**, worth tuning.
K at this point ≈ 0.19-0.36 °C/% (steeper than 700/1150's 0.107), showing "one K per board" is an
approximation; but since Kp is clipped by the noise cap, behavior is unaffected.

**Decide the plan by offline replay** (`tool/fan_pid/fan_replay_tuning.ps1`, feeding recorded real
temperature sequences; first confirm replay reproduces the measured span 5 / sd 1.08 under "current params"):

| Plan | span | distinct duty values (189 s) |
|---|---|---|
| current | 5 | **349** ← the acoustic problem is here |
| deadband 0.5 → 1.0 LSB (change alone) | 5 | 161 (sd actually 1.08→1.28) |
| Kp 4 → 2 (`max_dither` 4→2) | 3 | 338 |
| **Kp4 + duty hysteresis 2** | 4 | **3** |
| Kp2 + deadband1 + hysteresis3 | **0** | 1 (response slower) |

**Counterintuitive but explainable**: enlarging the deadband alone is **worse**. The reading is
integer-only, so error can only be 0/±1/±2; with deadband 0.5 an error of 0.6 moves it, amplitude
2 points; with deadband 1.0 it must accumulate to 1.0 before moving, amplitude 4 points. Fewer steps,
each one double the size. So the deadband must change together with lowering the gain, or leave it alone.

**The two changes landed** (v3.2.02u):

1. **Duty hysteresis** `fan_pid_t.duty_hyst`, derived from the plant, **not hand-filled**:
   `duty_hyst = 0.5 × Kp × sensor_lsb`, **set to 0 (disabled) if less than 2 points**.

   > ⚠️ Must use **`sensor_lsb` (quantization step)**, not `sensor_noise` (reading spread).
   > It was first written with noise, causing QAxe++ 2-phase Vcore to also be judged as 2-point
   > hysteresis (its noise=1.0), but that TPS53647 is actually **1/16 °C resolution**, and the
   > 2-point wander is a real response — suppressing it would be wrong. After the fix:
   > **only Nexus Vcore gets 2 %, the other 9 loops all get 0**.

   | Sensor | LSB | Example |
   |---|---|---|
   | TMP102 (12-bit) | 0.0625 °C | all ASIC loops |
   | TPS53647 (QAxe++/Rev6.1 Vcore) | 0.0625 °C | noise 1 °C ≠ LSB |
   | **TPS546D24A (Nexus Vcore)** | **1 °C** ← the only coarse one | datasheet 7.6.66 `LSB = 1°C` |

   - Nexus Vcore = 4.0 × 1.0 × 0.5 = **2** ✓
   - QAxe++ / Rev6.1 Vcore = 4.0 × 0.0625 × 0.5 ≈ 0.13 → **0** ✓ (verified on real hardware)
   - All ASIC loops → **0**; Gamma / NMAxe → **0**
   - Semantics: the continuous demand is computed as usual; only when deviating from the
     **actually applied value** by ≥ hyst does it really write the fan
   - Skipped when the floor is active (floor takes priority)
   - Reconciliation script: `fan_hyst_matrix.ps1` (must re-run after changing the plant)
2. **Takeover floor changed to a pure lower bound**: only clamp and back-compute the integral when
   `if (duty < fl)`, see above.

**Verification (`fan_verify.ps1` with Nexus params, same script same operating point)**:

| Phase | fan1 max single step t → **u** | jumps ≥15 t → **u** |
|---|---|---|
| A_SETUP | 84 → **2** | 1 → **0** |
| B_AUTO | 2 → **2** | 0 → **0** |
| C_COOLDOWN | 0 → **0** | 0 → **0** |
| **D_HANDOVER** | **48 → 2** | 1 → **0** |
| E_LOADDOWN | 2 → **2** | 0 → **0** |
| F_RESTORE | 64 → **2** | 1 → **0** |

**All phases max single step = 2 points, jumps 0**. D_HANDOVER's 48 → 2 is the direct evidence of the floor fix.

B_AUTO's span going from 9 to 18 is not a regression: out of 928 samples it changed only 13 times
(2 points each), the trajectory is a **one-way convergence** 70 -> 69 -> 68 -> 67 °C (`t=180` recovering
to target from the previous phase's 71 °C) — three-segment 2 % stepping, not oscillation.

**Production point 775/1200 (342 s steady state)**: fan1 39-69 %, vcore 66-72 °C,
**maxstep 2, jumps ≥5 = 0**; hashrate 10496 GH/s, 163.3 W, 13.75 A, `protection=none`,
no new abnormal reboots.

### v3.2.02u new tools
- `fan_wander_corr.ps1` — distinguish dither vs tracking (**run before every tuning pass**)
- `fan_replay_tuning.ps1` — offline replay of candidate params on recorded data, **compare plans without flashing**
- `nexus_actuator_sweep.ps1` — duty→rpm authority curve
- `steady_capture.ps1` — evenly-spaced steady-state sampling at the production point
- `fan_hyst_matrix.ps1` / `fan_regression_matrix.ps1` — reconcile hysteresis and tuning values after changing the plant

**`fan_verify.ps1` has been merged into a single file** (the old `fan_verify_nexus.ps1` is deleted).
Six phases fixed, boards differ only in numbers, all via parameters; `-DryRun` prints the schedule
without touching the device for a check. Calls for the three boards:

```powershell
# QAxe++ 2-phase (default params, line-for-line identical to the original fan_verify.ps1)
.\fan_verify.ps1 -Ip 192.168.124.7 -Out old.csv

# Nexus (equivalent to the original fan_verify_nexus.ps1)
.\fan_verify.ps1 -Ip 192.168.124.2 -Out nexus.csv `
    -Freq 700 -Vcore 1150 -TargetAsic 52 -AsicAbortC 66 -VcoreAbortC 76 `
    -AbortOnProtection -StepFreq 650 -StepVcore 1120 `
    -RestoreFreq 775 -RestoreVcore 1200 -RestoreTargetAsic 45 -RestoreTargetVcore 67

# Rev6.1 (equivalent to the original fan_verify_rev61.ps1)
.\fan_verify.ps1 -Ip 192.168.124.6 -Out rev61.csv `
    -Freq 725 -Vcore 1250 -AsicAbortC 65 -VcoreAbortC 92 `
    -StepFreq 800 -StepVcore 1290 -SetupSec 240 `
    -RestoreTargetAsic 40 -RestoreTargetVcore 75
```

> **Iron rule for point selection**: it must be in a range where the fan can actually move. On Rev6.1,
> 555/1170 gets clamped to 650 with the fan at 0 %; 800/1290 saturates both fans — both ends are flat
> lines, and A/B proves nothing.


## Key conclusion: the two fans' gain difference is not "fan number" but sensor noise

Key point: **the two fan loops differ 2.5× in gain, and the reason is not "which fan" but sensor
noise** (TMP102 0.2 °C vs TPS53647 1 °C). The old code gave both the same Kp=50 — for the ASIC loop
that is overshoot, for the Vcore loop it is straight-up a relay. This also shows "hardcoding two sets
of params by fan number" doesn't work either: change the sensor / divider network and the values should
change with it.

A line is printed at boot for easy field verification:
```
Fan[1] PID from plant: K=0.035 C/%, L=9.0s, tau=58s -> Kp=4.00 %/C, Ki=0.069,
       deadband=0.50C, filter=4.5s, slew +30/-1.4 %/s
```
The `[ESTIMATED]` suffix means that board has not had a step measurement yet.

### How to fill the model for a new board (4 numbers, no tuning)
1. Fix a low-power operating point (avoid fan saturation), set that fan to manual 100 %, wait for steady state.
2. Step to 50 %: record the time it starts changing (L) and the time to 63 % (τ).
3. `K = (T_100 - T_50) / 50`.
4. Read that sensor's jitter amplitude at a fixed duty, fill `sensor_noise`.
5. Write the 4 numbers into the corresponding `fan_plant_*()`, `identified = true`.

## Files changed
- `src/drivers/fan/fan.h` — `fan_thermal_plant_t` / `fan_pid_tuning_t`,
  `fan_config_t.thermal`, tuning and preset prototypes
- `src/drivers/fan/fan.cpp` — SIMC tuning rules + per-board presets + `pid_compute`
- `src/board/board.cpp` — 10 `Kp/Ki` constants replaced with `fan_cfg.thermal = fan_plant_*()`
- `src/thread/hw_threads.cpp` — configure PID from `fan.thermal`, fallback when model missing, boot log
### v3.2.02p field verification (124.18 measured)
Boot log confirms the chain is correct (no `[ESTIMATED]`):
```
Fan[0] K=0.310 C/%, L=8.6s, tau=45s -> Kp=5.63 %/C, Ki=0.125, slew +30/-1.3 %/s
Fan[1] K=0.070 C/%, L=9.0s, tau=58s -> Kp=4.00 %/C, Ki=0.069, slew +30/-1.0 %/s
```

| Phase | old v3.2.02o | new v3.2.02p |
|---|---|---|
| B_AUTO jumps ≥15 | **429 / 5 min** | **0** |
| B_AUTO max single step | 54 | **2** |
| B_AUTO fan | 31-100 % | 55-61 % (converging to target 67 °C ±0.4) |
| D_HANDOVER max single step | 100 | **1** |

**Newly found problem (fixed p -> q -> r)**: D_HANDOVER temperature peak was 71.3 °C in the p version,
higher than the old 69.4 °C. The cause was not jitter but **the fall being too smooth -> coasting past**:
```
t=580 89% 65.4C -> t=640 27% 66.5C -> t=660 7% 68.1C -> t=736 36% 71.3C(peak)
```
This operating point needs ~58 % steady state, but the coasted all the way down to 6-7 %.

Two fixes:
1. **q**: added `last_auto_duty[] / have_last_auto[]`, recording the steady-state duty while the PID
   owns the fan; on `auto=0 -> auto=1` use it as the floor. **But the release threshold was written as
   `target - deadband - 1.0` (=65.5 °C), and cooldown ends right at 65-66 °C -> released on the first
   tick, the floor was useless (q version still coasted to 5 %).**
2. **r**: release threshold changed to `meas_f >= target - deadband` (actually reaching target); and when
   clamped by the floor, **back-compute the integral** `integral = (floor - Kp*e)/Ki`, `pid.out = floor`.
   Previously it only clamped the output while the integral kept winding down, so on release the stored
   error yanked the fan down all at once.

The release condition is verifiable in the log:
```
q: Fan[1] takeover cushion released (in band):  65.5C vs target 67.0C   <- too early
r: Fan[1] takeover cushion released (at target): 66.5C vs target 67.0C  <- correct
```

### Handover-specific test (`tool/fan_pid/fan_test_handover.ps1`, 10 min)
Same path (manual 100 % -> auto, equivalent to soak cooldown handoff), three-version comparison:

| Version | fan minimum | temperature peak | max single step |
|---|---|---|---|
| v3.2.02o (original logic) | **0 %** | 69.4 °C | **100** |
| v3.2.02p | 4 % | **71.3 °C** | 1 |
| **v3.2.02r** | **55 %** | **67.9 °C** | 45 (once, at the handover instant) |

The r version's C_HANDOVER held the fan at 55-59 % throughout, temperature 65.4 -> 67.9, converging to 67.1-67.8.
Data: `docs/baseline/fan_handover_r.csv`

## Impact review (2026-11-08, all 5 envs compile)

### Full six-phase regression (v3.2.02r, `fan_verify.ps1`, 124.18)
Data: `docs/baseline/fan_verify_old_o.csv` vs `docs/baseline/fan_verify_r_full.csv`

| Phase | old v3.2.02o | new v3.2.02r |
|---|---|---|
| B_AUTO | f1 31-100, temp 65.9-67.8, jumps **429**, single step **54** | f1 60-64, temp 65.9-67.3, jumps **0**, single step **2** |
| C_COOLDOWN | f1 100, 65.0-67.3 | same (already correct) |
| D_HANDOVER | f1 **0**-100, temp 65.0-**69.4**, jumps **248**, single step **100** | f1 **59**-100, temp 64.8-**67.3**, jumps **1**, single step 38 |
| E_LOADSTEP | f1 66-100, 67.1-88.6 | f1 61-100, 67.1-89.9 |

**No performance regression** (1-minute average, 725/1250):

| Metric | before test | after regression |
|---|---|---|
| hashrate | 5863.7 GH/s | **5920.9 GH/s** |
| power | 101.86 W | **101.34 W** |
| input current | 8.247 A | **8.21 A** |
| Vcore temperature | 88.03 °C | 88.83 °C |
| rejected shares | 0 | **0** |
| protection events | none | **none** |

Compile: all 5 envs SUCCESS (NMAxe/Gamma 58.5 % Flash; the three QAxe++ 73.5 %).

### "Behavior improvements" confirmed during regression, not regressions
- E_LOADSTEP temperature ceiling 89.9 vs 88.6: on a load step to full, the fan rises at 30 %/s, so a
  brief slightly higher temperature is expected (the old version was a violent instant jump to 100 %);
  **jump count 1 -> 0**.
- A_SETUP higher starting temperature: that is residue from the previous operating point, unrelated to the phase.

### Unaffected interfaces (verified)
- `fan_status_t` **unchanged** (still id / self_test / self_test_state / speed / rpm)
  -> `http_server.cpp`, `application.cpp`, `monitor_ctx`, `benchmark_ctx` all unaffected.
- `fan_pid_t` / `fan_config_t` have no `memcpy` / `sizeof` serialization and are not written to NVS
  -> a larger struct does not break the persistence format.
- Manual fan (`auto=0`) path kept as-is: `speed` written directly by the API, PID does not intervene.
- Over-temperature protection chain (`hw_threads` OC/OT escalation) unchanged.
- Single-fan boards (NMAxe/Gamma): the `FAN_SLOTS_MAX=4` static array is used by index, no out-of-bounds.

### 5 issues found and fixed during the review
1. **`dt` unbounded** (most severe): `pid_start_ms` was not updated during override, so after soak's 90 s
   cooldown the first tick had `dt=90 s`, and `slew_down*dt` let the whole jump through — exactly canceling
   this fix. Fixed in two places: update `pid_start_ms` in the override branch too; clamp `dt` to 0.5 s in
   `pid_compute()`.
2. **`slew_down` had only an upper bound, no lower bound**: the original `0.05/K` for the ASIC plant
   (K=0.31) is only 0.16 %/s, i.e. 100->20 % takes 8 minutes while τ is only 40 s -> persistent
   **overcool/undershoot**. Changed to `max(0.05/K, 60/T)` clamped to 1-15 %/s, actually 1.0-1.7 %/s.
3. **NMAxe fixed-speed blower**: `fan_controllable=false` but the PID still computed. Under the old gains it
   happened to saturate at ~100 %, under the new gains it would show single digits -> UI display and benchmark
   `fanNeed` both regressed. Fix: `drive_fan()` adds a `!fan_controllable` branch always reporting 100 %;
   `bm_fan_duty_of()` returns 100 % in sync (consistent with the sweep's `fanAsic` rule).
4. **`last_warn_ms` underflow**: initialized with `now - 30000`, within the first 30 s after boot
   `millis()-last_warn_ms` wraps, swallowing the first warning. Changed to a sentinel-0 check.
5. **Handover flag eaten by the bump-less reset** (found in self-review): one `handover` flag carried both
   "reset the integral" and "hold the floor"; the former clearing the latter meant the floor never took effect.
   Split into `bumpless_pending[]` and `floor_duty[]/floor_until_ms[]`.

### Intentional behavior changes (not regressions)
- Fan response is slower: rise 25-30 %/s (~3.5 s to full), fall ~1-1.7 %/s. This is the cost of smoothing,
  and also the solution to customer complaint #5 (cooldown-handover overshoot).
- `>8 °C` error still goes straight to 100 %, and `slew_up` guarantees full speed within 4 s -> over-temperature
  protection margin unchanged.
- Unmeasured boards (Rev6.1/8.1, Nexus) use estimated plants, Kp lowered another 40 %, boot log marked
  `[ESTIMATED]`; after measuring you only fill in 4 numbers.

### Resources
| env | RAM | Flash |
|---|---|---|
| NMAxe | 24.3 % (79472 B) | 58.5 % |
| NMAxeGamma | 24.3 % (79472 B) | 58.5 % |
| NMQAxePP | 24.5 % (80128 B) | 73.5 % |
| NMQAxePPRev61 | 24.5 % (80128 B) | 73.5 % |
| NMQAxePPNexus | 24.5 % (80128 B) | 73.5 % |

RAM only +32 B (`fan_config_t` embeds 2 plant objects), Flash +~3 KB.

## Tuning basis / follow-ups
- Once the step data (C_STEP_*) gives steady-state gain and τ, re-check Kp/Ki and `slew_down`.
- Strongly recommend adding **power feed-forward** next: `duty_ff ≈ f(req_frq × req_vcore²)`, which can bring
  the fan straight to roughly the operating point when Benchmark switches points, with the integral only fixing
  the residual. Currently `FanCtx` has only temperature, no power/freq; to do this you'd need to pass `spec` and
  power info in, or read `spec->asic.req_frq/req_vcore` inside `drive_fan()`.
- Phase B (555-1170 / target 65) is used for same-point A/B of old vs new firmware: the old firmware should show
  ≥15-point periodic jumps within 8 minutes, the new one should not.

## Verification method
```
# 1) Plant identification (manual duty step, read-only sensors)
tool/fan_pid/fan_identify_asic.ps1 -Ip <ip> -Out docs/baseline/fan_asic_ident.csv
tool/fan_pid/fan_experiment.ps1    -Ip <ip> -Out docs/baseline/fan_vcore_ident.csv
# 2) Analysis: steady-state gain / dead time / time constant + jitter stats during the auto phase
tool/fan_pid/analyze_steps.ps1 -Csv <csv> -Signal asic
tool/fan_pid/analyze_steps.ps1 -Csv <csv> -Signal vcore
# 3) A/B: same script, same operating point, same target temperature, run once each on old and new firmware
tool/fan_pid/fan_verify.ps1 -Ip <ip> -TargetVcore 67 -Out <csv>
# 4) Capture boot logs over serial, verify the PID actually in effect:
#    "Fan[1] PID from plant: ... Kp=.. %/C, Ki=.." ([ESTIMATED] means not measured)
```

Criteria:
- Auto-phase `jumps>=15` count: old firmware 0.9/min -> new firmware should be 0.
- Cold start / load step: single-tick jumps gone, rising edge limited to 30 %/s, falling edge ~1-3 %/s.
- Handover: 100 % -> auto shows no temperature overshoot (old firmware +8 °C).

### Old firmware control-group measurement (v3.2.02o, 124.18, target 67 °C, `fan_verify.ps1`)
Data: `docs/baseline/fan_verify_old_o.csv`

| Phase | duration | f1 (min/avg/max) | Vcore temperature | jumps ≥15 | max single step |
|---|---|---|---|---|---|
| A_SETUP | 3.0 min | 0/92/100 | 65.9-72.9 | 25 | 100 |
| **B_AUTO** | 5.0 min | 31/65/100 | 65.9-67.8 | **429** | 54 |
| C_COOLDOWN | 1.5 min | 100/100/100 | 65.0-67.3 | 0 | 0 |
| **D_HANDOVER** | 4.0 min | 0/50/100 | 65.0-69.4 | **248** | **100** |
| E_LOADSTEP | 4.0 min | 66/100/100 | 67.1-88.6 | 1 | 34 |

Both field complaints were fully reproduced:
- **B_AUTO: 429 jumps ≥15 points in 5 minutes** (85.9/min) — not occasional, a sustained limit cycle.
- **D_HANDOVER: after cooldown 100 % hands back to auto, the first tick goes 100->0 %**, temperature surging
  from 65.0 to 69.4 °C — exactly the mechanism behind the customer's post-soak ASIC surge to 57.9 °C and
  input peak 14.85 A.
