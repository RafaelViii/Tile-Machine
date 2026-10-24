// Containing pin map — mirrors docs/HARDWARE.md (esp2).
#pragma once

#include <Arduino.h>

// HX711 load-cell amplifiers, one per container (C1 Raw HDPE, C2 Raw PP, C3 Mixed HDPE, C4 Mixed PP).
// DT pins 34-39 are input-only with no pull-ups: a missing HX711 leaves its DT floating (hx711.cpp checks).
constexpr uint8_t PIN_HX_DT[4] = {34, 35, 36, 39};
constexpr uint8_t PIN_HX_SCK[4] = {16, 17, 18, 19};

constexpr uint8_t PIN_SELECT1 = 32;   // C1 Raw HDPE kg select, momentary to GND
constexpr uint8_t PIN_CONFIRM1 = 33;  // C1 confirm / cancel
constexpr uint8_t PIN_SELECT2 = 25;   // C2 Raw PP kg select
constexpr uint8_t PIN_CONFIRM2 = 26;  // C2 confirm / cancel
constexpr uint8_t PIN_SEL_LEFT = 27;  // 3-way selector, LOW = LEFT (C3 Mixed HDPE)
constexpr uint8_t PIN_SEL_RIGHT = 14; // LOW = RIGHT (C4 Mixed PP); neither = NEUTRAL (stop)

constexpr uint8_t PIN_PCA_OE = 23;    // PCA9685 output enable, active LOW (10 kOhm pull-up: disabled during boot)
constexpr uint8_t PIN_I2C_SDA = 21;   // SH1106 OLED (0x3C) + PCA9685 (0x40)
constexpr uint8_t PIN_I2C_SCL = 22;
constexpr uint8_t PIN_STATUS_LED = 2; // on-board LED: solid = paired with hub, blink = searching

constexpr uint8_t OLED_I2C_ADDR = 0x3C;
constexpr uint8_t PCA9685_ADDR = 0x40;
