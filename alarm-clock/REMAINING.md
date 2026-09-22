# Remaining work — 2026-09-20

The original checklist is preserved in `docs/REMAINING-original.md`.
Firmware and supporting documentation have been completed; the remaining work
requires physical access or team decisions. Do not equate a successful build
or framebuffer capture with an electrically finished alarm clock.

## Hardware still blocking acceptance

1. **Verify the remaining buttons.** The Snooze button immediately left of the
   OLED uses GPIO18; the former Alarm button on GPIO17 has been removed and is
   no longer polled or diagnosed. In the latest user-assisted tap test, GPIO18
   remained LOW and no press/release event was seen. GPIO14 and the encoder
   switch were HIGH. Later automated checks found GPIO18 HIGH/released, but a
   deliberate physical press/release has not yet been verified.
   Disconnect power and check continuity:
   signal-to-ground must be open when released and closed only when pressed.
   The original suspected cause is use of the internally common switch legs.
   Confirm each re-wired input goes HIGH when released and generates one press.
2. **Verify/wire encoder A=15, B=16, SW=12.** Floating pins alone cannot distinguish
   an unwired encoder from open contacts. Confirm one event per detent in each
   direction and Select on release. Encoder navigation has been exercised through
   equivalent serial events, not the physical knob.
3. **Wire and calibrate the LDR.** Earlier raw readings were near zero; the latest home-selector test reports
   784–1006. Light response still needs verification.
   Topology: 3V3 → LDR → GPIO4; internal pull-down is retained. Collect dark,
   room-light and torch samples using `ldr`, then set LO/HI/hysteresis from actual
   separated ranges. The current 500/1600 thresholds are provisional.
4. **Resolve the buzzer load.** The diagram connects the reported low-resistance
   electromagnetic buzzer directly to GPIO13. GPIO_DRIVE_CAP_0 is not proof of
   safe current or inductive protection. Output is enabled at the user's explicit request for testing
   (`BUZZ_OUTPUT_ENABLED=true`); this does not certify the load. Identify and
   verify an appropriate drive circuit or GPIO-compatible transducer. This may require
   revisiting the kit-only/no-extra-parts restriction.
5. **Set/check actual local time and test battery retention.** The RTC now reports
   valid time again; earlier boots reported oscillator stopped and `Clock not set`.
   Check Time/Date against a reference, unplug USB, wait, reconnect and
   confirm the elapsed time. An MCU reset does not prove the CR2032 circuit.
6. **Physical acceptance:** OLED visibility/contrast; encoder/buttons; audible
   sound patterns; visible OLED flash/fade patterns; LDR auto brightness; real scheduled
   alarm/snooze/dismiss/timeout and coin-cell retention. Software checks cover
   these state transitions but cannot establish sensory/electrical behavior.

## Team/submission decisions

- Confirm the implemented Chart 7 reading: leaving Time/Date retains drafts;
  leaving the Set time/date session raises the unsaved-changes prompt.
- Confirm spring-gap alarms fire at the transition and repeated-hour alarms fire
  once per date. The implementation uses that policy; firing history resets on
  reboot. Request IDs are likewise scoped to the current boot.
- Fold `docs/MILESTONE-03-AMENDMENTS.md` into the submission: all eight moved
  pins, LDR topology, buzzer correction, added Enabled/Save/Cancel rows, no
  compile-time clock seeding, and the completed save protocol. The original
  PDF has been preserved and has not been reissued as a revised submission.

## Original software checklist: implemented

- B6–B8: asynchronous IDs, changed groups, deduplication, busy/status,
  two-second query policy, Saving screen, explicit review with a new ID.
- B9–B10: sound/lighting driver errors and unchanged selections; physical output
  failure cannot be sensed without feedback hardware.
- B11: DST scheduler notification, skipped-hour catch-up, duplicate suppression,
  queued simultaneous alarms, elapsed-time snoozes.
- B12: checked RTC reads every second, loss/recovery, distinct unavailable/unset.
- C13–C15: immediate snooze-limit feedback, next alarm on Home, ADC_ATTEN_DB_12.
- Additional fixes: format-only saves no longer rewind time; independent date/time
  groups preserve untouched live fields; DST drafts do not alter alarms before
  Save; durable-save verification; brightness preview isolation; preview timer
  underflow; menu-stack correction; encoder read/clear race; boot-held buttons.

See `docs/BENCH-RESULTS.md` and `artifacts/` for actual test evidence.
