// PCA9685 16-channel PWM driver for the 8 MG995 continuous-rotation servos (50 Hz), on the shared I2C bus.
// Minimal in-house driver: ok() turns false on any I2C error, and begin() can be called again later (hot-plug).
#pragma once

#include <Arduino.h>

class Pca9685 {
 public:
  /** Probe, set 50 Hz and turn every channel fully off. True if the chip answered. */
  bool begin(uint8_t addr);
  bool ok() const { return ok_; }
  /** Servo pulse of `us` microseconds on channel `ch` (0..15). */
  void pulseUs(uint8_t ch, uint16_t us);
  /** No pulse at all on `ch` (a continuous servo then can't creep). */
  void off(uint8_t ch);
  void allOff();

 private:
  bool write(uint8_t reg, const uint8_t* data, uint8_t n);
  uint8_t addr_ = 0x40;
  bool ok_ = false;
};
