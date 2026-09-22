# Milestone 03 implementation amendments

Prepared 2026-09-20 against the original Milestone 03 PDF and connected ESP32-S3.
This is an amendment sheet for the team; the original submission PDF is preserved.

## Actual pin assignments

All external signal jumpers use header J1. J3 is unused.

| Signal | Original PDF | Implemented GPIO |
|---|---:|---:|
| OLED SDA | 8 | 8 |
| OLED SCL | 9 | 9 |
| RTC SDA | 45 | 10 |
| RTC SCL | 48 | 11 |
| Encoder A | 21 (prose also incorrectly says 12) | 15 |
| Encoder B | 20 | 16 |
| Encoder switch | 12 | 12 |
| Back | 47 | 14 |
| Snooze | 39 | 18 |
| Alarm | 0 | Removed; GPIO17 unused |
| Photoresistor | 17 | 4 |
| Buzzer signal | 13 | 13 |
| On-board RGB | — | 48 |

OLED uses Wire, RTC uses Wire1. Do not join the two signal buses. The RTC SQW
pin is unconnected; a millis() tick reads the clock once per second.

## Hardware corrections

The photoresistor topology is **3V3 → LDR → GPIO4 → internal pull-down → GND**.
Brighter light should produce a larger ADC value. The measured initialization
order and digital gpio_pulldown_en() are retained. Thresholds are provisional
until dark/room/torch samples can be collected on the assembled board.

The buzzer was previously reported as low resistance, consistent with an
electromagnetic load rather than a piezo. The current screenshot shows direct
GPIO13-to-buzzer wiring. **The previous assertion that GPIO_DRIVE_CAP_0 makes
this connection safe is withdrawn.** Drive strength is not a verified load
current limit or inductive protection. Firmware initially disabled output.
BUZZ_OUTPUT_ENABLED is true for sound testing; that setting does not certify
the electrical load. Verify the actual part/load and use an
appropriate drive circuit, or a verified GPIO-compatible transducer, before
considering the circuit electrically verified. This may require changing the kit-only/no-extra-parts constraint.
Reference: [Espressif GPIO drive API](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/gpio.html)
and [ESP32-S3 electrical specifications](https://documentation.espressif.com/esp32_s3_datasheet_en.pdf).

The remaining external buttons are Snooze on GPIO18 (immediately left of the
OLED) and Back on GPIO14 (right of the encoder). The farther-left Alarm button
has been removed; GPIO17 is not polled or probed. Earlier reports showed button
inputs LOW with pull-ups; a held button and a wiring fault can both cause that.
Check released inputs separately. With power disconnected, signal and ground
must connect only when pressed. Internally common legs must not be used as the
two sides of the switch.

## Software decisions and completed changes

- User-requested Home navigation: rotate between Alarm 1, 2, 3 and Menu;
  press opens the target directly. Disabled alarms have sparse bell icons,
  enabled alarms solid bells, with a separate focus outline. The previous D
  indicator is now explicitly labeled DST beside the date. This supersedes
  Chart 1's original no-action-on-rotation behavior.

- Alarm menus include Enabled, Save and Cancel so an alarm can actually be armed.
- Chart 7's unsaved-changes prompt guards leaving the Set time/date editing
  session. Back inside Time/Date retains the draft, matching charts 4 and 5.
  This is the implemented interpretation; team sign-off is still pending.
- Chart 8 is implemented as a cooperative asynchronous request owner. Each
  request has a monotonic ID within the boot, changed time/date/format flags,
  and accepted/busy/success/error/review status. Duplicate IDs do not reapply
  writes; stale IDs require review. RTC time is verified before format storage.
  A format-only change never rewrites the clock. Date-only edits preserve the
  running time, and time-only edits preserve the current date.
- Saving freezes editing and displays progress for at least 400 ms. Status is
  queried throughout; after two seconds the same ID is queried, never rewritten
  if accepted. Unknown results require a fresh read and review. Partial success
  commits originals for successful groups, retains remaining changes, and tells
  the user what failed. Alarm overlays pause presentation of the final result.
- The RTC remains standard time. Local-to-standard conversion detects spring
  gaps and autumn overlaps: gaps propose moving ahead one hour, overlaps propose
  the second (standard-time) occurrence. Explicit confirmation creates a new ID.
- Scheduled spring-gap alarms fire at the transition. An alarm fires at most
  once per local calendar day in the current boot, avoiding autumn duplicates.
  Simultaneous alarms queue by alarm number. Manual clock jumps/reconnections
  do not cause bulk catch-up. Firing history is in RAM, so a reset in a repeated
  hour can repeat a daily alarm; it is not claimed to survive power loss.
- Snooze uses elapsed milliseconds, independent of RTC edits and DST. The OLED
  reports the snooze limit immediately. Ring length is also elapsed time.
- Home shows the next enabled alarm, and distinguishes Clock unavailable from
  Clock not set. Each second checks RTC transfers, oscillator status, BCD and
  calendar ranges. Reconnection recovers automatically; no compile-time seeding.
- Sound and light previews preserve the alarm draft on detected driver failure.
  LEDC return values and OLED I²C acknowledgement are checked. There is no acoustic/optical feedback sensor:
  an unplugged or silent device cannot be diagnosed by these software checks.
- DST edits stay in a draft until Save. Brightness previews apply immediately
  but cannot leak into another settings save. NVS writes check return values and
  reopen/read back the stored blob before reporting success.
- DST choices are Automatic / +1 hour / −1 hour / Off, followed by Save.
  Signed choices are relative one-hour adjustments to the displayed clock,
  applied once on Save in manual mode; Off restores standard time without an
  offset. Existing saved offsets are retained until explicitly changed.
  Brightness Back now prompts
  Save / Discard / Continue editing when its draft differs from saved settings.
- Back from changed DST settings now uses the same Save / Discard / Continue
  prompt, including pending relative hour adjustments. Alarm light patterns
  have moved from the board RGB LED to the monochrome OLED: Slow flash, Fast
  flash, Fade in. Flashing inverts the drawn screen, retaining readable controls;
  stopping restores normal rendering and brightness. The board LED is cleared
  at boot and has no alarm/preview pattern output.
- Snooze delay is restricted to 5–15 minutes, adjusted one minute per encoder
  step and clamped at both ends. Older out-of-range saved delays are corrected
  to the nearest bound during load and saved with readback verification. Valid
  delays and the other alarm fields are retained.
- Ring for accepts 0–60 minutes; 0 means indefinite, with Snooze and dismissal
  still available. Max snoozes accepts 0–10; 0 disables snoozing, and an eleventh
  request is refused when the maximum is 10.
- An unset clock's Time/Date draft starts at 2026-01-01 instead of 2000-01-01.
  A valid clock retains its current local date as the editor's starting value.
  The proposed date reaches the RTC only through the existing explicit Save.
- Snooze fires once on the debounced down edge, with no hold/release action.
  Holding or pressing again never dismisses, disables, cancels, or extends a
  snoozed alarm. Back remains dismissal while ringing. Snoozing shows live
  minutes:seconds for all pending snoozes; Knob/Back returns to the underlying
  screen with drafts intact. Home shows the earliest snooze's countdown, even
  if the clock is unset. Remaining time uses the monotonic deadline, rounded up.
- Home's former Next-alarm row now lists every enabled alarm in alarm-number
  order. Entries are centered in one, two, or three equal columns; all three
  use a compact font to fit full am/pm suffixes. The global 12/24 format applies.
  With a pending snooze, the existing countdown takes priority on that row.
- The encoder uses continuous PCNT accumulation, avoiding the read/clear race.
  Buttons held during boot must release before generating an event.
- ADC attenuation uses ADC_ATTEN_DB_12 converted to the installed Arduino API's
  attenuation type. The first ADC conversion still precedes attenuation setup.

## Verification boundary

See BENCH-RESULTS.md for measured results. Mock backend checks exercise save
errors and DST without touching RTC/NVS. Framebuffer captures prove generated
pixel layouts, not the physical panel's contrast or wiring. Human checks remain
necessary for encoder detents, switches, audible sound, visible RGB output,
LDR calibration, and RTC coin-cell retention after actual USB power removal.
