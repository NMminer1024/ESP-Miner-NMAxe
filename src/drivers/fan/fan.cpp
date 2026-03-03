#include "utils/logger/logger.h"
#include "board/board.h"
#include "global.h"
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

float pid_compute(fan_pid_t* pid, float setpoint, float measured, float dt) {
    const uint16_t MAX_INTEGRAL = 300.0f;
    float error = measured - setpoint;
    pid->integral += error * dt;

    if(pid->integral > MAX_INTEGRAL) pid->integral = MAX_INTEGRAL;
    if(pid->integral < -MAX_INTEGRAL) pid->integral = -MAX_INTEGRAL;

    float derivative = (error - pid->prev_error) / dt;
    float output = pid->Kp * error + pid->Ki * pid->integral + pid->Kd * derivative;
    pid->prev_error = error;

    if (output > pid->output_max) output = pid->output_max;
    if (output < pid->output_min) output = pid->output_min;
    LOG_D("target: %.2f, measured: %.2f, output: %.2f, error: %.2f, integral: %.2f, derivative: %.2f, dt: %.2f",
          setpoint, measured, output, error, pid->integral, derivative, dt);

    return output;
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