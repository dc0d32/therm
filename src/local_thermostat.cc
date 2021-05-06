#include "local_thermostat.h"
#include "config.h"
#include "tasks.h"
#include "utils.h"
#include "control.h"
#include "disp.h"

#include <bitset>

#define CIRCULATION_FAN_HISTORY_SIZE_IN_MIN 60
// we will try to keep the fan on for CIRCULATION_MIN out of last CIRCULATION_FAN_HISTORY_SIZE_IN_MIN
#define CIRCULATION_MIN 30

std::bitset<CIRCULATION_FAN_HISTORY_SIZE_IN_MIN> fan_state_history;
short fan_state_history_ptr = 0;
int64_t last_circ_fan_on_ts = -1;
bool previous_fan_status = false;

void circulation_watcher_task()
{
    fan_state_history[fan_state_history_ptr] = therm_state.fan_relay;
    fan_state_history_ptr = (fan_state_history_ptr + 1) % CIRCULATION_FAN_HISTORY_SIZE_IN_MIN;

    size_t fan_total_runtime = fan_state_history.count();

    if (therm_state.fan_relay && !previous_fan_status)
    {
        // this case handles fan turning on due to furnace; but not with current function
        last_circ_fan_on_ts = millis();
    }

    if (!therm_state.fan_relay && fan_total_runtime < CIRCULATION_MIN)
    {
        // we need to run the fan for few minutes
        if (fan_on())
        {
            last_circ_fan_on_ts = millis();
        }
    }

    if (therm_state.fan_relay && last_circ_fan_on_ts >= 0 && (millis() - last_circ_fan_on_ts) > MS_FROM_MINUTES(CIRCULATION_MIN))
    {
        // fans have run long time. Shut off
        if (fan_off())
        {
            last_circ_fan_on_ts = -1;
        }
        else
        {
            // else we'll try shutting down the fan next minute
        }
    }

    previous_fan_status = therm_state.fan_relay;
}

void monitor_local_mode_temperature()
{
    if (isnan(therm_state.cur_temp))
    {
        return;
    }

    float diff = therm_state.tgt_temp - therm_state.cur_temp;

    if (diff > 1)
    {
        // cold; heat on
        heat_on();
    }
    else if (diff < -1)
    {
        heat_off();
        // sched.add_or_update_task((void *)fan_off, 10, NULL, 0, 0, MS_FROM_SECONDS(60));
        fan_off(); // FIXME: comment this out when we enable circulation mode again
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

    // circulation related
    fan_state_history.reset();
    fan_state_history_ptr = 0;
    last_circ_fan_on_ts = -1;

    // FIXME: this is disabled for now during winter, can be enabled during summer
    // reason for disabling: if the fan runs without heat, the heat exchanger is effectively the coldest part
    // of the HVAC loop, and it accumulates condensation. Condensation = rust; lower life of furnace, carbon monoxide risk.
    // The rust/evaporating water on iron what I was smelling when the heat kicked in since enabling circulation.
    // Hence disabled during winter.
    // Ideal fix: replace the inaccurate DHT11 by something more reliable,
    // then check if the current internal moisture at internal temp will result in condensation
    // on the heat exchanger by looking at outside temperature; and enable circulation mode automatically.
    // Look into the dew point calculation. Could compare that with the external temp.

    // sched.add_or_update_task((void *)circulation_watcher_task, 0, NULL, 0, MS_FROM_MINUTES(1), 0);
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