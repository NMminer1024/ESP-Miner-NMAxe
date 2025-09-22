#include "logger.h"
#include "device.h"
#include <driver/pcnt.h>
#include "global.h"

#define FAN_PWM_CHANNEL 2
#define FAN_PWM_FREQ 1000*100
#define FAN_PWM_RESOLUTION 8
#define PCNT_H_LIM_VAL 30000
#define PULSES_PER_REVOLUTION 2  

struct pid{
    float Kp, Ki, Kd;
    float prev_error;
    float integral;
    float output_min, output_max;
};


static void fan_init(void){
    pinMode(NM_AXE_FAN_PWM_PIN, OUTPUT);
    ledcSetup(FAN_PWM_CHANNEL, FAN_PWM_FREQ, FAN_PWM_RESOLUTION);
    ledcAttachPin(NM_AXE_FAN_PWM_PIN, FAN_PWM_CHANNEL);

    pcnt_config_t pcnt_config = {
        .pulse_gpio_num = NM_AXE_FAN_PWM_RPM_MEASURE_PIN,
        .ctrl_gpio_num = PCNT_PIN_NOT_USED,
        .lctrl_mode = PCNT_MODE_KEEP,
        .hctrl_mode = PCNT_MODE_KEEP,
        .pos_mode = PCNT_COUNT_INC,   
        .neg_mode = PCNT_COUNT_DIS,   
        .counter_h_lim = PCNT_H_LIM_VAL,
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

static void fan_set_speed(float speed){
    float spd = (g_board.preference.fan.invert_ploarity) ? (1.0f - speed):speed;
    uint32_t dutyCycle = (uint32_t)(spd * (( 1 << FAN_PWM_RESOLUTION) - 1));
    ledcWrite(FAN_PWM_CHANNEL, dutyCycle);
}

static uint16_t calculate_rpm(int16_t pulse_count, double time_seconds) {
    return (uint16_t)((pulse_count * 60.0) / (time_seconds * PULSES_PER_REVOLUTION));
}

float pid_compute(pid* pid, float setpoint, float measured, float dt) {
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

void fan_thread_entry(void *args){
    char *name = (char*)malloc(20);
    strcpy(name, (char*)args);
    LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
    free(name);

    int16_t now_count = 0, last_count = 0,temp_cnt = 0;
    pid fan_pid = {
        .Kp = 100.0f,
        .Ki = 1.0f,
        .Kd = 50.0f,
        .prev_error = 0,
        .integral = 0,
        .output_min = 25.0f,
        .output_max = 100.0f
    };

    tmp102_init();
    fan_init();

    while(1){
        delay(125);// 8Hz
        //update board temperature
        g_board.status.temp.mcu    = (temp_cnt % 300 == 0) ? (float)get_mcu_temperature() : g_board.status.temp.mcu;
        g_board.status.temp.vcore  = (temp_cnt % 20 == 0) ? (float)get_vcore_temperature() : g_board.status.temp.vcore;
        g_board.status.temp.asic   = (temp_cnt % 1 == 0) ? (float)get_asic_temperature() : g_board.status.temp.asic;

        // Round to 1 decimal place
        g_board.status.temp.mcu   = roundf(g_board.status.temp.mcu * 10) / 10.0f;
        g_board.status.temp.vcore = roundf(g_board.status.temp.vcore * 10) / 10.0f;
        g_board.status.temp.asic  = roundf(g_board.status.temp.asic * 100) / 100.0f;
        temp_cnt++;
        
        // Fan self test flag set only once
        if(!g_board.preference.fan.self_test){
            g_board.preference.fan.self_test = (g_board.preference.fan.rpm > FAN_FULL_RPM_MIN) ? true : false;
            fan_set_speed(100.0 / 100.0);
        }

        // Calculate fan RPM
        static uint32_t start_ms = millis();
        if(millis() - start_ms > 1000){
            pcnt_get_counter_value(PCNT_UNIT_0, &now_count);
            uint16_t delta_pcnt = 0;
            if (now_count < last_count) delta_pcnt = (PCNT_H_LIM_VAL - last_count) + now_count;
            else delta_pcnt = now_count - last_count;
            
            g_board.preference.fan.rpm = calculate_rpm(delta_pcnt, (millis() - start_ms) / 1000.0);
            last_count = now_count;
            start_ms = millis();
        }

        if(!g_board.preference.fan.self_test)continue;

        if(g_board.preference.fan.is_auto_speed && g_board.preference.fan.self_test){
            static uint32_t pid_start = millis();
            float dt = (millis() - pid_start) / 1000.0f; // Convert to seconds
            g_board.preference.fan.speed = (uint16_t)pid_compute(&fan_pid, g_board.preference.fan.target_temp, g_board.status.temp.asic, dt);
            pid_start = millis();
        }
        fan_set_speed(g_board.preference.fan.speed / 100.0);
    }
}