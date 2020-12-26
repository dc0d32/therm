#include "local_thermostat.h"
#include "config.h"
#include "tasks.h"
#include "utils.h"
#include "control.h"
#include "disp.h"

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
    }
    else if (diff < -6)
    {
        // too hot, only fan on
        fan_on();
    }
    else
    {
        heat_off();
        sched.add_or_update_task((void *)fan_off, 100, NULL, 0, 0, MS_FROM_SECONDS(60));
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
    draw_icon_local_mode(therm_state.local_mode);
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
    draw_icon_local_mode(therm_state.local_mode);
}