#ifndef _CONNECTION_H
#define _CONNECTION_H
#include <WiFi.h>
#include <Arduino.h>

typedef struct{
    String ssid;
    String pwd;
} sta_conn_info_t;

typedef struct{
    IPAddress ip;
    String    pwd;
    String    ssid;
} ap_conn_info_t;

typedef struct{
    IPAddress   ip;
    IPAddress   gateway;
    IPAddress   subnet;
    IPAddress   dns;
    int         rssi;
    wl_status_t status;
    uint16_t    config_timeout;
} sta_status_t;

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
void webserver_thread_entry(void *args);
#endif // _WMANAGER_H
