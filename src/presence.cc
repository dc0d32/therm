#include "presence.h"

#include "mqtt.h"
#include "config.h"
#include "tasks.h"
#include "disp.h"
#include "utils.h"

int64_t previous_radar_trigger_ts = -1;

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
    if (previous_radar_trigger_ts < 0)
    {
        // if this is the first time the radar has tripped, check if it trips again within few seconds.
        // this double-triggering usually means that there's a person around
        // and helps avoid false triggers
        previous_radar_trigger_ts = millis();
        return;
    }
    else
    {
        if (millis() - previous_radar_trigger_ts < MS_FROM_SECONDS(RADAR_TRIGGER_CONFIRMATION_DURATION_SEC))
        {
            // detected presence multiple times in short duration
            // very likely that a person is around
            therm_state.presence = 1;
            send_mqtt_state_presence();
            draw_icon_person(therm_state.presence);
            set_bright_mode(therm_state.presence);
            sched.add_or_update_task((void *)&presence_detection_timeout_task, 0, NULL, 1, 0, MS_FROM_MINUTES(RADAR_EVENT_TIMEOUT_MIN));

            // reset the double trigger
            previous_radar_trigger_ts = -1;

            return;
        }
        else
        {
            // previous radar firing was too long ago. Re-arm the double-trigger to current timestamp
            previous_radar_trigger_ts = millis();
        }
    }
}

void setup_presence_detection()
{
    pinMode(RCWL0516_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(RCWL0516_PIN), presence_detection_task, RISING);
}
