// Hub pin map — mirrors docs/HARDWARE.md (esp0).
#pragma once

#include <Arduino.h>

constexpr uint8_t PIN_STATUS_LED = 2;   // on-board LED
constexpr uint8_t PIN_BOOT_BUTTON = 0;  // on-board BOOT button, read at runtime only
constexpr uint8_t PIN_I2C_SDA = 21;     // DS3231 RTC (0x68) + its AT24C32 EEPROM (0x57)
constexpr uint8_t PIN_I2C_SCL = 22;
constexpr bool STATUS_LED_ACTIVE_LOW = false;
