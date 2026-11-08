#include "utils/logger/logger.h"
#include "fan.h"

#define PULSES_PER_REVOLUTION 2        // Number of pulses per fan revolution

void fan_drv_init(fan_init_t init_param){
    pinMode(init_param.pwm.pin, OUTPUT);
    ledcSetup(init_param.pwm.ch, init_param.pwm.freq, init_param.pwm.resolution);
    ledcAttachPin(init_param.pwm.pin, init_param.pwm.ch);

    pcnt_unit_config(&init_param.torch);
    pcnt_set_filter_value(init_param.torch.unit, 100);
    pcnt_filter_enable(init_param.torch.unit);
    pcnt_counter_pause(init_param.torch.unit);
    pcnt_counter_clear(init_param.torch.unit);
    pcnt_counter_resume(init_param.torch.unit);
}

void fan_set_speed(fan_init_t init_param, float speed, bool invert = false){
    float spd = (invert) ? (1.0f - speed):speed;
    uint32_t dutyCycle = (uint32_t)(spd * (( 1 << init_param.pwm.resolution) - 1));
    ledcWrite(init_param.pwm.ch, dutyCycle);
}

uint16_t calculate_rpm(int16_t pulse_count, double time_seconds) {
    return (uint16_t)((pulse_count * 60.0) / (time_seconds * PULSES_PER_REVOLUTION));
}

// ---------------------------------------------------------------------------
//  Thermal loop tuning
//
//  Field report (v3.2.02k/m, NMQAxe++): the VRM fan moved 0 -> 92 % in 30+
//  point steps 28 times in 8.7 h while the regulator temperature stayed inside
//  60-61 °C. The ASIC fan was smooth on the same board. The old code carried
//  Kp = 50 %/°C, Ki = 1 %/(°C·s), Kd = 0 for EVERY board and EVERY fan.
//
//  Identification on 124.18 (QAxe++ 2-phase, 555MHz/1170mV, Vcore fan) gave
//      K = 0.034-0.082 °C/% (duty dependent)   L = 9 s   tau = 58 s
//  i.e. an FOPDT plant whose proportional path alone needs ~30 %/°C to close
//  the loop, while the TPS53647 temperature reading scatters by ~1 °C. That is
//  the whole hunting mechanism: a 0.7 °C wobble times Kp=50 is 35 duty points.
//  The gains therefore must come from the PLANT and the SENSOR NOISE, not from
//  a constant copied to every board.
// ---------------------------------------------------------------------------
void fan_pid_tuning_from_plant(const fan_thermal_plant_t* plant, fan_pid_tuning_t* out)
{
    // Sanitise: a plant with a zero gain, a zero time constant or a negative
    // dead time is a typo, and must not become an oscillating controller.
    float K = (plant->gain > 1e-4f) ? plant->gain : 1e-4f;
    float L = (plant->dead_time_s > 0.5f) ? plant->dead_time_s : 0.5f;
    float T = (plant->tau_s > 2.0f * L) ? plant->tau_s : 2.0f * L;
    float noise = (plant->sensor_noise > 0.01f) ? plant->sensor_noise : 0.01f;
    float dither = (plant->max_dither > 0.5f) ? plant->max_dither : 0.5f;

    // ── internal model control (lambda / SIMC) for FOPDT ────────────────────
    // Closed-loop time constant lambda = 2L: for a dead-time dominated thermal
    // plant that is the shortest response that still yields ~60 deg of phase
    // margin without overshoot. lambda = L is ~45 deg (ringing), 3L is sluggish.
    float lambda = 2.0f * L;
    float Kc = T / (K * (lambda + L));   // % duty per °C
    float Ti = T;                        // s, integral time = plant time constant

    // ── sensor-noise budget ─────────────────────────────────────────────────
    // Every degree of reading scatter multiplied by Kc becomes duty dither.
    // Cap the gain so one noise excursion cannot move the fan more than
    // max_dither percent. An unmeasured plant gets a tighter budget because its
    // real noise is not known yet.
    float Kc_noise = dither / noise;
    if (!plant->identified) Kc_noise *= 0.6f;
    if (Kc > Kc_noise) Kc = Kc_noise;
    if (Kc < 0.05f)    Kc = 0.05f;       // never lose all authority

    out->Kp = Kc;
    out->Ki = Kc / Ti;

    // ── measurement filter ──────────────────────────────────────────────────
    // Half the dead time: long enough to average the sensor's quantisation down
    // to a fraction of a degree, short enough not to add a second delay to the
    // loop. Also recovers sub-LSB resolution (the plant is not moved by 0.06 °C).
    out->tau_filter = 0.5f * L;
    if (out->tau_filter < 1.0f) out->tau_filter = 1.0f;

    // ── dead band ───────────────────────────────────────────────────────────
    // Inside half a noise excursion there is nothing to regulate: the reading
    // cannot tell "on target" from "one LSB away", and the integrator would
    // just walk the duty back and forth.
    out->deadband = 0.5f * noise;
    if (out->deadband < 0.15f) out->deadband = 0.15f;

    // ── rate limits ─────────────────────────────────────────────────────────
    // Up: actuator capability. The fan itself must not be the bottleneck when
    // the chip is hot.
    out->slew_up = (plant->slew_up > 1.0f) ? plant->slew_up : 25.0f;
    // Down: needs a TWO-SIDED bound, and both sides are plant properties.
    //
    //  upper bound (do not cool too fast): removing duty D cools the part by
    //    K*D once the plant settles, so the rate whose steady-state temperature
    //    change stays under ~0.05 °C/s is 0.05/K. This is what stops the soak
    //    hand-over (100 -> 6 %) from dumping the heat sink into the chip.
    //  lower bound (do not cool too slowly): the loop must be able to give the
    //    duty back within one plant time constant, otherwise the part UNDERshoots
    //    the target and sits below it for minutes - the fan is still spinning
    //    fast while the temperature has already fallen. 0.6 * 100 / tau is the
    //    rate that completes 60 % of the range in one tau.
    //
    // Without the second bound this produced visible under-cooling: on a plant
    // with K=0.31 the upper bound alone would be 0.16 %/s, i.e. 8 minutes to go
    // from 100 % to 20 %, while tau is only 40 s.
    float dn_hi = 0.05f / K;              // %/s, slowest is not the issue here
    float dn_tau = 60.0f / T;             // %/s, keep up with the plant
    float dn = (dn_tau > dn_hi) ? dn_tau : dn_hi;
    if (dn < 1.0f)  dn = 1.0f;
    if (dn > 15.0f) dn = 15.0f;
    if (plant->slew_down > 0.0f && plant->slew_down < dn) dn = plant->slew_down;
    out->slew_down = dn;

    // The integrator may have to hold 100 % on a hot high-power point, so it is
    // allowed to cover the whole output range; conditional integration (not this
    // clamp) is what prevents windup.
    out->integral_limit = (out->Ki > 0.0f) ? (100.0f / out->Ki) : 0.0f;

    out->lambda_s = lambda;
}

void fan_pid_apply_tuning(fan_pid_t* pid, const fan_pid_tuning_t* t)
{
    pid->Kp = t->Kp;
    pid->Ki = t->Ki;
    pid->Kd = 0.0f;
    pid->deadband       = t->deadband;
    pid->tau_filter     = t->tau_filter;
    pid->slew_up        = t->slew_up;
    pid->slew_down      = t->slew_down;
    pid->integral_limit = t->integral_limit;

    pid->prev_error = 0.0f;
    pid->integral   = 0.0f;
    pid->meas_f     = 0.0f;
    pid->d_f        = 0.0f;
    pid->out        = pid->output_min;
    pid->valid      = false;
}

bool fan_pid_configure(fan_pid_t* pid, const fan_thermal_plant_t* plant)
{
    if (pid == nullptr || plant == nullptr) return false;
    if (!(plant->gain > 1e-4f) || !(plant->tau_s > 0.0f) || !(plant->dead_time_s > 0.0f)) {
        // Unusable description (board table not filled in): keep whatever
        // limits the board set and let the caller install a generic preset.
        pid->prev_error = 0.0f;
        pid->integral   = 0.0f;
        pid->meas_f     = 0.0f;
        pid->d_f        = 0.0f;
        pid->out        = pid->output_min;
        pid->valid      = false;
        return false;
    }
    fan_pid_tuning_t t;
    fan_pid_tuning_from_plant(plant, &t);
    fan_pid_apply_tuning(pid, &t);
    return true;
}

// ---------------------------------------------------------------------------
//  Plant presets
//
//  Only the QAxe++ 2-phase numbers are measured (124.18, 2026-11-08, step tests
//  in docs/baseline/fan_124_18_experiment.csv). Everything else is derived from
//  the same class of hardware and marked identified = false, so the tuner keeps
//  extra margin until someone runs the same step test on that board. Filling one
//  in is a four-number edit, not a re-tune: run the fan at 100 %, then at 50 %,
//  and read K = dT/dDuty, L from the first response, tau from the 63 % point.
// ---------------------------------------------------------------------------
static fan_thermal_plant_t make_plant(float K, float L, float tau,
                                      float noise, float dither,
                                      float slew_up, float slew_down,
                                      bool identified)
{
    fan_thermal_plant_t p;
    p.gain         = K;
    p.dead_time_s  = L;
    p.tau_s        = tau;
    p.sensor_noise = noise;
    p.max_dither   = dither;
    p.slew_up      = slew_up;
    p.slew_down    = slew_down;
    p.identified   = identified;
    return p;
}

fan_thermal_plant_t fan_plant_fixed_blower(void)
{
    // NMAxe: the blower runs at full speed no matter what the PWM says. There is
    // no control loop; the description only exists so the fan table is complete.
    return make_plant(1.0f, 1.0f, 10.0f, 1.0f, 1.0f, 0.0f, 0.0f, true);
}

fan_thermal_plant_t fan_plant_generic_asic(void)
{
    // Unknown board: assume a small die on a decent heat sink fed by a 4-pin
    // blower. Conservative on purpose (identified = false tightens Kp).
    return make_plant(0.20f, 6.0f, 35.0f, 0.25f, 4.0f, 25.0f, 0.0f, false);
}

fan_thermal_plant_t fan_plant_generic_vcore(void)
{
    // Unknown board with a regulator-side temperature sensor: usually noisier
    // and slower than a die sensor.
    return make_plant(0.05f, 10.0f, 60.0f, 1.0f, 4.0f, 25.0f, 0.0f, false);
}

// NMQAxe++ (2-phase, BM1370 x4). MEASURED on 124.18.
//   Vcore: TPS53647 PMBus temperature, 100 %->55 % steps at 69 W.
//          K 0.079 °C/% average over 0-100 %, ~0.03 °C/% in the upper knee.
//          Use the high-duty (least sensitive) gain for stability, the error
//          scheduling adds the authority back at large errors.
fan_thermal_plant_t fan_plant_qaxepp2_asic(void)
{
    // Measured on 124.18 at 555MHz/1170mV, two settled manual steps:
    //   20 % -> 53.3 °C, 80 % -> 34.8 °C   =>  K = 0.31 °C/% (60 duty points)
    //   dead time ~9 s; the step response is NOT symmetric: heating (20->80 %)
    //   has tau ~63 s, cooling (80 %) settles in ~40 s. The design uses the
    //   FASTER direction, because that is the one that sets the stability limit;
    //   designing on the slower tau would raise Kp and ring on a cooldown.
    // The die sensor (TMP102) is far quieter than the VRM one, which is why this
    // loop may use a much larger gain than the Vcore loop on the same board.
    return make_plant(0.31f, 8.6f, 45.0f, 0.20f, 4.0f, 30.0f, 0.0f, true);
}

fan_thermal_plant_t fan_plant_qaxepp2_vcore(void)
{
    // Measured on 124.18 at 555MHz/1170mV (69 W), four manual duty steps:
    //   100->75 %: -0.034 °C/%   75->55 %: -0.075 °C/%   55->75 %: -0.082 °C/%
    //   dead time ~9 s (100 % step), tau 58 s
    // The gain is strongly duty-dependent (the 75-100 % knee is half as
    // sensitive as 55-75 %), so use the value of the band the loop actually
    // works in. The sensor scatters by ~1 °C, which is what really limits Kp.
    return make_plant(0.07f, 9.0f, 58.0f, 1.0f, 4.0f, 30.0f, 0.0f, true);
}

// NMQAxe++ Rev6.1 / Rev8.1 (3-4 phase): same fans and sensors, but the VRM has
// more phases and a bigger copper mass, so the regulator runs cooler per amp and
// responds more slowly. Not measured yet.
fan_thermal_plant_t fan_plant_qaxepp3_asic(void)
{
    return make_plant(0.30f, 6.0f, 45.0f, 0.20f, 4.0f, 30.0f, 0.0f, false);
}

fan_thermal_plant_t fan_plant_qaxepp3_vcore(void)
{
    return make_plant(0.030f, 11.0f, 70.0f, 1.0f, 4.0f, 30.0f, 0.0f, false);
}

// Nexus (3-phase TPS546D24A, series stack): the regulator telemetry is the same
// PMBus temperature channel, the module is larger and slower.
fan_thermal_plant_t fan_plant_nexus_asic(void)
{
    return make_plant(0.28f, 7.0f, 50.0f, 0.20f, 4.0f, 30.0f, 0.0f, false);
}

fan_thermal_plant_t fan_plant_nexus_vcore(void)
{
    return make_plant(0.030f, 12.0f, 75.0f, 1.0f, 4.0f, 30.0f, 0.0f, false);
}

// NMAxeGamma: single BM1370, one fan.
fan_thermal_plant_t fan_plant_gamma_asic(void)
{
    return make_plant(0.25f, 6.0f, 40.0f, 0.20f, 4.0f, 25.0f, 0.0f, false);
}

void fan_pid_reset(fan_pid_t* pid, float measured, float output)
{
    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->d_f = 0.0f;
    pid->out = output;
    if (isfinite(measured)) {
        pid->meas_f = measured;
        pid->valid  = true;
    } else {
        pid->valid  = false;
    }
}

float pid_compute(fan_pid_t* pid, float setpoint, float measured, float dt) {
    // A temperature probe can fail (I2C NACK, PMBus timeout) and hand us NaN.
    // Return NAN to mean "this sample is unusable, no output computed". The
    // caller must not interpret that as 0 %: a fan that stops while the ASIC
    // keeps hashing is the dangerous direction. The integral is dropped because
    // it was accumulated from readings that cannot be trusted.
    if (!isfinite(setpoint) || !isfinite(measured) || !isfinite(dt) || dt <= 0.0f) {
        pid->integral   = 0.0f;
        pid->prev_error = 0.0f;
        pid->valid      = false;
        return NAN;
    }

    // Clamp the step: the loop is meant to run at the temperature-sample rate
    // (~8 Hz). A bigger dt means the caller was blocked (or a takeover just
    // handed the fan back); feeding that value straight into the slew limiter
    // and the integrator turns one missed tick into a full-range jump.
    constexpr float DT_MAX = 0.5f;
    if (dt > DT_MAX) dt = DT_MAX;

    if (!pid->valid) {
        // First sample (or first sample after a handover / probe recovery):
        // seed the filter from the live reading instead of letting it ramp up
        // from 0, which would ask for full speed for a few seconds.
        pid->meas_f = measured;
        pid->d_f    = 0.0f;
        pid->valid  = true;
    }

    // ── measurement low-pass ────────────────────────────────────────────────
    // tau_filter is deliberately several control periods long: the sensor LSB
    // is 1/16 °C, the API rounds to 0.1 °C, and the plant does not move faster
    // than that. Averaging also recovers a usable fraction of a degree instead
    // of letting one LSB become a duty step.
    const float a = dt / (pid->tau_filter + dt);
    const float prev_f = pid->meas_f;
    pid->meas_f += a * (measured - pid->meas_f);

    // Derivative on the *measurement*, not on the error: this avoids a kick
    // when the target is changed from the UI. Kd is 0 by default; the plumbing
    // stays so the damping can be enabled after plant identification.
    const float d_raw = (pid->meas_f - prev_f) / dt;
    const float ad    = dt / (2.0f + dt);
    pid->d_f += ad * (d_raw - pid->d_f);

    // ── error with dead band ────────────────────────────────────────────────
    const float error = pid->meas_f - setpoint;
    float e = error;
    if (fabsf(e) < pid->deadband) e = 0.0f;   // hold the integrator in-band

    // Gain scheduling: near the set point use the gentle gain; when the chip is
    // far away (start-up, new benchmark point) open up so the fan gets there in
    // seconds instead of minutes. The blend is continuous to avoid a limit cycle
    // at the switch-over.
    float w = (fabsf(e) - 2.0f) / 6.0f;
    if (w < 0.0f) w = 0.0f;
    if (w > 1.0f) w = 1.0f;
    const float kp = pid->Kp * (1.0f + 2.0f * w);   // up to 3x far from target

    // ── integral with conditional anti-windup ───────────────────────────────
    float integral_next = pid->integral + e * dt;
    if (pid->integral_limit > 0.0f) {
        if (integral_next >  pid->integral_limit) integral_next =  pid->integral_limit;
        if (integral_next < -pid->integral_limit) integral_next = -pid->integral_limit;
    }

    float u_unsat = kp * e + pid->Ki * integral_next + pid->Kd * pid->d_f;
    float u = u_unsat;
    if (u > pid->output_max) u = pid->output_max;
    if (u < pid->output_min) u = pid->output_min;

    // Only integrate when doing so does not push a saturated output further into
    // the rail. This is what stops a long hot/cold excursion from parking the
    // integrator at +-300 and holding the fan at the rail afterwards.
    const bool sat_high = (u_unsat > pid->output_max);
    const bool sat_low  = (u_unsat < pid->output_min);
    if (!((sat_high && e > 0.0f) || (sat_low && e < 0.0f))) {
        pid->integral = integral_next;
    }
    pid->prev_error = error;

    // ── safety: a large error bypasses the rate limit ───────────────────────
    // Rate-limiting the *approach* to full speed is fine; refusing to cool a
    // chip that is 8 °C over its target is not.
    if (error > 8.0f) {
        pid->out = pid->output_max;
        return pid->out;
    }
    if (error < -8.0f) {
        // Cooling is not needed; still honour the slow down-ramp below so the
        // temperature does not undershoot when a hot load is removed.
        u = pid->output_min;
    }

    // ── asymmetric slew limit ───────────────────────────────────────────────
    // Fast up, slow down. The slow down is the fix for the soak hand-over: the
    // fan leaves 100 % gradually, so the loop can catch the target on the way
    // instead of dropping to 6 % and then overshooting while the heat sink
    // discharges into the chip.
    const float out_prev = pid->out;
    const float du_up   =  pid->slew_up   * dt;
    const float du_down = -pid->slew_down * dt;
    float du = u - out_prev;
    if (du >  du_up)   du =  du_up;
    if (du <  du_down) du =  du_down;
    pid->out = out_prev + du;

    if (pid->out > pid->output_max) pid->out = pid->output_max;
    if (pid->out < pid->output_min) pid->out = pid->output_min;

    // Belt and braces: a non-finite value can still appear from an infinite
    // derivative or a corrupted state. Report it as "no sample" so the caller
    // holds/ramps instead of casting it to a duty (the old code produced
    // 65535 % on the VRM fan this way).
    if (!isfinite(pid->out)) {
        pid->integral = 0.0f;
        pid->valid    = false;
        return NAN;
    }

    LOG_D("target: %.2f, measured: %.2f (filt %.2f), out: %.2f, error: %.2f, integral: %.2f, d: %.3f, dt: %.2f",
          setpoint, measured, pid->meas_f, pid->out, error, pid->integral, pid->d_f, dt);

    return pid->out;
}

uint16_t measure_fan_rpm_for_duration(fan_init_t init_param, float speed, uint32_t duration_ms, bool invert = false) {
    int16_t now_count = 0, last_count = 0;
    uint32_t sum = 0, count = 0;
    uint32_t start_time = 0, last_measure_time = 0;

    fan_set_speed(init_param, speed, invert); // set fan speed to target speed
    delay(500);                       // wait for fan speed to stabilize

    pcnt_counter_clear(init_param.torch.unit);

    start_time        = millis();
    last_measure_time = start_time;
    while (millis() - start_time < duration_ms) {
        if (millis() - last_measure_time >= 100) {
            uint16_t delta_pcnt = 0;
            uint32_t delta_time = millis() - last_measure_time;

            pcnt_get_counter_value(init_param.torch.unit, &now_count);
            if (now_count < last_count) {
                delta_pcnt = (init_param.torch.counter_h_lim - last_count) + now_count;
            } else {
                delta_pcnt = now_count - last_count;
            }
            sum += calculate_rpm(delta_pcnt, delta_time / 1000.0);
            last_count = now_count;
            last_measure_time = millis();
            count++;
        }
        delay(10);
    }
    return (count > 0) ? (sum / count) : 0;
}

bool guess_fan_polarity(fan_init_t init_param) {
    uint16_t rpm_50 = 0, rpm_100 = 0;

    // test at 50% speed
    rpm_50 = measure_fan_rpm_for_duration(init_param, 0.5, 5000);
    LOG_W("Fan speed at 50%% -> %d rpm", rpm_50);

    // test at 100% speed
    rpm_100 = measure_fan_rpm_for_duration(init_param, 1.0, 5000);
    LOG_W("Fan speed at 100%% -> %d rpm", rpm_100);
    
    // Determine polarity
    bool invert = (0.9 * rpm_100) <= rpm_50;

    return invert;
}