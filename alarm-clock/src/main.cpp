// EE 4953 Alarm Clock - hardware layer and alarm runtime
//
// Arduino-ESP32 core 3.x (ESP-IDF 5 underneath, so driver/ calls are
// available directly). Board: ESP32-S3-DevKitC-1 N16R8.
// Libraries: U8g2, RTClib.
//
// The menus live in ui.cpp; this file owns everything that touches a
// pin, plus the alarm/snooze timing that has to keep running no matter
// which screen is showing.
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <RTClib.h>
#include "driver/gpio.h"
#include "hal/adc_types.h"
#include "config.h"
#include "settings.h"
#include "input.h"
#include "hw.h"
#include "ui.h"
#include "save.h"
#include "scheduler.h"
#include "brightness.h"
#include "screen_effect.h"

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
static RTC_DS3231 rtc;
static AlarmSchedule schedule;
static DateTime s_standard;

static bool s_rtc_ok = false;
static bool s_clock_valid = false; // false until a real time is set
static DateTime s_local;           // local time, refreshed on the tick
static int8_t s_dst_off = 0;       // recomputed once per tick

// Buzzer
//
// Three sounds use different timing and frequency patterns.
static bool s_sound_ok = false, s_light_ok = true;
static bool s_sound_attached = false;
static uint32_t s_last_frequency = UINT32_MAX, s_last_duty = UINT32_MAX;
bool hw_sound_ok() {
  return s_sound_ok;
}
bool hw_light_ok() {
  return s_light_ok;
}
static void buzzer_begin() {
  pinMode(PIN_BUZZER, INPUT);
  if (!BUZZ_OUTPUT_ENABLED) {
    Serial.println("[sound] output disabled in config");
    return;
  }
  s_sound_attached = s_sound_ok = ledcAttach(PIN_BUZZER, 2000, BUZZ_RES_BITS);
  if (s_sound_ok)
    s_sound_ok = ledcWrite(PIN_BUZZER, 0);
  if (BUZZ_IS_LOW_IMPEDANCE)
    gpio_set_drive_capability((gpio_num_t)PIN_BUZZER, GPIO_DRIVE_CAP_0);
  Serial.println(s_sound_ok ? "[sound] output enabled; ready for user test"
                            : "[sound] driver initialization failed");
}
static void buzzer_off() {
  if (s_sound_attached && !ledcWrite(PIN_BUZZER, 0)) {
    ledcDetach(PIN_BUZZER);
    s_sound_attached = false;
    s_sound_ok = false;
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
  }
  s_last_frequency = s_last_duty = UINT32_MAX;
}
static bool buzzer_service(uint8_t sound, uint32_t el) {
  if (!s_sound_ok || sound < 1 || sound > 3)
    return false;
  uint32_t frequency = sound == 2 ? ((el / 150) % 2 ? 2400 : 1600) : 2000;
  uint32_t duty = sound == 1 && (el / 500) % 2 ? 0 : BUZZ_DUTY;
  if (sound == 3)
    duty = min(el / 120, uint32_t(BUZZ_DUTY));
  if (frequency != s_last_frequency) {
    s_sound_ok = ledcWriteTone(PIN_BUZZER, frequency) != 0;
    s_last_frequency = frequency;
    s_last_duty = UINT32_MAX;
  }
  if (s_sound_ok && duty != s_last_duty) {
    s_sound_ok = ledcWrite(PIN_BUZZER, duty);
    s_last_duty = duty;
  }
  if (!s_sound_ok)
    buzzer_off();
  return s_sound_ok;
}

// Alarm lighting uses the OLED. The board RGB LED is cleared
// once at boot and is never used for alarm patterns or previews.
static uint8_t s_screen_pattern = 0;
static uint32_t s_screen_elapsed = 0;
static void light_off() {
  s_screen_pattern = 0;
  s_screen_elapsed = 0;
}
static bool light_service(uint8_t pattern, uint32_t el) {
  if (pattern < 1 || pattern > 3)
    return s_light_ok = false;
  if (!s_screen_pattern) {
    Wire.beginTransmission(OLED_ADDR);
    s_light_ok = Wire.endTransmission() == 0;
    if (!s_light_ok)
      return false;
  }
  s_screen_pattern = pattern;
  s_screen_elapsed = el;
  return true;
}
void hw_display_present() {
  if (s_screen_pattern && screen_effect(s_screen_pattern, s_screen_elapsed).inverted) {
    uint8_t *pixels = oled.getBufferPtr();
    const size_t count = size_t(oled.getBufferTileWidth()) * oled.getBufferTileHeight() * 8;
    for (size_t i = 0; i < count; ++i)
      pixels[i] ^= 0xff;
  }
  oled.sendBuffer();
}
// Photoresistor
// 3V3 -> LDR -> GPIO4, with the chip's internal pull-down as the lower
// leg. There is no external resistor.
static void ldr_begin() {
  // Two things here were established by measuring, not by reading docs:
  //
  // 1. analogRead() is what attaches the pad to an ADC channel, so the
  //    attenuation call has to come AFTER it or it fails with
  //    "Pin is not configured as analog channel".
  // 2. The pull-down must be the DIGITAL-domain call. On this chip
  //    rtc_gpio_pulldown_en() is ignored once the pad is analog (measured
  //    4095 floating), while gpio_pulldown_en() holds (measured 18).
  //    pinMode(INPUT_PULLDOWN) does not survive either (measured 4055).
  (void)analogRead(PIN_LDR);
  analogSetPinAttenuation(PIN_LDR,
                          static_cast<adc_attenuation_t>(ADC_ATTEN_DB_12)); // ~0-3.1 V window
  (void)analogRead(PIN_LDR);
  gpio_pullup_dis((gpio_num_t)PIN_LDR);
  gpio_pulldown_en((gpio_num_t)PIN_LDR); // the divider's lower leg
}

uint16_t hw_ldr_raw() {
  uint32_t acc = 0;
  for (int i = 0; i < 16; i++)
    acc += analogRead(PIN_LDR);
  return (uint16_t)(acc / 16);
}

// Three levels with hysteresis so the display does not flicker at a
// boundary. Returns 1 (dim) .. 3 (bright).
static uint8_t ldr_level(uint16_t raw) {
  static uint8_t level = 2;
  level = brightness_from_light(raw, level);
  return level;
}

static uint8_t s_level = 0; // 0 = nothing applied yet
static int16_t s_oled_contrast = -1;
void hw_brightness_apply(uint8_t level, bool automatic) {
  s_level = level;
  // Automatic mode uses stronger dimming in low and medium ambient light.
  // Contrast zero is the OLED's minimum drive, not display-off.
  uint8_t base_contrast = level >= 3   ? 255
                          : level == 2 ? (automatic ? 40 : 110)
                                       : (automatic ? 0 : 8);
  uint8_t contrast =
      s_screen_pattern ? screen_effect(s_screen_pattern, s_screen_elapsed).contrast : base_contrast;
  if (contrast == s_oled_contrast)
    return;
  oled.setContrast(contrast);
  s_oled_contrast = contrast;
}
uint8_t hw_brightness_level() {
  return s_level;
}
// Clock access: timekeeping is the sole RTC writer
// Checked transactions: an ACK alone does not establish a valid RTC sample.
static bool rtc_registers(uint8_t reg, uint8_t *out, uint8_t count) {
  Wire1.beginTransmission(RTC_ADDR);
  Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0)
    return false;
  if (Wire1.requestFrom(uint8_t(RTC_ADDR), count) != count)
    return false;
  for (uint8_t i = 0; i < count; ++i)
    out[i] = Wire1.read();
  return true;
}
static bool bcd(uint8_t v, uint8_t &out) {
  if ((v & 15) > 9 || (v >> 4) > 9)
    return false;
  out = (v >> 4) * 10 + (v & 15);
  return true;
}
static bool rtc_sample(DateTime &out) {
  uint8_t r[7], status;
  bool was = s_rtc_ok;
  s_rtc_ok = rtc_registers(0, r, 7) && rtc_registers(0x0f, &status, 1);
  s_clock_valid = false;
  if (was != s_rtc_ok)
    Serial.println(s_rtc_ok ? "[rtc] reconnected" : "[rtc] Clock unavailable");
  if (!s_rtc_ok || (status & 0x80))
    return false;
  uint8_t se, mi, ho, day, month, year;
  if (!bcd(r[0] & 0x7f, se) || !bcd(r[1] & 0x7f, mi) ||
      !bcd(r[2] & ((r[2] & 0x40) ? 0x1f : 0x3f), ho) || !bcd(r[4] & 0x3f, day) ||
      !bcd(r[5] & 0x1f, month) || !bcd(r[6], year))
    return false;
  if (r[2] & 0x40) {
    if (ho < 1 || ho > 12)
      return false;
    ho = ho % 12 + ((r[2] & 0x20) ? 12 : 0);
  }
  if ((r[5] & 0x80) || se > 59 || mi > 59 || ho > 23 || month < 1 || month > 12 || day < 1 ||
      day > days_in_month(2000 + year, month))
    return false;
  out = DateTime(2000 + year, month, day, ho, mi, se);
  s_clock_valid = true;
  return true;
}
DateTime hw_rtc_read() {
  return s_standard;
}
bool hw_clock_available() {
  return s_rtc_ok;
}
bool hw_clock_valid() {
  return s_clock_valid;
}
int8_t hw_dst_offset() {
  return s_dst_off;
}
bool hw_rtc_write(const DateTime &standard) {
  if (!standard.isValid() || standard.year() < YEAR_MIN || standard.year() > YEAR_MAX)
    return false;
  uint8_t status;
  if (!rtc_registers(0x0f, &status, 1))
    return false;
  // rtc.begin can have failed at boot; initialize its bus device on reconnect.
  if (!rtc.begin(&Wire1))
    return false;
  rtc.adjust(standard);
  DateTime rb;
  if (!rtc_sample(rb))
    return false;
  int64_t delta = int64_t(rb.unixtime()) - standard.unixtime();
  if (delta < 0 || delta > 2)
    return false;
  s_standard = rb;
  s_dst_off = dst_offset_hours(rb);
  s_local = to_local(rb);
  schedule.discontinuity();
  Serial.printf("[rtc] verified standard=%lu\n", (unsigned long)rb.unixtime());
  return true;
}
static bool save_read(DateTime &value) {
  return rtc_sample(value);
}
void hw_dst_settings_changed() {
  schedule.discontinuity();
  s_dst_off = dst_offset_hours(s_standard);
  s_local = s_standard + TimeSpan(s_dst_off * 3600);
}
static bool save_format(bool fmt24h) {
  bool old = cfg.fmt24h;
  cfg.fmt24h = fmt24h;
  if (settings_save())
    return true;
  cfg.fmt24h = old;
  return false;
}
static const SaveBackend save_backend = {save_read, hw_rtc_write, save_format};

// Previews never change saved settings.
static uint8_t s_test_kind = 0; // 0 none, 1 sound, 2 light
static uint8_t s_test_which = 0;
static uint32_t s_test_start = 0;

bool hw_sound_test(uint8_t s) {
  hw_test_stop();
  if (!buzzer_service(s, 0))
    return false;
  s_test_kind = 1;
  s_test_which = s;
  s_test_start = millis();
  return true;
}
bool hw_light_test(uint8_t l) {
  hw_test_stop();
  if (!light_service(l, 0))
    return false;
  s_test_kind = 2;
  s_test_which = l;
  s_test_start = millis();
  return true;
}
bool hw_testing() {
  return s_test_kind != 0;
}
void hw_test_stop() {
  if (!s_test_kind)
    return;
  s_test_kind = 0;
  buzzer_off();
  light_off();
}
// Alarm runtime
static bool s_ringing = false;
static uint8_t s_ring_idx = 0;
static uint32_t s_ring_ms = 0;
static uint8_t s_snz_used[3] = {0, 0, 0};
static uint32_t s_snz_due[3] = {};
static bool s_snoozing[3] = {};
static uint8_t s_pending_alarms = 0;

static bool snooze_remaining(uint8_t index, uint32_t now, uint32_t &seconds) {
  seconds = 0;
  if (index >= 3 || !s_snoozing[index] || !cfg.alarm[index].enabled)
    return false;
  int32_t left = int32_t(s_snz_due[index] - now);
  seconds = left > 0 ? (uint32_t(left) + 999) / 1000 : 0;
  return true;
}
bool hw_snooze_remaining(uint8_t index, uint32_t &seconds) {
  return snooze_remaining(index, millis(), seconds);
}

bool hw_next_alarm(DateTime &local, uint8_t &index) {
  if (!s_clock_valid)
    return false;
  bool found = schedule.next(s_standard, cfg, local, index);
  for (uint8_t i = 0; i < 3; ++i) {
    if (!s_snoozing[i] || !cfg.alarm[i].enabled)
      continue;
    int32_t left = int32_t(s_snz_due[i] - millis());
    DateTime standard = s_standard + TimeSpan(left > 0 ? (left + 999) / 1000 : 0);
    DateTime next = to_local(standard);
    if (!found || next.unixtime() < local.unixtime()) {
      local = next;
      index = i;
      found = true;
    }
  }
  return found;
}

bool hw_alarm_ringing() {
  return s_ringing;
}
uint8_t hw_alarm_index() {
  return s_ring_idx;
}
uint8_t hw_alarm_snoozes_used() {
  return s_snz_used[s_ring_idx];
}

static void alarm_start(uint8_t i, bool from_snooze) {
  hw_test_stop();
  if (!from_snooze)
    s_snz_used[i] = 0;
  s_snoozing[i] = false;
  s_ringing = true;
  s_ring_idx = i;
  s_ring_ms = millis();
  ui_alarm_overlay(true);
  Serial.printf("[alarm] %u ringing (%s)\n", i + 1, from_snooze ? "snooze" : "scheduled");
}

static void alarm_silence() {
  s_ringing = false;
  buzzer_off();
  light_off();
  ui_alarm_overlay(false);
}

// Snooze is allowed only while snoozes remain.
bool hw_alarm_snooze() {
  if (!s_ringing)
    return false;
  uint8_t i = s_ring_idx;
  if (s_snz_used[i] >= cfg.alarm[i].snooze_max)
    return false;
  s_snz_used[i]++;
  s_snz_due[i] = millis() + (uint32_t)cfg.alarm[i].snooze_min * 60000UL;
  s_snoozing[i] = true;
  alarm_silence();
  ui_snoozed();
  Serial.printf("[alarm] %u snoozed %u min (%u of %u)\n", i + 1, cfg.alarm[i].snooze_min,
                s_snz_used[i], cfg.alarm[i].snooze_max);
  return true;
}

void hw_alarm_dismiss() {
  if (!s_ringing)
    return;
  uint8_t i = s_ring_idx;
  s_snoozing[i] = false;
  // A one-shot alarm has done its job, so it switches itself off.
  if (!cfg.alarm[i].daily && cfg.alarm[i].enabled) {
    cfg.alarm[i].enabled = false;
    settings_save();
  }
  alarm_silence();
  Serial.printf("[alarm] %u dismissed\n", i + 1);
}

void hw_alarm_settings_changed(uint8_t i) {
  s_snoozing[i] = false;
  s_pending_alarms &= ~(1 << i);
}
static void service_due_alarms(uint32_t now) {
  if (s_ringing)
    return;
  for (uint8_t i = 0; i < 3; ++i) {
    if (!cfg.alarm[i].enabled) {
      s_snoozing[i] = false;
      s_pending_alarms &= ~(1 << i);
      continue;
    }
    if (s_snoozing[i] && int32_t(now - s_snz_due[i]) >= 0) {
      alarm_start(i, true);
      return;
    }
    if (s_pending_alarms & (1 << i)) {
      s_pending_alarms &= ~(1 << i);
      alarm_start(i, false);
      return;
    }
  }
}

static bool alarm_input(InputEvent ev) {
  if (ev == EV_SNOOZE_PRESS || ev == EV_SNOOZE_HOLD) {
    if (s_ringing && !hw_alarm_snooze()) {
      Serial.println("[alarm] no snoozes left");
      ui_snooze_refused();
    } else if (!s_ringing) {
      // Pressing again can reopen the countdown, but cannot cancel or extend it.
      ui_snoozed();
    }
    return true;
  }
  if (!s_ringing)
    return false;
  if (ev == EV_BACK_PRESS || ev == EV_BACK_HOLD)
    hw_alarm_dismiss();
  return true; // a ringing alarm owns the other controls
}
static bool alarm_timeout(uint32_t now) {
  if (s_ringing && cfg.alarm[s_ring_idx].length_min != 0 &&
      now - s_ring_ms >= uint32_t(cfg.alarm[s_ring_idx].length_min) * 60000UL) {
    Serial.println("[alarm] length expired");
    hw_alarm_dismiss();
    return true;
  }
  return false;
}
// setup
void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL, OLED_HZ); // bus 0: display
  Wire1.begin(PIN_RTC_SDA, PIN_RTC_SCL, RTC_HZ);
  Wire1.setTimeOut(25); // bus 1: RTC

  oled.setI2CAddress(OLED_ADDR << 1);
  oled.begin();
  oled.setFont(u8g2_font_6x12_tf);

  s_rtc_ok = rtc.begin(&Wire1);
  if (!s_rtc_ok)
    Serial.println("[rtc] DS3231 not found on bus 1");
  // The oscillator-stop flag determines whether the clock needs setting.
  // Do not seed the RTC from compile time.
  s_clock_valid = s_rtc_ok && !rtc.lostPower();
  if (s_rtc_ok && !s_clock_valid)
    Serial.println("[rtc] oscillator stopped - clock needs setting");

  buzzer_begin();
  rgbLedWrite(PIN_RGB_LED, 0, 0, 0);
  light_off();
  ldr_begin();
  input_begin();
  settings_load();
  ui_begin();

  hw_brightness_apply(cfg.auto_bright ? ldr_level(hw_ldr_raw()) : cfg.manual_level,
                      cfg.auto_bright);
  if (rtc_sample(s_standard)) {
    s_local = to_local(s_standard);
    s_dst_off = dst_offset_hours(s_standard);
  } // so the first frame is not 2000-01-01
}

// loop
void loop() {
  static uint32_t last_tick = 0, last_ui = 0;
  uint32_t now = millis();

  // 1 Hz: read the clock, apply DST, service alarms
  // SQW is not wired, so this is a software timer (see config.h).
  if (now - last_tick >= TICK_MS) {
    last_tick = now;
    if (rtc_sample(s_standard)) {
      int8_t offset = dst_offset_hours(s_standard);
      if (offset != s_dst_off)
        Serial.printf("[dst] offset %d -> %d; scheduler notified\n", s_dst_off, offset);
      s_dst_off = offset;
      s_local = s_standard + TimeSpan(offset * 3600);
      s_pending_alarms |= schedule.poll(s_standard, cfg);
    } else
      schedule.discontinuity();
  }
  service_due_alarms(now);
  clock_save.service(cfg, save_backend);

  // input
  InputEvent ev = input_poll();
  if (ev != EV_NONE) {
    if (!alarm_input(ev))
      ui_event(ev);
  }

  // Input can start a preview after peripheral calls advance millis().
  // Refresh now so subtraction cannot underflow and expire it immediately.
  now = millis();
  // ringing alarm
  if (s_ringing) {
    uint32_t el = now - s_ring_ms;
    // Stop ringing when the configured duration expires.
    if (!alarm_timeout(now)) {
      buzzer_service(cfg.alarm[s_ring_idx].sound, el);
      light_service(cfg.alarm[s_ring_idx].light, el);
    }
  }
  // preview
  else if (s_test_kind) {
    uint32_t el = now - s_test_start;
    if (el >= TEST_MS) {
      hw_test_stop();
    } else if (s_test_kind == 1) {
      if (!buzzer_service(s_test_which, el)) {
        hw_test_stop();
        ui_output_error(true);
      }
    } else {
      if (!light_service(s_test_which, s_test_which == 3 ? el * 15 : el)) {
        hw_test_stop();
        ui_output_error(false);
      }
    }
  }

  // redraw
  if (now - last_ui >= UI_REFRESH_MS) {
    last_ui = now;
    ui_service(now);
    bool automatic = cfg.auto_bright;
    uint8_t level = cfg.manual_level;
    ui_brightness_mode(automatic, level);
    hw_brightness_apply(automatic ? ldr_level(hw_ldr_raw()) : level, automatic);
    ui_draw(s_local);
  }
}
