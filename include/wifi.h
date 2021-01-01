#ifndef __WIFI_H__
#define __WIFI_H__

#include "config.h"

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

extern ThermConfig therm_conf;


bool is_wifi_connected();
bool is_wifi_in_ap_mode();
void init_mdns();
void init_wifi();
void init_web_server();

#endif // __WIFI_H__