#ifndef _CONNECTION_H
#define _CONNECTION_H
#include <WiFi.h>
#include <Arduino.h>

typedef struct{
    String ssid;
    String pwd;
}wifi_conn_info_t;

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
void webserver_thread_entry(void *args);
#endif // _WMANAGER_H
