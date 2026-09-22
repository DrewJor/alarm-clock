#include "config.h"
#include "scheduler.h"

static uint32_t date_key(const DateTime &t) {
  return uint32_t(t.year()) * 10000 + t.month() * 100 + t.day();
}
int8_t clock_offset(const DateTime &t, const Settings &s) {
  return s.dst_auto ? (us_dst_active(t) ? 1 : 0) : s.dst_manual_hr;
}
bool dst_adjustment(const DateTime &t, const Settings &s, int8_t direction, int8_t &offset) {
  if (direction != -1 && direction != 1)
    return false;
  int adjusted = int(clock_offset(t, s)) + direction;
  if (adjusted < INT8_MIN || adjusted > INT8_MAX)
    return false;
  offset = int8_t(adjusted);
  return true;
}
bool local_to_standard(const DateTime &local, const Settings &s, DateTime &out) {
  if (!s.dst_auto) {
    out = local - TimeSpan(s.dst_manual_hr * 3600);
    return out.year() >= YEAR_MIN && out.year() <= YEAR_MAX;
  }
  DateTime daylight = local - TimeSpan(3600);
  bool standard_ok = !us_dst_active(local);
  bool daylight_ok = us_dst_active(daylight);
  // In the gap propose moving forward an hour; in the fold choose the
  // second (standard) occurrence. Both require explicit user confirmation.
  out = standard_ok ? local : (daylight_ok ? daylight : local);
  return standard_ok != daylight_ok;
}

uint8_t AlarmSchedule::poll(const DateTime &standard, const Settings &s) {
  int8_t off = clock_offset(standard, s);
  DateTime local = standard + TimeSpan(off * 3600);
  uint32_t stamp = standard.unixtime(), day = date_key(local);
  uint16_t minute = local.hour() * 60 + local.minute();
  // Catch a skipped hour only across a continuous clock tick, never an
  // arbitrary manual clock jump or reconnection after an outage.
  bool spring = have_previous && stamp >= previous_standard && stamp - previous_standard <= 5 &&
                off > previous_offset;
  DateTime previous_local(previous_standard + previous_offset * 3600);
  uint8_t due = 0;
  for (uint8_t i = 0; i < 3; ++i) {
    const Alarm &a = s.alarm[i];
    if (!a.enabled || fired_date[i] == day)
      continue;
    if (!a.daily && (a.year != local.year() || a.month != local.month() || a.day != local.day()))
      continue;
    uint16_t at = a.hour * 60 + a.minute;
    bool skipped = spring && date_key(previous_local) == day &&
                   at > previous_local.hour() * 60 + previous_local.minute() && at <= minute;
    if (at == minute || skipped) {
      due |= 1 << i;
      fired_date[i] = day;
    }
  }
  previous_standard = stamp;
  previous_offset = off;
  have_previous = true;
  return due;
}

bool AlarmSchedule::next(const DateTime &standard, const Settings &s, DateTime &result,
                         uint8_t &index) const {
  DateTime now = standard + TimeSpan(clock_offset(standard, s) * 3600);
  uint32_t best = UINT32_MAX;
  for (uint8_t i = 0; i < 3; ++i) {
    const Alarm &a = s.alarm[i];
    if (!a.enabled)
      continue;
    for (uint8_t day = 0; day < (a.daily ? 2 : 1); ++day) {
      DateTime candidate(a.daily ? now.year() : a.year, a.daily ? now.month() : a.month,
                         a.daily ? now.day() : a.day, a.hour, a.minute, 0);
      candidate = candidate + TimeSpan(day * 86400);
      if (fired_date[i] == date_key(candidate))
        continue;
      DateTime target;
      bool unique = local_to_standard(candidate, s, target);
      if (!unique && s.dst_auto) {
        if (us_dst_active(candidate)) {
          // Spring gap: wake at the transition, not at 03:mm.
          target = DateTime(candidate.year(), candidate.month(), candidate.day(), 2, 0, 0);
        } else {
          // Fall fold: choose the earliest future occurrence if this alarm
          // has not fired today. Compare actual standard instants, not labels.
          DateTime first = candidate - TimeSpan(3600);
          if (first.unixtime() >= standard.unixtime())
            target = first;
        }
      }
      if (target.unixtime() < standard.unixtime() || target.unixtime() >= best)
        continue;
      best = target.unixtime();
      result = target + TimeSpan(clock_offset(target, s) * 3600);
      index = i;
    }
  }
  return best != UINT32_MAX;
}
