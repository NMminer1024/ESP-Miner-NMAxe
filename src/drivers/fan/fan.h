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
    bool       polarity; // false: normal, true: inverted
    fan_init_t init;
    fan_pid_t  pid;
}fan_config_t;

typedef struct{
    uint8_t     id;        // Fan identifier
    bool        self_test; // Self-test status
    uint16_t    speed;     //%
    uint16_t    rpm;       //RPM
}fan_status_t;


void fan_drv_init(fan_init_t init_param);
uint16_t measure_fan_rpm_for_duration(fan_init_t init_param, float speed, uint32_t duration_ms);
bool guess_fan_polarity(fan_init_t init_param);
void fan_set_speed(fan_init_t init_param, float speed, bool invert);
uint16_t calculate_rpm(int16_t pulse_count, double time_seconds);
float pid_compute(fan_pid_t* pid, float setpoint, float measured, float dt);
#endif 