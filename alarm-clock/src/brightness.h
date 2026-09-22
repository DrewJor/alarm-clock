#pragma once
#include <stdint.h>
#include "config.h"

// Each boundary remembers only the adjacent level. A sudden light change
// must not leave Low stuck in the upper dead band (or High in the lower).
inline uint8_t brightness_from_light(uint16_t raw, uint8_t previous) {
  if (previous == 1) {
    if (raw > LDR_THRESH_HI + LDR_HYSTERESIS) return 3;
    return raw > LDR_THRESH_LO + LDR_HYSTERESIS ? 2 : 1;
  }
  if (previous == 3) {
    if (raw < LDR_THRESH_LO - LDR_HYSTERESIS) return 1;
    return raw < LDR_THRESH_HI - LDR_HYSTERESIS ? 2 : 3;
  }
  if (raw < LDR_THRESH_LO - LDR_HYSTERESIS) return 1;
  if (raw > LDR_THRESH_HI + LDR_HYSTERESIS) return 3;
  return 2;
}
