#include <assert.h>
#include <stdio.h>
#include "../src/brightness.h"

int main() {
  // Large changes that land in the opposite boundary's dead band used to
  // leave the display stuck at its old extreme indefinitely.
  assert(brightness_from_light(LDR_THRESH_HI, 1) == 2);
  assert(brightness_from_light(LDR_THRESH_LO, 3) == 2);
  assert(brightness_from_light(0, 3) == 1);
  assert(brightness_from_light(4095, 1) == 3);
  // Sweeps must reach all three levels in order in both directions.
  uint8_t level = 1;
  unsigned seen = 0;
  for (int raw = 0; raw <= 4095; ++raw) {
    uint8_t next = brightness_from_light(raw, level);
    assert(next >= level && next <= 3);
    level = next;
    seen |= 1u << level;
  }
  assert(seen == 14 && level == 3);
  seen = 0;
  for (int raw = 4095; raw >= 0; --raw) {
    uint8_t next = brightness_from_light(raw, level);
    assert(next <= level && next >= 1);
    level = next;
    seen |= 1u << level;
  }
  assert(seen == 14 && level == 1);
  for (int delta = -LDR_HYSTERESIS; delta <= LDR_HYSTERESIS; ++delta) {
    assert(brightness_from_light(LDR_THRESH_LO + delta, 1) == 1);
    assert(brightness_from_light(LDR_THRESH_LO + delta, 2) == 2);
    assert(brightness_from_light(LDR_THRESH_HI + delta, 2) == 2);
    assert(brightness_from_light(LDR_THRESH_HI + delta, 3) == 3);
  }
  puts("Brightness: sudden changes, full ADC sweeps, and hysteresis checks passed");
}
