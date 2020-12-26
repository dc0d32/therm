#include "local_thermostat.h"
#include "config.h"
#include "tasks.h"
#include "utils.h"
#include "control.h"

void monitor_local_mode_temperature()
{
    if (isnan(therm_state.cur_temp))
    {
        return;
    }

    float diff = therm_state.tgt_temp - therm_state.cur_temp;

    if (diff > 2)
    {
        // cold; heat on
        heat_on();
        fan_on();
    }
    else if (diff < -6)
    {
        // too hot, only fan on
        fan_on();
    }
    else
    {
        heat_off();
        fan_off(); // ideally this should be on a task. get the heat out of the furnace by running the fans a bit longer
    }
}

void enable_local_thermostat()
{
    if (therm_state.local_mode)
    {
        return;
    }
    if (isnan(therm_state.tgt_temp))
    {
        update_target_temp(73.0f);
    }
    therm_state.local_mode = 1;
    sched.add_or_update_task((void *)monitor_local_mode_temperature, 0, NULL, 0, MS_FROM_SECONDS(10), 0);
}

void disable_local_thermostat()
{
    if (!therm_state.local_mode)
    {
        return;
    }
    sched.remove_task((void *)monitor_local_mode_temperature, 0);
    update_target_temp(NAN);
    therm_state.local_mode = 0;
}