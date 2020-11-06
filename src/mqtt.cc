#include "mqtt.h"
#include "wifi.h"
#include "tasks.h"
#include "control.h"
#include <ArduinoJson.h>

String stat_topic, cmnd_topic;

const PROGMEM char *topic_component = "therm";
const PROGMEM char *topic_rl_fan = "rl_fan";
const PROGMEM char *topic_rl_heat = "rl_heat";
const PROGMEM char *topic_sw_presence = "sw_presence";
const PROGMEM char *topic_cur_temp = "cur_temp";
const PROGMEM char *topic_cur_hum = "cur_hum";
const PROGMEM char *topic_set_temp = "set_temp";

WiFiClient mqtt_espClient;
PubSubClient mqtt_client(mqtt_espClient);

void mqtt_incoming_message_callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (uint i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  DynamicJsonDocument jdoc(200);
  auto json_error = deserializeJson(jdoc, payload, length);
  if (json_error)
  {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(json_error.f_str());
    return;
  }
  JsonObject jobj = jdoc.as<JsonObject>();

  if (jobj.containsKey(topic_rl_fan))
  {
    String state_str = jobj[topic_rl_fan];
    if (state_str.equalsIgnoreCase("on"))
      fan_on();
    else if (state_str.equalsIgnoreCase("off"))
      fan_off();
  }
  if (jobj.containsKey(topic_rl_heat))
  {
    String state_str = jobj[topic_rl_heat];
    if (state_str.equalsIgnoreCase("on"))
      heat_on();
    else if (state_str.equalsIgnoreCase("off"))
      heat_off();
  }
  if (jobj.containsKey(topic_set_temp))
  {
    float target_temp = jobj[topic_set_temp];

    if (!isnan(target_temp))
    {
      update_target_temp(target_temp);
    }
  }

  // we don't understand other message yet

  return;
}

void mqtt_connect()
{
  if (!mqtt_client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    if (mqtt_client.connect(wifi_conf.host.c_str(), wifi_conf.mqtt_user.c_str(), wifi_conf.mqtt_pass.c_str()))
    {
      Serial.println("connected");
      Serial.println("Subscribe to " + cmnd_topic);
      mqtt_client.subscribe(cmnd_topic.c_str(), 1);

      mqtt_client.setCallback(mqtt_incoming_message_callback);
    }
  }
}

void mqtt_update_task(void *)
{
  mqtt_client.loop();
}

void send_mqtt_state(const JsonDocument &jdoc)
{
  if (!mqtt_client.connected())
    return;

  String serialized_payload;
  serializeJsonPretty(jdoc, serialized_payload);
  Serial.println("MQTT: " + stat_topic + " = " + serialized_payload);
  mqtt_client.publish(stat_topic.c_str(), serialized_payload.c_str());
}

void send_mqtt_state_relays()
{
  DynamicJsonDocument jdoc(200);

  jdoc[topic_rl_fan] = (therm_state.fan_relay ? "on" : "off");
  jdoc[topic_rl_heat] = (therm_state.heat_relay ? "on" : "off");

  send_mqtt_state(jdoc);
}

void send_mqtt_state_presence()
{
  DynamicJsonDocument jdoc(200);

  jdoc[topic_sw_presence] = (therm_state.presence ? "on" : "off");

  send_mqtt_state(jdoc);
}

void send_mqtt_state_cur_temp()
{
  DynamicJsonDocument jdoc(200);

  bool should_send = false;
  if (!isnan(therm_state.cur_temp))
  {
    jdoc[topic_cur_temp] = therm_state.cur_temp;
    should_send = true;
  }

  if (!isnan(therm_state.cur_hum))
  {
    jdoc[topic_cur_hum] = therm_state.cur_hum;
    should_send = true;
  }

  if (should_send)
  {
    send_mqtt_state(jdoc);
  }
}

void send_mqtt_state_target_temp()
{
  DynamicJsonDocument jdoc(200);

  bool should_send = false;
  if (!isnan(therm_state.tgt_temp))
  {
    jdoc[topic_set_temp] = therm_state.tgt_temp;
    should_send = true;
  }

  if (should_send)
  {
    send_mqtt_state(jdoc);
  }
}

void init_mqtt()
{
  String common_mid = topic_component;
  common_mid += "/";
  common_mid += wifi_conf.host;
  stat_topic = "stat/" + common_mid;
  cmnd_topic = "cmnd/" + common_mid;

  Serial.println(String("MQTT server: " + wifi_conf.mqtt_server));
  mqtt_client.setServer(wifi_conf.mqtt_server.c_str(), 1883);

  sched.add_or_update_task((void *)mqtt_connect, 0, NULL, 0, 30 * 1000, 15000);
  sched.add_or_update_task((void *)mqtt_update_task, 0, NULL, 0, 1, 1000);
}
