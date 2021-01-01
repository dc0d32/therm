#include "wifi.h"
#include "tasks.h"
#include "disp.h"

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
      init_web_server();
      Serial.println(String("already connected to WiFi SSID: ") + wifi_conf.ssid);
      draw_icon_wifi(true);

      return;
    }
    WiFi.disconnect();
  }

  draw_icon_wifi(false);

  Serial.println(String("attempting to connnect to WiFi SSID: ") + wifi_conf.ssid);
  WiFi.hostname(wifi_conf.host);
  WiFi.mode(WiFiMode_t::WIFI_STA);
  WiFi.begin(wifi_conf.ssid, wifi_conf.pass);
  // since we're not sure yet whether connection is successful, we don't set the wifi icon yet. It will be set the next time wifi_connect gets called
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

void handle_404()
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

const char config_form_html[] PROGMEM = R"===(
<form method="GET" action="/c">
  SSID: <input type="text" name="ssid" /> <br />
  pass: <input type="password" name="pass" /> <br />
  <input type="submit" />
</form>
<form method="GET" action="/c">
  host: <input type="text" name="host" /> <br />
  <input type="submit" />
</form>
<form method="GET" action="/c">
  MQTT server: <input type="text" name="mqtt_server" /> <br />
  MQTT user: <input type="text" name="mqtt_user" /> <br />
  MQTT pass: <input type="password" name="mqtt_pass" /> <br />
  <input type="submit" />
</form>
<form method='POST' action='/update' enctype='multipart/form-data'>
  <input type='file' name='update'>
  <input type='submit' value='Update'>
</form>
)===";

void handle_root()
{
  web_server.send(200, "text/html", config_form_html);
}

void handle_config_update_params()
{
  auto ssid = web_server.arg("ssid");
  auto pass = web_server.arg("pass");
  auto host = web_server.arg("host");
  auto mqtt_server = web_server.arg("mqtt_server");
  auto mqtt_user = web_server.arg("mqtt_user");
  auto mqtt_pass = web_server.arg("mqtt_pass");

  if (!wifi_conf.read("/wifi.conf"))
  {
    if (ssid.isEmpty() || pass.isEmpty() || host.isEmpty() || mqtt_server.isEmpty() || mqtt_user.isEmpty())
    {
      web_server.send(400, "text/html", String("empty input @") + millis());
      return;
    }
  }

  if (!ssid.isEmpty())
    wifi_conf.ssid = ssid;
  if (!pass.isEmpty())
    wifi_conf.pass = pass;
  if (!host.isEmpty())
    wifi_conf.host = host;
  if (!mqtt_server.isEmpty())
    wifi_conf.mqtt_server = mqtt_server;
  if (!mqtt_user.isEmpty())
    wifi_conf.mqtt_user = mqtt_user;
  if (!mqtt_pass.isEmpty())
    wifi_conf.mqtt_pass = mqtt_pass;

  web_server.send(200, "text/html", String("OK @") + millis());

  wifi_conf.write("/wifi.conf");
  web_server.sendHeader("Connection", "close");
  web_server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
  ESP.restart();
}

void init_web_server()
{
  web_server.on("/", handle_root);
  web_server.on("/c", handle_config_update_params);
  web_server.onNotFound(handle_404);
  web_server.on(
      "/update", HTTP_POST,
      []() { // when the file is fully received
        ESP.restart();
      },
      []() {
        HTTPUpload &upload = web_server.upload();
        if (upload.status == UPLOAD_FILE_START)
        {
          Serial.setDebugOutput(true);
          WiFiUDP::stopAll();
          Serial.printf("Update: %s\n", upload.filename.c_str());
          uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
          if (!Update.begin(maxSketchSpace))
          { //start with max available size
            Update.printError(Serial);
          }
        }
        else if (upload.status == UPLOAD_FILE_WRITE)
        {
          if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
          {
            Update.printError(Serial);
          }
          else
          {
            for (size_t i = 0; i <= upload.currentSize / 10240; i++)
            {
              Serial.write('.');
            }
          }
        }
        else if (upload.status == UPLOAD_FILE_END)
        {
          if (Update.end(true))
          { //true to set the size to the current progress
            Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
          }
          else
          {
            Update.printError(Serial);
          }
          Serial.setDebugOutput(false);
          web_server.sendHeader("Connection", "close");
          web_server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        }
        yield();
      });
  web_server.begin();
  sched.add_or_update_task((void *)web_server_handle_client_task, 0, NULL, 0, 1, 0);
  Serial.println("HTTP server started");
}

void init_wifi()
{
  if (!wifi_conf.read("/wifi.conf"))
  {
    // unable to read stored wifi creds, start in AP mode and get wifi config from user
    wifi_start_ap();
    init_web_server();
  }
  else
  {
    // wifi config read. now connect
    // check wifi periodically, and reconnect if needed.
    sched.add_or_update_task((void *)wifi_connect, 0, NULL, 0, 30 * 1000, 0);
  }
  init_mdns();
}
