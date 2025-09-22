#include "led.h"
#include "device.h"
#include "Arduino.h"
#include "logger.h"
#include "global.h"
#include "stratum.h"

static int pwmChannel = 3;   
static int freq = 5*1000;    
static int resolution = 8;   
static int sysledPin = NM_AXE_LED_SYS_STA_PIN;      

static void ledInit(void){
    pinMode(NM_AXE_LED_WIFI_STA_PIN, OUTPUT);
    digitalWrite(NM_AXE_LED_WIFI_STA_PIN, HIGH);

    pinMode(NM_AXE_LED_POOL_STA_PIN, OUTPUT);
    digitalWrite(NM_AXE_LED_POOL_STA_PIN, HIGH);

    pinMode(sysledPin, OUTPUT);
    ledcSetup(pwmChannel, freq, resolution);
    ledcAttachPin(sysledPin, pwmChannel);
    ledcWrite(pwmChannel, 255);
}


static void led_off(void){
    digitalWrite(NM_AXE_LED_WIFI_STA_PIN, HIGH);
    digitalWrite(NM_AXE_LED_POOL_STA_PIN, HIGH);
    ledcWrite(pwmChannel, 255);
}


void led_thread_entry(void *args){
    char *name = (char*)malloc(20);
    strcpy(name, (char*)args);
    LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
    free(name);
    LOG_I("Initializing led...");


    ledInit();
    uint64_t led_cnt = 0;
    const uint8_t  dot = 20;
    while(true){
        delay(10);

        if(g_board.preference.led.sleep) {
            led_off();
            continue;
        }

        if(!g_board.preference.led.enable){
            led_off();
            continue;
        }
           

        switch (led_cnt % 201){
        case 1 * dot:
                if(g_board.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(NM_AXE_LED_WIFI_STA_PIN, LOW);
                else digitalWrite(NM_AXE_LED_WIFI_STA_PIN, HIGH);

                if(g_board.stratum->is_subscribed()) digitalWrite(NM_AXE_LED_POOL_STA_PIN, LOW);
                else digitalWrite(NM_AXE_LED_POOL_STA_PIN, HIGH);

            break;
        case 2 * dot:
                if(g_board.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(NM_AXE_LED_WIFI_STA_PIN, HIGH);
                else digitalWrite(NM_AXE_LED_WIFI_STA_PIN, LOW);

                if(g_board.stratum->is_subscribed()) digitalWrite(NM_AXE_LED_POOL_STA_PIN, HIGH);
                else digitalWrite(NM_AXE_LED_POOL_STA_PIN, LOW);

            break;
        case 3 * dot:
                if(g_board.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(NM_AXE_LED_WIFI_STA_PIN, HIGH);
                else digitalWrite(NM_AXE_LED_WIFI_STA_PIN, HIGH);

                if(g_board.stratum->is_subscribed()) digitalWrite(NM_AXE_LED_POOL_STA_PIN, HIGH);
                else digitalWrite(NM_AXE_LED_POOL_STA_PIN, HIGH);
            break;
        case 4 * dot:
                if(g_board.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(NM_AXE_LED_WIFI_STA_PIN, HIGH);
                else digitalWrite(NM_AXE_LED_WIFI_STA_PIN, LOW);

                if(g_board.stratum->is_subscribed()) digitalWrite(NM_AXE_LED_POOL_STA_PIN, HIGH);
                else digitalWrite(NM_AXE_LED_POOL_STA_PIN, LOW);

            break;
        case 5 * dot:
                if(g_board.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(NM_AXE_LED_WIFI_STA_PIN, HIGH);
                else digitalWrite(NM_AXE_LED_WIFI_STA_PIN, HIGH);

                if(g_board.stratum->is_subscribed()) digitalWrite(NM_AXE_LED_POOL_STA_PIN, HIGH);
                else digitalWrite(NM_AXE_LED_POOL_STA_PIN, HIGH);
            break;
        case 6 * dot:
                if(g_board.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(NM_AXE_LED_WIFI_STA_PIN, HIGH);
                else digitalWrite(NM_AXE_LED_WIFI_STA_PIN, LOW);

                if(g_board.stratum->is_subscribed()) digitalWrite(NM_AXE_LED_POOL_STA_PIN, HIGH);
                else digitalWrite(NM_AXE_LED_POOL_STA_PIN, LOW);
            break;
        case 7 * dot:
                if(g_board.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(NM_AXE_LED_WIFI_STA_PIN, HIGH);
                else digitalWrite(NM_AXE_LED_WIFI_STA_PIN, HIGH);

                if(g_board.stratum->is_subscribed()) digitalWrite(NM_AXE_LED_POOL_STA_PIN, HIGH);
                else digitalWrite(NM_AXE_LED_POOL_STA_PIN, HIGH);
            break;
        case 8 * dot:
                if(g_board.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(NM_AXE_LED_WIFI_STA_PIN, HIGH);
                else digitalWrite(NM_AXE_LED_WIFI_STA_PIN, LOW);

                if(g_board.stratum->is_subscribed()) digitalWrite(NM_AXE_LED_POOL_STA_PIN, HIGH);
                else digitalWrite(NM_AXE_LED_POOL_STA_PIN, LOW);
            break;
        case 9 * dot:
                if(g_board.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(NM_AXE_LED_WIFI_STA_PIN, HIGH);
                else digitalWrite(NM_AXE_LED_WIFI_STA_PIN, HIGH);

                if(g_board.stratum->is_subscribed()) digitalWrite(NM_AXE_LED_POOL_STA_PIN, HIGH);
                else digitalWrite(NM_AXE_LED_POOL_STA_PIN, HIGH);
            break;
        case 10 * dot:
                if(g_board.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(NM_AXE_LED_WIFI_STA_PIN, HIGH);
                else digitalWrite(NM_AXE_LED_WIFI_STA_PIN, LOW);

                if(g_board.stratum->is_subscribed()) digitalWrite(NM_AXE_LED_POOL_STA_PIN, HIGH);
                else digitalWrite(NM_AXE_LED_POOL_STA_PIN, LOW);
            break;
        }

        // sys led, indicate hashrate
        if(g_board.mstatus.hashrate._3m > 0){
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
