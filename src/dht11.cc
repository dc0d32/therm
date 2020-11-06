#include <ctype.h>

#include "dht11.h"
#include "config.h"
#include "tasks.h"
#include "mqtt.h"

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

struct
{
    float temp_window_sum = NAN, hum_window_sum = NAN;
    uint8 temp_sample_counts = 0, hum_sample_counts = 0;
} dht11_avg_storage;

DHT_Unified dht_sensor(DHT11_PIN, DHT11); // DHT temp sensor (adafruit library)

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
