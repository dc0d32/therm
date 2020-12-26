#include "presence.h"

#include "mqtt.h"
#include "config.h"
#include "tasks.h"
#include "disp.h"

void presence_detection_timeout_task(void *)
{
    // Serial.println(F("--- idle for too long"));
    // digitalWrite(RELAY_FAN_PIN, LOW);
    therm_state.presence = 0;
    send_mqtt_state_presence();
    draw_icon_person(therm_state.presence);
    set_bright_mode(therm_state.presence);
}

ICACHE_RAM_ATTR void presence_detection_task()
{
    // Serial.println("+++ presence detected");
    // digitalWrite(RELAY_FAN_PIN, HIGH);
    therm_state.presence = 1;
    send_mqtt_state_presence();
    draw_icon_person(therm_state.presence);
    set_bright_mode(therm_state.presence);
    sched.add_or_update_task((void *)&presence_detection_timeout_task, 0, NULL, 1, 0, 5 * 60 * 1000); // 5 min delay, not periodic
}

void setup_presence_detection()
{
    pinMode(RCWL0516_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(RCWL0516_PIN), presence_detection_task, RISING);
}
