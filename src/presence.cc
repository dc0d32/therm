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

void presence_detected()
{
    therm_state.presence = 1;
    send_mqtt_state_presence();
    draw_icon_person(therm_state.presence);
    set_bright_mode(therm_state.presence);
    sched.add_or_update_task((void *)&presence_detection_timeout_task, 0, NULL, 1, 0, MS_FROM_MINUTES(RADAR_EVENT_TIMEOUT_MIN));
}

// this task triggers presence when the radar pin has been high for long period
// some of our sensors keep the status high for as long as the movement is detected.
void presence_detection_double_trigger_slow_sensor_read_task(void *)
{
    noInterrupts();
    if (digitalRead(RCWL0516_PIN) == 1)
    {
        // the status is still high
        presence_detected();
        // reset the double trigger
        previous_radar_trigger_ts = -1;
    }
    interrupts();
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

        // some of our sensors keep the status high for as long as movement is detected, and then some
        // in that case we won't get another call to the presence_detection_task ISR
        sched.add_or_update_task((void *)&presence_detection_double_trigger_slow_sensor_read_task, 0, NULL, 1, 0, MS_FROM_SECONDS(RADAR_TRIGGER_CONFIRMATION_DURATION_SEC));
        return;
    }
    else
    {
        if (millis() - previous_radar_trigger_ts < MS_FROM_SECONDS(RADAR_TRIGGER_CONFIRMATION_DURATION_SEC))
        {
            // detected presence multiple times in short duration
            // very likely that a person is around
            presence_detected();

            // reset the double trigger
            previous_radar_trigger_ts = -1;
            sched.remove_task((void *)&presence_detection_double_trigger_slow_sensor_read_task, 0);

            return;
        }
        else
        {
            // previous radar firing was too long ago. Re-arm the double-trigger to current timestamp
            previous_radar_trigger_ts = millis();
            // some of our sensors keep the status high for as long as movement is detected, and then some
            // in that case we won't get another call to the presence_detection_task ISR
            sched.add_or_update_task((void *)&presence_detection_double_trigger_slow_sensor_read_task, 0, NULL, 1, 0, MS_FROM_SECONDS(RADAR_TRIGGER_CONFIRMATION_DURATION_SEC));
        }
    }
}

void setup_presence_detection()
{
    pinMode(RCWL0516_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(RCWL0516_PIN), presence_detection_task, RISING);
}
