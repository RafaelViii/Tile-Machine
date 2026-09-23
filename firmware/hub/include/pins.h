// Hub pin map — mirrors docs/HARDWARE.md (esp0).
#pragma once

#include <Arduino.h>

constexpr uint8_t PIN_STATUS_LED = 2;   // on-board LED
constexpr uint8_t PIN_BOOT_BUTTON = 0;  // on-board BOOT button, read at runtime only
constexpr bool STATUS_LED_ACTIVE_LOW = false;
