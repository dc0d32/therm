
#include <arduino.h>

#include "tasks.h"
#include "config.h"
#include "utils.h"
#include "wifi.h"
#include "mqtt.h"
#include "dht11.h"
#include "knob.h"
#include "presence.h"
#include "control.h"


#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>



// global vars

scheduler sched(16); // 'task' scheduler


// screen related
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

///////////////////////////////////////////////////////////////////////////////////////
// graphics

// 'flame', 16x16px
const unsigned char bmp_flame[] PROGMEM = {
    0xff, 0xff, 0xfe, 0xff, 0xfe, 0x7f, 0xfe, 0x7f, 0xfc, 0x3f, 0xf8, 0x37, 0xf8, 0x87, 0xe0, 0xc3,
    0xc3, 0xc3, 0xc3, 0xe3, 0xc7, 0xe3, 0xcf, 0xe3, 0xcf, 0xe7, 0xef, 0xef, 0xff, 0xff, 0xff, 0xff};

// 'fan', 16x16px
const unsigned char bmp_fan[] PROGMEM = {
    0xfc, 0x3f, 0xf8, 0x1f, 0xf8, 0x1f, 0xfc, 0x1f, 0xfc, 0x1f, 0x82, 0x19, 0x00, 0x20, 0x00, 0x00,
    0x00, 0x00, 0x04, 0x00, 0x98, 0x41, 0xf8, 0x3f, 0xf8, 0x3f, 0xf8, 0x1f, 0xf8, 0x1f, 0xfc, 0x3f};

// 'person', 16x16px
const unsigned char person[] PROGMEM = {
    0xff, 0xe7, 0xff, 0xe7, 0xfc, 0x3f, 0xf3, 0x9f, 0xf7, 0x0f, 0xff, 0x2f, 0xfe, 0x60, 0xfc, 0xff,
    0xfc, 0xff, 0xfa, 0x7f, 0x07, 0x9f, 0xff, 0x9f, 0xff, 0xbf, 0xff, 0xbf, 0xff, 0x7f, 0x00, 0x00};

///////////////////////////////////////////////////////////////////////////////////////


void setup()
{
  Serial.begin(9600);

  pinMode(RELAY_FAN_PIN, OUTPUT);
  pinMode(RELAY_HEAT_PIN, OUTPUT);

  Serial.println("init fs");
  init_fs();
  Serial.println("init wifi");
  init_wifi();
  Serial.println("init mqtt");
  init_mqtt();

  Serial.println("init dht");
  setup_dht();
  Serial.println("init knob");
  init_knob();
  Serial.println("init radar");
  setup_presence_detection();
}

void loop()
{
  sched.run(0);
}
