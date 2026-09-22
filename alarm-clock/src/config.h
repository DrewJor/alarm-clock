#pragma once

// EE 4953 Alarm Clock - hardware configuration
// ESP32-S3-DevKitC-1 (WROOM-1 N16R8)
//
// All signals use header J1 (back), tapped from row j.
// The front header J3 is unused. Hole numbers refer to the breadboard
// layout in the hardware design document.

// I2C bus 0 : SSD1306 display
#define PIN_OLED_SDA 8 // j11
#define PIN_OLED_SCL 9 // j8
#define OLED_ADDR 0x3C // a few modules are 0x3D - check with a scan
#define OLED_HZ 400000

// RTC: DS3231 on a separate I2C bus
#define PIN_RTC_SDA 10 // j7
#define PIN_RTC_SCL 11 // j6
#define RTC_ADDR 0x68
#define EEPROM_ADDR 0x57 // AT24C32 on the same module, unused
#define RTC_HZ 400000

// NOTE: the DS3231 SQW output is NOT wired. The once-per-second tick is a
// software timer (see TICK_MS), not a hardware interrupt.

// EC11 rotary encoder
#define PIN_ENC_A 15            // j15
#define PIN_ENC_B 16            // j14
#define PIN_ENC_SW 12           // j5
#define ENC_GLITCH_NS 1000      // PCNT hardware filter: rejects contact bounce
#define ENC_COUNTS_PER_DETENT 4 // 2 PCNT channels = 4x decoding

// Buttons (active LOW, internal pull-ups)
// GPIO17's former Alarm button has been removed; leave that input unused.
#define PIN_BTN_SNOOZE 18 // j12 - button immediately left of OLED; snooze while ringing
#define PIN_BTN_BACK 14   // j3  - back one screen; Stop while ringing

// Passive buzzer
#define PIN_BUZZER 13 // j4
#define BUZZ_RES_BITS 10
#define BUZZ_DUTY 512 // 50% of 2^10

// Output enable does not verify the buzzer load or drive circuit.
#define BUZZ_IS_LOW_IMPEDANCE true
#define BUZZ_OUTPUT_ENABLED true

// Photoresistor
#define PIN_LDR 4 // j19, ADC1_3

// Wired 3V3 -> LDR -> GPIO4, with the chip's INTERNAL PULL-DOWN as the
// lower leg. There is no external resistor. Brighter light reads HIGHER.
// Calibrate thresholds from dark, room-light and bright-light readings.
#define LDR_THRESH_HI 1600 // above this -> brightness level 3
#define LDR_THRESH_LO 500  // below this -> brightness level 1
#define LDR_HYSTERESIS 120 // stops the level flickering at a boundary

// On-board RGB LED
// Wired on the PCB. Cleared once at boot; alarm lighting uses the OLED.
#define PIN_RGB_LED 48 // silkscreen RGB@IO48 on this board revision

// Timing
#define TICK_MS 1000 // clock tick
#define DEBOUNCE_MS 30
#define HOLD_MS 600 // press vs hold threshold
#define UI_REFRESH_MS 100

// Editor value ranges
#define YEAR_MIN 2000
#define YEAR_MAX 2099      // clamp at the limits, do not wrap
#define SNOOZE_DELAY_MIN 5 // minutes; one-minute encoder steps
#define SNOOZE_DELAY_MAX 15
#define SNOOZE_COUNT_MIN 0 // 0 = snoozing is not allowed on this alarm
#define SNOOZE_COUNT_MAX 10
#define ALARM_LEN_MIN 0 // 0 = indefinite; otherwise minutes before stopping
#define ALARM_LEN_MAX 60

// Run calendar, scheduler, save, UI and alarm runtime checks at boot.
#define SELFTEST_RULES 1

// Log physical and injected input events.
#define LOG_INPUT 1

// UI timing
#define TEST_MS 4000 // how long a sound / light preview runs
#define MSG_MS 1600  // how long a confirmation message stays up

// Local USB bench controls: status, frame, cw, ccw, select, back, alarm,
// snooze, stop, ldr, sound N, light N. No clock/settings backdoor.
#define BENCH_SERIAL 1
