#include "config.h"

#include <FS.h>
#include <LittleFS.h>
#include "utils.h"

////////////////////////////////////////////////////////////////
// TODO: persist this in EEPROM

ThermState therm_state;

////////////////////////////////////////////////////////////////
// WIFI config

ThermConfig::ThermConfig() : ssid(""), pass("")
{
    host = "Therm_" + get_chip_id();
    calibration_offset_temp = 0;
    calibration_offset_hum = 0;
    relays_available = false;
}

ThermConfig::~ThermConfig()
{
    ssid.clear();
    pass.clear();
}

bool ThermConfig::read(const char *filePath)
{
    File configFile = LittleFS.open(filePath, "r");
    if (!configFile)
    {
        Serial.println("Failed to open config file");
        return false;
    }

    ssid = configFile.readStringUntil('\n');
    trim_string(ssid);
    if (ssid.length() == 0)
        return false;
    pass = configFile.readStringUntil('\n');
    trim_string(pass);
    host = configFile.readStringUntil('\n');
    trim_string(host);
    mqtt_server = configFile.readStringUntil('\n');
    trim_string(mqtt_server);
    mqtt_user = configFile.readStringUntil('\n');
    trim_string(mqtt_user);
    mqtt_pass = configFile.readStringUntil('\n');
    trim_string(mqtt_pass);

    {
        // calibration
        {
            // temperature
            String val_str = configFile.readStringUntil('\n');
            trim_string(val_str);
            calibration_offset_temp = val_str.toFloat();
        }

        {
            // humidity
            String val_str = configFile.readStringUntil('\n');
            trim_string(val_str);
            calibration_offset_hum = val_str.toFloat();
        }
    }

    {
        String val_str = configFile.readStringUntil('\n');
        trim_string(val_str);
        relays_available = val_str.toInt();
    }
    
    configFile.close();
    return true;
}

bool ThermConfig::write(const char *filePath)
{
    File configFile = LittleFS.open(filePath, "w");
    if (!configFile)
    {
        Serial.println("Failed to open config file");
        return false;
    }

    configFile.write(this->ssid.c_str(), this->ssid.length());
    configFile.write('\n');
    configFile.write(this->pass.c_str(), this->pass.length());
    configFile.write('\n');
    configFile.write(this->host.c_str(), this->host.length());
    configFile.write('\n');
    configFile.write(mqtt_server.c_str(), mqtt_server.length());
    configFile.write('\n');
    configFile.write(mqtt_user.c_str(), mqtt_user.length());
    configFile.write('\n');
    configFile.write(mqtt_pass.c_str(), mqtt_pass.length());
    configFile.write('\n');

    {
        String val_str = String(calibration_offset_temp);
        configFile.write(val_str.c_str(), val_str.length());
        configFile.write('\n');
    }
    {
        String val_str = String(calibration_offset_hum);
        configFile.write(val_str.c_str(), val_str.length());
        configFile.write('\n');
    }

    configFile.write(relays_available ? "1" : "0", 1);
    configFile.write('\n');

    configFile.close();
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////

// filesystem
void init_fs()
{
    Serial.println("Mount LittleFS");
    if (!LittleFS.begin())
    {
        Serial.println("Format.");
        LittleFS.format();
        Serial.println("Mount newly formatted LittleFS");
        LittleFS.begin();
    }
    Serial.println("LittleFS mounted");
}
