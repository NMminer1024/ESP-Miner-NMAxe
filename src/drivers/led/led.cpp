#include "led.h"
#include "board.h"
#include "Arduino.h"
#include "logger.h"
#include "global.h"
#include "stratum.h"

void led_thread_entry(void *args){
    board_sal_t *baord = (board_sal_t*)args;

    String taskName = "(led)";
    LOG_I("%s thread started on core %d...", taskName, xPortGetCoreID());

    LOG_I("Initializing led...");

    // LED pins setup
    const int pwmChannel = 3;   
    const int freq = 5*1000;    
    const int resolution = 8;   
    pinMode(baord->info.led_spec.wifi_pin, OUTPUT);
    digitalWrite(baord->info.led_spec.wifi_pin, HIGH);

    pinMode(baord->info.led_spec.pool_pin, OUTPUT);
    digitalWrite(baord->info.led_spec.pool_pin, HIGH);

    pinMode(baord->info.led_spec.sys_pin, OUTPUT);
    ledcSetup(pwmChannel, freq, resolution);
    ledcAttachPin(baord->info.led_spec.sys_pin, pwmChannel);
    ledcWrite(pwmChannel, 255);// off

    uint64_t led_cnt = 0;
    const uint8_t  dot = 20;
    while(true){
        delay(10);

        if(baord->info.preference.led.sleep) {
            // led_off();
            digitalWrite(baord->info.led_spec.wifi_pin, HIGH);
            digitalWrite(baord->info.led_spec.pool_pin, HIGH);
            ledcWrite(pwmChannel, 255);
            continue;
        }

        if(!baord->info.preference.led.enable){
            // led_off();
            digitalWrite(baord->info.led_spec.wifi_pin, HIGH);
            digitalWrite(baord->info.led_spec.pool_pin, HIGH);
            ledcWrite(pwmChannel, 255);
            continue;
        }
           

        switch (led_cnt % 201){
        case 1 * dot:
                if(baord->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(baord->info.led_spec.wifi_pin, LOW);
                else digitalWrite(baord->info.led_spec.wifi_pin, HIGH);

                if(baord->stratum->is_subscribed()) digitalWrite(baord->info.led_spec.pool_pin, LOW);
                else digitalWrite(baord->info.led_spec.pool_pin, HIGH);

            break;
        case 2 * dot:
                if(baord->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(baord->info.led_spec.wifi_pin, HIGH);
                else digitalWrite(baord->info.led_spec.wifi_pin, LOW);

                if(baord->stratum->is_subscribed()) digitalWrite(baord->info.led_spec.pool_pin, HIGH);
                else digitalWrite(baord->info.led_spec.pool_pin, LOW);

            break;
        case 3 * dot:
                if(baord->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(baord->info.led_spec.wifi_pin, HIGH);
                else digitalWrite(baord->info.led_spec.wifi_pin, HIGH);

                if(baord->stratum->is_subscribed()) digitalWrite(baord->info.led_spec.pool_pin, HIGH);
                else digitalWrite(baord->info.led_spec.pool_pin, HIGH);
            break;
        case 4 * dot:
                if(baord->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(baord->info.led_spec.wifi_pin, HIGH);
                else digitalWrite(baord->info.led_spec.wifi_pin, LOW);

                if(baord->stratum->is_subscribed()) digitalWrite(baord->info.led_spec.pool_pin, HIGH);
                else digitalWrite(baord->info.led_spec.pool_pin, LOW);

            break;
        case 5 * dot:
                if(baord->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(baord->info.led_spec.wifi_pin, HIGH);
                else digitalWrite(baord->info.led_spec.wifi_pin, HIGH);

                if(baord->stratum->is_subscribed()) digitalWrite(baord->info.led_spec.pool_pin, HIGH);
                else digitalWrite(baord->info.led_spec.pool_pin, HIGH);
            break;
        case 6 * dot:
                if(baord->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(baord->info.led_spec.wifi_pin, HIGH);
                else digitalWrite(baord->info.led_spec.wifi_pin, LOW);

                if(baord->stratum->is_subscribed()) digitalWrite(baord->info.led_spec.pool_pin, HIGH);
                else digitalWrite(baord->info.led_spec.pool_pin, LOW);
            break;
        case 7 * dot:
                if(baord->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(baord->info.led_spec.wifi_pin, HIGH);
                else digitalWrite(baord->info.led_spec.wifi_pin, HIGH);

                if(baord->stratum->is_subscribed()) digitalWrite(baord->info.led_spec.pool_pin, HIGH);
                else digitalWrite(baord->info.led_spec.pool_pin, HIGH);
            break;
        case 8 * dot:
                if(baord->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(baord->info.led_spec.wifi_pin, HIGH);
                else digitalWrite(baord->info.led_spec.wifi_pin, LOW);

                if(baord->stratum->is_subscribed()) digitalWrite(baord->info.led_spec.pool_pin, HIGH);
                else digitalWrite(baord->info.led_spec.pool_pin, LOW);
            break;
        case 9 * dot:
                if(baord->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(baord->info.led_spec.wifi_pin, HIGH);
                else digitalWrite(baord->info.led_spec.wifi_pin, HIGH);

                if(baord->stratum->is_subscribed()) digitalWrite(baord->info.led_spec.pool_pin, HIGH);
                else digitalWrite(baord->info.led_spec.pool_pin, HIGH);
            break;
        case 10 * dot:
                if(baord->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(baord->info.led_spec.wifi_pin, HIGH);
                else digitalWrite(baord->info.led_spec.wifi_pin, LOW);

                if(baord->stratum->is_subscribed()) digitalWrite(baord->info.led_spec.pool_pin, HIGH);
                else digitalWrite(baord->info.led_spec.pool_pin, LOW);
            break;
        }

        // sys led, indicate hashrate
        if(baord->status.miner.hashrate._3m > 0){
            ledcWrite(pwmChannel, (uint32_t)((1 + sin(led_cnt/100.0f)) * (1<<resolution - 1)));
        }
        else{
            ledcWrite(pwmChannel, (uint32_t)((1 + sin(20*led_cnt/100.0f)) * (1<<resolution - 1)));
        }
        led_cnt++;
    }
    LOG_I("led thread exit...");
    vTaskDelete(NULL);
}
