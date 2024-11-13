#ifndef _CONNECTION_H
#define _CONNECTION_H
#include <WiFi.h>
#include <Arduino.h>

typedef struct{
    String ssid;
    String pwd;
    String hostname;
} axe_wifi_conn_param_t;

typedef struct{
    IPAddress ip;
    String    pwd;
    String    ssid;
} axe_ap_conn_param_t;

typedef struct{
    IPAddress ip;
    IPAddress gateway;
    IPAddress subnet;
    IPAddress dns;
    int       rssi;
    wl_status_t status;
    uint16_t  config_timeout;
} axe_wifi_state_t;




typedef struct{
    axe_ap_conn_param_t   softap_param;
    axe_wifi_conn_param_t conn_param;
    axe_wifi_state_t      status_param;
    SemaphoreHandle_t     reconnect_xsem;
    SemaphoreHandle_t     force_cfg_xsem;
}wifi_info_t;


void axe_wifi_connecet(axe_wifi_conn_param_t param);

#endif // _WMANAGER_H
