#include <assert.h>
#include <stdio.h>
#include "../src/screen_effect.h"

int main() {
  assert(screen_effect(1, 0).inverted);
  assert(screen_effect(1, 499).inverted);
  assert(!screen_effect(1, 500).inverted);
  assert(!screen_effect(1, 999).inverted);
  assert(screen_effect(1, 1000).inverted);
  assert(screen_effect(2, 249).inverted);
  assert(!screen_effect(2, 250).inverted);
  assert(screen_effect(2, 500).inverted);
  uint8_t previous = 8;
  for (uint32_t ms = 0; ms <= 61000; ms += 100) {
    ScreenEffect fade = screen_effect(3, ms);
    assert(fade.inverted && fade.contrast >= previous);
    previous = fade.contrast;
  }
  assert(screen_effect(3, 0).contrast == 8);
  assert(screen_effect(3, 60000).contrast == 255);
  assert(screen_effect(3, UINT32_MAX).contrast == 255);
  assert(screen_effect(1, UINT32_MAX).contrast == 255);
  assert(!screen_effect(0, 0).inverted);
  puts("Screen effects: flash boundaries, fade progression and saturation passed");
}
