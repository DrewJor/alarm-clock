// Definitive test: does an internal pull survive on a pin in ADC mode?
// Nothing is connected to GPIO4, so a working pull-down must read near 0
// and a working pull-up near full scale.
#include <Arduino.h>
#include "driver/rtc_io.h"
#include "driver/gpio.h"
#define LDR 4
static uint16_t avg(){ uint32_t a=0; for(int i=0;i<24;i++) a+=analogRead(LDR); return a/24; }
void setup(){
  Serial.begin(115200); delay(1500);
  Serial.println("\n=== can an internal pull act as the divider leg? ===");
  Serial.println("(nothing wired to GPIO4: pull-down should read ~0, pull-up ~4095)\n");

  pinMode(LDR, INPUT_PULLDOWN); delay(10);
  Serial.printf("  digital pull-down, digitalRead .......... %d\n", digitalRead(LDR));

  (void)analogRead(LDR); analogSetPinAttenuation(LDR, ADC_11db); (void)analogRead(LDR);
  Serial.printf("  after ADC init, no pull ................. %u\n", avg());

  rtc_gpio_pullup_dis((gpio_num_t)LDR); rtc_gpio_pulldown_en((gpio_num_t)LDR); delay(10);
  Serial.printf("  rtc_gpio_pulldown_en .................... %u\n", avg());

  rtc_gpio_pulldown_dis((gpio_num_t)LDR); rtc_gpio_pullup_en((gpio_num_t)LDR); delay(10);
  Serial.printf("  rtc_gpio_pullup_en ...................... %u\n", avg());

  rtc_gpio_pullup_dis((gpio_num_t)LDR);
  gpio_pulldown_en((gpio_num_t)LDR); delay(10);
  Serial.printf("  gpio_pulldown_en (digital-domain call) .. %u\n", avg());

  gpio_pulldown_dis((gpio_num_t)LDR);
  pinMode(LDR, INPUT_PULLDOWN); delay(10);
  Serial.printf("  pinMode(INPUT_PULLDOWN) then read ....... %u\n", avg());
  Serial.println("\n=== end ===");
}
void loop(){ delay(1000); }
