
#include <arduino.h>

#include "tasks.h"
#include "config.h"
#include "utils.h"
#include "wifi.h"

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <LittleFS.h>

#include <ArduinoJson.h>


#include <PubSubClient.h>

///////////////////////////////////////////////////////////////////////////////////////

#define RELAY_FAN_PIN 16  // D0
#define RELAY_HEAT_PIN 13 // D7
#define DHT11_PIN 14      // D5
#define RCWL0516_PIN 12   // D6
#define SSD1306_SDA_PIN 4 // D2
#define SSD1306_SCL_PIN 5 // D1
#define KNOB_A_PIN 0      // D3
#define KNOB_B_PIN 2      // D4
#define KNOB_BTN_PIN 15   // D8

///////////////////////////////////////////////////////////////////////////////////////

// global vars

scheduler sched(16); // 'task' scheduler

DHT_Unified dht_sensor(DHT11_PIN, DHT11); // DHT temp sensor (adafruit library)

// screen related
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// MQTT
String stat_topic, cmnd_topic;

const PROGMEM char *topic_component = "therm";
const PROGMEM char *topic_rl_fan = "rl_fan";
const PROGMEM char *topic_rl_heat = "rl_heat";
const PROGMEM char *topic_sw_presence = "sw_presence";
const PROGMEM char *topic_cur_temp = "cur_temp";
const PROGMEM char *topic_cur_hum = "cur_hum";
const PROGMEM char *topic_set_temp = "set_temp";

// states
struct
{
  uint8 fan_relay = 0, heat_relay = 0;
  uint8 presence = 0;
  float cur_temp = NAN, cur_hum = NAN, tgt_temp = NAN, last_reported_temp = NAN, last_reported_hum = NAN;
  uint64 last_reported_ts = 0;
} therm_state;

// knob
uint8 knob_pin_state_history = 0;
int8 knob_delta = 0;

// temp and humidity avg
#define MIN_TEMP_DELTA_BETWEEN_REPORTS 0.1
#define MIN_HUM_DELTA_BETWEEN_REPORTS 1.0
#define TEMP_REPORT_NOCHANGE_PERIOD (60 * 1000)

struct
{
  float temp_window_sum = NAN, hum_window_sum = NAN;
  uint8 temp_sample_counts = 0, hum_sample_counts = 0;
} dht11_avg_storage;
#define NUM_SAMPLES_FOR_TEMP_AVG 10

///////////////////////////////////////////////////////////////////////////////////////
// fwd declarations
void send_mqtt_state_relays();
void send_mqtt_state_presence();
void send_mqtt_state_cur_temp();
void send_mqtt_state_target_temp();

bool fan_on();
bool fan_off();
bool heat_on();
bool heat_off();
void update_target_temp(float);

///////////////////////////////////////////////////////////////////////////////////////

void dht11_sensor_read_task(void *)
{
  // Get temperature event and print its value.
  sensors_event_t event;
  dht_sensor.temperature().getEvent(&event);
  if (isnan(event.temperature))
  {
    Serial.println(F("Error reading temperature!"));
  }
  else
  {
    float temperature = event.temperature * 1.8 + 32;

    // Serial.print(F("Temperature: "));
    // Serial.print(temperature);
    // Serial.println(F("°F"));

    if (dht11_avg_storage.temp_sample_counts == 0)
    {
      dht11_avg_storage.temp_window_sum = temperature;
    }
    else
    {
      while (dht11_avg_storage.temp_sample_counts >= NUM_SAMPLES_FOR_TEMP_AVG)
      {
        therm_state.cur_temp = dht11_avg_storage.temp_window_sum / dht11_avg_storage.temp_sample_counts;
        dht11_avg_storage.temp_window_sum -= therm_state.cur_temp;
        --dht11_avg_storage.temp_sample_counts;
      }

      dht11_avg_storage.temp_window_sum += temperature;
    }
    ++dht11_avg_storage.temp_sample_counts;
  }
  // Get humidity event and print its value.
  dht_sensor.humidity().getEvent(&event);
  if (isnan(event.relative_humidity))
  {
    Serial.println(F("Error reading humidity!"));
  }
  else
  {
    // Serial.print(F("Humidity: "));
    // Serial.print(event.relative_humidity);
    // Serial.println(F("%"));

    if (dht11_avg_storage.hum_sample_counts == 0)
    {
      dht11_avg_storage.hum_window_sum = event.relative_humidity;
    }
    else
    {
      while (dht11_avg_storage.hum_sample_counts >= NUM_SAMPLES_FOR_TEMP_AVG)
      {
        therm_state.cur_hum = dht11_avg_storage.hum_window_sum / dht11_avg_storage.hum_sample_counts;
        dht11_avg_storage.hum_window_sum -= therm_state.cur_hum;
        --dht11_avg_storage.hum_sample_counts;
      }

      dht11_avg_storage.hum_window_sum += event.relative_humidity;
    }
    ++dht11_avg_storage.hum_sample_counts;
  }
}

void dht11_sensor_report_task()
{
  if (isnan(therm_state.cur_temp) || isnan(therm_state.cur_hum))
  {
    return;
  }

  if (isnan(therm_state.last_reported_temp) ||
      (abs(therm_state.last_reported_temp - therm_state.cur_temp) >= MIN_TEMP_DELTA_BETWEEN_REPORTS || abs(therm_state.last_reported_hum - therm_state.cur_hum) >= MIN_HUM_DELTA_BETWEEN_REPORTS || millis() - therm_state.last_reported_ts > TEMP_REPORT_NOCHANGE_PERIOD))
  {
    therm_state.last_reported_temp = therm_state.cur_temp;
    therm_state.last_reported_hum = therm_state.cur_hum;
    therm_state.last_reported_ts = millis();
    send_mqtt_state_cur_temp();
  }
}

void setup_dht()
{
  // Initialize device.
  dht_sensor.begin();

  const int temp_read_n_seconds = 2;
  sched.add_or_update_task((void *)&dht11_sensor_read_task, 0, NULL, 1, temp_read_n_seconds * 1000, 0 /*5000*/);
  sched.add_or_update_task((void *)&dht11_sensor_report_task, 0, NULL, 1, 2000, NUM_SAMPLES_FOR_TEMP_AVG * temp_read_n_seconds * 1000);
}

///////////////////////////////////////////////////////////////////////////////////////

void presence_detection_timeout_task(void *)
{
  // Serial.println(F("--- idle for too long"));
  // digitalWrite(RELAY_FAN_PIN, LOW);
  therm_state.presence = 0;
  send_mqtt_state_presence();
}

ICACHE_RAM_ATTR void presence_detection_task()
{
  // Serial.println("+++ presence detected");
  // digitalWrite(RELAY_FAN_PIN, HIGH);
  therm_state.presence = 1;
  send_mqtt_state_presence();
  sched.add_or_update_task((void *)&presence_detection_timeout_task, 0, NULL, 1, 0, 10000 /*15 * 60 * 1000*/); // 15 min delay, not periodic
}

void setup_presence_detection()
{
  pinMode(RCWL0516_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(RCWL0516_PIN), presence_detection_task, RISING);
}

///////////////////////////////////////////////////////////////////////////////////////

// filesystem
void init_fs()
{
  Serial.println("Mount LittleFS");
  if (!LittleFS.begin())
  {
    Serial.println("Format.");
    LittleFS.format();
    Serial.println("Mount newly formatted LittleFS");
    LittleFS.begin();
  }
  Serial.println("LittleFS mounted");
}


///////////////////////////////////////////////////////////////////////////////////////

// MQTT

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

///////////////////////////////////////////////////////////////////////////////////////

// knob

void report_new_target_temp_task()
{
  send_mqtt_state_target_temp();
}

void knob_interrupt_handler_impl(bool pin_a, bool pin_b)
{
  uint8_t state = (pin_a << 1) | pin_b;
  if (state != (knob_pin_state_history & 0x3))
  {
    // Serial.println(String("ec11 new state = ") + state);
    // Serial.println(String("knob_pin_state_history = ") + knob_pin_state_history);

    knob_pin_state_history <<= 2;
    knob_pin_state_history |= state;

    if (knob_pin_state_history == 0x87)
    {
      --knob_delta;
      // Serial.println(String("delta ") + knob_delta);
      knob_pin_state_history = 0;
    }
    else if (knob_pin_state_history == 0x4B)
    {
      ++knob_delta;
      // Serial.println(String("delta ") + knob_delta);
      knob_pin_state_history = 0;
    }
  }
}

ICACHE_RAM_ATTR void knob_interrupt_handler()
{
  // Serial.println(String("Interrupt"));
  knob_interrupt_handler_impl(digitalRead(KNOB_A_PIN), digitalRead(KNOB_B_PIN));
}

void knob_rotate_handler_task()
{
  if (!knob_delta)
    return;
  // Serial.println(String("delta = ") + knob_delta);

  if (!isnan(therm_state.tgt_temp))
  {
    therm_state.tgt_temp += knob_delta * 0.25;
    // Serial.println(String("target temp ") + therm_state.tgt_temp);

    sched.add_or_update_task((void *)report_new_target_temp_task, 0, NULL, 0, 0, 5 * 1000);
  }
  knob_delta = 0;
}

void knob_button_handle_change(int state)
{
  Serial.println(String("button state changed ") + state);
}

bool prev_knob_isr_button_state = 0;
void knob_button_debouncer_task()
{
  int current_state = digitalRead(KNOB_BTN_PIN);
  if (prev_knob_isr_button_state == current_state)
  {
    // state held for duration of time, report
    knob_button_handle_change(current_state);
  }
}

ICACHE_RAM_ATTR void knob_button_interrupt_handler()
{
  int current_state = digitalRead(KNOB_BTN_PIN);
  prev_knob_isr_button_state = current_state;
  sched.add_or_update_task((void *)knob_button_debouncer_task, 0, NULL, 0, 0, 1);
}

void init_knob()
{
  attachInterrupt(digitalPinToInterrupt(KNOB_A_PIN), knob_interrupt_handler, CHANGE);
  attachInterrupt(digitalPinToInterrupt(KNOB_B_PIN), knob_interrupt_handler, CHANGE);
  attachInterrupt(digitalPinToInterrupt(KNOB_BTN_PIN), knob_button_interrupt_handler, CHANGE);
  sched.add_or_update_task((void *)knob_rotate_handler_task, 0, NULL, 0, 1, 0);
}

///////////////////////////////////////////////////////////////////////////////////////
// graphics

// 'flame', 16x16px
const unsigned char bmp_flame[] PROGMEM = {
    0xff, 0xff, 0xfe, 0xff, 0xfe, 0x7f, 0xfe, 0x7f, 0xfc, 0x3f, 0xf8, 0x37, 0xf8, 0x87, 0xe0, 0xc3,
    0xc3, 0xc3, 0xc3, 0xe3, 0xc7, 0xe3, 0xcf, 0xe3, 0xcf, 0xe7, 0xef, 0xef, 0xff, 0xff, 0xff, 0xff};

// 'fan', 16x16px
const unsigned char bmp_fan[] PROGMEM = {
    0xfc, 0x3f, 0xf8, 0x1f, 0xf8, 0x1f, 0xfc, 0x1f, 0xfc, 0x1f, 0x82, 0x19, 0x00, 0x20, 0x00, 0x00,
    0x00, 0x00, 0x04, 0x00, 0x98, 0x41, 0xf8, 0x3f, 0xf8, 0x3f, 0xf8, 0x1f, 0xf8, 0x1f, 0xfc, 0x3f};

// 'person', 16x16px
const unsigned char person[] PROGMEM = {
    0xff, 0xe7, 0xff, 0xe7, 0xfc, 0x3f, 0xf3, 0x9f, 0xf7, 0x0f, 0xff, 0x2f, 0xfe, 0x60, 0xfc, 0xff,
    0xfc, 0xff, 0xfa, 0x7f, 0x07, 0x9f, 0xff, 0x9f, 0xff, 0xbf, 0xff, 0xbf, 0xff, 0x7f, 0x00, 0x00};

///////////////////////////////////////////////////////////////////////////////////////

// control functions

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

///////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////

void setup()
{
  Serial.begin(9600);

  pinMode(RELAY_FAN_PIN, OUTPUT);
  pinMode(RELAY_HEAT_PIN, OUTPUT);

  Serial.println("init fs");
  init_fs();
  Serial.println("init wifi");
  init_wifi();
  Serial.println("init mqtt");
  init_mqtt();

  Serial.println("init dht");
  setup_dht();
  Serial.println("init knob");
  init_knob();
  Serial.println("init radar");
  setup_presence_detection();
}

void loop()
{
  sched.run(0);
}
