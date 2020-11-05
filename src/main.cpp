
#include <arduino.h>

#include "tasks.h"
#include "config.h"
#include "utils.h"

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <LittleFS.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

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

// Wifi
ESP8266WebServer web_server(80);

// MQTT
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
} therm_state;

// knob
uint8 knob_pin_state_history = 0;
int8 knob_delta = 0;

// temp and humidity avg
#define MIN_TEMP_DELTA_BETWEEN_REPORTS 0.05
#define MIN_HUM_DELTA_BETWEEN_REPORTS 0.1
struct
{
  float temp_window_sum = NAN, hum_window_sum = NAN;
  uint8 temp_sample_counts = 0, hum_sample_counts = 0;
} dht11_avg_storage;
#define NUM_SAMPLES_FOR_TEMP_AVG 5

///////////////////////////////////////////////////////////////////////////////////////
// fwd declarations
void send_mqtt_state();

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
    // Serial.print(F("Temperature: "));
    // Serial.print(event.temperature);
    // Serial.println(F("°C"));

    if (dht11_avg_storage.temp_sample_counts == 0)
    {
      dht11_avg_storage.temp_window_sum = event.temperature;
    }
    else
    {
      while (dht11_avg_storage.temp_sample_counts >= NUM_SAMPLES_FOR_TEMP_AVG)
      {
        therm_state.cur_temp = dht11_avg_storage.temp_window_sum / dht11_avg_storage.temp_sample_counts;
        dht11_avg_storage.temp_window_sum -= therm_state.cur_temp;
        --dht11_avg_storage.temp_sample_counts;
      }

      dht11_avg_storage.temp_window_sum += event.temperature;
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

  if (isnan(therm_state.last_reported_temp))
  {
    therm_state.last_reported_temp = therm_state.cur_temp;
    therm_state.last_reported_hum = therm_state.cur_hum;
  }
  else if (abs(therm_state.last_reported_temp - therm_state.cur_temp) >= MIN_TEMP_DELTA_BETWEEN_REPORTS || abs(therm_state.last_reported_hum - therm_state.cur_hum) >= MIN_HUM_DELTA_BETWEEN_REPORTS)
  {
    send_mqtt_state();
    therm_state.last_reported_temp = therm_state.cur_temp;
    therm_state.last_reported_hum = therm_state.cur_hum;
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
  Serial.println(F("--- idle for too long"));
  // digitalWrite(RELAY_FAN_PIN, LOW);
  therm_state.presence = 0;
  send_mqtt_state();
}

ICACHE_RAM_ATTR void presence_detection_task()
{
  Serial.println("+++ presence detected");
  // digitalWrite(RELAY_FAN_PIN, HIGH);
  therm_state.presence = 1;
  send_mqtt_state();
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

// wifi related stuff

WifiConfig wifi_conf;

bool is_wifi_connected()
{
  if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED)
  {
    Serial.printf(" Connected\n");
    Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.printf("Not Connected\n");
  return false;
}

bool is_wifi_in_ap_mode()
{
  return WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA;
}

void web_server_handle_client_task(void *)
{
  web_server.handleClient();
}

void wifi_connect()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    if (WiFi.SSID().compareTo(wifi_conf.ssid) == 0)
    {
      Serial.println(String("already connected to WiFi SSID: ") + wifi_conf.ssid);
      return;
    }
    WiFi.disconnect();
  }

  Serial.println(String("attempting to connnect to WiFi SSID: ") + wifi_conf.ssid);
  WiFi.hostname(wifi_conf.host);
  WiFi.mode(WiFiMode_t::WIFI_STA);
  WiFi.begin(wifi_conf.ssid, wifi_conf.pass);
}

void wifi_start_ap()
{
  IPAddress ip(192, 168, 8, 1);
  IPAddress gateway(192, 168, 8, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.hostname("therm");
  WiFi.softAPConfig(ip, gateway, subnet);
  Serial.println("starting AP");
  if (!WiFi.softAP("therm", "12345678"))
  {
    ESP.restart();
  }
  Serial.println("AP started.");
  Serial.println("SSID: " + WiFi.softAPSSID());
}

void mdns_update_task()
{
  MDNS.update();
}

void init_mdns()
{
  if (MDNS.begin(WiFi.hostname()))
  {
    Serial.println("MDNS responder started");
  }

  sched.add_or_update_task((void *)mdns_update_task, 0, NULL, 2, 1, 0);
}

void handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += web_server.uri();
  message += "\nMethod: ";
  message += (web_server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += web_server.args();
  message += "\n";

  for (uint8_t i = 0; i < web_server.args(); i++)
  {
    message += " " + web_server.argName(i) + ": " + web_server.arg(i) + "\n";
  }

  web_server.send(404, "text/plain", message);
}

const char wifi_start_ssid_config_phase_html[] PROGMEM = R"===(
<form method="GET" action="/c">
SSID: <input type="text" name="ssid" /> <br />
pass: <input type="password" name="pass" /> <br />
host: <input type="text" name="host" /> <br />
MQTT server: <input type="text" name="mqtt_server" /> <br />
MQTT user: <input type="text" name="mqtt_user" /> <br />
MQTT pass: <input type="password" name="mqtt_pass" /> <br />
<input type="submit" />
</form>
)===";

void wifi_start_ssid_config_phase_handle_root()
{
  web_server.send(200, "text/html", wifi_start_ssid_config_phase_html);
}

void wifi_start_ssid_config_phase_handle_params()
{
  auto ssid = web_server.arg("ssid");
  auto pass = web_server.arg("pass");
  auto host = web_server.arg("host");
  auto mqtt_server = web_server.arg("mqtt_server");
  auto mqtt_user = web_server.arg("mqtt_user");
  auto mqtt_pass = web_server.arg("mqtt_pass");

  if (ssid.isEmpty() || pass.isEmpty() || host.isEmpty() || mqtt_server.isEmpty() || mqtt_user.isEmpty())
  {
    web_server.send(400, "text/html", String("empty input @") + millis());
    return;
  }

  wifi_conf.ssid = ssid;
  wifi_conf.pass = pass;
  wifi_conf.host = host;
  wifi_conf.mqtt_server = mqtt_server;
  wifi_conf.mqtt_user = mqtt_user;
  wifi_conf.mqtt_pass = mqtt_pass;

  web_server.send(200, "text/html", String("OK @") + millis());

  wifi_conf.write("/wifi.conf");
  ESP.restart();
}

void wifi_start_ssid_config_phase()
{
  web_server.on("/", wifi_start_ssid_config_phase_handle_root);
  web_server.on("/c", wifi_start_ssid_config_phase_handle_params);
  web_server.onNotFound(handleNotFound);
  web_server.begin();
  sched.add_or_update_task((void *)web_server_handle_client_task, 0, NULL, 0, 1, 0);
  Serial.println("SSID Config HTTP server started");
}

void init_wifi()
{
  if (!wifi_conf.read("/wifi.conf"))
  {
    // unable to read stored wifi creds, start in AP mode and get wifi config from user
    wifi_start_ap();
    wifi_start_ssid_config_phase();
  }
  else
  {
    // wifi config read. now connect
    // check wifi periodically, and reconnect if needed.
    sched.add_or_update_task((void *)wifi_connect, 0, NULL, 0, 30 * 1000, 0);
  }
  init_mdns();
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

  String topic_str(topic);
  int state = -1;
  if (lwip_strnicmp((const char *)payload, "on", length) == 0)
    state = 1;
  else if (lwip_strnicmp((const char *)payload, "off", length) == 0)
    state = 0;

  int relay_pin = -1;
  if (topic_str.endsWith(topic_rl_fan))
  {
    relay_pin = RELAY_FAN_PIN;
    Serial.println(String("fan state = ") + state);
    if (state >= 0)
    {
      digitalWrite(relay_pin, state);
      therm_state.fan_relay = state;
    }
  }
  else if (topic_str.endsWith(topic_rl_heat))
  {
    relay_pin = RELAY_HEAT_PIN;
    Serial.println(String("heat state = ") + state);
    if (state >= 0)
    {
      digitalWrite(relay_pin, state);
      therm_state.heat_relay = state;
    }
  }
  else if (topic_str.endsWith(topic_set_temp))
  {
    auto buff = std::auto_ptr<char>(new char[length + 1]);
    strncpy(buff.get(), (char *)payload, length);
    buff.get()[length] = 0;
    String payload_str = String(buff.get());

    Serial.println(String("set temp = ") + payload_str);

    therm_state.tgt_temp = payload_str.toFloat(); // TODO: pull this out into a function
    return;
  }
  else
  {
    // we don't understand the message yet (then why did the broker send it our way?)
    return;
  }

  // send the updated
}

void mqtt_connect()
{
  if (!mqtt_client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    if (mqtt_client.connect(wifi_conf.host.c_str(), wifi_conf.mqtt_user.c_str(), wifi_conf.mqtt_pass.c_str()))
    {
      Serial.println("connected");
      String topic_prefix = "cmnd/";
      topic_prefix += topic_component;
      topic_prefix += "/";
      topic_prefix += wifi_conf.host;
      topic_prefix += "/";
      Serial.println("Subscribe to " + topic_prefix + topic_rl_fan);
      mqtt_client.subscribe((topic_prefix + topic_rl_fan).c_str(), 1);
      Serial.println("Subscribe to " + topic_prefix + topic_rl_heat);
      mqtt_client.subscribe((topic_prefix + topic_rl_heat).c_str(), 1);
      Serial.println("Subscribe to " + topic_prefix + topic_set_temp);
      mqtt_client.subscribe((topic_prefix + topic_set_temp).c_str(), 1);

      mqtt_client.setCallback(mqtt_incoming_message_callback);
    }
  }
}

void mqtt_update_task(void *)
{
  mqtt_client.loop();
}

void send_mqtt_state()
{
  String topic_prefix = "stat/";
  topic_prefix += topic_component;
  topic_prefix += "/";
  topic_prefix += wifi_conf.host;
  topic_prefix += "/";

  Serial.println("MQTT: " + (topic_prefix + topic_rl_fan) + " = " + (therm_state.fan_relay ? "on" : "off"));
  mqtt_client.publish((topic_prefix + topic_rl_fan).c_str(), therm_state.fan_relay ? "on" : "off");

  Serial.println("MQTT: " + (topic_prefix + topic_rl_heat) + " = " + (therm_state.heat_relay ? "on" : "off"));
  mqtt_client.publish((topic_prefix + topic_rl_heat).c_str(), therm_state.heat_relay ? "on" : "off");

  Serial.println("MQTT: " + (topic_prefix + topic_sw_presence) + " = " + (therm_state.presence ? "on" : "off"));
  mqtt_client.publish((topic_prefix + topic_sw_presence).c_str(), therm_state.presence ? "on" : "off");

  if (!isnan(therm_state.cur_temp))
  {
    Serial.println("MQTT: " + (topic_prefix + topic_cur_temp) + " = " + therm_state.cur_temp);
    mqtt_client.publish((topic_prefix + topic_cur_temp).c_str(), String(therm_state.cur_temp).c_str());
  }

  if (!isnan(therm_state.cur_hum))
  {
    Serial.println("MQTT: " + (topic_prefix + topic_cur_hum) + " = " + therm_state.cur_hum);
    mqtt_client.publish((topic_prefix + topic_cur_hum).c_str(), String(therm_state.cur_hum).c_str());
  }

  if (!isnan(therm_state.tgt_temp))
  {
    Serial.println("MQTT: " + (topic_prefix + topic_set_temp) + " = " + therm_state.tgt_temp);
    mqtt_client.publish((topic_prefix + topic_set_temp).c_str(), String(therm_state.tgt_temp).c_str());
  }
}

void init_mqtt()
{
  Serial.println(String("MQTT server: " + wifi_conf.mqtt_server));
  mqtt_client.setServer(wifi_conf.mqtt_server.c_str(), 1883);

  sched.add_or_update_task((void *)mqtt_connect, 0, NULL, 0, 30 * 1000, 5000);
  sched.add_or_update_task((void *)mqtt_update_task, 0, NULL, 0, 10, 1000);
}

///////////////////////////////////////////////////////////////////////////////////////

// knob

void report_new_target_temp_task()
{
  send_mqtt_state();
}

void knob_interrupt_handler_impl(bool pin_a, bool pin_b)
{
  uint8_t state = (pin_a << 1) | pin_b;
  if (state != (knob_pin_state_history & 0x3))
  {
    knob_pin_state_history <<= 2;
    knob_pin_state_history |= state;

    // Serial.println(String("knob_pin_state_history = ") + knob_pin_state_history);

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

  if (!isnan(therm_state.tgt_temp))
  {
    therm_state.tgt_temp += knob_delta * 0.25;
    Serial.println(String("target temp ") + therm_state.tgt_temp);

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
