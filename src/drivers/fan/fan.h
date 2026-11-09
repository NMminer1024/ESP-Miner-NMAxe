#ifndef _FAN___H_
#define _FAN___H_
#include <Arduino.h>
#include <driver/pcnt.h>


typedef struct{
    // ── tuning ──────────────────────────────────────────────────────────────
    float Kp, Ki, Kd;
    float output_min, output_max;
    // ── smoothing / robustness ──────────────────────────────────────────────
    // The PID runs on a temperature that is quantised by the sensor (TMP102 and
    // the TPS53647 both report 1/16 °C, rounded to 0.1 °C for the API) and on a
    // plant whose response lags the duty by tens of seconds. A large Kp turns
    // that combination into a bang-bang controller: a 0.7 °C move was enough to
    // swing the VRM fan 35 duty points. These fields make the loop behave like
    // a slow thermal regulator instead.
    float deadband;        // °C, error ignored inside this band (integrator hold)
    float tau_filter;      // s, low-pass on the measured temperature
    float slew_up;         // %/s, fastest allowed duty increase
    float slew_down;       // %/s, fastest allowed duty decrease (asymmetric: slow)
    float integral_limit;  // °C·s, clamp on the integral state
    // Applied-duty hysteresis (%). The continuous demand may move, but the value
    // sent to the fan only follows once it has drifted this far away from what
    // the fan is really doing. This is what stops a sensor with a coarse LSB from
    // turning into permanent ±1 % duty dither; 0 = disabled.
    float duty_hyst;
    // ── state ───────────────────────────────────────────────────────────────
    float prev_error;      // kept for backward compatibility
    float integral;
    float meas_f;          // filtered temperature
    float d_f;             // filtered dT/dt (measurement derivative)
    float out;             // last *float* duty produced (NOT rounded)
    bool  valid;           // filter/output initialised
}fan_pid_t;

typedef struct{
    struct {
        uint8_t       pin;
        uint8_t       ch;
        uint32_t      freq; // Hz
        uint8_t       resolution; // bits
    }pwm;
    pcnt_config_t torch;
    uint16_t     self_test_rpm_thr; // RPM, minimum RPM when fan is at full speed in self-test
    uint16_t     danger_rpm_thr;    // RPM, if RPM is below this value during operation, it's considered a fault
}fan_init_t;


// ============================================================================
//  Thermal plant description (one per board, one per fan)
//
//  A temperature-controlled fan is a first-order-plus-dead-time (FOPDT) plant:
//
//      dT/dt = ( K * u(t - L) - (T - T_amb) ) / tau
//
//  with K = °C per % duty, L = dead time (air path + sensor response) and
//  tau = dominant thermal time constant. The PID gains are NOT free parameters:
//  they follow from the plant plus the noise budget of the sensor. Describing
//  the PLANT per board/fan instead of copying gains means
//    - a new board needs four numbers from one step test, not a tuning session,
//    - boards with the same hardware share one entry instead of four literals,
//    - the tuner can enforce robustness (no gain above what the sensor noise
//      allows) instead of leaving that to whoever typed the last Kp.
// ============================================================================
typedef struct{
    float gain;          // K   : |dT/dDuty| °C per % near the operating point
    float dead_time_s;   // L   : air path + sensor/CMOS response delay
    float tau_s;         // tau : dominant thermal time constant
    float sensor_noise;  // °C  : worst-case reading scatter/resolution
    float sensor_lsb;    // °C  : quantisation step of the REPORTED reading
    float max_dither;    // %   : duty scatter the sensor noise may cause
    float slew_up;       // %/s : actuator limit, speeding up
    float slew_down;     // %/s : how fast cooling may be removed
    bool  identified;    // true = measured on hardware, false = engineering guess
}fan_thermal_plant_t;

// Gains produced by the tuner. Derived, never typed by hand.
typedef struct{
    float Kp;            // % per °C
    float Ki;            // % per (°C·s)
    float deadband;      // °C
    float tau_filter;    // s, measurement low-pass
    float slew_up;       // %/s
    float slew_down;     // %/s
    float integral_limit;// °C·s
    float duty_hyst;     // %, applied-duty hysteresis (0 = off)
    float lambda_s;      // s, resulting closed-loop time constant (for logs)
}fan_pid_tuning_t;

typedef struct{
    uint8_t    id;
    bool       polarity;   // false: normal, true: inverted
    bool       auto_speed; // whether to automatically adjust speed based on temperature
    float      target_temp; // target temperature for automatic speed control
    fan_init_t init;
    fan_pid_t  pid;
    fan_thermal_plant_t thermal; // plant + noise budget for this fan on this board
}fan_config_t;

// Keep the auto-fan target below the over-temperature trip point by this margin,
// otherwise the PID would hold the temperature inside the protection zone.
#define FAN_TARGET_TEMP_MARGIN_C  5.0f

static inline float fan_clamp_target_temp(float target, float limit_high)
{
    float max_t = limit_high - FAN_TARGET_TEMP_MARGIN_C;
    if (target < 0.0f) return 0.0f;
    return (target > max_t) ? max_t : target;
}

// Tri-state self-test result so the UI can distinguish "still testing"
// from "tested and failed" (plain bool self_test is false in both cases).
// Written by the fan self-test task AFTER rpm/self_test are updated, so the
// UI can treat a non-zero state as "the rpm next to it is this round's result".
enum fan_self_test_state_t : uint8_t {
    FAN_SELF_TEST_TESTING = 0,
    FAN_SELF_TEST_PASS    = 1,
    FAN_SELF_TEST_FAIL    = 2,
};

typedef struct{
    uint8_t     id;        // Fan identifier
    bool        self_test; // Self-test status
    uint8_t     self_test_state; // fan_self_test_state_t: last completed self-test round result
    uint16_t    speed;     //%
    uint16_t    rpm;       //RPM
}fan_status_t;


void fan_drv_init(fan_init_t init_param);
uint16_t measure_fan_rpm_for_duration(fan_init_t init_param, float speed, uint32_t duration_ms, bool invert);
bool guess_fan_polarity(fan_init_t init_param);
void fan_set_speed(fan_init_t init_param, float speed, bool invert);
uint16_t calculate_rpm(int16_t pulse_count, double time_seconds);
float pid_compute(fan_pid_t* pid, float setpoint, float measured, float dt);

// ---------------------------------------------------------------------------
//  Plant presets.
//  Values are per BOARD (thermal mass + airflow layout + sensor), not per chip.
//  `identified` says whether a step test on real hardware backs the numbers;
//  unmeasured presets are deliberately conservative (see fan_pid_tuning_from_plant).
// ---------------------------------------------------------------------------
fan_thermal_plant_t fan_plant_fixed_blower(void);   // PWM has no effect on airflow
fan_thermal_plant_t fan_plant_generic_asic(void);   // fallback: unknown board
fan_thermal_plant_t fan_plant_generic_vcore(void);  // fallback: unknown board

fan_thermal_plant_t fan_plant_qaxepp2_asic(void);   // NMQAxe++     (2-phase, TMP102)
fan_thermal_plant_t fan_plant_qaxepp2_vcore(void);  // NMQAxe++     (2-phase, TPS53647)
fan_thermal_plant_t fan_plant_qaxepp3_asic(void);   // Rev6.1/Rev8.1 (3/4-phase)
fan_thermal_plant_t fan_plant_qaxepp3_vcore(void);
fan_thermal_plant_t fan_plant_nexus_asic(void);     // Nexus (3-phase TPS546D24A)
fan_thermal_plant_t fan_plant_nexus_vcore(void);
fan_thermal_plant_t fan_plant_gamma_asic(void);     // NMAxeGamma (1 chip, 1 fan)

// Derive PID gains from the plant. rule: SIMC/lambda tuning for FOPDT, then
// clamp the proportional gain so sensor noise cannot become duty dither.
void fan_pid_tuning_from_plant(const fan_thermal_plant_t* plant, fan_pid_tuning_t* out);

// Apply a tuning set, resetting all loop state.
void fan_pid_apply_tuning(fan_pid_t* pid, const fan_pid_tuning_t* tuning);

// Configure a fan's loop from its plant; returns false when the plant description
// is unusable (caller then falls back to a generic preset).
bool fan_pid_configure(fan_pid_t* pid, const fan_thermal_plant_t* plant);

// Re-seed the loop after a manual/benchmark takeover or a sensor fault so the
// next output starts from the duty that is actually applied right now and the
// temperature filter does not carry a stale value across the handover.
void fan_pid_reset(fan_pid_t* pid, float measured, float output);
#endif 