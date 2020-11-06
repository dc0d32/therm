#ifndef __CONTROL_H__
#define __CONTROL_H__

bool fan_on();
bool fan_off();
bool heat_on();
bool heat_off();
void update_target_temp(float target_temp);

#endif // __CONTROL_H__
