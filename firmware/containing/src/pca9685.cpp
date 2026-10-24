#include "pca9685.h"

#include <Wire.h>

namespace {
constexpr uint8_t REG_MODE1 = 0x00;
constexpr uint8_t REG_MODE2 = 0x01;
constexpr uint8_t REG_LED0 = 0x06;     // LEDn_ON_L at 0x06 + 4n
constexpr uint8_t REG_ALL_LED = 0xFA;  // ALL_LED_ON_L
constexpr uint8_t REG_PRESCALE = 0xFE;
constexpr uint8_t MODE1_SLEEP = 0x10;
constexpr uint8_t MODE1_AI = 0x20;     // register auto-increment
constexpr uint8_t MODE2_OUTDRV = 0x04; // totem-pole outputs
constexpr uint8_t PRESCALE_50HZ = 121; // 25 MHz / (4096 * 50 Hz) - 1
constexpr uint32_t PERIOD_US = 20000;
constexpr uint8_t FULL_OFF = 0x10;     // bit 4 of LEDn_OFF_H
}  // namespace

bool Pca9685::write(uint8_t reg, const uint8_t* data, uint8_t n) {
  Wire.beginTransmission(addr_);
  Wire.write(reg);
  Wire.write(data, n);
  if (Wire.endTransmission() != 0) ok_ = false;
  return ok_;
}

bool Pca9685::begin(uint8_t addr) {
  addr_ = addr;
  Wire.beginTransmission(addr_);
  ok_ = Wire.endTransmission() == 0;
  if (!ok_) return false;
  // The prescaler can only be set while asleep. After waking, outputs start once the oscillator runs
  // (~500 us); every channel is FULL OFF by then, so no restart sequence (and no delay) is needed.
  uint8_t b = MODE1_SLEEP | MODE1_AI;
  write(REG_MODE1, &b, 1);
  b = PRESCALE_50HZ;
  write(REG_PRESCALE, &b, 1);
  b = MODE2_OUTDRV;
  write(REG_MODE2, &b, 1);
  allOff();
  b = MODE1_AI;  // wake
  write(REG_MODE1, &b, 1);
  return ok_;
}

void Pca9685::pulseUs(uint8_t ch, uint16_t us) {
  if (!ok_ || ch > 15) return;
  const uint16_t off = (uint16_t)min<uint32_t>(4095, (uint32_t)us * 4096 / PERIOD_US);
  const uint8_t d[4] = {0, 0, (uint8_t)(off & 0xFF), (uint8_t)(off >> 8)};
  write(REG_LED0 + 4 * ch, d, 4);
}

void Pca9685::off(uint8_t ch) {
  if (!ok_ || ch > 15) return;
  const uint8_t d[4] = {0, 0, 0, FULL_OFF};
  write(REG_LED0 + 4 * ch, d, 4);
}

void Pca9685::allOff() {
  const uint8_t d[4] = {0, 0, 0, FULL_OFF};
  write(REG_ALL_LED, d, 4);
}
