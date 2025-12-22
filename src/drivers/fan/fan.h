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
    uint8_t  pwm_pin;
    uint8_t  torch_pin;
    uint8_t  pwm_ch;
    uint32_t pwm_freq; // Hz
    uint8_t  pwm_revolution;      // bits
    uint32_t p_cnt_h_limt;   // PCNT high limit value
}fan_init_param_t;



void fan_init(fan_init_param_t init_param);
void measure_fan_rpm_for_duration(float speed, uint32_t duration_ms, uint16_t &measured_rpm, bool invert);
bool guess_fan_polarity(void);
void fan_set_speed(float speed, bool invert);
uint16_t calculate_rpm(int16_t pulse_count, double time_seconds);
float pid_compute(fan_pid_t* pid, float setpoint, float measured, float dt);
#endif 