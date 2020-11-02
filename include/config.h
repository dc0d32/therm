#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <WString.h>

struct WifiConfig
{
    String ssid;
    String pass;
    String host;
    String mqtt_server;
    String mqtt_user;
    String mqtt_pass;

    WifiConfig();
    ~WifiConfig();
    
    bool read(const char *filePath);
    bool write(const char *filePath);

};

#endif