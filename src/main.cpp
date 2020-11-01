
#include <arduino.h>

#include "tasks.h"

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

#define RELAY0_PIN 16   // D0
#define RELAY1_PIN 13   // D7
#define DHT11_PIN 14    // D5
#define RCWL0516_PIN 12 // D6

// global vars

scheduler sched(8); // 'task' scheduler

DHT_Unified dht_sensor(DHT11_PIN, DHT11); // DHT temp sensor (adafruit library)
bool presence_detection_status = false;



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

  pinMode(RELAY0_PIN, OUTPUT);
  pinMode(RELAY1_PIN, OUTPUT);

  Serial.begin(9600);
  // delay(2000);

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
