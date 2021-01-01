#include "mqtt.h"
#include "wifi.h"
#include "tasks.h"
#include "control.h"
#include "disp.h"
#include <ArduinoJson.h>

String stat_topic_prefix, cmnd_topic;

const PROGMEM char *topic_component = "therm";
const PROGMEM char *topic_rl_fan = "rl_fan";
const PROGMEM char *topic_rl_heat = "rl_heat";
const PROGMEM char *topic_sw_presence = "sw_presence";
const PROGMEM char *topic_cur_temp = "cur_temp";
const PROGMEM char *topic_cur_hum = "cur_hum";
const PROGMEM char *topic_set_temp = "set_temp";

const PROGMEM char *topic_suffix_relays = "relays";
const PROGMEM char *topic_suffix_dht11 = "dht11";
const PROGMEM char *topic_suffix_radar = "presence";
const PROGMEM char *topic_suffix_target = "setpoint";

WiFiClient mqtt_espClient;
PubSubClient mqtt_client(mqtt_espClient);

void mqtt_incoming_message_callback(char *topic, byte *payload, unsigned int length)
{
  if (therm_state.local_mode)
    return;

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
    if (mqtt_client.connect(therm_conf.host.c_str(), therm_conf.mqtt_user.c_str(), therm_conf.mqtt_pass.c_str()))
    {
      Serial.println("connected");

      announce_devices_to_homeassistant();

      Serial.println("Subscribe to " + cmnd_topic);
      mqtt_client.subscribe(cmnd_topic.c_str(), 1);

      mqtt_client.setCallback(mqtt_incoming_message_callback);
    }
  }

  // update status on screen
  draw_icon_homeassistant(mqtt_client.connected());
}

void mqtt_update_task(void *)
{
  mqtt_client.loop();
}

void send_mqtt_state(const String &topic, const JsonDocument &jdoc, bool retained = false)
{
  // uncomment this if we don't want to send local actions over to MQTT
  // TODO: look at this more carefully when we move the control to the PI
  // if (therm_state.local_mode) return;

  if (!mqtt_client.connected())
    return;

  String serialized_payload;
  serializeJsonPretty(jdoc, serialized_payload);
  Serial.println("MQTT: " + topic + " = " + serialized_payload);
  bool publish_status = mqtt_client.publish(topic.c_str(), serialized_payload.c_str(), retained);
  if (!publish_status)
  {
    Serial.println("MQTT publish FAILED.");
    Serial.println("MQTT buffer size = " + String(mqtt_client.getBufferSize()));
    Serial.println("MQTT message size = " + String(serialized_payload.length()));
  }
}

/*
void send_mqtt_state(const JsonDocument &jdoc, bool retained = false)
{
  send_mqtt_state(stat_topic_prefix, jdoc, retained);
}
*/

void send_mqtt_state_relays()
{
  DynamicJsonDocument jdoc(200);

  jdoc[topic_rl_fan] = (therm_state.fan_relay ? "on" : "off");
  jdoc[topic_rl_heat] = (therm_state.heat_relay ? "on" : "off");

  send_mqtt_state(stat_topic_prefix + "/" + topic_suffix_relays, jdoc);
}

void send_mqtt_state_presence()
{
  DynamicJsonDocument jdoc(200);

  jdoc[topic_sw_presence] = (therm_state.presence ? "on" : "off");

  send_mqtt_state(stat_topic_prefix + "/" + topic_suffix_radar, jdoc);
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
    send_mqtt_state(stat_topic_prefix + "/" + topic_suffix_dht11, jdoc);
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
    send_mqtt_state(stat_topic_prefix + "/" + topic_suffix_target, jdoc);
  }
}

void announce_devices_to_homeassistant()
{
  String device_id = therm_conf.host;
  String homeassistant_prefix = "homeassistant";

  {
    // sensor: current temp

    DynamicJsonDocument jdoc(500);
    String hassio_device = "temperature";
    String unique_id = device_id + "_" + hassio_device;
    {
      // device ID. This helps hassio group all the sensors of current esp instance together
      auto dev_obj = jdoc.createNestedObject("dev");
      dev_obj["name"] = device_id;
      dev_obj["mf"] = "Prashant";
      auto ids_array = dev_obj.createNestedArray("ids");
      ids_array.add(device_id);
    }
    jdoc["name"] = unique_id;                                      // the name of the sensor that shows up in hassio
    jdoc["stat_t"] = stat_topic_prefix + "/" + topic_suffix_dht11; // the stat topic that we publish. This is the one that hassio will start listening to
    jdoc["uniq_id"] = unique_id;                                   // unique ID of the device in hassio. Doesn't get used for anything except as a unique ID
    jdoc["unit_of_measurement"] = "°F";                            // we measure in F
    jdoc["device_class"] = "temperature";                          // this is the hassio device class
    jdoc["value_template"] = "{{value_json['cur_temp']}}";         // this is how hassio pulls the value from our overall status message JSON

    auto config_topic = homeassistant_prefix + "/sensor/" + unique_id + "/config";
    send_mqtt_state(config_topic, jdoc, true);
  }

  {
    // sensor: current humidity

    DynamicJsonDocument jdoc(500);
    String hassio_device = "humidity";
    String unique_id = device_id + "_" + hassio_device;
    {
      // device ID. This helps hassio group all the sensors of current esp instance together
      auto dev_obj = jdoc.createNestedObject("dev");
      dev_obj["name"] = device_id;
      dev_obj["mf"] = "Prashant";
      auto ids_array = dev_obj.createNestedArray("ids");
      ids_array.add(device_id);
    }
    jdoc["name"] = unique_id;                                      // the name of the sensor that shows up in hassio
    jdoc["stat_t"] = stat_topic_prefix + "/" + topic_suffix_dht11; // the stat topic that we publish. This is the one that hassio will start listening to
    jdoc["uniq_id"] = unique_id;                                   // unique ID of the device in hassio. Doesn't get used for anything except as a unique ID
    jdoc["unit_of_measurement"] = "%";                             // we measure in F
    jdoc["device_class"] = "humidity";                             // this is the hassio device class
    jdoc["value_template"] = "{{value_json['cur_hum']}}";          // this is how hassio pulls the value from our overall status message JSON

    auto config_topic = homeassistant_prefix + "/sensor/" + unique_id + "/config";
    send_mqtt_state(config_topic, jdoc, true);
  }

  {
    // sensor: target temp

    DynamicJsonDocument jdoc(500);
    String hassio_device = "target_temperature";
    String unique_id = device_id + "_" + hassio_device;
    {
      // device ID. This helps hassio group all the sensors of current esp instance together
      auto dev_obj = jdoc.createNestedObject("dev");
      dev_obj["name"] = device_id;
      dev_obj["mf"] = "Prashant";
      auto ids_array = dev_obj.createNestedArray("ids");
      ids_array.add(device_id);
    }
    jdoc["name"] = unique_id;                                       // the name of the sensor that shows up in hassio
    jdoc["stat_t"] = stat_topic_prefix + "/" + topic_suffix_target; // the stat topic that we publish. This is the one that hassio will start listening to
    jdoc["uniq_id"] = unique_id;                                    // unique ID of the device in hassio. Doesn't get used for anything except as a unique ID
    jdoc["unit_of_measurement"] = "°F";                             // we measure in F
    jdoc["device_class"] = "temperature";                           // this is the hassio device class
    jdoc["value_template"] = "{{value_json['set_temp']}}";          // this is how hassio pulls the value from our overall status message JSON

    auto config_topic = homeassistant_prefix + "/sensor/" + unique_id + "/config";
    send_mqtt_state(config_topic, jdoc, true);
  }

  {
    // sensor: presence

    DynamicJsonDocument jdoc(500);
    String hassio_device = "presence";
    String unique_id = device_id + "_" + hassio_device;
    {
      // device ID. This helps hassio group all the sensors of current esp instance together
      auto dev_obj = jdoc.createNestedObject("dev");
      dev_obj["name"] = device_id;
      dev_obj["mf"] = "Prashant";
      auto ids_array = dev_obj.createNestedArray("ids");
      ids_array.add(device_id);
    }
    jdoc["name"] = unique_id;                                      // the name of the sensor that shows up in hassio
    jdoc["stat_t"] = stat_topic_prefix + "/" + topic_suffix_radar; // the stat topic that we publish. This is the one that hassio will start listening to
    jdoc["uniq_id"] = unique_id;                                   // unique ID of the device in hassio. Doesn't get used for anything except as a unique ID
    jdoc["device_class"] = "occupancy";                            // this is the hassio device class
    jdoc["pl_on"] = "on";                                          // payload that indicates "ON" state
    jdoc["pl_off"] = "off";                                        // payload that indicates "OFF" state
    jdoc["value_template"] = "{{value_json['sw_presence']}}";      // this is how hassio pulls the value from our overall status message JSON

    auto config_topic = homeassistant_prefix + "/binary_sensor/" + unique_id + "/config";
    send_mqtt_state(config_topic, jdoc, true);
  }

  {
    // sensor: furnace relay

    DynamicJsonDocument jdoc(500);
    String hassio_device = "furnace";
    String unique_id = device_id + "_" + hassio_device;
    {
      // device ID. This helps hassio group all the sensors of current esp instance together
      auto dev_obj = jdoc.createNestedObject("dev");
      dev_obj["name"] = device_id;
      dev_obj["mf"] = "Prashant";
      auto ids_array = dev_obj.createNestedArray("ids");
      ids_array.add(device_id);
    }
    jdoc["name"] = unique_id;                                       // the name of the sensor that shows up in hassio
    jdoc["stat_t"] = stat_topic_prefix + "/" + topic_suffix_relays; // the stat topic that we publish. This is the one that hassio will start listening to
    jdoc["uniq_id"] = unique_id;                                    // unique ID of the device in hassio. Doesn't get used for anything except as a unique ID
    jdoc["device_class"] = "heat";                                  // this is the hassio device class
    jdoc["pl_on"] = "on";                                           // payload that indicates "ON" state
    jdoc["pl_off"] = "off";                                         // payload that indicates "OFF" state
    jdoc["value_template"] = "{{value_json['rl_heat']}}";           // this is how hassio pulls the value from our overall status message JSON

    auto config_topic = homeassistant_prefix + "/binary_sensor/" + unique_id + "/config";
    send_mqtt_state(config_topic, jdoc, true);
  }

  {
    // sensor: fan relay

    DynamicJsonDocument jdoc(500);
    String hassio_device = "fan";
    String unique_id = device_id + "_" + hassio_device;
    {
      // device ID. This helps hassio group all the sensors of current esp instance together
      auto dev_obj = jdoc.createNestedObject("dev");
      dev_obj["name"] = device_id;
      dev_obj["mf"] = "Prashant";
      auto ids_array = dev_obj.createNestedArray("ids");
      ids_array.add(device_id);
    }
    jdoc["name"] = unique_id;                                       // the name of the sensor that shows up in hassio
    jdoc["stat_t"] = stat_topic_prefix + "/" + topic_suffix_relays; // the stat topic that we publish. This is the one that hassio will start listening to
    jdoc["uniq_id"] = unique_id;                                    // unique ID of the device in hassio. Doesn't get used for anything except as a unique ID
    jdoc["pl_on"] = "on";                                           // payload that indicates "ON" state
    jdoc["pl_off"] = "off";                                         // payload that indicates "OFF" state
    jdoc["value_template"] = "{{value_json['rl_fan']}}";            // this is how hassio pulls the value from our overall status message JSON

    auto config_topic = homeassistant_prefix + "/binary_sensor/" + unique_id + "/config";
    send_mqtt_state(config_topic, jdoc, true);
  }
}

void init_mqtt()
{
  String common_mid = topic_component;
  common_mid += "/";
  common_mid += therm_conf.host;
  stat_topic_prefix = "stat/" + common_mid;
  cmnd_topic = "cmnd/" + common_mid;

  Serial.println(String("MQTT server: " + therm_conf.mqtt_server));
  mqtt_client.setBufferSize(512);
  mqtt_client.setServer(therm_conf.mqtt_server.c_str(), 1883);

  sched.add_or_update_task((void *)mqtt_connect, 0, NULL, 0, 30 * 1000, 15000);
  sched.add_or_update_task((void *)mqtt_update_task, 0, NULL, 0, 1, 1000);
}
