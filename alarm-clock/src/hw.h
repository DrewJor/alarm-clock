#pragma once
// =====================================================================
//  The only things the menu code is allowed to ask of the hardware.
//  ui.cpp never touches a peripheral directly, so the pin-level work
//  stays in main.cpp where the wiring diagnostics already live.
// =====================================================================
#include <stdint.h>
#include <RTClib.h>
#include <U8g2lib.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled;
void hw_display_present(); // apply any screen light effect, then send the drawn frame

// ---- previews (Milestone 03 sections 11 and 12) -----------------------
// A test plays for a fixed time and never changes a saved setting.
bool    hw_sound_test(uint8_t sound);
bool    hw_light_test(uint8_t light);
void    hw_test_stop();
bool    hw_testing();

// ---- brightness (section 13) -----------------------------------------
void    hw_brightness_apply(uint8_t level, bool automatic = false); // 1..3 -> OLED contrast
uint8_t hw_brightness_level();                  // what is showing now
uint16_t hw_ldr_raw();

// ---- clock ------------------------------------------------------------
// Timekeeping is the sole RTC writer (chart 8). The value passed in and
// the value returned are both STANDARD time - DST is applied on top.
bool     hw_rtc_write(const DateTime &standard);
DateTime hw_rtc_read();
bool     hw_clock_valid();          // false until a real time has been set
int8_t   hw_dst_offset();           // cached at the tick, so drawing is cheap
void     hw_dst_settings_changed(); // refresh display; manual edits do not replay skipped alarms

// ---- ringing alarm ----------------------------------------------------
bool     hw_alarm_ringing();
uint8_t  hw_alarm_index();
bool     hw_alarm_snooze();         // false when no snoozes are left
void     hw_alarm_dismiss();
uint8_t  hw_alarm_snoozes_used();
bool     hw_snooze_remaining(uint8_t index, uint32_t &seconds);

bool hw_clock_available();
bool hw_next_alarm(DateTime &local, uint8_t &index);
// Driver status detects API failures, not a disconnected/inaudible transducer.
bool hw_sound_ok();
bool hw_light_ok();
void hw_alarm_settings_changed(uint8_t index);
