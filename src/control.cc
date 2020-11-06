#include "control.h"

#include <ctype.h>

#include "config.h"
#include "tasks.h"
#include "mqtt.h"

int64_t last_fan_off_ts = -1;
bool fan_off()
{
  bool ret = false;
  if (!therm_state.fan_relay)
  {
    ret = true;
  }
  else
  {
    if (therm_state.heat_relay)
    {
      // can't turn off fan when heat is on
      ret = false;
    }
    else
    {
      // TODO: in case we want to enforce minimum running time for a fan, handle that here.

      therm_state.fan_relay = 0;
      digitalWrite(RELAY_FAN_PIN, LOW);
      sched.remove_task((void *)fan_off, 0);
      last_fan_off_ts = millis();
      ret = true;
    }
  }
  sched.add_or_update_task((void *)send_mqtt_state_relays, 0, NULL, 0, 0, 100);
  return ret;
}

bool fan_on()
{
  bool ret = false;

  if (therm_state.fan_relay)
  {
    ret = true;
  }
  else
  {
    // check for cooldown. We don't want to turn the fan on immediately after it was turned off
    if (last_fan_off_ts > 0 && millis() - last_fan_off_ts < 5 * 60 * 1000)
    {
      ret = false;
    }
    else
    {
      therm_state.fan_relay = 1;
      digitalWrite(RELAY_FAN_PIN, HIGH);
      sched.add_or_update_task((void *)fan_off, 0, NULL, 0, 0, 60 * 60 * 1000);
      ret = true;
    }
  }
  sched.remove_task((void *)fan_on, 0);
  sched.remove_task((void *)fan_on, 1);
  sched.add_or_update_task((void *)send_mqtt_state_relays, 0, NULL, 0, 0, 100);
  return ret;
}

int64_t last_heat_off_ts = -1;
bool heat_off()
{
  bool ret = false;
  if (!therm_state.heat_relay)
  {
    ret = true;
  }
  else
  {
    // TODO: in case we want to enforce minimum running time for a heat, handle that here.

    therm_state.heat_relay = 0;
    digitalWrite(RELAY_HEAT_PIN, LOW);
    sched.remove_task((void *)heat_off, 0);
    last_heat_off_ts = millis();
    ret = true;
  }
  sched.add_or_update_task((void *)send_mqtt_state_relays, 0, NULL, 0, 0, 100);
  return ret;
}

bool heat_on()
{
  bool ret = false;

  if (therm_state.heat_relay)
  {
    ret = true;
  }
  else
  {
    // check for cooldown. We don't want to turn the heat on immediately after it was turned off
    if (last_heat_off_ts > 0 && millis() - last_heat_off_ts < 5 * 60 * 1000)
    {
      ret = false;
    }
    else
    {
      therm_state.heat_relay = 1;
      digitalWrite(RELAY_HEAT_PIN, HIGH);
      sched.add_or_update_task((void *)fan_on, 1, NULL, 0, 0, 30 * 1000);        // safety task: fan must come on few seconds after heat does, even if we don't hear anything from the controller
      sched.add_or_update_task((void *)heat_off, 0, NULL, 0, 0, 30 * 60 * 1000); // safety task: fan can not run for for too long
      ret = true;
    }
  }
  sched.add_or_update_task((void *)send_mqtt_state_relays, 0, NULL, 0, 0, 100);
  return ret;
}

void update_target_temp(float target)
{
  // Serial.println(String("set temp = ") + target);
  therm_state.tgt_temp = target;
  sched.add_or_update_task((void *)send_mqtt_state_target_temp, 0, NULL, 0, 0, 100);
}
