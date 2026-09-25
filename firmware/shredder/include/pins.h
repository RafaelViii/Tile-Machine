// Shredder pin map — mirrors docs/HARDWARE.md (esp1). Pins kept from the legacy sketch.
#pragma once

#include <Arduino.h>

constexpr uint8_t PIN_START = 32;      // momentary NO to GND, INPUT_PULLUP
constexpr uint8_t PIN_STOP = 33;       // momentary NO to GND (see STOP_IS_NC)
constexpr uint8_t PIN_SW_AUTO = 25;    // 3-way switch, LOW = AUTO
constexpr uint8_t PIN_SW_MANUAL = 26;  // 3-way switch, LOW = MANUAL; neither = OFF (center)
constexpr uint8_t PIN_IR = 14;         // IR obstacle sensor
constexpr uint8_t PIN_RELAY = 27;      // shredder motor relay
constexpr uint8_t PIN_BUZZER = 13;     // PASSIVE buzzer module (LEDC tones)
constexpr uint8_t PIN_I2C_SDA = 21;    // SH1106 1.3" OLED
constexpr uint8_t PIN_I2C_SCL = 22;
constexpr uint8_t PIN_STATUS_LED = 2;  // on-board LED: solid = paired with hub, blink = searching

// Polarity flags (never hard-code levels elsewhere).
constexpr bool RELAY_ACTIVE_LOW = false;   // confirmed: relay turns ON with HIGH (as in the legacy sketch)
constexpr bool IR_ACTIVE_LOW = true;       // most IR modules pull LOW on detect
constexpr bool STOP_IS_NC = false;         // set true if STOP is rewired normally-closed (fail-safe)
constexpr bool BUZZER_ACTIVE_LOW = false;  // set true if the buzzer module sounds on LOW

constexpr uint8_t OLED_I2C_ADDR = 0x3C;
