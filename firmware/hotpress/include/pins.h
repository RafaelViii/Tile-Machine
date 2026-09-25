// Hot Press · Designing · Curing pin map — mirrors docs/HARDWARE.md (esp3).
#pragma once

#include <Arduino.h>

constexpr uint8_t PIN_ON_BUTTON = 32;     // latching button to GND, LOW = ON -> SSR 1
constexpr uint8_t PIN_SEL_LEFT = 25;      // 3-way selector, LOW = LEFT (Hot Press heating)
constexpr uint8_t PIN_SEL_RIGHT = 26;     // 3-way selector, LOW = RIGHT (AUTO); neither = middle (cooling)
constexpr uint8_t PIN_SSR_DESIGN_CURE = 18;  // SSR-25-DA #1 input "+": Designing + Curing
constexpr uint8_t PIN_SSR_HOTPRESS = 19;     // SSR-25-DA #2 input "+": Hot Press
constexpr uint8_t PIN_I2C_SDA = 21;       // SH1106 1.3" OLED
constexpr uint8_t PIN_I2C_SCL = 22;
constexpr uint8_t PIN_STATUS_LED = 2;     // on-board LED: solid = paired with hub, blink = searching

// Polarity flags (never hard-code levels elsewhere).
constexpr bool RELAY_ACTIVE_LOW = false;  // SSR-25-DA: input HIGH = ON (confirmed by user)

constexpr uint8_t OLED_I2C_ADDR = 0x3C;
