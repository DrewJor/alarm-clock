// EE 4953 Alarm Clock - screen state machine
//
// Editors keep drafts separate from settings until an explicit Save.
//
// Two conventions hold everywhere:
//    * encoder rotate  = move the highlight, or change the active value
//    * encoder press   = Select / confirm,  Back button = Back / cancel
#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "settings.h"
#include "input.h"
#include "hw.h"
#include "ui.h"
#include "save.h"
#include "scheduler.h"

// Screens
enum Screen : uint8_t {
  SCR_HOME,
  SCR_MENU,
  SCR_TD_MENU, // set time/date submenu
  SCR_TD_TIME, // time field list
  SCR_TD_DATE, // date field list
  SCR_TD_SAVING,
  SCR_TD_REVIEW,
  SCR_TD_CONFIRM, // save / discard / continue
  SCR_AL_LIST,    // alarm 1 / 2 / 3
  SCR_AL_MENU,    // individual alarm menu
  SCR_AL_DATEOPT, // daily or specific date
  SCR_AL_DATE,
  SCR_AL_TIME,
  SCR_AL_SOUND,
  SCR_AL_SOUND_OPT, // set / test
  SCR_AL_LIGHT,
  SCR_AL_LIGHT_OPT,
  SCR_AL_SNOOZE,
  SCR_AL_CONFIRM, // save / discard / continue
  SCR_BRIGHT,     // brightness editor
  SCR_BRIGHT_CONFIRM,
  SCR_DST,
  SCR_DST_CONFIRM,
  SCR_MSG // transient notice
};

static Screen scr = SCR_HOME;
static uint8_t cur = 3;      // Home starts with Menu selected
static bool editing = false; // encoder changes a value, not the highlight
static bool overlay = false; // a ringing alarm owns the screen

// Navigation stack: returning from a child screen restores the selected row.
struct NavEntry {
  uint8_t scr, cur;
};
static NavEntry nav[8];
static uint8_t nav_sp = 0;

static void go(Screen s) {
  if (nav_sp < 8)
    nav[nav_sp++] = {(uint8_t)scr, cur};
  scr = s;
  cur = 0;
  editing = false;
}
static void go_replace(Screen s) {
  scr = s;
  cur = 0;
  editing = false;
}
static void go_back() {
  editing = false;
  if (nav_sp) {
    NavEntry e = nav[--nav_sp];
    scr = (Screen)e.scr;
    cur = e.cur;
  } else {
    scr = SCR_HOME;
    cur = 3;
  }
}
static void go_home() {
  nav_sp = 0;
  scr = SCR_HOME;
  cur = 3;
  editing = false;
}
static void pop_to(Screen s) {
  editing = false;
  while (nav_sp && scr != s) {
    NavEntry e = nav[--nav_sp];
    scr = (Screen)e.scr;
    cur = e.cur;
  }
  if (scr != s) {
    scr = s;
    cur = 0;
  }
}
// Transient message
static char msg_l1[24], msg_l2[24];
static bool msg_active = false;
static uint32_t msg_until = 0;
static Screen msg_next = SCR_HOME;
static uint8_t msg_next_cur = 0;

static void msg_show(const char *l1, const char *l2, Screen next, uint8_t next_cur) {
  snprintf(msg_l1, sizeof(msg_l1), "%s", l1 ? l1 : "");
  snprintf(msg_l2, sizeof(msg_l2), "%s", l2 ? l2 : "");
  msg_next = next;
  msg_next_cur = next_cur;
  msg_until = millis() + MSG_MS;
  msg_active = true;
  scr = SCR_MSG;
  editing = false;
}
// Stay where we are once the notice clears.
static void msg_here(const char *l1, const char *l2 = nullptr) {
  msg_show(l1, l2, scr, cur);
}
// Drafts
// Clock time/date drafts use local values, matching what
// the user reads off the screen; the DST offset is removed on save.
struct TD {
  uint16_t year;
  uint8_t month, day, hour, minute, second;
  bool fmt24h;
};
static TD td, td_orig, td_snap;
static bool td_reviewed_time = false; // first setup must review both time and date
static bool td_reviewed_date = false;
static bool td_day_adjusted = false; // show "Day adjusted"

// Alarm draft
static uint8_t sel_alarm = 0, sel_sound = 1, sel_light = 1;
static Alarm al, al_orig, al_snap;
static Screen al_parent = SCR_AL_LIST;
static uint8_t al_parent_cur = 0;

// Brightness and DST originals, for Discard
static bool br_auto_orig;
static uint8_t br_level_orig;
static bool dst_auto_orig;
static int8_t dst_hr_orig;
static bool dst_auto_draft;
static int8_t dst_hr_draft;
static int8_t dst_direction = 0; // pending one-hour adjustment, applied once on Save
static bool br_auto_draft;
static uint8_t br_level_draft;
static SaveRequest td_request;
static SaveResult td_result;
static uint32_t td_started, td_queried;
static bool td_accepted = false;
static uint32_t snooze_notice_until = 0;
static bool snooze_countdown = false;

static bool next_snooze(uint8_t &index, uint32_t &seconds) {
  bool found = false;
  for (uint8_t i = 0; i < 3; ++i) {
    uint32_t left;
    if (hw_snooze_remaining(i, left) && (!found || left < seconds)) {
      index = i;
      seconds = left;
      found = true;
    }
  }
  return found;
}
// Small numeric helpers
static int wrapi(int v, int lo, int hi) {
  int n = hi - lo + 1;
  return lo + ((v - lo) % n + n) % n;
}
static int clampi(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static bool td_equal(const TD &a, const TD &b) {
  return a.year == b.year && a.month == b.month && a.day == b.day && a.hour == b.hour &&
         a.minute == b.minute && a.second == b.second && a.fmt24h == b.fmt24h;
}
static bool alarm_equal(const Alarm &a, const Alarm &b) {
  return a.enabled == b.enabled && a.hour == b.hour && a.minute == b.minute && a.daily == b.daily &&
         a.year == b.year && a.month == b.month && a.day == b.day && a.sound == b.sound &&
         a.light == b.light && a.snooze_min == b.snooze_min && a.snooze_max == b.snooze_max &&
         a.length_min == b.length_min;
}

// Convert a 24-hour value to the displayed 12-hour value.
static uint8_t hour12(uint8_t h24) {
  uint8_t h = h24 % 12;
  return h ? h : 12;
}
static bool is_pm(uint8_t h24) {
  return h24 >= 12;
}
// Put a 12-hour digit and an AM/PM flag back together.
static uint8_t hour24(uint8_t h12, bool pm) {
  return (uint8_t)(h12 % 12) + (pm ? 12 : 0);
}
// Shared field editing
//
// Entering value editing snapshots the WHOLE draft, so Back restores
// every dependent value (a clamped day, a normalised hour) and not just
// the one field that was on screen.
static uint8_t td_time_fields() {
  return td.fmt24h ? 3 : 4;
} // H M S [AM/PM]
static uint8_t al_time_fields() {
  return cfg.fmt24h ? 2 : 3;
} // H M   [AM/PM]

// After a year or month change the day may no longer exist. Reduce it to
// the last valid day and show "Day adjusted".
static void clamp_day(uint16_t y, uint8_t m, uint8_t &d, bool &flag) {
  uint8_t max = days_in_month(y, m);
  if (d > max) {
    d = max;
    flag = true;
  }
}

static void td_step(int dir) {
  switch (cur) {
  case 0: // hour
    if (td.fmt24h)
      td.hour = wrapi(td.hour + dir, 0, 23);
    else
      td.hour = hour24(wrapi(hour12(td.hour) + dir, 1, 12), is_pm(td.hour));
    break;
  case 1:
    td.minute = wrapi(td.minute + dir, 0, 59);
    break;
  case 2:
    td.second = wrapi(td.second + dir, 0, 59);
    break;
  case 3:
    td.hour = hour24(hour12(td.hour), !is_pm(td.hour));
    break; // AM/PM toggle
  }
}
static void td_date_step(int dir) {
  switch (cur) {
  case 0: // year: clamp, never wrap
    td.year = clampi(td.year + dir, YEAR_MIN, YEAR_MAX);
    clamp_day(td.year, td.month, td.day, td_day_adjusted);
    break;
  case 1:
    td.month = wrapi(td.month + dir, 1, 12);
    clamp_day(td.year, td.month, td.day, td_day_adjusted);
    break;
  case 2:
    td.day = wrapi(td.day + dir, 1, days_in_month(td.year, td.month));
    break;
  }
}
static void al_time_step(int dir) {
  switch (cur) {
  case 0:
    if (cfg.fmt24h)
      al.hour = wrapi(al.hour + dir, 0, 23);
    else
      al.hour = hour24(wrapi(hour12(al.hour) + dir, 1, 12), is_pm(al.hour));
    break;
  case 1:
    al.minute = wrapi(al.minute + dir, 0, 59);
    break;
  case 2:
    al.hour = hour24(hour12(al.hour), !is_pm(al.hour));
    break;
  }
}
static void al_date_step(int dir) {
  static bool ignore = false;
  switch (cur) {
  case 0:
    al.year = clampi(al.year + dir, YEAR_MIN, YEAR_MAX);
    clamp_day(al.year, al.month, al.day, ignore);
    break;
  case 1:
    al.month = wrapi(al.month + dir, 1, 12);
    clamp_day(al.year, al.month, al.day, ignore);
    break;
  case 2:
    al.day = wrapi(al.day + dir, 1, days_in_month(al.year, al.month));
    break;
  }
}
// Clamp at the limits: snooze delay is 5-15 minutes in one-minute steps.
static void al_snooze_step(int dir) {
  switch (cur) {
  case 0:
    al.snooze_min = clampi(al.snooze_min + dir, SNOOZE_DELAY_MIN, SNOOZE_DELAY_MAX);
    break;
  case 1:
    al.snooze_max = clampi(al.snooze_max + dir, SNOOZE_COUNT_MIN, SNOOZE_COUNT_MAX);
    break;
  case 2:
    al.length_min = clampi(al.length_min + dir, ALARM_LEN_MIN, ALARM_LEN_MAX);
    break;
  }
}
// Set time/date
static void td_enter() {
  bool valid = hw_clock_valid();
  if (valid) {
    DateTime l = to_local(hw_rtc_read());
    td = {l.year(), l.month(), l.day(), l.hour(), l.minute(), l.second(), cfg.fmt24h};
  } else {
    // Start an unset clock's draft at 2026-01-01. This is only a proposal;
    // the user has to look at both editors before saving the current time.
    td = {2026, 1, 1, 0, 0, 0, cfg.fmt24h};
  }
  td_orig = td;
  td_reviewed_time = td_reviewed_date = valid;
  td_day_adjusted = false;
  go(SCR_TD_MENU);
}

// Pending first setup counts as unsaved time and date.
static bool td_dirty() {
  if (!hw_clock_valid())
    return true;
  return !td_equal(td, td_orig);
}

static void td_submit(bool confirmed = false) {
  td_request.id = save_new_id();
  td_request.confirmed = confirmed;
  td_started = td_queried = millis();
  td_accepted = clock_save.submit(td_request) != SaveStatus::Busy;
  go_replace(SCR_TD_SAVING);
}

static void td_save() {
  if (scr == SCR_TD_CONFIRM)
    go_back();
  if (td.year < YEAR_MIN || td.year > YEAR_MAX || td.month < 1 || td.month > 12 || td.day < 1 ||
      td.day > days_in_month(td.year, td.month) || td.hour > 23 || td.minute > 59 ||
      td.second > 59) {
    msg_show("Invalid date or time", "Nothing was saved", SCR_TD_MENU, cur);
    return;
  }
  if (!td_reviewed_time || !td_reviewed_date) {
    msg_show("First setup: open", "Time and Date first", SCR_TD_MENU, cur);
    return;
  }
  td_request = SaveRequest{};
  if (!hw_clock_valid() || td.hour != td_orig.hour || td.minute != td_orig.minute ||
      td.second != td_orig.second)
    td_request.groups |= SAVE_TIME;
  if (!hw_clock_valid() || td.year != td_orig.year || td.month != td_orig.month ||
      td.day != td_orig.day)
    td_request.groups |= SAVE_DATE;
  if (td.fmt24h != td_orig.fmt24h)
    td_request.groups |= SAVE_FORMAT;
  td_request.local = DateTime(td.year, td.month, td.day, td.hour, td.minute, td.second);
  td_request.fmt24h = td.fmt24h;
  td_submit();
}

static void td_discard() {
  if (scr == SCR_TD_CONFIRM)
    go_back();
  td = td_orig;
  pop_to(SCR_MENU);
  msg_show("Changes discarded", nullptr, SCR_MENU, cur);
}

// Leaving the time/date session raises the unsaved-changes dialog.
// Back from either field list retains the draft within the session.
static void td_request_leave() {
  if (!td_dirty()) {
    go_back();
    return;
  }
  go(SCR_TD_CONFIRM);
  cur = 2; // "Continue editing" starts highlighted
}
// Alarms
static void al_enter(uint8_t i) {
  sel_alarm = i;
  al_parent = scr;
  al_parent_cur = cur;
  al = al_orig = cfg.alarm[i];
  go(SCR_AL_MENU);
}
static void al_save() {
  if (scr == SCR_AL_CONFIRM)
    go_back();
  if (al.hour > 23 || al.minute > 59) {
    msg_here("Invalid alarm time");
    return;
  }
  if (!al.daily && (al.year < YEAR_MIN || al.year > YEAR_MAX || al.month < 1 || al.month > 12 ||
                    al.day < 1 || al.day > days_in_month(al.year, al.month))) {
    msg_here("Invalid alarm date");
    return;
  }
  Alarm old = cfg.alarm[sel_alarm];
  cfg.alarm[sel_alarm] = al;
  if (!settings_save()) {
    cfg.alarm[sel_alarm] = old;
    msg_here("Alarm not saved", "Storage error");
    return;
  }
  hw_alarm_settings_changed(sel_alarm);
  al_orig = al;
  char l2[24];
  snprintf(l2, sizeof(l2), "Alarm %u", sel_alarm + 1);
  // Saving ends the editing session, regardless of its entry point.
  // Clear the stack so Back cannot reopen the saved editor.
  go_home();
  msg_show("Saved", l2, SCR_HOME, cur);
}
static void al_discard() {
  if (scr == SCR_AL_CONFIRM)
    go_back();
  al = al_orig;
  pop_to(al_parent);
  msg_show("Changes discarded", nullptr, al_parent, al_parent_cur);
}
static void al_request_leave() {
  if (alarm_equal(al, al_orig)) {
    go_back();
    return;
  }
  go(SCR_AL_CONFIRM);
  cur = 2;
}
// Brightness and DST
static void bright_enter() {
  br_auto_draft = br_auto_orig = cfg.auto_bright;
  br_level_draft = br_level_orig = cfg.manual_level;
  go(SCR_BRIGHT);
}
static void bright_discard() {
  if (scr == SCR_BRIGHT_CONFIRM)
    go_back();
  cfg.auto_bright = br_auto_orig;   // the level was applied live, so
  cfg.manual_level = br_level_orig; // undoing it means re-applying
  if (!cfg.auto_bright)
    hw_brightness_apply(cfg.manual_level);
  go_back();
}
static void bright_save() {
  if (scr == SCR_BRIGHT_CONFIRM)
    go_back();
  cfg.auto_bright = br_auto_draft;
  cfg.manual_level = br_level_draft;
  if (!settings_save()) {
    cfg.auto_bright = br_auto_orig;
    cfg.manual_level = br_level_orig;
    msg_here("Brightness not saved", "Storage error");
    return;
  }
  go_back();
  msg_here("Brightness saved");
}
static void bright_request_leave() {
  if (br_auto_draft == br_auto_orig && br_level_draft == br_level_orig) {
    go_back();
    return;
  }
  go(SCR_BRIGHT_CONFIRM);
  cur = 2; // Continue editing; never discard a live preview implicitly.
}
static void dst_enter() {
  dst_auto_draft = dst_auto_orig = cfg.dst_auto;
  dst_hr_draft = dst_hr_orig = cfg.dst_manual_hr;
  dst_direction = 0;
  go(SCR_DST);
}
static void dst_discard() {
  if (scr == SCR_DST_CONFIRM)
    go_back();
  cfg.dst_auto = dst_auto_orig;
  cfg.dst_manual_hr = dst_hr_orig;
  go_back();
}
static void dst_request_leave() {
  bool dirty = dst_auto_draft != dst_auto_orig ||
               (!dst_auto_draft && (dst_direction || dst_hr_draft != dst_hr_orig));
  if (!dirty) {
    go_back();
    return;
  }
  go(SCR_DST_CONFIRM);
  cur = 2;
}
static void dst_save() {
  if (scr == SCR_DST_CONFIRM)
    go_back();
  if (dst_direction) {
    if (!hw_clock_valid()) {
      msg_here("Set clock first");
      return;
    }
    // Retry from saved settings, never from a previously attempted draft.
    if (!dst_adjustment(hw_rtc_read(), cfg, dst_direction, dst_hr_draft)) {
      msg_here("Hour limit reached");
      return;
    }
  }
  cfg.dst_auto = dst_auto_draft;
  cfg.dst_manual_hr = dst_hr_draft;
  if (!settings_save()) {
    cfg.dst_auto = dst_auto_orig;
    cfg.dst_manual_hr = dst_hr_orig;
    msg_here("DST not saved", "Storage error");
    return;
  }
  hw_dst_settings_changed();
  go_back();
  msg_here("DST saved");
}
// Event handling
static void move(int dir, uint8_t n) { // highlight wraps
  if (n)
    cur = (uint8_t)wrapi(cur + dir, 0, n - 1);
}

static void td_snap_save() {
  td_snap = td;
}
static void td_snap_restore() {
  td = td_snap;
  td_day_adjusted = false;
}
static void al_snap_save() {
  al_snap = al;
}
static void al_snap_restore() {
  al = al_snap;
}

// Return true when an active value edit consumes the event.
static bool edit_common(InputEvent e, void (*step)(int), void (*restore)()) {
  if (!editing)
    return false;
  if (e == EV_ENC_CW)
    step(+1);
  else if (e == EV_ENC_CCW)
    step(-1);
  else if (e == EV_ENC_PRESS)
    editing = false; // keep the draft value
  else if (e == EV_BACK_PRESS) {
    restore();
    editing = false;
  }
  return true;
}

void ui_event(InputEvent e) {
  // A ringing alarm owns the buttons; main.cpp deals with Snooze and Back.
  if (overlay)
    return;
  if (snooze_countdown) {
    uint8_t index;
    uint32_t seconds;
    if (next_snooze(index, seconds)) {
      if (e == EV_BACK_PRESS || e == EV_BACK_HOLD || e == EV_ENC_PRESS || e == EV_ENC_CW ||
          e == EV_ENC_CCW || e == EV_ENC_HOLD)
        snooze_countdown = false;
      return;
    }
    snooze_countdown = false;
  }

  // A held Back also dismisses notices, then follows normal draft rules.
  if (e == EV_BACK_HOLD)
    e = EV_BACK_PRESS;

  // Any press clears a notice early.
  if (scr == SCR_MSG) {
    if (e == EV_ENC_PRESS || e == EV_BACK_PRESS) {
      scr = msg_next;
      cur = msg_next_cur;
      msg_active = false;
    }
    return;
  }

  // Saving continues behind alarm overlays and cannot be cancelled.
  if (scr == SCR_TD_SAVING)
    return;

  switch (scr) {

  case SCR_HOME:
    if (e == EV_ENC_CW)
      move(+1, 4);
    else if (e == EV_ENC_CCW)
      move(-1, 4);
    else if (e == EV_ENC_PRESS) {
      if (cur < 3)
        al_enter(cur);
      else
        go(SCR_MENU);
    }
    break;
  case SCR_MENU:
    if (e == EV_ENC_CW)
      move(+1, 4);
    else if (e == EV_ENC_CCW)
      move(-1, 4);
    else if (e == EV_BACK_PRESS)
      go_back();
    else if (e == EV_ENC_PRESS) {
      switch (cur) {
      case 0:
        td_enter();
        break;
      case 1:
        go(SCR_AL_LIST);
        break;
      case 2:
        bright_enter();
        break;
      case 3:
        dst_enter();
        break;
      }
    }
    break;
  case SCR_TD_MENU:
    if (e == EV_ENC_CW)
      move(+1, 5);
    else if (e == EV_ENC_CCW)
      move(-1, 5);
    else if (e == EV_BACK_PRESS)
      td_request_leave();
    else if (e == EV_ENC_PRESS) {
      switch (cur) {
      case 0:
        td.fmt24h = !td.fmt24h;
        break;
      case 1:
        td_reviewed_time = true;
        go(SCR_TD_TIME);
        break;
      case 2:
        td_reviewed_date = true;
        go(SCR_TD_DATE);
        break;
      case 3:
        td_save();
        break;
      case 4:
        td_discard();
        break;
      }
    }
    break;
  case SCR_TD_TIME:
    if (edit_common(e, td_step, td_snap_restore))
      break;
    if (e == EV_ENC_CW)
      move(+1, td_time_fields());
    else if (e == EV_ENC_CCW)
      move(-1, td_time_fields());
    else if (e == EV_ENC_PRESS) {
      td_snap_save();
      editing = true;
    } else if (e == EV_BACK_PRESS)
      go_back();
    break;
  case SCR_TD_DATE:
    if (edit_common(e, td_date_step, td_snap_restore))
      break;
    if (e == EV_ENC_CW) {
      move(+1, 3);
      td_day_adjusted = false;
    } else if (e == EV_ENC_CCW) {
      move(-1, 3);
      td_day_adjusted = false;
    } else if (e == EV_ENC_PRESS) {
      td_snap_save();
      editing = true;
    } else if (e == EV_BACK_PRESS)
      go_back();
    break;
  case SCR_TD_CONFIRM:
    if (e == EV_ENC_CW)
      move(+1, 3);
    else if (e == EV_ENC_CCW)
      move(-1, 3);
    else if (e == EV_BACK_PRESS)
      go_back();
    else if (e == EV_ENC_PRESS) {
      if (cur == 0)
        td_save();
      else if (cur == 1)
        td_discard();
      else
        go_back();
    }
    break;
  case SCR_AL_LIST:
    if (e == EV_ENC_CW)
      move(+1, 4);
    else if (e == EV_ENC_CCW)
      move(-1, 4);
    else if (e == EV_BACK_PRESS)
      go_back();
    else if (e == EV_ENC_PRESS) {
      if (cur < 3)
        al_enter(cur);
      else
        go_home();
    }
    break;
  case SCR_AL_MENU:
    if (e == EV_ENC_CW)
      move(+1, 7);
    else if (e == EV_ENC_CCW)
      move(-1, 7);
    else if (e == EV_BACK_PRESS)
      al_request_leave();
    else if (e == EV_ENC_PRESS) {
      switch (cur) {
      case 0:
        al.enabled = !al.enabled;
        break;
      case 1:
        go(SCR_AL_DATEOPT);
        break;
      case 2:
        go(SCR_AL_SOUND);
        break;
      case 3:
        go(SCR_AL_LIGHT);
        break;
      case 4:
        go(SCR_AL_SNOOZE);
        break;
      case 5:
        al_save();
        break;
      case 6:
        al_discard();
        break;
      }
    }
    break;
  case SCR_AL_DATEOPT:
    if (e == EV_ENC_CW)
      move(+1, 2);
    else if (e == EV_ENC_CCW)
      move(-1, 2);
    else if (e == EV_BACK_PRESS)
      go_back();
    else if (e == EV_ENC_PRESS) {
      al.daily = cur == 0;
      go_replace(al.daily ? SCR_AL_TIME : SCR_AL_DATE);
    }
    break;
  case SCR_AL_DATE:
    if (edit_common(e, al_date_step, al_snap_restore))
      break;
    if (e == EV_ENC_CW)
      move(+1, 4);
    else if (e == EV_ENC_CCW)
      move(-1, 4);
    else if (e == EV_BACK_PRESS)
      go_back();
    else if (e == EV_ENC_PRESS) {
      if (cur < 3) {
        al_snap_save();
        editing = true;
      } else
        go(SCR_AL_TIME);
    }
    break;
  case SCR_AL_TIME: {
    if (edit_common(e, al_time_step, al_snap_restore))
      break;
    uint8_t nf = al_time_fields();
    if (e == EV_ENC_CW)
      move(+1, nf + 1);
    else if (e == EV_ENC_CCW)
      move(-1, nf + 1);
    else if (e == EV_BACK_PRESS)
      go_back();
    else if (e == EV_ENC_PRESS) {
      if (cur < nf) {
        al_snap_save();
        editing = true;
      } else
        pop_to(SCR_AL_MENU);
    }
    break;
  }
  case SCR_AL_SOUND:
    if (e == EV_ENC_CW)
      move(+1, 3);
    else if (e == EV_ENC_CCW)
      move(-1, 3);
    else if (e == EV_BACK_PRESS) {
      hw_test_stop();
      go_back();
    } else if (e == EV_ENC_PRESS) {
      sel_sound = cur + 1;
      go(SCR_AL_SOUND_OPT);
    }
    break;
  case SCR_AL_SOUND_OPT:
    if (e == EV_ENC_CW)
      move(+1, 2);
    else if (e == EV_ENC_CCW)
      move(-1, 2);
    else if (e == EV_BACK_PRESS) {
      hw_test_stop();
      go_back();
    } else if (e == EV_ENC_PRESS) {
      if (cur == 0) {
        if (!hw_sound_ok()) {
          ui_output_error(true);
          break;
        }
        al.sound = sel_sound;
        hw_test_stop();
        char b[24];
        snprintf(b, sizeof(b), "Sound %u selected", sel_sound);
        go_back();
        msg_show(b, "Save to keep it", SCR_AL_SOUND, sel_sound - 1);
      } else if (!hw_sound_test(sel_sound))
        ui_output_error(true);
    }
    break;
  case SCR_AL_LIGHT:
    if (e == EV_ENC_CW)
      move(+1, 3);
    else if (e == EV_ENC_CCW)
      move(-1, 3);
    else if (e == EV_BACK_PRESS) {
      hw_test_stop();
      go_back();
    } else if (e == EV_ENC_PRESS) {
      sel_light = cur + 1;
      go(SCR_AL_LIGHT_OPT);
    }
    break;
  case SCR_AL_LIGHT_OPT:
    if (e == EV_ENC_CW)
      move(+1, 2);
    else if (e == EV_ENC_CCW)
      move(-1, 2);
    else if (e == EV_BACK_PRESS) {
      hw_test_stop();
      go_back();
    } else if (e == EV_ENC_PRESS) {
      if (cur == 0) {
        if (!hw_light_ok()) {
          ui_output_error(false);
          break;
        }
        al.light = sel_light;
        hw_test_stop();
        char b[24];
        snprintf(b, sizeof(b), "Light %u selected", sel_light);
        go_back();
        msg_show(b, "Save to keep it", SCR_AL_LIGHT, sel_light - 1);
      } else if (!hw_light_test(sel_light))
        ui_output_error(false);
    }
    break;
  case SCR_AL_SNOOZE:
    if (edit_common(e, al_snooze_step, al_snap_restore))
      break;
    if (e == EV_ENC_CW)
      move(+1, 3);
    else if (e == EV_ENC_CCW)
      move(-1, 3);
    else if (e == EV_ENC_PRESS) {
      al_snap_save();
      editing = true;
    } else if (e == EV_BACK_PRESS)
      go_back();
    break;
  case SCR_AL_CONFIRM:
    if (e == EV_ENC_CW)
      move(+1, 3);
    else if (e == EV_ENC_CCW)
      move(-1, 3);
    else if (e == EV_BACK_PRESS)
      go_back();
    else if (e == EV_ENC_PRESS) {
      if (cur == 0)
        al_save();
      else if (cur == 1)
        al_discard();
      else
        go_back();
    }
    break;
  case SCR_BRIGHT:
    if (e == EV_ENC_CW)
      move(+1, 7);
    else if (e == EV_ENC_CCW)
      move(-1, 7);
    else if (e == EV_BACK_PRESS)
      bright_request_leave();
    else if (e == EV_ENC_PRESS) {
      switch (cur) {
      case 0:
        br_auto_draft = true;
        break;
      case 1:
        br_auto_draft = false;
        break;
      case 2:
      case 3:
      case 4:
        br_auto_draft = false;
        br_level_draft = cur - 1;
        break;
      case 5:
        bright_save();
        break;
      case 6:
        bright_discard();
        break;
      }
    }
    break;

  case SCR_BRIGHT_CONFIRM:
    if (e == EV_ENC_CW)
      move(+1, 3);
    else if (e == EV_ENC_CCW)
      move(-1, 3);
    else if (e == EV_BACK_PRESS)
      go_back();
    else if (e == EV_ENC_PRESS) {
      if (cur == 0)
        bright_save();
      else if (cur == 1)
        bright_discard();
      else
        go_back();
    }
    break;

  case SCR_DST:
    if (e == EV_ENC_CW)
      move(+1, 5);
    else if (e == EV_ENC_CCW)
      move(-1, 5);
    else if (e == EV_BACK_PRESS)
      dst_request_leave();
    else if (e == EV_ENC_PRESS) {
      switch (cur) {
      case 0:
        dst_auto_draft = true;
        dst_direction = 0;
        break;
      case 1:
        dst_auto_draft = false;
        dst_direction = 1;
        break;
      case 2:
        dst_auto_draft = false;
        dst_direction = -1;
        break;
      case 3:
        dst_auto_draft = false;
        dst_direction = 0;
        dst_hr_draft = 0;
        break;
      case 4:
        dst_save();
        break;
      }
    }
    break;

  case SCR_DST_CONFIRM:
    if (e == EV_ENC_CW)
      move(+1, 3);
    else if (e == EV_ENC_CCW)
      move(-1, 3);
    else if (e == EV_BACK_PRESS)
      go_back();
    else if (e == EV_ENC_PRESS) {
      if (cur == 0)
        dst_save();
      else if (cur == 1)
        dst_discard();
      else
        go_back();
    }
    break;

  case SCR_TD_REVIEW:
    if (e == EV_BACK_PRESS)
      go_replace(SCR_TD_MENU);
    else if (e == EV_ENC_CW || e == EV_ENC_CCW)
      move(1, 2);
    else if (e == EV_ENC_PRESS) {
      if (cur == 0)
        go_replace(SCR_TD_MENU);
      else {
        const DateTime &v = td_result.proposed;
        td.year = v.year();
        td.month = v.month();
        td.day = v.day();
        td.hour = v.hour();
        td.minute = v.minute();
        td.second = v.second();
        td_request.local = v;
        td_request.groups &= ~td_result.committed;
        if (td_request.groups & (SAVE_TIME | SAVE_DATE))
          td_request.groups |= SAVE_TIME | SAVE_DATE;
        td_submit(true); // user-confirmed proposal always gets a new ID
      }
    }
    break;

  default:
    break;
  }
}
void ui_alarm_overlay(bool on) {
  overlay = on;
  if (on)
    snooze_countdown = false;
  if (!on && scr == SCR_MSG)
    msg_until = millis() + MSG_MS;
}
void ui_snooze_refused() {
  snooze_notice_until = millis() + MSG_MS;
}
void ui_snoozed() {
  uint8_t index;
  uint32_t seconds;
  snooze_countdown = next_snooze(index, seconds);
}
void ui_output_error(bool sound) {
  msg_here(sound ? "Sound Playback Error" : "Lighting HW Error",
           sound && !BUZZ_OUTPUT_ENABLED ? "Output disabled" : "Selection unchanged");
}
void ui_brightness_mode(bool &automatic, uint8_t &level) {
  if (scr == SCR_BRIGHT || scr == SCR_BRIGHT_CONFIRM ||
      (scr == SCR_MSG && msg_next == SCR_BRIGHT)) {
    automatic = br_auto_draft;
    level = br_level_draft;
  }
}
void ui_service(uint32_t now) {
  if (scr == SCR_TD_SAVING) {
    if (!td_accepted)
      td_accepted = clock_save.submit(td_request) != SaveStatus::Busy;
    td_result = clock_save.query(td_request.id);
    if (now - td_queried >= 2000) {
      // Query only; never resubmit an accepted request on timeout.
      td_queried = now;
      td_result = clock_save.query(td_request.id);
      Serial.printf("[save] query id=%lu status=%u\n", (unsigned long)td_request.id,
                    unsigned(td_result.status));
    }
    if (td_accepted && !overlay && now - td_started >= 400) {
      if (td_result.status == SaveStatus::Success || td_result.status == SaveStatus::Error) {
        uint8_t committed = td_result.committed;
        if (committed & SAVE_TIME) {
          td_orig.hour = td.hour;
          td_orig.minute = td.minute;
          td_orig.second = td.second;
        }
        if (committed & SAVE_DATE) {
          td_orig.year = td.year;
          td_orig.month = td.month;
          td_orig.day = td.day;
        }
        if (committed & SAVE_FORMAT)
          td_orig.fmt24h = td.fmt24h;
        if (td_result.status == SaveStatus::Success) {
          td_orig = td;
          pop_to(SCR_MENU);
          msg_show("Saved", nullptr, SCR_MENU, cur);
        } else {
          go_replace(SCR_TD_MENU);
          msg_here(committed & (SAVE_TIME | SAVE_DATE) ? "Time set" : "Save failed",
                   td_result.detail);
        }
      } else if (td_result.status == SaveStatus::Review ||
                 (td_result.status == SaveStatus::Unknown && now - td_started >= 2000)) {
        if (td_result.status == SaveStatus::Unknown) {
          td_result.proposed = to_local(hw_rtc_read());
          td_result.detail = "Check current clock";
          // An unknown result requires returning to the draft; never infer success.
        }
        go_replace(SCR_TD_REVIEW);
      }
    }
  }
  if (!overlay && scr == SCR_MSG && msg_active && (int32_t)(now - msg_until) >= 0) {
    scr = msg_next;
    cur = msg_next_cur;
    msg_active = false;
  }
}

// OLED list and editor drawing helpers.
static const uint8_t ROW_Y[4] = {23, 34, 45, 56};
static void draw_title(const char *t) {
  oled.setFont(u8g2_font_6x12_tf);
  oled.drawStr(2, 9, t);
  oled.drawHLine(0, 12, 128);
}
static void draw_row(uint8_t slot, const char *s, bool hi) {
  if (slot > 3)
    return;
  uint8_t y = ROW_Y[slot];
  if (hi) {
    oled.drawBox(0, y - 9, 128, 11);
    oled.setDrawColor(0);
  }
  oled.drawStr(2, y, s);
  oled.setDrawColor(1);
}
static void draw_list(const char *t, const char *const *items, uint8_t n) {
  draw_title(t);
  uint8_t top = 0;
  if (n > 4) {
    if (cur >= 3)
      top = cur - 3;
    if (top + 4 > n)
      top = n - 4;
  }
  for (uint8_t i = 0; i < 4 && top + i < n; i++)
    draw_row(i, items[top + i], (uint8_t)(top + i) == cur);
  if (n > 4) {
    oled.drawVLine(126, 14, 44);
    uint8_t h = (uint8_t)(44u * 4u / n);
    if (h < 5)
      h = 5;
    uint8_t y = (uint8_t)(14 + (uint32_t)(44 - h) * top / (n - 4));
    oled.drawBox(124, y, 4, h);
  }
}
static void draw_field(uint8_t x, uint8_t y, const char *s, bool hi, bool act) {
  uint8_t w = oled.getStrWidth(s);
  if (act) {
    oled.drawBox(x - 3, y - 13, w + 6, 17);
    oled.setDrawColor(0);
    oled.drawStr(x, y, s);
    oled.setDrawColor(1);
  } else {
    oled.drawStr(x, y, s);
    if (hi)
      oled.drawHLine(x - 3, y + 3, w + 6);
  }
}
static void fmt_hm(char *b, size_t n, uint8_t h, uint8_t m, bool f24) {
  if (f24)
    snprintf(b, n, "%02u:%02u", h, m);
  else
    snprintf(b, n, "%u:%02u %s", hour12(h), m, is_pm(h) ? "PM" : "AM");
}

// 1. Home
// SSD1306 is monochrome: a sparse bell gives disabled alarms a dulled
// appearance, while enabled alarms use a solid bell. Focus is a separate
// outline so an unset alarm remains selectable without looking enabled.
static void draw_home_choices(const Settings &settings) {
  static const uint8_t bell_on[8] = {0x18, 0x3c, 0x7e, 0x7e, 0x7e, 0x7e, 0xff, 0x18};
  static const uint8_t bell_off[8] = {0x08, 0x24, 0x42, 0x40, 0x02, 0x40, 0x55, 0x10};
  oled.setFont(u8g2_font_5x7_tf);
  for (uint8_t i = 0; i < 4; ++i) {
    uint8_t x = i < 3 ? i * 27 : 83;
    uint8_t width = i < 3 ? 25 : 45;
    if (cur == i)
      oled.drawRFrame(x, 52, width, 12, 2);
    if (i < 3) {
      oled.drawXBMP(x + 4, 54, 8, 8, settings.alarm[i].enabled ? bell_on : bell_off);
      char label[2] = {char('1' + i), 0};
      oled.drawStr(x + 15, 61, label);
    } else
      oled.drawStr(x + 12, 61, "Menu");
  }
}
static void draw_home_alarm_status(const Settings &settings) {
  char buf[24];
  uint8_t snooze_index;
  uint32_t snooze_seconds;
  oled.setFont(u8g2_font_5x7_tf);
  if (next_snooze(snooze_index, snooze_seconds)) {
    snprintf(buf, sizeof(buf), "Snooze %u: %02lu:%02lu left", snooze_index + 1,
             (unsigned long)(snooze_seconds / 60), (unsigned long)(snooze_seconds % 60));
    oled.drawStr((128 - oled.getStrWidth(buf)) / 2, 49, buf);
    return;
  }
  uint8_t count = 0;
  for (const auto &alarm : settings.alarm)
    if (alarm.enabled)
      ++count;
  if (!count) {
    const char *empty = "No alarms set";
    oled.drawStr((128 - oled.getStrWidth(empty)) / 2, 49, empty);
    return;
  }
  // Full am/pm labels fit three equal columns using the compact font.
  // One or two alarms use the larger font and the same centered layout.
  if (count == 3)
    oled.setFont(u8g2_font_4x6_tf);
  uint8_t slot = 0;
  for (uint8_t i = 0; i < 3; ++i) {
    const Alarm &a = settings.alarm[i];
    if (!a.enabled)
      continue;
    if (settings.fmt24h)
      snprintf(buf, sizeof(buf), "%u. %02u:%02u", i + 1, a.hour, a.minute);
    else
      snprintf(buf, sizeof(buf), "%u. %u:%02u%s", i + 1, hour12(a.hour), a.minute,
               is_pm(a.hour) ? "pm" : "am");
    int left = slot * 128 / count, right = (slot + 1) * 128 / count;
    oled.drawStr(left + (right - left - oled.getStrWidth(buf)) / 2, 49, buf);
    ++slot;
  }
}
static void draw_home(const DateTime &t, const Settings &settings = cfg) {
  char buf[24];
  if (!hw_clock_valid()) {
    oled.setFont(u8g2_font_6x12_tf);
    oled.drawStr(8, 18, hw_clock_available() ? "Clock not set" : "Clock unavailable");
    oled.drawStr(8, 32, "Menu > Set time/date");
    draw_home_alarm_status(settings);
    draw_home_choices(settings);
    return;
  }
  oled.setFont(u8g2_font_6x12_tf);
  snprintf(buf, sizeof(buf), "%s %04u-%02u-%02u", weekday_short(t.dayOfTheWeek()), t.year(),
           t.month(), t.day());
  oled.drawStr(2, 10, buf);
  if (hw_dst_offset()) {
    oled.setFont(u8g2_font_5x7_tf);
    oled.drawStr(111, 9, "DST");
  }
  if (settings.fmt24h)
    snprintf(buf, sizeof(buf), "%02u:%02u", t.hour(), t.minute());
  else
    snprintf(buf, sizeof(buf), "%u:%02u", hour12(t.hour()), t.minute());
  oled.setFont(u8g2_font_logisoso24_tn);
  uint8_t w = oled.getStrWidth(buf);
  uint8_t x = (128 - w - 22) / 2;
  oled.drawStr(x, 39, buf);
  oled.setFont(u8g2_font_6x12_tf);
  snprintf(buf, sizeof(buf), ":%02u", t.second());
  oled.drawStr(x + w + 4, 39, buf);
  if (!settings.fmt24h)
    oled.drawStr(x + w + 4, 26, is_pm(t.hour()) ? "PM" : "AM");

  draw_home_alarm_status(settings);
  draw_home_choices(settings);
}

// 4 / 10. time field list
static void draw_time_fields(const char *t, uint8_t hh, uint8_t mm, const uint8_t *ss, bool f24,
                             uint8_t nf, const char *extra) {
  draw_title(t);
  char b[8];
  oled.setFont(u8g2_font_9x15_tr);

  uint8_t x = ss ? 6 : 20;
  snprintf(b, sizeof(b), "%02u", f24 ? hh : hour12(hh));
  draw_field(x, 34, b, cur == 0, editing && cur == 0);
  oled.drawStr(x + 21, 34, ":");

  snprintf(b, sizeof(b), "%02u", mm);
  draw_field(x + 31, 34, b, cur == 1, editing && cur == 1);

  uint8_t next = 2, xa = x + 52;
  if (ss) {
    oled.drawStr(x + 52, 34, ":");
    snprintf(b, sizeof(b), "%02u", *ss);
    draw_field(x + 62, 34, b, cur == 2, editing && cur == 2);
    next = 3;
    xa = x + 86;
  }
  if (!f24) {
    oled.setFont(u8g2_font_6x12_tf);
    draw_field(xa, 32, is_pm(hh) ? "PM" : "AM", cur == next, editing && cur == next);
  }

  oled.setFont(u8g2_font_6x12_tf);
  if (extra)
    draw_row(3, extra, cur == nf);
  else
    oled.drawStr(2, 62, editing ? "Turn to change" : "Press to edit");
}

// 5 / 10. date field list
static void draw_date_fields(const char *t, uint16_t yy, uint8_t mo, uint8_t dd, bool show_adjusted,
                             const char *extra) {
  draw_title(t);
  char b[8];
  oled.setFont(u8g2_font_9x15_tr);

  snprintf(b, sizeof(b), "%04u", yy);
  draw_field(6, 32, b, cur == 0, editing && cur == 0);
  oled.drawStr(47, 32, "-");
  snprintf(b, sizeof(b), "%02u", mo);
  draw_field(58, 32, b, cur == 1, editing && cur == 1);
  oled.drawStr(79, 32, "-");
  snprintf(b, sizeof(b), "%02u", dd);
  draw_field(90, 32, b, cur == 2, editing && cur == 2);

  oled.setFont(u8g2_font_6x12_tf);
  if (show_adjusted)
    oled.drawStr(2, 46, "Day adjusted");
  else
    oled.drawStr(2, 46, weekday_name(weekday_of(yy, mo, dd)));
  if (extra)
    draw_row(3, extra, cur == 3);
}

// the ringing overlay
static void draw_ringing() {
  const Alarm &a = cfg.alarm[hw_alarm_index()];
  char b[24];
  oled.setFont(u8g2_font_6x12_tf);
  snprintf(b, sizeof(b), "ALARM %u", hw_alarm_index() + 1);
  oled.drawBox(0, 0, 128, 13);
  oled.setDrawColor(0);
  oled.drawStr(2, 10, b);
  oled.setDrawColor(1);

  fmt_hm(b, sizeof(b), a.hour, a.minute, cfg.fmt24h);
  oled.setFont(u8g2_font_9x15_tr);
  oled.drawStr(8, 34, b);

  oled.setFont(u8g2_font_6x12_tf);
  uint8_t left = (a.snooze_max > hw_alarm_snoozes_used())
                     ? (uint8_t)(a.snooze_max - hw_alarm_snoozes_used())
                     : 0;
  if (left)
    snprintf(b, sizeof(b), "Snooze x%u   Back=off", left);
  else
    snprintf(b, sizeof(b), "No snoozes  Back=off");
  oled.drawStr(2, 62, b);
  if (int32_t(snooze_notice_until - millis()) > 0)
    oled.drawStr(2, 46, "Snooze limit reached");
  else if (!hw_sound_ok())
    oled.drawStr(2, 46, "Sound output error");
  else if (!hw_light_ok())
    oled.drawStr(2, 46, "Lighting HW error");
}

static bool draw_snoozed() {
  uint8_t index;
  uint32_t seconds;
  if (!next_snooze(index, seconds))
    return false;
  draw_title("Snoozed");
  uint8_t row = 0;
  for (uint8_t i = 0; i < 3; ++i) {
    if (!hw_snooze_remaining(i, seconds))
      continue;
    char b[24];
    snprintf(b, sizeof(b), "Alarm %u: %02lu:%02lu left", i + 1, (unsigned long)(seconds / 60),
             (unsigned long)(seconds % 60));
    oled.drawStr(2, 25 + row++ * 12, b);
  }
  oled.setFont(u8g2_font_5x7_tf);
  oled.drawStr(2, 63, "Knob/Back: return");
  return true;
}

void ui_draw(const DateTime &local) {
  oled.clearBuffer();
  oled.setDrawColor(1);

  if (overlay) {
    draw_ringing();
    hw_display_present();
    return;
  }
  if (snooze_countdown) {
    if (draw_snoozed()) {
      hw_display_present();
      return;
    }
    snooze_countdown = false;
  }

  char r[7][22];
  const char *items[7];
  for (uint8_t i = 0; i < 7; i++)
    items[i] = r[i];

  switch (scr) {

  case SCR_HOME:
    draw_home(local);
    break;

  case SCR_MENU: {
    static const char *m[4] = {"Set time/date", "Set alarm", "Brightness", "DST"};
    draw_list("Main menu", m, 4);
    break;
  }

  case SCR_TD_MENU: {
    char t[14];
    fmt_hm(t, sizeof(t), td.hour, td.minute, td.fmt24h);
    snprintf(r[0], 22, "Format: %s", td.fmt24h ? "24-hour" : "12-hour");
    snprintf(r[1], 22, "Time: %s", t);
    snprintf(r[2], 22, "Date: %04u-%02u-%02u", td.year, td.month, td.day);
    snprintf(r[3], 22, "Save");
    snprintf(r[4], 22, "Cancel");
    draw_list(hw_clock_valid() ? "Set time/date" : "Setup required", items, 5);
    break;
  }

  case SCR_TD_TIME:
    draw_time_fields("Time", td.hour, td.minute, &td.second, td.fmt24h, 99, nullptr);
    break;

  case SCR_TD_DATE:
    draw_date_fields("Date", td.year, td.month, td.day, td_day_adjusted, nullptr);
    break;

  case SCR_TD_CONFIRM: {
    static const char *m[3] = {"Save", "Discard", "Continue editing"};
    draw_list("Save changes?", m, 3);
    break;
  }

  case SCR_AL_LIST:
    for (uint8_t i = 0; i < 3; i++) {
      char t[12];
      fmt_hm(t, sizeof(t), cfg.alarm[i].hour, cfg.alarm[i].minute, cfg.fmt24h);
      snprintf(r[i], 22, "Alarm %u  %-8s %s", i + 1, t, cfg.alarm[i].enabled ? "on" : "off");
    }
    snprintf(r[3], 22, "Back to clock");
    draw_list("Alarms", items, 4);
    break;

  case SCR_AL_MENU: {
    snprintf(r[0], 22, "Enabled: %s", al.enabled ? "yes" : "no");
    if (al.daily)
      snprintf(r[1], 22, "Time: daily");
    else
      snprintf(r[1], 22, "Time: %02u-%02u", al.month, al.day);
    snprintf(r[2], 22, "Sound: %u", al.sound);
    snprintf(r[3], 22, "Light: %u", al.light);
    snprintf(r[4], 22, "Snooze: %umin x%u", al.snooze_min, al.snooze_max);
    snprintf(r[5], 22, "Save & exit");
    snprintf(r[6], 22, "Cancel");
    char t[22];
    snprintf(t, sizeof(t), "Alarm %u", sel_alarm + 1);
    draw_list(t, items, 7);
    break;
  }

  case SCR_AL_DATEOPT: {
    static const char *m[2] = {"Every day", "On one date"};
    draw_list("When?", m, 2);
    break;
  }

  case SCR_AL_DATE:
    draw_date_fields("Alarm date", al.year, al.month, al.day, false, "Next: time");
    break;

  case SCR_AL_TIME:
    draw_time_fields("Alarm time", al.hour, al.minute, nullptr, cfg.fmt24h, al_time_fields(),
                     "Done");
    break;

  case SCR_AL_SOUND:
    for (uint8_t i = 0; i < 3; i++)
      snprintf(r[i], 22, "%c Sound %u", al.sound == i + 1 ? '*' : ' ', i + 1);
    draw_list("Sounds", items, 3);
    break;

  case SCR_AL_SOUND_OPT: {
    snprintf(r[0], 22, "Use sound %u", sel_sound);
    snprintf(r[1], 22, "Test%s", hw_testing() ? " (playing)" : "");
    char t[22];
    snprintf(t, sizeof(t), "Sound %u", sel_sound);
    draw_list(t, items, 2);
    break;
  }

  case SCR_AL_LIGHT: {
    static const char *nm[3] = {"Slow flash", "Fast flash", "Fade in"};
    for (uint8_t i = 0; i < 3; i++)
      snprintf(r[i], 22, "%c %u %s", al.light == i + 1 ? '*' : ' ', i + 1, nm[i]);
    draw_list("Lights", items, 3);
    break;
  }

  case SCR_AL_LIGHT_OPT: {
    snprintf(r[0], 22, "Use light %u", sel_light);
    snprintf(r[1], 22, "Test%s", hw_testing() ? " (running)" : "");
    char t[22];
    snprintf(t, sizeof(t), "Light %u", sel_light);
    draw_list(t, items, 2);
    break;
  }

  case SCR_AL_SNOOZE: {
    // Chevrons mark the value that the encoder is changing right now.
    const char *o = editing ? "<" : " ", *c = editing ? ">" : " ";
    snprintf(r[0], 22, "Delay     %s%u%s min", cur == 0 ? o : " ", al.snooze_min,
             cur == 0 ? c : " ");
    snprintf(r[1], 22, "Max snooze%s%u%s", cur == 1 ? o : " ", al.snooze_max, cur == 1 ? c : " ");
    if (al.length_min == 0)
      snprintf(r[2], 22, "Ring: %s0%s indefinite", cur == 2 ? o : " ", cur == 2 ? c : " ");
    else
      snprintf(r[2], 22, "Ring for  %s%u%s min", cur == 2 ? o : " ", al.length_min,
               cur == 2 ? c : " ");
    draw_list("Snooze settings", items, 3);
    break;
  }

  case SCR_AL_CONFIRM: {
    static const char *m[3] = {"Save", "Discard", "Continue editing"};
    draw_list("Save changes?", m, 3);
    break;
  }

  case SCR_BRIGHT: {
    static const char *lv[3] = {"Low", "Medium", "High"};
    snprintf(r[0], 22, "[%c] Automatic", br_auto_draft ? 'x' : ' ');
    snprintf(r[1], 22, "[%c] Manual", !br_auto_draft ? 'x' : ' ');
    for (uint8_t i = 0; i < 3; i++)
      snprintf(r[2 + i], 22, "   %c %u %s", (!br_auto_draft && br_level_draft == i + 1) ? '*' : ' ',
               i + 1, lv[i]);
    snprintf(r[5], 22, "Save");
    snprintf(r[6], 22, "Discard");
    draw_list("Brightness", items, 7);
    break;
  }

  case SCR_BRIGHT_CONFIRM: {
    static const char *m[3] = {"Save", "Discard", "Continue editing"};
    draw_list("Save brightness?", m, 3);
    break;
  }

  case SCR_DST_CONFIRM: {
    static const char *m[3] = {"Save", "Discard", "Continue editing"};
    draw_list("Save DST changes?", m, 3);
    break;
  }

  case SCR_DST: {
    snprintf(r[0], 22, "[%c] Automatic", dst_auto_draft ? 'x' : ' ');
    snprintf(r[1], 22, "[%c] +1 hour", dst_direction == 1 ? 'x' : ' ');
    snprintf(r[2], 22, "[%c] -1 hour", dst_direction == -1 ? 'x' : ' ');
    snprintf(r[3], 22, "[%c] Off",
             !dst_auto_draft && !dst_direction && dst_hr_draft == 0 ? 'x' : ' ');
    snprintf(r[4], 22, "Save");
    draw_list("Daylight saving", items, 5);
    break;
  }

  case SCR_TD_SAVING:
    draw_title("Saving...");
    oled.drawStr(2, 32, "Please wait");
    oled.drawStr(2, 48, "Alarms stay active");
    break;
  case SCR_TD_REVIEW: {
    draw_title("Review required");
    const DateTime &v = td_result.proposed;
    snprintf(r[0], 22, "%04u-%02u-%02u %02u:%02u", v.year(), v.month(), v.day(), v.hour(),
             v.minute());
    oled.drawStr(2, 24, r[0]);
    oled.drawStr(
        2, 35, td_result.status == SaveStatus::Unknown ? "Current clock" : "DST: standard choice");
    draw_row(2, "Back to editing", cur == 0);
    draw_row(3, "Confirm this time", cur == 1);
    break;
  }

  case SCR_MSG:
    oled.setFont(u8g2_font_6x12_tf);
    oled.drawStr((int)(64 - oled.getStrWidth(msg_l1) / 2), 28, msg_l1);
    if (msg_l2[0])
      oled.drawStr((int)(64 - oled.getStrWidth(msg_l2) / 2), 44, msg_l2);
    break;
  }

  hw_display_present();
}

void ui_begin() {
  scr = SCR_HOME;
  cur = 3;
  nav_sp = 0;
  editing = false;
  overlay = false;
  snooze_countdown = false;
}
