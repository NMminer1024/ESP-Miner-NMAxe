#include "led.h"
#include "device.h"
#include "Arduino.h"
#include "logger.h"
#include "global.h"
#include "stratum.h"

static int pwmChannel = 3;   
static int freq = 5*1000;    
static int resolution = 8;   
static int sysledPin = LED_SYS_STA_PIN;      

static void ledInit(void){
    pinMode(LED_WIFI_STA_PIN, OUTPUT);
    digitalWrite(LED_WIFI_STA_PIN, HIGH);

    pinMode(LED_POOL_STA_PIN, OUTPUT);
    digitalWrite(LED_POOL_STA_PIN, HIGH);

    pinMode(sysledPin, OUTPUT);
    ledcSetup(pwmChannel, freq, resolution);
    ledcAttachPin(sysledPin, pwmChannel);
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
    while(g_nmaxe.led.indicator){
        switch (led_cnt % 201){
        case 1 * dot:
                if(g_nmaxe.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(LED_WIFI_STA_PIN, LOW);
                else digitalWrite(LED_WIFI_STA_PIN, HIGH);

                if(g_nmaxe.stratum.is_subscribed()) digitalWrite(LED_POOL_STA_PIN, LOW);
                else digitalWrite(LED_POOL_STA_PIN, HIGH);

            break;
        case 2 * dot:
                if(g_nmaxe.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(LED_WIFI_STA_PIN, HIGH);
                else digitalWrite(LED_WIFI_STA_PIN, LOW);

                if(g_nmaxe.stratum.is_subscribed()) digitalWrite(LED_POOL_STA_PIN, HIGH);
                else digitalWrite(LED_POOL_STA_PIN, LOW);

            break;
        case 3 * dot:
                if(g_nmaxe.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(LED_WIFI_STA_PIN, HIGH);
                else digitalWrite(LED_WIFI_STA_PIN, HIGH);

                if(g_nmaxe.stratum.is_subscribed()) digitalWrite(LED_POOL_STA_PIN, HIGH);
                else digitalWrite(LED_POOL_STA_PIN, HIGH);
            break;
        case 4 * dot:
                if(g_nmaxe.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(LED_WIFI_STA_PIN, HIGH);
                else digitalWrite(LED_WIFI_STA_PIN, LOW);

                if(g_nmaxe.stratum.is_subscribed()) digitalWrite(LED_POOL_STA_PIN, HIGH);
                else digitalWrite(LED_POOL_STA_PIN, LOW);

            break;
        case 5 * dot:
                if(g_nmaxe.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(LED_WIFI_STA_PIN, HIGH);
                else digitalWrite(LED_WIFI_STA_PIN, HIGH);

                if(g_nmaxe.stratum.is_subscribed()) digitalWrite(LED_POOL_STA_PIN, HIGH);
                else digitalWrite(LED_POOL_STA_PIN, HIGH);
            break;
        case 6 * dot:
                if(g_nmaxe.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(LED_WIFI_STA_PIN, HIGH);
                else digitalWrite(LED_WIFI_STA_PIN, LOW);

                if(g_nmaxe.stratum.is_subscribed()) digitalWrite(LED_POOL_STA_PIN, HIGH);
                else digitalWrite(LED_POOL_STA_PIN, LOW);
            break;
        case 7 * dot:
                if(g_nmaxe.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(LED_WIFI_STA_PIN, HIGH);
                else digitalWrite(LED_WIFI_STA_PIN, HIGH);

                if(g_nmaxe.stratum.is_subscribed()) digitalWrite(LED_POOL_STA_PIN, HIGH);
                else digitalWrite(LED_POOL_STA_PIN, HIGH);
            break;
        case 8 * dot:
                if(g_nmaxe.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(LED_WIFI_STA_PIN, HIGH);
                else digitalWrite(LED_WIFI_STA_PIN, LOW);

                if(g_nmaxe.stratum.is_subscribed()) digitalWrite(LED_POOL_STA_PIN, HIGH);
                else digitalWrite(LED_POOL_STA_PIN, LOW);
            break;
        case 9 * dot:
                if(g_nmaxe.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(LED_WIFI_STA_PIN, HIGH);
                else digitalWrite(LED_WIFI_STA_PIN, HIGH);

                if(g_nmaxe.stratum.is_subscribed()) digitalWrite(LED_POOL_STA_PIN, HIGH);
                else digitalWrite(LED_POOL_STA_PIN, HIGH);
            break;
        case 10 * dot:
                if(g_nmaxe.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(LED_WIFI_STA_PIN, HIGH);
                else digitalWrite(LED_WIFI_STA_PIN, LOW);

                if(g_nmaxe.stratum.is_subscribed()) digitalWrite(LED_POOL_STA_PIN, HIGH);
                else digitalWrite(LED_POOL_STA_PIN, LOW);
            break;
        }

        // sys led, indicate hashrate
        if(g_nmaxe.mstatus.hashrate > 0){
            ledcWrite(pwmChannel, (uint32_t)((1 + sin(led_cnt/100.0f)) * (1<<resolution - 1)));
        }
        else{
            ledcWrite(pwmChannel, (uint32_t)((1 + sin(20*led_cnt/100.0f)) * (1<<resolution - 1)));
        }
        led_cnt++;
        delay(10);
    }
    LOG_I("led thread exit...");
    vTaskDelete(NULL);
}
