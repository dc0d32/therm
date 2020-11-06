#ifndef __CONFIG_H__
#define __CONFIG_H__

///////////////////////////////////////////////////////////////////////////////////////

#define RELAY_FAN_PIN 16  // D0
#define RELAY_HEAT_PIN 13 // D7
#define DHT11_PIN 14      // D5
#define RCWL0516_PIN 12   // D6
#define SSD1306_SDA_PIN 4 // D2
#define SSD1306_SCL_PIN 5 // D1
#define KNOB_A_PIN 0      // D3
#define KNOB_B_PIN 2      // D4
#define KNOB_BTN_PIN 15   // D8

///////////////////////////////////////////////////////////////////////////////////////

// screen related
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// temp and humidity avg
#define MIN_TEMP_DELTA_BETWEEN_REPORTS 0.1
#define MIN_HUM_DELTA_BETWEEN_REPORTS 1.0
#define TEMP_REPORT_NOCHANGE_PERIOD (60 * 1000)

#define NUM_SAMPLES_FOR_TEMP_AVG 10

///////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////
#include <WString.h>
#include <math.h>

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

void init_fs();

extern WifiConfig wifi_config;

struct ThermState
{
  uint8 fan_relay = 0, heat_relay = 0;
  uint8 presence = 0;
  float cur_temp = NAN, cur_hum = NAN, tgt_temp = NAN, last_reported_temp = NAN, last_reported_hum = NAN;
  uint64 last_reported_ts = 0;
};

extern ThermState therm_state;



#endif