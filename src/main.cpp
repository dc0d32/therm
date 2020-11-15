
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
#include "disp.h"

// global vars

void setup()
{
    Serial.begin(74880);
    init_disp();

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
