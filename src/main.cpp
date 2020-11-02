
#include <arduino.h>

#include "tasks.h"
#include "config.h"

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

///////////////////////////////////////////////////////////////////////////////////////

#define RELAY0_PIN 16     // D0
#define RELAY1_PIN 13     // D7
#define DHT11_PIN 14      // D5
#define RCWL0516_PIN 12   // D6
#define SSD1306_SDA_PIN 4 // D2
#define SSD1306_SCL_PIN 5 // D1

///////////////////////////////////////////////////////////////////////////////////////

// global vars

scheduler sched(8); // 'task' scheduler

DHT_Unified dht_sensor(DHT11_PIN, DHT11); // DHT temp sensor (adafruit library)
bool presence_detection_status = false;

// screen related
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Wifi
ESP8266WebServer web_server(80);

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
    Serial.print(F("Temperature: "));
    Serial.print(event.temperature);
    Serial.println(F("°C"));
  }
  // Get humidity event and print its value.
  dht_sensor.humidity().getEvent(&event);
  if (isnan(event.relative_humidity))
  {
    Serial.println(F("Error reading humidity!"));
  }
  else
  {
    Serial.print(F("Humidity: "));
    Serial.print(event.relative_humidity);
    Serial.println(F("%"));
  }
}
void setup_dht()
{
  // Initialize device.
  dht_sensor.begin();

  sched.add_or_update_task((void *)&dht11_sensor_read_task, 0, NULL, 1, 2000, 0 /*5000*/);
}

///////////////////////////////////////////////////////////////////////////////////////

void presence_detection_timeout_task(void *)
{
  Serial.println(F("--- idle for too long"));
  digitalWrite(RELAY0_PIN, LOW);
  presence_detection_status = false;
}

ICACHE_RAM_ATTR void presence_detection_task()
{
  Serial.println("+++ presence detected");
  digitalWrite(RELAY0_PIN, HIGH);
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
  return WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED;
}

void web_server_handle_client_task(void *)
{
  web_server.handleClient();
}

bool ensure_wifi_connection()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    if (WiFi.SSID().compareTo(wifi_conf.ssid) == 0)
      return true;
    WiFi.disconnect();
  }

  WiFi.hostname(wifi_conf.host);
  WiFi.mode(WiFiMode_t::WIFI_STA);
  WiFi.begin(wifi_conf.ssid, wifi_conf.pass);

  int retries_left = 100;
  while (WiFi.status() != WL_CONNECTED && retries_left)
  {
    delay(500);
    Serial.print(".");
    --retries_left;
  }

  if (!retries_left)
  {
    return false;
  }

  Serial.printf(" Connected\n");
  Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());

  return true;
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
    init_mdns();
    return;
  }

  // wifi config read. now connect
  if (!ensure_wifi_connection())
  {
    ESP.restart();
  }

  // check wifi periodically, and reconnect if needed.
  sched.add_or_update_task((void *)ensure_wifi_connection, 0, NULL, 0, 30 * 1000, 0);

  init_mdns();
}

///////////////////////////////////////////////////////////////////////////////////////

void setup()
{
  Serial.begin(9600);

  pinMode(RELAY0_PIN, OUTPUT);
  pinMode(RELAY1_PIN, OUTPUT);

  init_fs();
  init_wifi();

  if (is_wifi_connected())
  {
    setup_dht();
    setup_presence_detection();
  }
}

void loop()
{
  sched.run(0);
}
