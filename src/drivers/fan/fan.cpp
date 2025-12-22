#include "logger.h"
#include "board.h"
#include "global.h"
#include "fan.h"

#define PULSES_PER_REVOLUTION 2        // Number of pulses per fan revolution

static fan_init_param_t fan_init_params;


void fan_init(fan_init_param_t init_param){
    fan_init_params = init_param;
    pinMode(init_param.pwm_pin, OUTPUT);
    ledcSetup(init_param.pwm_ch, init_param.pwm_freq, init_param.pwm_revolution);
    ledcAttachPin(init_param.pwm_pin, init_param.pwm_ch);

    pcnt_config_t pcnt_config = {
        .pulse_gpio_num = init_param.torch_pin,
        .ctrl_gpio_num = PCNT_PIN_NOT_USED,
        .lctrl_mode = PCNT_MODE_KEEP,
        .hctrl_mode = PCNT_MODE_KEEP,
        .pos_mode = PCNT_COUNT_INC,   
        .neg_mode = PCNT_COUNT_DIS,   
        .counter_h_lim = init_param.p_cnt_h_limt,
        .counter_l_lim = 0,           
        .unit = PCNT_UNIT_0,
        .channel = PCNT_CHANNEL_0
    };

    pcnt_unit_config(&pcnt_config);
    pcnt_set_filter_value(PCNT_UNIT_0, 100);
    pcnt_filter_enable(PCNT_UNIT_0);
    pcnt_counter_pause(PCNT_UNIT_0);
    pcnt_counter_clear(PCNT_UNIT_0);
    pcnt_counter_resume(PCNT_UNIT_0);
}


void fan_set_speed(float speed, bool invert = false){
    float spd = (invert) ? (1.0f - speed):speed;
    uint32_t dutyCycle = (uint32_t)(spd * (( 1 << fan_init_params.pwm_revolution) - 1));
    ledcWrite(fan_init_params.pwm_ch, dutyCycle);
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

void measure_fan_rpm_for_duration(float speed, uint32_t duration_ms, uint16_t &measured_rpm, bool invert = false) {
    int16_t now_count = 0, last_count = 0;
    uint32_t start_time = millis();
    uint32_t last_measure_time = start_time;

    fan_set_speed(speed, invert);

    while (millis() - start_time < duration_ms) {
        if (millis() - last_measure_time >= 100) {
            pcnt_get_counter_value(PCNT_UNIT_0, &now_count);
            uint16_t delta_pcnt = 0;
            if (now_count < last_count) {
                delta_pcnt = (fan_init_params.p_cnt_h_limt - last_count) + now_count;
            } else {
                delta_pcnt = now_count - last_count;
            }
            
            measured_rpm = calculate_rpm(delta_pcnt, (millis() - last_measure_time) / 1000.0);
            last_count = now_count;
            last_measure_time = millis();
        }
        delay(10);
    }
}

bool guess_fan_polarity(void) {
    LOG_I("Guessing fan polarity, please wait...");
    uint16_t rpm_50 = 0, rpm_100 = 0;

    // test at 50% speed
    measure_fan_rpm_for_duration(0.5, 2000, rpm_50);
    LOG_I("Fan RPM at 50%% speed: %d", rpm_50);

    // test at 100% speed
    measure_fan_rpm_for_duration(1.0, 2000, rpm_100);
    LOG_I("Fan RPM at 100%% speed: %d", rpm_100);
    
    // Determine polarity
    bool invert = (0.9 * rpm_100) <= rpm_50;
    
    LOG_W("Fan polarity %s", invert ? "inverted" : "normal");
    return invert;
}