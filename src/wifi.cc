#include "wifi.h"
#include "tasks.h"

extern scheduler sched;

ESP8266WebServer web_server(80);
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
