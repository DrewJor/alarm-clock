#pragma once
#include "settings.h"

// One occurrence per alarm and local calendar date. State survives clock edits
// and DST changes in this boot; snoozes use a separate monotonic timer.
class AlarmSchedule {
public:
  uint8_t poll(const DateTime &standard, const Settings &settings);
  bool next(const DateTime &standard, const Settings &settings, DateTime &local,
            uint8_t &index) const;
  void discontinuity() {
    have_previous = false;
  }

private:
  uint32_t fired_date[3] = {};
  uint32_t previous_standard = 0;
  int8_t previous_offset = 0;
  bool have_previous = false;
};

int8_t clock_offset(const DateTime &standard, const Settings &settings);

// Manual DST choices shift the currently displayed time, including any
// automatic DST already in effect. Does not write the clock or settings.
bool dst_adjustment(const DateTime &standard, const Settings &settings, int8_t direction,
                    int8_t &offset);

// Returns false for an ambiguous/nonexistent local time; proposed contains
// the standard-time choice to display for explicit review.
bool local_to_standard(const DateTime &local, const Settings &settings, DateTime &proposed);
