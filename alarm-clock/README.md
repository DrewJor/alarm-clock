# EE 4953 Alarm Clock

ESP32-S3-DevKitC-1, WROOM-1 N16R8 firmware with three alarms, OLED menus,
DS3231 standard-time clock, US automatic/manual DST, snooze, OLED light patterns and
ambient brightness. External signals use J1; OLED and RTC use separate I²C buses.

**Firmware is implemented; physical acceptance is still pending.** See
[REMAINING.md](REMAINING.md) for the remaining wiring, calibration and load checks,
[bench results](docs/BENCH-RESULTS.md) for measured evidence, and
[milestone amendments](docs/MILESTONE-03-AMENDMENTS.md) for submission changes.

## Build and upload

Use the existing `../.piovenv/bin/pio` environment. The pinned pioarduino platform
uses Arduino core 3.3.12 / IDF 5.5.5; registry espressif32 core 2.x is incompatible.
The N16R8 requires `qio_opi` (octal PSRAM). Libraries resolve to U8g2 2.36.18 and
RTClib 2.1.4. The app currently uses the original 8 MB partition map within the
16 MB physical flash, leaving the upper half unused.

```sh
../.piovenv/bin/pio run
```

The connected CH343 UART bridge is `/dev/cu.usbmodem5C930434691`. Use the board's
UART socket, 115200 baud for logging. Native USB is not the console.

The installed esptool/esp_pylib versions conflict in the progress logger. Upload
with the working interpreter and `--silent`, using **firmware.bin at 0x10000**:

```sh
~/.platformio/penv/bin/python -m esptool --silent --chip esp32s3 \
  --port /dev/cu.usbmodem5C930434691 --baud 460800 \
  write-flash 0x10000 .pio/build/esp32-s3-devkitc-1/firmware.bin
```

That app offset was verified against the connected board's partition table. If
changing partitions or using a different board, verify the map again. Do not
upload firmware.factory.bin over an existing installation: its gaps overwrite
NVS. The original partition/NVS backup is `artifacts/board-config-before.bin`,
covering 0x8000–0xffff. Keep it local; it is a recovery backup, not a normal upload.

## Pin map

| Function | GPIO |
|---|---|
| OLED SDA / SCL | 8 / 9 |
| RTC SDA / SCL | 10 / 11 |
| Encoder A / B / Select | 15 / 16 / 12 |
| Snooze (immediately left of OLED) / Back (right of encoder) | 18 / 14 |
| Buzzer control | 13 |
| Photoresistor | 4 |
| On-board RGB (cleared at boot; unused for alarms) | 48 |

The LDR is 3V3 → LDR → GPIO4, using the internal digital pull-down. Preserve the
first ADC conversion before attenuation configuration and gpio_pulldown_en().
Run wiring_report() before starting Wire/Wire1, because it reconfigures pins.

**Buzzer:** output is now enabled at the user's explicit request for sound
testing (`BUZZ_OUTPUT_ENABLED=true`). The low-impedance setting and weakest
GPIO drive are retained. This is an output switch, not a certification of the
direct-connected buzzer's electrical suitability. The earlier load/driver
concern remains unresolved. Use an alarm's Sound → Test option for a bounded
four-second preview; Back stops it early. No sound files are needed.

## Controls

- On Home, turn the encoder between Alarm 1, 2, 3 and Menu; press to open the selected alarm or settings. Menu is selected at startup.
- Inside menus, turn the encoder to select a row; press to enter/edit/confirm.
- Back: cancel a field edit or return; unsaved sessions show Save/Discard/Continue.
- The button immediately left of the OLED is Snooze. The former Alarm button
  farther left has been removed; GPIO17 is no longer read or checked. Use the
  Home alarm icons or Menu → Set alarm to open alarms.
- While ringing: Snooze delays it immediately on the debounced press; Back
  dismisses it. Holding Snooze never dismisses or disables an alarm.
- Each alarm's snooze delay is 5–15 minutes in one-minute steps. The encoder
  stops at either limit. Older saved delays outside that range are clamped to
  the nearest limit; valid saved delays are retained.
- Ring for accepts 0–60 minutes. Zero is displayed as **indefinite** and rings
  until snoozed or dismissed; positive values stop after the selected duration.
- Max snoozes accepts 0–10. Zero disables snoozing; after the selected number
  of snoozes, further requests are refused and the alarm keeps ringing.
- Snoozing opens a live minutes:seconds countdown for each waiting alarm.
  Knob or Back returns to the previous screen without cancelling the snooze
  or discarding menu drafts. Press Snooze again to reopen the countdown;
  repeated presses do not extend it. Disabling an alarm cancels its snooze.
- Alarms must be enabled and explicitly saved. Multiple due alarms queue.
- Save & exit returns to the clock with Menu selected. The alarm list also has
  a Back to clock row, so the encoder can leave it without the Back button.
- First clock setup requires visiting both Time and Date before Save.
- Saving the 12/24-hour format applies to every alarm, including existing and
  disabled alarms. Only the displayed format changes: 7:00 PM becomes 19:00,
  midnight becomes 00:00, and each alarm still rings at the same time.
- Brightness previews live. Back with changes asks Save / Discard / Continue
  editing; unchanged settings return immediately. The preview stays active
  while the prompt is open, and Discard restores the saved brightness.
  Automatic brightness uses stronger dimming: OLED contrast 0 / 40 / 255 for
  low / medium / high ambient light. Manual levels remain 8 / 110 / 255.
- DST offers Automatic, +1 hour, −1 hour, Off, then Save. Automatic uses US
  rules; Off uses standard time with no offset. The signed choices adjust the
  currently displayed time by exactly one hour and select manual mode on Save.
  Choosing an adjustment repeatedly before saving still applies it only once;
  Back with changes prompts Save / Discard / Continue editing. Existing offsets
  survive an update. Manual changes do not
  trigger alarms skipped over by the clock adjustment.
- Alarm lighting uses the screen: Slow flash, Fast flash, and Fade in. Flashing
  alternates normal and inverted text so the controls stay readable. Fade in
  raises the contrast of an inverted screen over 60 seconds. Light → Test gives
  a four-second preview. Stop, snooze, and timeout restore normal screen
  appearance and the current automatic/manual brightness; the board LED stays off.

Home shows the earliest snooze countdown when one is pending, otherwise every
enabled alarm's number/time in equal-width centered columns (one, two, or three).
Labels use full lowercase am/pm in 12-hour mode, or 24-hour times. Three alarms
use a compact font so the whole row fits. Disabled alarms are omitted from this
row; no enabled alarms shows "No alarms set". It also shows three
selectable bell icons, and a Menu button. Unset alarms use sparse, dulled bell
icons; enabled alarms use solid bells. An outline shows encoder focus. `DST`
appears beside the date when a DST/manual offset is active. A missing RTC shows Clock
unavailable; an invalid/unset RTC asks to be set. Reconnection is retried every
second. The clock is never seeded from compile time.

## Save and alarm policies

Clock saves use a cooperative asynchronous request owner. Changed time, date and
format groups have explicit IDs and status, verified RTC writes and durable NVS
readback. Accepted requests cannot be cancelled; the UI queries the existing ID
after two seconds instead of repeating the write. Partial failures retain the
unsaved draft and commit only successful originals. Format-only saves do not
write the RTC. Date-only edits preserve live time; time-only edits preserve date.

The RTC holds standard time. Spring-gap alarms fire at the transition; fall-back
alarms fire once per local calendar date. Fired history and snooze sessions are
RAM state and do not survive reset. Clock edits/reconnection do not replay whole
missed intervals. Setting an ambiguous local clock time requires review: a gap
proposes moving forward an hour; an overlap proposes the standard occurrence.

LEDC return values and the OLED's I²C ACK detect some driver/connection errors,
not inaudible sound or an unreadable panel. Actual sound and visible screen
effects must be checked on the bench.

## Diagnostics and verification

`SELFTEST_RULES=1` enables boot checks with mock RTC/NVS callbacks plus temporary
RAM-only alarm runtime checks. They restore settings and do not alter the real
RTC or NVS. `LOG_INPUT=1` logs physical and injected events. `BENCH_SERIAL=1`
exposes local USB diagnostics; set it to 0 for a release without test commands.

```sh
~/.platformio/penv/bin/python tools/bench.py status
~/.platformio/penv/bin/python tools/bench.py select frame:menu
~/.platformio/penv/bin/python tools/bench.py 'light 3' wait:2 status
```

Commands: `status`, `ldr`, `frame`, `cw`, `ccw`, `select`, `back`, `holdback`,
`alarm`, `snooze`, `holdsnooze`, `stop`, `sound 1..3`, `light 1..3`, and `ring-test 1..3`.
The client also accepts `wait:seconds` and `frame:filename` (writes PBM). Opening
the CH343 port may reset the board. Keep a single client open for a test sequence.
Serial UI commands perform the same actions as the buttons, including Save.
`holdsnooze` exercises the legacy hold event's safe snooze behavior. `stop` is
explicit bench cleanup: dismiss ringing, cancel pending snoozes, stop previews.
`frame:filename:home-preview MASK FORMAT` captures Home with a temporary
enabled-alarm bitmask (0–7) and format (12 or 24). It renders a settings copy
without changing saved settings, live alarm scheduling, or navigation.
`ring-test` starts the chosen alarm using its configured settings; it does not
prove that the physical button, buzzer or clock-based trigger works.

The 4-second light preview compresses the fade's 60-second brightness ramp.
Real ringing uses the full ramp. Settings changes that fail
storage verification are reported instead of displaying Saved.

## Source layout

| Files | Responsibility |
|---|---|
| config.h | Pin map, timing, provisional LDR thresholds, diagnostic flags |
| input.* | Continuous PCNT encoder count, debounced buttons |
| settings.* | NVS, defaults, calendar and US DST rules |
| scheduler.* | DST mapping, occurrence deduplication, next alarm |
| save.* | Request IDs, asynchronous groups/status, review/partial failure |
| main.cpp / hw.h | Checked peripherals, RTC recovery, alarm/snooze, bench commands |
| ui.* | Drafts, menus, saving/review/error screens and OLED rendering |
| selftest.* | Scheduler/save regression checks with mocked writes |
