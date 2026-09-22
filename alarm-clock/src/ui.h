#pragma once

// =====================================================================
// The screen state machine. main.cpp feeds it input events and asks it
// to draw; everything about menus and drafts lives in ui.cpp.
// =====================================================================
#include <RTClib.h>
#include "input.h"

void ui_begin();
void ui_event(InputEvent e);         // one debounced event
void ui_draw(const DateTime &local); // call at the UI refresh rate
void ui_alarm_overlay(bool on);      // alarm takes / releases the screen
void ui_service(uint32_t now);       // timed screens (messages, tests)

void ui_snooze_refused();
void ui_snoozed(); // show live countdown without losing menu drafts
void ui_output_error(bool sound);
void ui_brightness_mode(bool &automatic, uint8_t &level);
