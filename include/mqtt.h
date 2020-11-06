#ifndef __MQTT_H__
#define __MQTT_H__

#include <PubSubClient.h>

void send_mqtt_state_relays();
void send_mqtt_state_presence();
void send_mqtt_state_cur_temp();
void send_mqtt_state_target_temp();
void init_mqtt();


#endif // __MQTT_H__