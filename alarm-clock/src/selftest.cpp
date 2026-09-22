#include <Arduino.h>
#include "config.h"
#include "scheduler.h"
#include "save.h"
#include "selftest.h"
#include <string.h>

namespace {
int failures, checks, writes, formats;
DateTime mock_clock;
bool fail_write, fail_format;
void check(bool ok, const char *name) {
  ++checks;
  if (!ok) {
    ++failures;
    Serial.printf("[test] FAIL %s\n", name);
  }
}
bool read(DateTime &v) {
  v = mock_clock;
  return true;
}
bool write(const DateTime &v) {
  ++writes;
  if (fail_write)
    return false;
  mock_clock = v;
  return true;
}
bool format(bool) {
  ++formats;
  return !fail_format;
}
} // namespace
void firmware_selftest() {
  failures = checks = writes = formats = 0;
  Settings s;
  settings_defaults(s);
  {
    Settings older = s;
    older.alarm[0].snooze_min = 1;
    older.alarm[1].snooze_min = 30;
    older.alarm[2].snooze_min = 9;
    check(settings_clamp_snooze_delays(older), "old snooze settings need migration");
    check(older.alarm[0].snooze_min == 5 && older.alarm[1].snooze_min == 15,
          "old snooze settings clamp to 5-15 minutes");
    check(older.alarm[2].snooze_min == 9 && older.alarm[0].hour == s.alarm[0].hour,
          "valid snooze delay and alarm time survive migration");
    check(!settings_clamp_snooze_delays(older), "snooze migration is idempotent");
  }
  {
    // The old absolute -1 choice jumped back two hours from summer DST.
    Settings modes = s;
    DateTime summer(2026, 9, 20, 12, 0, 0), winter(2026, 1, 20, 12, 0, 0);
    int8_t shifted = 0;
    check(dst_adjustment(summer, modes, -1, shifted) && shifted == 0,
          "minus one from automatic summer DST removes exactly one hour");
    check(dst_adjustment(summer, modes, 1, shifted) && shifted == 2,
          "plus one is relative even when DST is already active");
    check(dst_adjustment(winter, modes, -1, shifted) && shifted == -1,
          "minus one from automatic winter time");
    modes.dst_auto = false;
    modes.dst_manual_hr = -1;
    check(dst_adjustment(summer, modes, 1, shifted) && shifted == 0,
          "plus one from existing negative offset");
    check(dst_adjustment(summer, modes, -1, shifted) && shifted == -2,
          "minus one from existing negative offset");
    check(modes.dst_manual_hr == -1, "choosing an adjustment does not mutate settings");
    modes.dst_manual_hr = 0;
    check(clock_offset(summer, modes) == 0 && clock_offset(winter, modes) == 0,
          "DST Off uses standard time in both seasons");
    modes.dst_manual_hr = INT8_MAX;
    check(!dst_adjustment(summer, modes, 1, shifted), "positive hour adjustment cannot overflow");
    modes.dst_manual_hr = INT8_MIN;
    check(!dst_adjustment(summer, modes, -1, shifted), "negative hour adjustment cannot overflow");
    modes.dst_manual_hr = 0;
    AlarmSchedule manual_jump;
    modes.alarm[0].enabled = true;
    modes.alarm[0].hour = 12;
    modes.alarm[0].minute = 30;
    check(manual_jump.poll(summer, modes) == 0, "before manual hour change");
    modes.dst_manual_hr = 1;
    manual_jump.discontinuity();
    check(manual_jump.poll(summer + TimeSpan(1), modes) == 0,
          "manual hour change does not ring skipped alarms");
  }
  s.alarm[0].enabled = true;
  s.alarm[0].hour = 2;
  s.alarm[0].minute = 30;
  AlarmSchedule spring;
  check(spring.poll(DateTime(2026, 3, 8, 1, 59, 59), s) == 0, "spring before gap");
  check(spring.poll(DateTime(2026, 3, 8, 2, 0, 0), s) == 1, "spring skipped alarm fires");
  check(spring.poll(DateTime(2026, 3, 8, 2, 0, 1), s) == 0, "spring once only");
  AlarmSchedule fall;
  s.alarm[0].hour = 1;
  check(fall.poll(DateTime(2026, 11, 1, 0, 30, 0), s) == 1, "fall first occurrence");
  check(fall.poll(DateTime(2026, 11, 1, 1, 30, 0), s) == 0, "fall repeat suppressed");
  check(fall.poll(DateTime(2026, 11, 2, 1, 30, 0), s) == 1, "next day same minute fires");
  AlarmSchedule together;
  s.alarm[1] = s.alarm[0];
  s.alarm[2] = s.alarm[0];
  check(together.poll(DateTime(2026, 11, 2, 1, 30, 0), s) == 7, "simultaneous alarms all queued");
  s.alarm[0].daily = false;
  s.alarm[0].year = 2026;
  s.alarm[0].month = 11;
  s.alarm[0].day = 3;
  s.alarm[1].enabled = s.alarm[2].enabled = false;
  AlarmSchedule dated;
  check(dated.poll(DateTime(2026, 11, 2, 1, 30, 0), s) == 0, "dated alarm wrong date");
  check(dated.poll(DateTime(2026, 11, 3, 1, 30, 0), s) == 1, "dated alarm correct date");
  DateTime mapped;
  check(local_to_standard(DateTime(2026, 3, 8, 3, 30, 0), s, mapped) && mapped.hour() == 2,
        "spring 03:30 conversion");
  check(!local_to_standard(DateTime(2026, 3, 8, 2, 30, 0), s, mapped) && mapped.hour() == 2,
        "gap requires review");
  check(!local_to_standard(DateTime(2026, 11, 1, 1, 30, 0), s, mapped) && mapped.hour() == 1,
        "fold requires review");
  check(local_to_standard(DateTime(2026, 11, 1, 0, 30, 0), s, mapped) && mapped.day() == 31 &&
            mapped.hour() == 23,
        "fold previous day conversion");
  s.dst_auto = false;
  s.dst_manual_hr = -1;
  check(local_to_standard(DateTime(2026, 9, 20, 12, 0, 0), s, mapped) && mapped.hour() == 13,
        "negative manual offset");
  s.dst_auto = true;
  s.alarm[0].daily = true;
  s.alarm[0].hour = 7;
  s.alarm[0].minute = 0;
  AlarmSchedule upcoming;
  uint8_t index;
  check(upcoming.next(DateTime(2026, 9, 20, 5, 0, 0), s, mapped, index) && mapped.hour() == 7 &&
            mapped.day() == 20,
        "next alarm today");
  check(upcoming.next(DateTime(2026, 9, 20, 7, 0, 0), s, mapped, index) && mapped.day() == 21,
        "next alarm tomorrow");
  s.alarm[0].hour = 2;
  s.alarm[0].minute = 30;
  check(upcoming.next(DateTime(2026, 3, 8, 1, 0, 0), s, mapped, index) && mapped.hour() == 3 &&
            mapped.minute() == 0,
        "next alarm spring gap at transition");
  check(upcoming.next(DateTime(2026, 3, 7, 12, 0, 0), s, mapped, index) && mapped.day() == 8 &&
            mapped.hour() == 3,
        "tomorrow spring gap normalized");
  s.alarm[0].hour = 1;
  s.alarm[0].minute = 30;
  check(upcoming.next(DateTime(2026, 11, 1, 0, 45, 0), s, mapped, index) && mapped.day() == 1 &&
            mapped.hour() == 1,
        "fall future second occurrence after missed first");
  s.alarm[0].enabled = false;
  check(!upcoming.next(DateTime(2026, 9, 20, 5, 0, 0), s, mapped, index), "no upcoming alarms");

  SaveBackend backend{read, write, format};
  SaveOwner owner;
  mock_clock = DateTime(2026, 9, 20, 10, 15, 30);
  fail_write = fail_format = false;
  SaveRequest r;
  r.id = 1;
  r.groups = SAVE_FORMAT;
  r.fmt24h = true;
  check(owner.submit(r) == SaveStatus::Accepted && writes == 0 && formats == 0,
        "submit asynchronous");
  check(owner.submit(r) == SaveStatus::Accepted, "duplicate accepted request deduplicated");
  SaveRequest other = r;
  other.id = 2;
  check(owner.submit(other) == SaveStatus::Busy, "second request busy");
  owner.service(s, backend);
  check(owner.query(1).status == SaveStatus::Success && writes == 0 && formats == 1,
        "format only never rewrites clock");
  owner.submit(r);
  owner.service(s, backend);
  check(formats == 1, "duplicate completed ID never writes");
  check(owner.query(99).status == SaveStatus::Unknown, "unknown query has no write");
  r.id = 2;
  r.groups = SAVE_DATE;
  r.local = DateTime(2026, 9, 21, 1, 2, 3);
  owner.submit(r);
  owner.service(s, backend);
  owner.service(s, backend);
  check(mock_clock.day() == 21 && mock_clock.hour() == 10 && mock_clock.minute() == 15 &&
            mock_clock.second() == 30,
        "date only preserves running time");
  check(owner.query(2).committed == SAVE_DATE, "changed group flags recorded");
  r.id = 3;
  r.groups = SAVE_TIME;
  r.local = DateTime(2000, 1, 1, 13, 14, 15);
  owner.submit(r);
  owner.service(s, backend);
  owner.service(s, backend);
  check(mock_clock.year() == 2026 && mock_clock.day() == 21 && mock_clock.hour() == 12,
        "time only preserves current date");
  r.id = 4;
  r.groups = SAVE_TIME | SAVE_DATE | SAVE_FORMAT;
  r.local = DateTime(2026, 9, 22, 9, 0, 0);
  fail_format = true;
  owner.submit(r);
  owner.service(s, backend);
  check(owner.query(4).status == SaveStatus::Accepted, "clock then format separate phases");
  owner.service(s, backend);
  check(owner.query(4).status == SaveStatus::Error &&
            owner.query(4).committed == (SAVE_TIME | SAVE_DATE),
        "partial success retains clock commit");
  int before = writes;
  fail_format = false;
  r.id = 5;
  r.groups = SAVE_FORMAT;
  owner.submit(r);
  owner.service(s, backend);
  check(writes == before && owner.query(5).status == SaveStatus::Success,
        "partial retry only remaining format");
  r.id = 6;
  r.groups = SAVE_TIME | SAVE_DATE;
  r.local = DateTime(2026, 3, 8, 2, 30, 0);
  owner.submit(r);
  owner.service(s, backend);
  check(owner.query(6).status == SaveStatus::Review && writes == before,
        "gap review performs no write");
  check(owner.query(6).proposed.hour() == 3, "gap proposal displayed at 03:30");
  r.id = 7;
  r.confirmed = true;
  owner.submit(r);
  owner.service(s, backend);
  owner.service(s, backend);
  check(owner.query(7).status == SaveStatus::Success && writes == before + 1,
        "confirmed review new ID writes once");
  r.id = 1;
  check(owner.submit(r) == SaveStatus::Review, "stale IDs cannot execute again");
  r.id = 8;
  r.confirmed = false;
  r.local = DateTime(2026, 9, 20, 12, 0, 0);
  fail_write = true;
  owner.submit(r);
  owner.service(s, backend);
  check(owner.query(8).status == SaveStatus::Error && owner.query(8).committed == 0,
        "failed clock leaves all groups pending");
  uint32_t deadline = 0xfffffff0UL + 60000UL;
  check(int32_t(uint32_t(0xfffffff0UL + 59999UL) - deadline) < 0, "snooze rollover not early");
  check(int32_t(uint32_t(0xfffffff0UL + 60000UL) - deadline) >= 0, "snooze rollover due");
  Serial.printf("[test] scheduler/save: %d checks, %d failures (mock backend; no RTC/NVS writes)\n",
                checks, failures);
}

void saved_alarm_selftest(const Settings &saved) {
  failures = checks = 0;
  // Use the loaded alarm records unchanged, isolating each slot so simultaneous
  // times cannot hide a mismatch. All dates and setting changes stay in RAM.
  for (uint8_t slot = 0; slot < 3; ++slot) {
    for (bool fmt24 : {false, true}) {
      for (uint8_t mode = 0; mode < 4; ++mode) {
        for (uint8_t month : {uint8_t(1), uint8_t(7)}) {
          Settings s = saved;
          for (uint8_t i = 0; i < 3; ++i)
            if (i != slot)
              s.alarm[i].enabled = false;
          s.fmt24h = fmt24;
          s.dst_auto = mode == 0;
          s.dst_manual_hr = mode == 2 ? 1 : mode == 3 ? -1 : 0;
          const Alarm original = s.alarm[slot];
          DateTime local(original.daily ? 2026 : original.year,
                         original.daily ? month : original.month,
                         original.daily ? 20 : original.day, original.hour, original.minute, 0);
          DateTime standard;
          bool unique = local_to_standard(local, s, standard);
          // A saved dated alarm can itself be in a spring gap or fall fold;
          // dedicated transition cases below cover those ambiguous labels.
          if (!unique && s.dst_auto)
            continue;
          check(unique, "saved alarm local time maps under each DST mode");
          AlarmSchedule schedule;
          DateTime next;
          uint8_t index = 255;
          bool found = schedule.next(standard - TimeSpan(60), s, next, index);
          check(original.enabled ? found && index == slot && next.unixtime() == local.unixtime()
                                 : !found,
                "saved alarm next time matches local clock in both formats");
          check(schedule.poll(standard - TimeSpan(60), s) == 0,
                "saved alarm does not fire a minute early");
          s.fmt24h = !fmt24; // change display after this alarm was already scheduled
          check(schedule.poll(standard, s) == (original.enabled ? (1 << slot) : 0),
                "format switch preserves saved alarm firing time and enabled state");
          check(schedule.poll(standard + TimeSpan(1), s) == 0,
                "format switch does not duplicate saved alarm");
          check(memcmp(&original, &s.alarm[slot], sizeof(Alarm)) == 0,
                "DST and format leave saved alarm record unchanged");
        }
      }
    }
  }
  // Transition policy must hold for both daily and dated alarms in either
  // display format, including a format toggle inside the repeated hour.
  for (bool fmt24 : {false, true}) {
    {
      Settings s;
      settings_defaults(s);
      s.fmt24h = fmt24;
      s.dst_auto = false;
      s.dst_manual_hr = 0;
      s.alarm[0].enabled = true;
      AlarmSchedule schedule;
      DateTime before(2026, 7, 20, 5, 59, 59), after(2026, 7, 20, 6, 0, 0), next;
      uint8_t index = 255;
      int8_t adjusted = 0;
      check(schedule.poll(before, s) == 0, "saved 07:00 alarm before manual DST change");
      check(dst_adjustment(before, s, 1, adjusted) && adjusted == 1,
            "apply +1 hour to existing alarm schedule");
      s.dst_manual_hr = adjusted;
      schedule.discontinuity();
      s.fmt24h = !fmt24;
      check(schedule.next(before, s, next, index) && next.hour() == 7 && next.minute() == 0,
            "next saved alarm remains 07:00 after DST and format changes");
      check(schedule.poll(after, s) == 1, "saved 07:00 alarm rings on adjusted local clock");
      check(dst_adjustment(after, s, -1, adjusted) && adjusted == 0,
            "apply -1 hour after existing alarm fired");
      s.dst_manual_hr = adjusted;
      schedule.discontinuity();
      s.fmt24h = fmt24;
      check(schedule.poll(DateTime(2026, 7, 20, 7, 0, 0), s) == 0,
            "manual fall back and format switch do not repeat already fired alarm");
      check(schedule.poll(DateTime(2026, 7, 21, 7, 0, 0), s) == 1,
            "existing daily alarm still rings next day after DST changes");
    }
    for (bool daily : {false, true}) {
      Settings s;
      settings_defaults(s);
      s.fmt24h = fmt24;
      Alarm &a = s.alarm[0];
      a.enabled = true;
      a.daily = daily;
      a.year = 2026;
      a.month = 3;
      a.day = 8;
      a.hour = 2;
      a.minute = 30;
      AlarmSchedule spring;
      check(spring.poll(DateTime(2026, 3, 8, 1, 59, 59), s) == 0,
            "both formats before spring transition");
      s.fmt24h = !fmt24;
      check(spring.poll(DateTime(2026, 3, 8, 2, 0, 0), s) == 1,
            "both formats catch daily/dated skipped alarm at 03:00");
      check(spring.poll(DateTime(2026, 3, 8, 2, 30, 0), s) == 0,
            "spring alarm does not repeat at 03:30");
      a.month = 11;
      a.day = 1;
      a.hour = 1;
      AlarmSchedule fall;
      check(fall.poll(DateTime(2026, 11, 1, 0, 30, 0), s) == 1,
            "daily/dated alarm fires in first fall hour");
      s.fmt24h = fmt24;
      check(fall.poll(DateTime(2026, 11, 1, 1, 30, 0), s) == 0,
            "format toggle in fall hour cannot ring twice");
    }
  }
  Serial.printf("[test] saved alarms/DST/format: %d checks, %d failures (loaded alarm copies; no "
                "RTC/NVS writes)\n",
                checks, failures);
}
