#include <Arduino.h>
#include <Preferences.h>
#include "config.h"
#include "settings.h"

Settings cfg;
static Preferences prefs;

// Bump this whenever a field is added to or moved inside Settings. A saved
// blob with a different magic is ignored, so an old layout can never be
// reinterpreted as the new one.
#define CFG_MAGIC 0x41434C33UL      // 'ACL3'

void settings_defaults(Settings &s) {
  s = Settings{};
  s.magic = CFG_MAGIC;
  for (uint8_t i = 0; i < 3; i++) {
    s.alarm[i].enabled    = false;
    s.alarm[i].hour       = 7;
    s.alarm[i].minute     = 0;
    s.alarm[i].daily      = true;
    s.alarm[i].year       = YEAR_MIN;
    s.alarm[i].month      = 1;
    s.alarm[i].day        = 1;
    s.alarm[i].sound      = 1;
    s.alarm[i].light      = 1;
    s.alarm[i].snooze_min = 9;
    s.alarm[i].snooze_max = 3;
    s.alarm[i].length_min = 5;
  }
  s.fmt24h        = false;
  s.auto_bright   = true;
  s.manual_level  = 2;
  s.dst_auto      = true;
  s.dst_manual_hr = 0;
}

bool settings_clamp_snooze_delays(Settings &s) {
  bool changed = false;
  for (auto &alarm : s.alarm) {
    uint8_t delay = alarm.snooze_min;
    if (delay < SNOOZE_DELAY_MIN) delay = SNOOZE_DELAY_MIN;
    if (delay > SNOOZE_DELAY_MAX) delay = SNOOZE_DELAY_MAX;
    changed |= delay != alarm.snooze_min;
    alarm.snooze_min = delay;
  }
  return changed;
}

void settings_load() {
  settings_defaults(cfg);
  prefs.begin("clock", false);   // read-write, so the namespace exists even
  bool have = prefs.isKey("cfg");// on a blank chip
  Settings tmp;
  bool ok = false;
  if (have && prefs.getBytesLength("cfg") == sizeof(tmp)
           && prefs.getBytes("cfg", &tmp, sizeof(tmp)) == sizeof(tmp)
           && tmp.magic == CFG_MAGIC) {
    cfg = tmp;
    ok  = true;
  }
  if (!ok) {
    // Write the defaults out now rather than on the first Save. Otherwise
    // every boot up to that point logs an NVS "NOT_FOUND" at error level.
    Serial.println(have ? "[nvs] stored settings have an old layout - using defaults"
                        : "[nvs] first boot - writing default settings");
    cfg.magic = CFG_MAGIC;
    prefs.putBytes("cfg", &cfg, sizeof(cfg));
  }
  prefs.end();
  if (settings_clamp_snooze_delays(cfg)) {
    Serial.println("[nvs] older snooze delays adjusted to 5-15 minutes");
    if (!settings_save()) Serial.println("[nvs] corrected snooze delays active in RAM; persistence failed");
  }
}

bool settings_save() {
  cfg.magic = CFG_MAGIC;
  if (!prefs.begin("clock", false)) return false;
  bool ok = prefs.putBytes("cfg", &cfg, sizeof(cfg)) == sizeof(cfg);
  prefs.end();
  Settings readback{};
  if (!prefs.begin("clock", true)) return false;
  ok = ok && prefs.getBytes("cfg", &readback, sizeof(readback)) == sizeof(readback)
          && memcmp(&cfg, &readback, sizeof(cfg)) == 0;
  prefs.end();
  if (!ok) Serial.println("[nvs] save verification FAILED");
  return ok;
}

// =====================================================================
//  Calendar
// =====================================================================
// Divisible by 4, except century years, which must also be divisible by
// 400. So 2000 is a leap year and 2100 is not - Milestone 03 section 5.
bool is_leap(uint16_t y) {
  return (y % 4 == 0) && (y % 100 != 0 || y % 400 == 0);
}

uint8_t days_in_month(uint16_t y, uint8_t m) {
  static const uint8_t len[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
  if (m < 1 || m > 12) return 31;
  if (m == 2 && is_leap(y)) return 29;
  return len[m - 1];
}

uint8_t weekday_of(uint16_t y, uint8_t m, uint8_t d) {
  // Noon avoids any edge case at the day boundary.
  return DateTime(y, m, d, 12, 0, 0).dayOfTheWeek();   // 0 = Sunday
}

const char *weekday_name(uint8_t w) {
  static const char *n[7] = { "Sunday","Monday","Tuesday","Wednesday",
                              "Thursday","Friday","Saturday" };
  return n[w % 7];
}
const char *weekday_short(uint8_t w) {
  static const char *n[7] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
  return n[w % 7];
}

// =====================================================================
//  Daylight saving
// =====================================================================
// Day-of-month of the nth Sunday of a month (n counts from 1).
static uint8_t nth_sunday(uint16_t y, uint8_t m, uint8_t n) {
  uint8_t dow_of_1st = weekday_of(y, m, 1);          // 0 = Sunday
  uint8_t first      = 1 + ((7 - dow_of_1st) % 7);
  return first + 7 * (n - 1);
}

// US rule: DST runs from 02:00 local standard on the 2nd Sunday in March
// to 02:00 local daylight on the 1st Sunday in November. The argument is
// STANDARD time, so the November edge is compared at 01:00 standard.
bool us_dst_active(const DateTime &t) {
  uint8_t m = t.month();
  if (m < 3 || m > 11) return false;
  if (m > 3 && m < 11) return true;

  if (m == 3) {
    uint8_t s = nth_sunday(t.year(), 3, 2);
    if (t.day() != s) return t.day() > s;
    return t.hour() >= 2;
  }
  uint8_t s = nth_sunday(t.year(), 11, 1);
  if (t.day() != s) return t.day() < s;
  return t.hour() < 1;
}

int8_t dst_offset_hours(const DateTime &standard) {
  if (cfg.dst_auto) return us_dst_active(standard) ? 1 : 0;
  return cfg.dst_manual_hr;
}

DateTime to_local(const DateTime &standard) {
  return standard + TimeSpan(dst_offset_hours(standard) * 3600);
}

// =====================================================================
//  Optional self-test for the calendar and DST rules.
//
//  These are the parts of the firmware that are easy to get subtly wrong
//  and impossible to notice until a Sunday in March. Set SELFTEST_RULES
//  to 1 in config.h and read the serial log.
// =====================================================================
#if SELFTEST_RULES
static int st_fail = 0;
static void chk(bool got, bool want, const char *what) {
  if (got != want) { st_fail++; Serial.printf("[test] FAIL %s (got %d)\n", what, got); }
}
static void chku(int got, int want, const char *what) {
  if (got != want) { st_fail++; Serial.printf("[test] FAIL %s: got %d want %d\n", what, got, want); }
}
void settings_selftest() {
  st_fail = 0;
  chk(is_leap(2024), true,  "2024 leap");
  chk(is_leap(2026), false, "2026 leap");
  chk(is_leap(2000), true,  "2000 leap");
  chk(is_leap(2100), false, "2100 leap");
  chku(days_in_month(2024, 2), 29, "Feb 2024");
  chku(days_in_month(2026, 2), 28, "Feb 2026");
  chku(days_in_month(2026, 4), 30, "Apr");
  chku(days_in_month(2026, 12), 31, "Dec");
  chku(weekday_of(2026, 9, 20), 0, "2026-09-20 is a Sunday");
  chku(weekday_of(2026, 3, 8),  0, "2026-03-08 is a Sunday");

  // US changeovers. The argument is STANDARD time throughout.
  struct { uint16_t y; uint8_t mo, d, h; bool want; const char *what; } t[] = {
    { 2026, 1,15,12, false, "2026 mid-winter" },
    { 2026, 3, 7,12, false, "2026 day before DST" },
    { 2026, 3, 8, 1, false, "2026 DST morning 01:59" },
    { 2026, 3, 8, 2, true,  "2026 DST starts 02:00" },
    { 2026, 3, 9,12, true,  "2026 day after DST" },
    { 2026, 7, 4,12, true,  "2026 midsummer" },
    { 2026,11, 1, 0, true,  "2026 DST morning 00:59" },
    { 2026,11, 1, 1, false, "2026 DST ends 01:00" },
    { 2026,11, 2,12, false, "2026 day after DST end" },
    { 2026,12,25,12, false, "2026 Christmas" },
    { 2025, 3, 8,12, false, "2025 day before DST" },
    { 2025, 3, 9, 2, true,  "2025 DST starts" },
    { 2025,11, 2, 1, false, "2025 DST ends" },
    { 2024, 3,10, 2, true,  "2024 DST starts" },
    { 2024,11, 3, 1, false, "2024 DST ends" },
  };
  for (auto &c : t)
    chk(us_dst_active(DateTime(c.y, c.mo, c.d, c.h, 0, 0)), c.want, c.what);

  Serial.printf("[test] calendar/DST: %s\n", st_fail ? "FAILURES ABOVE" : "all passed");
}
#endif
