#include "logger.h"
#include "device.h"
#include <driver/pcnt.h>
#include "global.h"

#define FAN_PWM_CHANNEL 2
#define FAN_PWM_FREQ 1000*100
#define FAN_PWM_RESOLUTION 8
#define PCNT_H_LIM_VAL 30000
#define PULSES_PER_REVOLUTION 2  

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
    float spd = (g_nmaxe.fan.invert_ploarity) ? (1.0f - speed):speed;
    uint32_t dutyCycle = (uint32_t)(spd * (( 1 << FAN_PWM_RESOLUTION) - 1));
    ledcWrite(FAN_PWM_CHANNEL, dutyCycle);
}

static uint16_t calculate_rpm(int16_t pulse_count, double time_seconds) {
    return (uint16_t)((pulse_count * 60.0) / (time_seconds * PULSES_PER_REVOLUTION));
}

void fan_thread_entry(void *args){
    char *name = (char*)malloc(20);
    strcpy(name, (char*)args);
    LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
    free(name);

    uint32_t start_ms = millis();
    int16_t now_count = 0, last_count = 0;
    fan_init();
    while(1){
        if(g_nmaxe.fan.is_auto_speed){
            // Linearly increase fan speed from 40 to 60 degrees
            g_nmaxe.fan.speed = (g_nmaxe.asic.temp < 40.0f) ? 0.0f :
                                (g_nmaxe.asic.temp > 60.0f) ? 100.0f :
                                (g_nmaxe.asic.temp - 40.0f) * (100.0f / 20.0f);
        }
        fan_set_speed(g_nmaxe.fan.speed / 100.0f);

        if(millis() - start_ms > 1000){
            pcnt_get_counter_value(PCNT_UNIT_0, &now_count);
            uint16_t delta_pcnt = 0;
            if (now_count < last_count) delta_pcnt = (PCNT_H_LIM_VAL - last_count) + now_count;
            else delta_pcnt = now_count - last_count;
            
            g_nmaxe.fan.rpm = calculate_rpm(delta_pcnt, (millis() - start_ms) / 1000.0);
            last_count = now_count;
            start_ms = millis();
        }

        // Fan self test flag set only once
        if(!g_nmaxe.fan.self_test){
            g_nmaxe.fan.self_test = (g_nmaxe.fan.rpm > FAN_FULL_RPM_MIN) ? true : false;
        }
        delay(100);
    }
}