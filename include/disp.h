#ifndef __DISP_H__
#define __DISP_H__

void init_disp();
void set_bright_mode(bool);
void draw_current_temp();
void draw_target_temp();

void draw_icon_heat(bool);
void draw_icon_fan(bool);
void draw_icon_person(bool);
void draw_icon_wifi(bool);
void draw_icon_homeassistant(bool);
void draw_icon_local_mode(bool);

#endif // __DISP_H__
