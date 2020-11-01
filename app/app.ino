
#include <arduino.h>
#include <pthread.h>

#include "tasks.h"

#define RELAY0_PIN 16 // D0
#define RELAY1_PIN 13 // D7

scheduler sched(8);

void setup()
{

  pinMode(RELAY0_PIN, OUTPUT);
  pinMode(RELAY1_PIN, OUTPUT);

  // Serial.begin(9600);

  sched.add_or_update_task((void *)&task1_1, 0, NULL, 0, 0, 0);
  sched.add_or_update_task((void *)&task2_1, 0, NULL, 0, 0, 0);
}

void task1_1(void *)
{
  digitalWrite(RELAY0_PIN, HIGH);
  sched.add_or_update_task((void *)&task1_2, 0, NULL, 0, 0, 200);
}
void task1_2(void *)
{
  digitalWrite(RELAY0_PIN, LOW);
  sched.add_or_update_task((void *)&task1_1, 0, NULL, 0, 0, 200);
}

void task2_1(void *)
{
  digitalWrite(RELAY1_PIN, HIGH);
  sched.add_or_update_task((void *)&task2_2, 0, NULL, 0, 0, 201);
}
void task2_2(void *)
{
  digitalWrite(RELAY1_PIN, LOW);
  sched.add_or_update_task((void *)&task2_1, 0, NULL, 0, 0, 201);
}



void loop()
{
  sched.run(0);


  
  /*
  digitalWrite(RELAY0_PIN, LOW);
  digitalWrite(RELAY1_PIN, HIGH);
  delay(1000);
  digitalWrite(RELAY0_PIN, HIGH);
  digitalWrite(RELAY1_PIN, LOW);
  delay(1000);
  */
}
