#pragma once
#include <stdint.h>

struct ScreenEffect {
  bool inverted;
  uint8_t contrast;
};

// Invert the drawn screen instead of blanking it, keeping alarm controls
// readable in both phases. The fade uses a bright background over 60 seconds.
inline ScreenEffect screen_effect(uint8_t pattern, uint32_t elapsed) {
  if (pattern == 1) return { (elapsed / 500) % 2 == 0, 255 };
  if (pattern == 2) return { (elapsed / 250) % 2 == 0, 255 };
  if (pattern == 3) return { true, uint8_t(elapsed >= 60000 ? 255 : 8 + 247 * elapsed / 60000) };
  return { false, 0 };
}
