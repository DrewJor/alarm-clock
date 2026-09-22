# EE 4953 Alarm Clock — everything still missing

Paste the block below into a fresh session to pick this up.

---

I'm finishing an EE 4953 Capstone alarm clock (team: Jordan, Sadowski, Spath,
Vanderbilt). The firmware is at `~/Desktop/Coding Projects/Alarm Clock/alarm-clock`.
Milestone 03 (the spec) is at `~/Downloads/Milestone 3 - Alarm Clock Project-2.pdf`.
There is no `pdftotext` on this machine — extract PDF text/images with a pure-Python
zlib script if you need to re-read it.

## Hardware

ESP32-S3-DevKitC-1, WROOM-1 **N16R8** (16 MB flash, **octal** PSRAM). One 830-point
breadboard, kit parts only, no added resistors. Every signal is on header **J1**
(the BACK header, tapped from row j); header J3 is unused.

| Signal | GPIO | Notes |
|---|---|---|
| OLED SDA / SCL | 8 / 9 | `Wire`, SSD1306 @ 0x3C |
| RTC SDA / SCL | 10 / 11 | `Wire1`, DS3231 @ 0x68 + AT24C32 @ 0x57 |
| Encoder A / B / SW | 15 / 16 / 12 | EC11, PCNT hardware decode |
| Alarm / Snooze / Back buttons | 17 / 18 / 14 | active low, internal pull-ups |
| Buzzer | 13 | MEASURED electromagnetic (few ohms), runs at `GPIO_DRIVE_CAP_0` |
| Photoresistor | 4 | 3V3 → LDR → GPIO4, internal pull-down is the lower leg |
| RGB LED | 48 | on-board, no jumper |

Two independent I²C buses, per the milestone pin tables. DS3231 SQW is **not**
wired — the 1 Hz tick is a `millis()` timer.

## Toolchain

pioarduino platform 55.03.312 (NOT registry `espressif32` — that ships Arduino
core 2.x, where `driver/pulse_cnt.h` and `ledcAttach()` don't exist). Arduino core
3.3.12 / ESP-IDF 5.5.5. `board_build.arduino.memory_type = qio_opi`.

PlatformIO lives at `"…/Alarm Clock/.piovenv/bin/pio"`. Serial is
`/dev/cu.usbmodem5C930434691` (CH343 bridge = the **UART** socket, the correct one).

`pio run -t upload` is broken by an esptool/esp_pylib version clash. Workaround:

```bash
~/.platformio/penv/bin/python -m esptool --silent --chip esp32s3 \
  --port /dev/cu.usbmodem5C930434691 --baud 460800 \
  write-flash 0x0 .pio/build/esp32-s3-devkitc-1/firmware.factory.bin
```

Note: that factory image spans 0x0 upward and **wipes NVS**, so saved settings are
lost on every full reflash. A plain reset keeps them.

`pyserial` is only in `~/.platformio/penv`, not system python3.

## Current state

Builds with no warnings (RAM 8.0%, Flash 12.4%), flashes, boots, runs for minutes
without a reset. All 14 Milestone 03 software sections are implemented across
`src/{main,ui,settings,input}.cpp` + `src/{config,hw,ui,settings,input}.h`.
`ui.cpp` never touches a pin — it goes through `hw.h`.

`SELFTEST_RULES` in `config.h` runs 25 on-chip assertions over leap years, month
lengths, weekdays and the US DST changeovers for 2024–2026. All pass.

Every boot prints a wiring report, an I²C scan, an LDR reading and the self-test
result. `LOG_INPUT` prints every input event.

**Measured facts that contradict the obvious choice — do not "fix" these:**
- The LDR pull-down must be `gpio_pulldown_en()`. `rtc_gpio_pulldown_en()` reads
  4095 (ignored once the pad is analog); `pinMode(INPUT_PULLDOWN)` reads 4055;
  `gpio_pulldown_en()` reads 13.
- `analogSetPinAttenuation()` must come **after** the first `analogRead()`, because
  that read is what attaches the pad to an ADC channel.
- `wiring_report()` must run **before** `Wire.begin()` — it reconfigures GPIO8–11.

# WHAT IS MISSING

## A. Bench work — blocking, nothing below can be tested without it

1. **Three tactile switches are shorted to ground at rest** (GPIO14/17/18, measured
   2–6 Ω). They're rotated 90°: a 4-leg switch joins its two legs on each side
   internally, so signal and ground landed on an already-connected pair. Turn each
   a quarter turn so the joined pair straddles the trench. Confirm the boot report
   shows `floating` for all three.
2. **Encoder is unwired** (GPIO15 A, 16 B, 12 SW). The encoder press is Select, so
   nothing can be navigated until this exists.
3. **Photoresistor is unwired** (GPIO4). Reads 0–5 now.
4. **Calibrate `LDR_THRESH_HI` / `LDR_THRESH_LO`** in `config.h` from readings taken
   in the dark, in room light, and under a phone torch.
5. **Set the clock** via Main menu → Set time/date, then power-cycle and confirm it
   survived. That also proves the CR2032.

## B. Spec requirements not implemented

6. **Chart 8 asynchronous save protocol.** Currently a synchronous verified write.
   The spec asks for a unique request ID, changed-group flags, accepted/busy/
   success/error/review-required statuses, owner-side ID deduplication, and a
   2-second timeout that **queries the existing request ID rather than repeating
   the write**. Decide with the team whether this is required for a local RTC or
   whether the synchronous write is an acceptable documented simplification.
7. **Chart 8 "SHOW SAVING" state.** No visible saving screen — the write is instant.
8. **Chart 8 "Review required"** status and its confirm-with-a-new-ID flow.
9. **Chart 11 "Sound Playback Error"** — detect a failed tone, notify the user,
   leave the saved sound unchanged.
10. **Chart 12 "Lighting Hardware Error"** — same for the RGB LED.
11. **Section 14: DST must notify the alarm system.** This is the real bug of the
    group. `s_dst_off` is recomputed each tick but nothing tells the alarm code.
    Consequences today:
    - **Spring forward:** an alarm set inside the skipped hour never fires.
    - **Fall back:** the repeated hour can fire an alarm **twice**, because
      `check_alarms()` guards only on `static int8_t last_min`.
    Fix by keying "already fired" on a date+time stamp rather than the minute
    alone, and by defining what a skipped-hour alarm should do (fire at the new
    local time is the usual choice).
12. **Chart 1 "Clock unavailable" vs "Set time/date".** These are one message now.
    Also `s_rtc_ok` is sampled once in `setup()`, so if the DS3231 is unplugged at
    runtime `rtc.now()` returns garbage and Home keeps drawing it. Re-check the bus
    periodically and show "Clock unavailable".

## C. Polish

13. A refused snooze (max count reached) only logs to serial. Put it on the OLED at
    the moment of the press.
14. Home doesn't show the next alarm time — only per-alarm on/off markers.
15. `ADC_11db` is deprecated; it becomes `ADC_ATTEN_DB_12` on a newer core.

## D. Never tested — all blocked on section A

16. Any menu navigation at all.
17. Alarm fire / snooze / dismiss / length timeout.
18. Buzzer audible output and the three sound patterns.
19. The three RGB light patterns on GPIO48.
20. Auto-brightness tracking the LDR.
21. Clock persistence across a power cycle.
22. **OLED rendering has never been looked at.** Every layout coordinate in
    `ui.cpp` is arithmetic I did on paper — column positions, the scroll window,
    the highlight boxes, the inverse-video field editor. Expect to nudge them.

## E. Milestone 03 document — must match the board before submission

23. **Eight pin assignments changed** when everything moved to header J1. The
    tables still say: RTC SCL 48, RTC SDA 45, Encoder A 21, Encoder B 20, Back 47,
    Snooze 39, Alarm 0, Photoresistor 17. The build uses 11, 10, 15, 16, 14, 18,
    17, 4 respectively.
24. The document calls it a **"Piezo Buzzer"**. It measured a few ohms to ground,
    so it is electromagnetic, and the pad is held at its weakest drive strength to
    stay inside the pin's current limit. The document's own note about verifying
    the load before connecting has been satisfied — record the result.
25. The photoresistor **topology changed**. The document has GPIO17 + ground; the
    build is 3V3 → LDR → GPIO4 with the chip's internal pull-down as the lower leg
    and no external resistor. Brighter light reads *higher*.
26. Record that the alarm menus gained **Enabled / Save / Cancel** rows. The
    flowchart shows four options and a save prompt but no way to arm an alarm.
27. Record the **chart 7 scope decision**: the unsaved-changes prompt fires when
    leaving the Set time/date submenu, not the Time or Date screens. Chart 7's prose
    says it covers all three, but charts 4 and 5 both say Back from those lists
    returns to the submenu *with the draft retained*, which prompting would
    contradict. **Confirm which reading the team wants** — it's one call to
    `td_request_leave()` per case to switch.
28. Record that the clock is no longer seeded from compile time. Chart 1 forbids a
    fabricated time; the DS3231 oscillator-stop flag now decides validity.

# How to work

Verify on the board, don't assume — that's how the pull-down, the attenuation
ordering, the octal PSRAM and the shorted buttons were all caught. Build, flash,
reset, and read the serial log. Tell me what the board actually reported.
