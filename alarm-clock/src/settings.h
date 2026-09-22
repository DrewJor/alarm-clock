#pragma once

// Persistent settings, calendar helpers and US DST rules.
#include <stdint.h>
#include <RTClib.h>

// one alarm
// Times are stored as 24-hour internally and
// converted for display; the 12/24 setting is presentation only.
struct Alarm {
  bool enabled;
  uint8_t hour, minute; // 0-23, 0-59
  bool daily;           // true = every day, false = the date below
  uint16_t year;        // only meaningful when daily == false
  uint8_t month, day;
  uint8_t sound;      // 1..3
  uint8_t light;      // 1..3
  uint8_t snooze_min; // 5-15 minutes between snooze and re-ring
  uint8_t snooze_max; // 0-10; zero disables snoozing
  uint8_t length_min; // 0 = indefinite; otherwise minutes before stopping
};

struct Settings {
  uint32_t magic; // guards against loading an older layout
  Alarm alarm[3];
  bool fmt24h;
  bool auto_bright;
  uint8_t manual_level; // 1..3
  bool dst_auto;
  int8_t dst_manual_hr; // accumulated manual hour offset; used when dst_auto is false
};

extern Settings cfg;

void settings_defaults(Settings &s);
bool settings_clamp_snooze_delays(Settings &s); // true if an older out-of-range delay changed
void settings_load();
bool settings_save();

// calendar
bool is_leap(uint16_t y);
uint8_t days_in_month(uint16_t y, uint8_t m);
uint8_t weekday_of(uint16_t y, uint8_t m, uint8_t d); // 0 = Sunday
const char *weekday_name(uint8_t w);
const char *weekday_short(uint8_t w);

// daylight saving
// The DS3231 always holds STANDARD time. The offset is applied on the way
// out, so a DST change never rewrites the chip.
bool us_dst_active(const DateTime &standard);
int8_t dst_offset_hours(const DateTime &standard);
DateTime to_local(const DateTime &standard);

