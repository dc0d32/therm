#include "knob.h"

#include "config.h"
#include "tasks.h"
#include "mqtt.h"


uint8 knob_pin_state_history;
int8 knob_delta;

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
