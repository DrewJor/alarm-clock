#pragma once
#include <stdint.h>

// Debounced button and encoder events consumed by the UI.
enum InputEvent : uint8_t {
  EV_NONE = 0,
  EV_ENC_CW,
  EV_ENC_CCW, // one per detent
  EV_ENC_PRESS,
  EV_ENC_HOLD, // confirm / exit
  EV_ALARM_PRESS,
  EV_ALARM_HOLD, // diagnostic shortcut only; physical GPIO17 button removed
  EV_SNOOZE_PRESS,
  EV_SNOOZE_HOLD,
  EV_BACK_PRESS,
  EV_BACK_HOLD
};

void input_begin();
InputEvent input_poll(); // call every loop; returns EV_NONE when idle
const char *input_name(InputEvent e);
