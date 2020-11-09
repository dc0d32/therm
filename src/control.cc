#include "control.h"

#include <ctype.h>

#include "config.h"
#include "tasks.h"
#include "mqtt.h"
#include "disp.h"
#include "utils.h"

int64_t last_fan_off_ts = -1;
bool fan_off()
{
  // Serial.println("fan off called");

  bool ret = false;
  if (!therm_state.fan_relay)
  {
    // Serial.println("fan already off");
    ret = true;
  }
  else
  {
    if (therm_state.heat_relay)
    {
      // can't turn off fan when heat is on
      // Serial.println("can't turn off fan when heat is on");

      ret = false;
    }
    else
    {
      // TODO: in case we want to enforce minimum running time for a fan, handle that here.

      therm_state.fan_relay = 0;
      digitalWrite(RELAY_FAN_PIN, LOW);
      bool was_fan_off_task_removed = sched.remove_task((void *)fan_off, 0);
      // Serial.println(String("fan turned off. scheduled task removed = ") + was_fan_off_task_removed);
      last_fan_off_ts = millis();
      ret = true;
    }
  }
  sched.add_or_update_task((void *)send_mqtt_state_relays, 0, NULL, 0, 0, 100);
  draw_icon_fan(therm_state.fan_relay);
  return ret;
}

bool fan_on()
{
  // Serial.println("fan on called");

  bool ret = false;

  if (therm_state.fan_relay)
  {
    // Serial.println(String("fan already on"));

    ret = true;
  }
  else
  {
    // check for cooldown. We don't want to turn the fan on immediately after it was turned off
    if (last_fan_off_ts > 0 && millis() - last_fan_off_ts < MS_FROM_MINUTES(5))
    {
      ret = false;
      // Serial.println(String("fan in cooldown mode, not turning back on"));
    }
    else
    {
      therm_state.fan_relay = 1;
      digitalWrite(RELAY_FAN_PIN, HIGH);
      // Serial.println(String("fan turned on. scheduled Off task = ") + was_fan_off_task_added);
      bool was_fan_off_task_added = sched.add_or_update_task((void *)fan_off, 0, NULL, 0, 0, MS_FROM_MINUTES(45)); // safety task: fan can't run continuously for too long
      ret = true;
    }
  }
  bool was_fan_on_task_removed = sched.remove_task((void *)fan_on, 0);
  // Serial.println(String("scheduled Fan On task removed = ") + was_fan_on_task_removed);

  sched.add_or_update_task((void *)send_mqtt_state_relays, 0, NULL, 0, 0, 100);
  draw_icon_fan(therm_state.fan_relay);
  return ret;
}

int64_t last_heat_off_ts = -1;
bool heat_off()
{
  // Serial.println("heat off called");
  bool ret = false;
  if (!therm_state.heat_relay)
  {
    // Serial.println(String("heat already off"));

    ret = true;
  }
  else
  {
    // TODO: in case we want to enforce minimum running time for a heat, handle that here.

    therm_state.heat_relay = 0;
    digitalWrite(RELAY_HEAT_PIN, LOW);
    bool was_heat_on_task_removed = sched.remove_task((void *)fan_on, 0);
    bool was_heat_off_task_removed = sched.remove_task((void *)heat_off, 0);
    // Serial.println(String("heat turned off. scheduled task removed status: fan on = ") + was_heat_on_task_removed + String(", heat off = ") + was_heat_off_task_removed);

    last_heat_off_ts = millis();
    ret = true;
  }
  sched.add_or_update_task((void *)send_mqtt_state_relays, 0, NULL, 0, 0, 100);
  draw_icon_heat(therm_state.heat_relay);
  return ret;
}

bool heat_on()
{
  // Serial.println("heat on called");
  bool ret = false;

  if (therm_state.heat_relay)
  {
    // Serial.println(String("heat already on"));

    ret = true;
  }
  else
  {
    // check for cooldown. We don't want to turn the heat on immediately after it was turned off
    if (last_heat_off_ts > 0 && millis() - last_heat_off_ts < MS_FROM_MINUTES(5))
    {
      // Serial.println(String("heat won't turn on. in cooldown"));

      ret = false;
    }
    else
    {
      therm_state.heat_relay = 1;
      digitalWrite(RELAY_HEAT_PIN, HIGH);
      bool was_fan_on_task_added = sched.add_or_update_task((void *)fan_on, 0, NULL, 0, 0, MS_FROM_MINUTES(1));     // safety task: fan must come on few seconds after heat does, even if we don't hear anything from the controller
      bool was_heat_off_task_added = sched.add_or_update_task((void *)heat_off, 0, NULL, 0, 0, MS_FROM_MINUTES(30)); // safety task: heat can not run for for too long

      // Serial.println(String("heat turned on. scheduled task added: fan on = ") + was_fan_on_task_added + String(" , heat off = ") + was_heat_off_task_added);

      ret = true;
    }
  }
  sched.add_or_update_task((void *)send_mqtt_state_relays, 0, NULL, 0, 0, 100);
  draw_icon_heat(therm_state.heat_relay);
  return ret;
}

void update_target_temp(float target)
{
  // Serial.println(String("set temp = ") + target);
  therm_state.tgt_temp = target;
  draw_target_temp();
  sched.add_or_update_task((void *)send_mqtt_state_target_temp, 0, NULL, 0, 0, 100);
}
