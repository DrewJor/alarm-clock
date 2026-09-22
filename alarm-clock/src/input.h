#pragma once
#include <stdint.h>

// One queue for every control. The UI never polls pins directly - it just
// asks "what happened?" and switches on the answer. Adding a control later
// means adding an enum value, not touching the menu code.
enum InputEvent : uint8_t {
  EV_NONE = 0,
  EV_ENC_CW, EV_ENC_CCW,          // one per detent
  EV_ENC_PRESS, EV_ENC_HOLD,      // confirm / exit
  EV_ALARM_PRESS, EV_ALARM_HOLD, // diagnostic shortcut only; physical GPIO17 button removed
  EV_SNOOZE_PRESS, EV_SNOOZE_HOLD,
  EV_BACK_PRESS, EV_BACK_HOLD
};

void        input_begin();
InputEvent  input_poll();          // call every loop; returns EV_NONE when idle
const char* input_name(InputEvent e);
