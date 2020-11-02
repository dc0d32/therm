
#include <arduino.h>

#include "tasks.h"

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>


#define RELAY0_PIN 16     // D0
#define RELAY1_PIN 13     // D7
#define DHT11_PIN 14      // D5
#define RCWL0516_PIN 12   // D6
#define SSD1306_SDA_PIN 4 // D2
#define SSD1306_SCL_PIN 5 // D1

// global vars

scheduler sched(8); // 'task' scheduler

DHT_Unified dht_sensor(DHT11_PIN, DHT11); // DHT temp sensor (adafruit library)
bool presence_detection_status = false;

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);


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

void setup()
{
  Serial.begin(9600);
  // delay(2000);

  delay(2000);
  Serial.println(F("SSD1306 begin"));
  Wire.begin(SSD1306_SDA_PIN, SSD1306_SCL_PIN);
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ; // Don't proceed, loop forever
  }

  /*
  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();
  delay(2000); // Pause for 2 seconds
  */

  display.clearDisplay();

  while (true)
  {
    int box_size = 20;
    for (size_t i = 0; i < 128; i += box_size)
    {
      for (size_t j = 0; j < 64; j += box_size)
      {
        // display.clearDisplay();

        // Draw a single pixel in white
        display.fillRect(i, j, box_size, box_size, SSD1306_WHITE);

        // Show the display buffer on the screen. You MUST call display() after
        // drawing commands to make them visible on screen!
        display.display();
        // delay(50);
      }
    }
  }
  // Draw a single pixel in white
  display.drawPixel(10, 10, SSD1306_WHITE);

  // Show the display buffer on the screen. You MUST call display() after
  // drawing commands to make them visible on screen!
  display.display();
  delay(2000);
  // Clear the buffer
  display.clearDisplay();

  pinMode(RELAY0_PIN, OUTPUT);
  pinMode(RELAY1_PIN, OUTPUT);

  setup_dht();
  setup_presence_detection();
}

void loop()
{
  sched.run(1);

  /*
  digitalWrite(RELAY0_PIN, LOW);
  digitalWrite(RELAY1_PIN, HIGH);
  delay(1000);
  digitalWrite(RELAY0_PIN, HIGH);
  digitalWrite(RELAY1_PIN, LOW);
  delay(1000);
  */
}
