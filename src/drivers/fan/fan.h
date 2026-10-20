#ifndef _FAN___H_
#define _FAN___H_
#include <Arduino.h>
#include <driver/pcnt.h>


typedef struct{
    float Kp, Ki, Kd;
    float prev_error;
    float integral;
    float output_min, output_max;
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


typedef struct{
    uint8_t    id;
    bool       polarity;   // false: normal, true: inverted
    bool       auto_speed; // whether to automatically adjust speed based on temperature
    float      target_temp; // target temperature for automatic speed control
    fan_init_t init;
    fan_pid_t  pid;
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
#endif 