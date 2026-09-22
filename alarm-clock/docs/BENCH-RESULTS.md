# Bench results — 2026-09-20

## Evenly spaced active alarms on Home

Replaced the Next label with all enabled alarms, preserving alarm numbers and
centering entries in equal-width columns. One/two alarms use the 5x7 font;
three use 4x6 so full am/pm suffixes fit. Both clock formats are supported,
disabled alarms are omitted from the row, and no enabled alarms shows
`No alarms set`. A pending snooze retains its countdown priority.

Built without warnings and uploaded app-only. All 501 existing boot checks
passed. Visually reviewed all eight enabled-alarm combinations in each format
(16 actual device framebuffer renders). A new bench-only renderer uses a
settings copy, so these previews never enable real alarms or change storage.
Before/after status confirms all saved alarms were unchanged. A separate
injected snooze confirmed Home still shows `Snooze 1: 09:00 left`; test snooze
was cleared afterward. Evidence: `artifacts/home-active-alarms/build.log`,
`verified/{last-serial.log,review.png}` and `snooze/` beneath
`artifacts/home-active-alarms/`.

## Saved alarms with DST and 12/24-hour format

Added 322 scheduler checks against RAM copies of the three loaded alarm records:
Automatic/Off/manual +1/manual −1, winter/summer, both display formats, next
alarm selection, enabled/disabled state, exact firing minute, and format
switching after scheduling. Additional cases exercise manual DST changes after
an alarm is set, duplicate suppression after moving back, next-day firing, and
spring/fall transitions for daily and dated alarms in both formats. Added 34
checks of the actual UI formatter/editor conversion covering midnight, noon,
AM/PM, and round trips for all 24 hours. All 501 boot checks passed; synthetic
transition dates never touch the RTC or saved settings.

The RTC now reports valid time. Actual menu saves verified 12 → 24 → 12 hours
and manual DST offset −2 → −1 → −2, restoring the user's current settings.
Every status sample retained all alarm fields; local-minus-standard timestamps
equalled the selected offset exactly, and there were no RTC writes. Visually
reviewed the saved alarms in both formats: 2:24 PM / 14:24, 12:02 AM / 00:02,
and disabled 7:00 AM / 07:00. Thus existing alarm times follow the adjusted local
clock and display format without being converted or overwritten in storage.
No functional defect was found in these checks; this update adds regression
coverage. Evidence: `artifacts/dst-alarm-format/build.log`, `verified/` and
`live/{last-serial.log,review.png}` under `artifacts/dst-alarm-format/`.

## Snooze button behavior and countdown

Removed the 600 ms Snooze hold action that dismissed a ringing alarm or
cancelled waiting snoozes. GPIO18 now emits one Snooze event on the debounced
down edge and none on hold/release. Both short and legacy hold events use the
snooze path; repeated input while waiting only reopens the countdown. Back
still dismisses a ringing alarm. A dated alarm stays enabled while snoozing.

Snoozing opens a screen listing each waiting alarm and live MM:SS remaining.
Knob/Back restores the underlying menu/draft; Home shows the earliest pending
snooze's countdown, including when the RTC is unset. Countdown uses the same
monotonic deadline as re-ringing, rounded up and clamped at zero.

Built without warnings, uploaded app-only, and passed all 145 boot checks
(25 calendar, 54 scheduler/save, 66 runtime). New checks cover held/repeated
input, retaining a dated alarm's Enabled flag, deadline rounding, zero,
millis rollover, and re-ringing with the countdown removed. Deadline checks
advance synthetic service timestamps rather than waiting real minutes.

Live serial checks confirmed Alarm 1 remained enabled/snoozing after held
input and repeated hold; remaining time decreased from 539107 to 534871 ms.
Visually reviewed frames show 09:00 then 08:56, Home at 08:55, and simultaneous
Alarm 1/2 countdowns. Date-editor frames before and after the alarm/countdown
are byte-identical, preserving an active 2027 draft. Test edits were discarded,
test snoozes explicitly cleared through the bench command, and saved alarm
status matched before/after. GPIO18 read released; this does not verify a
user-operated physical press or measured full-duration snooze.
Evidence: `artifacts/snooze-countdown/build.log` and
`artifacts/snooze-countdown/verified/{last-serial.log,review.png}`.

## Stronger automatic dimming

Automatic brightness now maps low/medium/high to OLED contrast 0/40/255,
previously 8/110/255. Manual levels remain 8/110/255. Alarm light effects
retain their existing contrast override. Sensor thresholds and hysteresis
are unchanged; contrast zero requests minimum drive, not display-off.

Built and uploaded app-only; all 136 boot checks passed. Live status confirmed
automatic low at contrast 0, manual previews at 8/110/255, discard restoring
automatic low at 0, and light preview at 255 followed by restoration to 0.
Saved settings were retained. LDR readings were 10–13 during these checks;
automatic medium/high transitions and perceived panel brightness under changing
room light were not physically verified. Evidence:
`artifacts/stronger-auto-dim/build.log` and
`artifacts/stronger-auto-dim/verified/last-serial.log`.

## Unset clock date starts at 2026

Changed the Time/Date draft for an unset clock from 2000-01-01 to 2026-01-01.
A valid clock still seeds the editor with its current local date/time; this
change does not set the RTC automatically or change the supported year range.
Built and uploaded app-only, with all 136 existing boot checks passing. Live
framebuffer review confirmed Date opens at 2026-01-01 and one clockwise edit
steps to 2027-01-01. Cancelled and discarded the draft; no RTC write occurred,
and saved alarm status matched before/after. Evidence:
`artifacts/date-default/build.log` and `artifacts/date-default/verified/`.

## Snooze limits and indefinite ringing

Built and uploaded app-only firmware with delay 5–15 minutes in one-minute
steps, maximum snoozes 0–10 (zero refuses snooze and leaves the alarm ringing),
and ring duration 0–60 minutes (zero rings indefinitely until snoozed or
dismissed). Older saved delays outside 5–15 are clamped and persisted on load.

All 136 boot checks passed: 25 calendar/DST, 54 scheduler/save, and 57 alarm
runtime checks. Coverage includes exact 5/9/10/15-minute snooze deadlines,
indefinite ringing across a simulated day and after re-ringing, finite expiry,
zero snoozes, ten accepted snoozes, and refusal of the eleventh. These advance
service timestamps and do not represent elapsed wall-clock endurance tests.

Live serial navigation and visually reviewed device framebuffers confirmed
delay bounds 5/15 and adjacent values 6/14, snooze bounds 0/10 and adjacent
values 1/9, and the explicit `Ring: <0> indefinite` label followed by 1 minute.
All draft edits were discarded. Before/after status records match for all three
saved alarms; no physical input interfered. GPIO18 was HIGH/released at both
status samples, but physical button operation remains unverified.

The RTC now reports its oscillator-stop flag and invalid time; Home correctly
shows `Clock not set`. No RTC write occurred during this verification. The user
must set time/date again and check backup retention; saved alarm settings remain.
Evidence: `artifacts/snooze-limits/build.log` and
`artifacts/snooze-limits/verified/{last-serial.log,verification.png}`.

## Nine-versus-ten-minute snooze investigation

All three saved alarms currently have `snooze_min=9`; the Alarm 1 settings
framebuffer shows 9 minutes. Added eight runtime checks using temporary RAM
settings for 9- and 10-minute snoozes: the actual deadline matches 540000 or
600000 ms after activation, no re-ring occurs one millisecond before it, and
the runtime re-rings at the deadline. All 95 boot checks pass (25 calendar,
50 scheduler/save, 20 runtime). The new checks advance the service timestamp;
they do not establish real elapsed wall-clock duration.

Built without warnings and uploaded diagnostics reporting saved delay and
remaining milliseconds. A brief injected Alarm 1 snooze showed 539676 ms
remaining immediately after activation, then 536789 ms about three seconds
later. The test snooze was cancelled; no saved setting was changed. The reported
extra minute has not been reproduced. Clarification is pending on whether the
user means an edited value, the next-alarm display, or measured elapsed time.
Evidence: `artifacts/snooze-duration/build.log`, `baseline/` and `deadline/`
under `artifacts/snooze-duration/`. GPIO18 now reads HIGH/released at idle;
this run did not include a physical button press.

## Remaining Snooze button and removed Alarm button

The user's layout identifies the button immediately left of the OLED as GPIO18.
It remains Snooze; the farther-left GPIO17 Alarm button has been removed.
GPIO17 was removed from the polled button table and boot wiring probes. Alarm
selection remains available through Home and the menu (the USB diagnostic
`alarm` command remains available). Built without compiler warnings and uploaded;
all 87 boot checks pass. The existing clock/alarm/brightness settings were retained.

During a 45-second physical-input observation, the user confirmed tapping and
releasing the remaining button. No physical input event was logged. All four
status samples reported `snooze18=0`, `back14=1`, `select12=1`; boot probing also
reported GPIO18 LOW despite its pull-up. The firmware therefore does not see
release/press transitions. Physical Snooze is **not verified or working in this
test**; the current wiring must be inspected. The measurements do not identify
the exact mechanical fault or prove the remaining switch is connected to GPIO18.
Evidence: `artifacts/snooze-button/build.log` and
`artifacts/snooze-button/physical/last-serial.log`.

Recheck: a `SNOOZE_HOLD` event now appears, but the user confirmed the button
is connected and released. All subsequent samples still report `snooze18=0`.
The second boot probe briefly read floating before the input became LOW, so
the hold event is not proof of a successful deliberate press. A disconnected
GPIO18-jumper test remains necessary to isolate the external wiring from the
pin/configuration. Logs: `artifacts/snooze-button/recheck/last-serial.log` and
`artifacts/snooze-button/released-check/last-serial.log`.

## DST Back prompt and OLED lighting

Uploaded the DST Save / Discard / Continue prompt and moved all alarm/preview
lighting from the board RGB LED to the OLED. Light choices are Slow flash
(one cycle/second), Fast flash (two cycles/second), and Fade in (60 seconds,
compressed into four seconds for previews). Flashing inverts the framebuffer,
retaining readable text. The board LED is only written black once at boot.

Live DST navigation verified unchanged Back exits, changed Back prompts,
Continue and held Back retain the draft, Discard leaves the offset unchanged,
and Save applies the pending one-hour change. Automatic and Off changes also
prompt. The original manual −1-hour offset was restored after the save test.

OLED captures verified 100% pixel inversion between both phases of slow/fast
previews and the ringing screen. Fade preview contrast rose from 22 to 172;
preview cancellation, preview expiry, alarm dismissal and snooze each restored
normal rendering and contrast 110 at automatic brightness level 2. The injected
snooze was cancelled afterward. Saved alarms and brightness stayed unchanged.
This verifies generated frames/contrast commands, not a camera observation of
the physical panel or board LED.

Build succeeded without compiler warnings. All 87 boot checks passed; native
`tools/test_screen_effect.cpp` checks flash boundaries, monotonic fade and
saturation. Evidence: `artifacts/screen-dst/build.log`, `navigation/last-serial.log`
and `effects/last-serial.log` under `artifacts/screen-dst/`, with visually reviewed
`review.png` contact sheets in both subdirectories.

## Relative DST adjustments and Off

The current menu order is Automatic, +1 hour, −1 hour, Off, Save. Signed
choices apply one relative hour on Save and disable automatic DST; Off uses
zero offset. Multiple presses before Save select the same pending adjustment
rather than accumulating hours. Existing offsets remain compatible. Manual
settings saves refresh the display and mark a scheduler discontinuity so they
cannot trigger the automatic spring-gap catch-up rule.

Built without compiler warnings and uploaded app-only. All 87 boot checks pass
(25 calendar, 50 scheduler/save, 12 runtime). New checks cover relative shifts
from automatic summer/winter time and existing manual offsets, Off, integer
overflow, and avoiding skipped-alarm playback on a manual shift.

Live serial navigation verified offsets −1 → 0 → −1, Automatic → +1, then
“−1 hour” → 0 (exactly one hour back), “+1 hour” → +1, Off → 0. A final −1
restored the user's original manual offset. Selecting +1 twice before Save
still changed the display by only one hour. Parsed timestamps matched each
offset, alarm settings were identical at every status sample, and no RTC writes
occurred. Menu framebuffers were visually reviewed. Evidence:
`artifacts/dst-adjustment/build.log` and `artifacts/dst-adjustment/verified/`
(`last-serial.log`, `review.png`). This supersedes the earlier On/Off menu below.

## DST menu and brightness confirmation

Built without compiler warnings and uploaded the simplified Automatic / DST On /
DST Off menu. On maps to +1 hour and Off to zero. The existing saved −1-hour
offset remains intact until a replacement mode is explicitly saved; the menu
labels that old setting and asks for a mode if Save is pressed without choosing.
Live framebuffer checks exercised all three choices and Back/discard without
changing the user's clock or saved DST offset.

Brightness Back now prompts Save / Discard / Continue editing when dirty.
Live serial checks verified unchanged Back exits immediately; High remains
previewed while confirming; Continue and held Back resume editing with the
draft intact; Discard restores Automatic; Save commits Manual/Low. A second
Save restored the original Automatic setting with manual level 1. Alarm times,
enabled states, format, and DST stayed unchanged. All 76 boot checks passed.
Evidence: `artifacts/settings-prompts/build.log` and
`artifacts/settings-prompts/verified/last-serial.log`; the visually reviewed
OLED framebuffer contact sheet is `artifacts/settings-prompts/verified/review.png`.
These injected button events verify navigation, not physical switch operation
or optical brightness response.

## Existing alarms follow the clock format

Verified on the uploaded firmware without a code change: switched from the
saved 24-hour mode to 12-hour mode, saved the existing Alarm 1 unchanged, then
saved 24-hour mode again. All three alarm list entries and the Alarm 1 editor
followed the global format: 12:24 PM → 12:24 and 7:00 AM → 07:00.
The AM/PM editor field disappears in 24-hour mode. Alarm times/enabled flags
were unchanged, and the original 24-hour preference was restored. No RTC write
occurred during the format-only saves. Captures and serial evidence are in
`artifacts/alarm-format/roundtrip/`; `review.png` compares the displays.

## Automatic brightness investigation

Automatic mode is saved and active. Initial steady readings were 1127–1133;
after the update they were 1109–1115, selecting Medium. These are unlabelled
ambient samples, not confirmed dark/room/torch calibration points.

Fixed a hysteresis defect: a sudden jump from Low into the upper boundary's
dead band could leave the display Low indefinitely; the reverse could leave
it High. `tools/test_brightness.cpp` verifies both cases, full ADC sweeps and
noise rejection at both boundaries. Compile/run with
`c++ -std=c++11 -Wall -Wextra -Werror tools/test_brightness.cpp -o /tmp/alarm-test-brightness && /tmp/alarm-test-brightness`.
The checks pass, firmware builds without compiler warnings, and the app-only
upload succeeded. All 76 existing boot checks pass. Evidence is in
`artifacts/brightness/build.log` and `artifacts/brightness/updated/last-serial.log`.

Status now distinguishes saved and active automatic mode, manual setting,
applied level and thresholds. Thresholds remain 500/1600 with hysteresis 120
pending user-assisted sensor measurements. Actual light response and visible
OLED dimming have not yet been verified; this fix alone does not establish
the cause of the reported symptom.

## Alarm exit fix

Built and uploaded the Save & exit change, preserving NVS. Successful alarm
saves now clear the navigation stack and return to Home with Menu selected,
whether opened from Home or the alarm list. The list includes Back to clock,
and held Back can dismiss a save notice.

Live serial-event checks verified both save routes, held Back during the Saved
notice, opening Menu afterward, and leaving the list using only the encoder.
Framebuffer captures were visually reviewed in
`artifacts/alarm-exit/final/review.png`; the matching serial log is alongside it.
All 76 boot checks pass; the build has no compiler warnings
(`artifacts/alarm-exit-build.log`). Alarm 1 remained enabled at 12:24 PM and
alarms 2/3 remained disabled at 7:00 AM. The initial test under `verified/`
overlapped physical input and is superseded by the uninterrupted `final/` run.

Latest wiring diagnostics show Back floating, with Alarm/Snooze LOW; Back was
LOW during the earlier baseline. These observations alone do not establish
physical switch operation. One startup I²C transmit error appeared in the final
log; both buses subsequently responded and RTC status remained valid.

**Later update:** the user explicitly requested buzzer output enabled for testing.
`BUZZ_OUTPUT_ENABLED=true` replaces the misleading VERIFIED switch name. The
results below describe the preceding build with sound disabled; the new build
and boot logs are `artifacts/buzzer-enabled-build.log` and
`artifacts/buzzer-enabled/last-serial.log`. No acoustic result is claimed until
the user tests the physical buzzer.

Target: connected ESP32-S3 (revision 0.2), 8 MB embedded PSRAM, UART bridge
`/dev/cu.usbmodem5C930434691`. Firmware uses the existing partition map, verified
byte-for-byte against the generated partition table before upload.

## Build and flash

- Platform: pioarduino 55.03.312, Arduino 3.3.12, ESP-IDF 5.5.5.
- Final build succeeded without compiler warnings in `artifacts/build.log`.
- Static RAM: 26,492 bytes (8.1%); reported flash use: 425,327 bytes (12.7%).
- Firmware binary: 433,792 bytes.
- SHA-256: `70da961c02cd6fea166347622ebb48902f8883c7df8bd2b8944e99b0c60a1ab0`.
- Read and retained a 32,768-byte backup of 0x8000–0xffff, including NVS.
- Uploaded **only firmware.bin at 0x10000**, successfully; NVS was preserved.
- esptool's default progress logger raised an installed-version mismatch.
  `--silent` with `~/.platformio/penv/bin/python` worked for backup and upload.

## Home selector update

The current build adds direct Home selection of Alarm 1, 2, 3 and Menu, with
muted bell icons for disabled alarms and solid bells for enabled alarms. The
focus outline is independent of alarm state. DST is spelled out by the date.
Menu is selected at startup. The original version returned direct alarm
Save/Discard to Home and list-opened alarms to the list; successful Save now
always returns to Home as described above.

Live serial navigation opened all four targets and checked return paths. Actual
OLED captures are under `artifacts/home-selector/`; `navigation.png` shows all
four destinations. All 76 on-chip checks still pass. Temporary Alarm 1 enable
changes used for icon inspection were restored to disabled.

The latest boot reported LDR raw 784 and status raw 1006, rather than the earlier
near-zero readings; calibration still requires dark/room/torch samples. The
three buttons continue to report LOW at rest.

## On-chip regression checks

The boot log records each suite's outcome. Tests are run on the actual ESP32.
The scheduler/save suite uses mock callbacks, so synthetic dates/errors do not
change the hardware RTC or NVS. The runtime suite temporarily uses RAM settings,
then restores the loaded configuration; no sound is enabled by these tests.

| Suite | Checks | Coverage |
|---|---:|---|
| Calendar / DST | 25 | Leap years, month lengths, weekdays, US edges 2024–2026 |
| Scheduler / save | 39 | Spring catch-up, fall duplicate suppression, next-alarm dates, simultaneous alarms, manual offsets, async acceptance, busy, duplicate/stale IDs, unknown queries, changed groups, partial failure/retry, gap review, confirmed new ID, rollover comparisons |
| Alarm runtime | 12 | Start, snooze, no early re-ring, count retention, refusal at limit, dismiss, queue order, length timeout, cancellation, disabled-alarm cleanup |

No physical time passage is implied by the runtime timeout test: it advances the
runtime's elapsed-time state. Real minute-long timing and user-operated controls
still belong to physical acceptance.

## Live UI and output checks

Performed using the serial equivalents of encoder/button events:

- Home → Main menu → Time/Date; active field editing and Back cancellation.
- Unsaved changes prompt, Saving screen, successful format saves.
- Format toggled and restored to the original **12-hour** setting; subsequent
  resets reload it. No `[rtc] verified` write occurred in format-only tests.
- All three alarms remained disabled after tests.
- Sound test displays **Sound Playback Error / Check buzzer driver**; the firmware
  intentionally prevents PWM into the unverified direct-connected load.
- Light preview shows **Test (running)** after the timer fix. RMT returns success.
  Final smoke test invokes all three patterns. Software driver success does not
  prove that the LED is visibly working or that its colors are correct.
- Brightness preview reports level **3**; Back/discard returns to prior automatic
  level **1**, without saving the draft.
- `ring-test 1` shows the alarm overlay; Back dismisses it and restores Home.
  This is an injected runtime trigger, not proof of the real-time schedule.
- Captured actual U8g2 framebuffer bytes from the device and visually reviewed
  the rendered Home, menu, time/date, saving, error, brightness, preview and
  ringing screens. See `artifacts/oled-review.png` and the individual PBM files.
  These are framebuffer captures, not photographs of the OLED panel.

## Hardware observations

- OLED bus responds at **0x3C**; RTC bus at **0x57 and 0x68**.
- RTC reports available and valid calendar data; seconds advance across tests.
  Neither wall-clock accuracy nor battery retention is certified by this result.
- GPIO14, GPIO17 and GPIO18 remain **LOW at rest**, including with pull-ups.
- Encoder inputs are floating in the wiring diagnostic. Connectivity and detent
  direction cannot be inferred without operating the encoder.
- LDR raw values observed during this session: approximately **0–14**.
- Sound driver is deliberately disabled (`sound=0`); light driver reports success
  (`light=1`). GPIO13 must not be enabled merely because its drive setting is low.

## Evidence files

- `artifacts/final-serial.log`: final firmware boot checks and smoke test.
- `artifacts/boot-navigation.log`: initial firmware boot and field navigation.
- `artifacts/save-preview.log`: save/error/preview UI tests.
- `artifacts/runtime-preview.log`: runtime suite, restored format, brightness
  3→1, light running and injected ringing/dismiss.
- `artifacts/build.log`: final build output.
- `artifacts/oled-review.png`: readable contact sheet of device framebuffers.

## Not yet verified

Physical switches/encoder; actual buzzer part and safe load/driver; acoustic
patterns; physical RGB colors; live unplug/reconnect of RTC; LDR calibration and
light response; OLED contrast under room lighting; real power-off coin-cell
retention. Fault statuses are covered by mock save tests, not electrical fault
injection. The original milestone PDF still needs the team to incorporate the
amendment sheet before submission.
