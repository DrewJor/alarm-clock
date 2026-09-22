#include <Arduino.h>
#include "driver/pulse_cnt.h" // IDF 5 PCNT driver (Arduino-ESP32 3.x)
#include "config.h"
#include "input.h"

// Rotary encoder
//
// PCNT decodes quadrature in hardware and rejects pulses shorter than
// ENC_GLITCH_NS. Software accumulates counts into complete detents.
static pcnt_unit_handle_t s_unit = nullptr;
static int s_sub_detent = 0; // counts not yet worth a whole detent
static int s_previous_count = 0;
static int s_pending = 0; // detents seen but not yet reported

static void encoder_begin() {
  pinMode(PIN_ENC_A, INPUT_PULLUP); // no external pull-ups on the board
  pinMode(PIN_ENC_B, INPUT_PULLUP);

  pcnt_unit_config_t unit_cfg = {};
  unit_cfg.low_limit = -10000;
  unit_cfg.high_limit = 10000;
  unit_cfg.flags.accum_count = 1;
  ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &s_unit));

  pcnt_glitch_filter_config_t filter = {};
  filter.max_glitch_ns = ENC_GLITCH_NS;
  ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(s_unit, &filter));

  // Two channels = 4x decoding. Divide by ENC_COUNTS_PER_DETENT below.
  pcnt_chan_config_t ca = {};
  ca.edge_gpio_num = PIN_ENC_A;
  ca.level_gpio_num = PIN_ENC_B;
  pcnt_channel_handle_t cha = nullptr;
  ESP_ERROR_CHECK(pcnt_new_channel(s_unit, &ca, &cha));
  ESP_ERROR_CHECK(pcnt_channel_set_edge_action(cha, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                               PCNT_CHANNEL_EDGE_ACTION_INCREASE));
  ESP_ERROR_CHECK(pcnt_channel_set_level_action(cha, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                                PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

  pcnt_chan_config_t cb = {};
  cb.edge_gpio_num = PIN_ENC_B;
  cb.level_gpio_num = PIN_ENC_A;
  pcnt_channel_handle_t chb = nullptr;
  ESP_ERROR_CHECK(pcnt_new_channel(s_unit, &cb, &chb));
  ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chb, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                               PCNT_CHANNEL_EDGE_ACTION_DECREASE));
  ESP_ERROR_CHECK(pcnt_channel_set_level_action(chb, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                                PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

  ESP_ERROR_CHECK(pcnt_unit_add_watch_point(s_unit, -10000));
  ESP_ERROR_CHECK(pcnt_unit_add_watch_point(s_unit, 10000));
  ESP_ERROR_CHECK(pcnt_unit_enable(s_unit));
  ESP_ERROR_CHECK(pcnt_unit_clear_count(s_unit));
  ESP_ERROR_CHECK(pcnt_unit_start(s_unit));
}

// Read a continuous accumulated count. Clearing between read and clear
// loses edges arriving between those two operations.
static void encoder_pump() {
  int raw = 0;
  if (pcnt_unit_get_count(s_unit, &raw) != ESP_OK)
    return;
  int delta = int32_t(uint32_t(raw) - uint32_t(s_previous_count));
  s_previous_count = raw;
  s_sub_detent += delta;
  int detents = s_sub_detent / ENC_COUNTS_PER_DETENT;
  if (detents != 0) {
    s_sub_detent -= detents * ENC_COUNTS_PER_DETENT;
    s_pending += detents;
  }
}
// Buttons - active LOW with internal pull-ups, software debounced.
// Snooze fires once on the debounced down edge; holding never cancels it.
// Other buttons fire PRESS on release or HOLD after HOLD_MS.
struct Button {
  uint8_t pin;
  InputEvent ev_press, ev_hold;
  bool armed = false;
  bool stable_low = false;
  bool hold_fired = false;
  uint8_t last_reading = HIGH;
  uint32_t changed_ms = 0;
  uint32_t down_ms = 0;
};

static Button s_btn[] = {
    {PIN_ENC_SW, EV_ENC_PRESS, EV_ENC_HOLD},
    {PIN_BTN_SNOOZE, EV_SNOOZE_PRESS, EV_SNOOZE_HOLD},
    {PIN_BTN_BACK, EV_BACK_PRESS, EV_BACK_HOLD},
};
static const int N_BTN = sizeof(s_btn) / sizeof(s_btn[0]);

static InputEvent button_poll(Button &b, uint32_t now) {
  uint8_t r = digitalRead(b.pin);
  if (r != b.last_reading) {
    b.last_reading = r;
    b.changed_ms = now;
    return EV_NONE;
  }
  if (now - b.changed_ms < DEBOUNCE_MS)
    return EV_NONE; // still settling

  bool low = (r == LOW);
  // Ignore boot-held buttons until released.
  if (!b.armed) {
    if (!low)
      b.armed = true;
    return EV_NONE;
  }
  if (low && !b.stable_low) { // just pressed
    b.stable_low = true;
    b.down_ms = now;
    b.hold_fired = false;
    if (b.ev_press == EV_SNOOZE_PRESS) {
      b.hold_fired = true; // suppress both hold and the release event
      return b.ev_press;
    }
  } else if (low && !b.hold_fired && now - b.down_ms >= HOLD_MS) {
    b.hold_fired = true;
    return b.ev_hold;                // held long enough
  } else if (!low && b.stable_low) { // just released
    b.stable_low = false;
    if (!b.hold_fired)
      return b.ev_press; // short press
  }
  return EV_NONE;
}
void input_begin() {
  for (int i = 0; i < N_BTN; i++)
    pinMode(s_btn[i].pin, INPUT_PULLUP);
  encoder_begin();
}

InputEvent input_poll() {
  uint32_t now = millis();

  // One detent per call, so a three-click spin yields three events rather
  // than one. The queue drains over the next few passes of loop().
  encoder_pump();
  for (int i = 0; i < N_BTN; i++) {
    InputEvent e = button_poll(s_btn[i], now);
    if (e != EV_NONE)
      return e;
  }
  if (s_pending > 0) {
    s_pending--;
    return EV_ENC_CW;
  }
  if (s_pending < 0) {
    s_pending++;
    return EV_ENC_CCW;
  }
  return EV_NONE;
}

const char *input_name(InputEvent e) {
  switch (e) {
  case EV_ENC_CW:
    return "ENC_CW";
  case EV_ENC_CCW:
    return "ENC_CCW";
  case EV_ENC_PRESS:
    return "ENC_PRESS";
  case EV_ENC_HOLD:
    return "ENC_HOLD";
  case EV_ALARM_PRESS:
    return "ALARM";
  case EV_ALARM_HOLD:
    return "ALARM_HOLD";
  case EV_SNOOZE_PRESS:
    return "SNOOZE";
  case EV_SNOOZE_HOLD:
    return "SNOOZE_HOLD";
  case EV_BACK_PRESS:
    return "BACK";
  case EV_BACK_HOLD:
    return "BACK_HOLD";
  default:
    return "NONE";
  }
}
