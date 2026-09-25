// Shredder compile-time settings. Runtime rules (delays, volume) come from the web config
// (ConfigShredder, stored in NVS); tile::defaultShredderConfig() is used until one arrives.
#pragma once

#include <TileProtocol.h>

constexpr uint16_t SHREDDER_FW = tile::fwEncode(0, 2, 4);

constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;       // START/STOP filter at boot; web config buttonDebounceMs replaces it
constexpr uint32_t SWITCH_SETTLE_MS = 250;        // switch filter at boot; web config switchDebounceMs replaces it
// Power-up interlock: after every boot the switch must read OFF *continuously* this long before
// the shredder unlocks. A single first reading is not trusted: on the real board it read OFF for
// an instant at power-up while the switch was on AUTO, which bypassed the interlock.
constexpr uint32_t INTERLOCK_OFF_HOLD_MS = 1000;
constexpr uint32_t FRAME_MS = 80;                 // OLED refresh (~12 fps; one frame ~25 ms of I2C)
constexpr uint32_t RUNNING_TICK_MS = 4000;        // quiet reminder beep while the motor runs
constexpr uint32_t IDENTIFY_MS = 3000;
constexpr uint32_t RESULT_FLASH_MS = 1500;        // "cancelled"/"timeout" message time on the OLED
constexpr uint8_t BUZZER_LEDC_CHANNEL = 0;

// Fault bits reported in StatusCommon.faults (web: shared/format.ts, same order)
constexpr uint16_t FAULT_SWITCH_WIRING = 0x01;  // both switch contacts closed
constexpr uint16_t FAULT_OLED_MISSING = 0x02;
constexpr uint16_t FAULT_NOISE_START = 0x04;    // noisy input signals (PinNoiseMonitor)
constexpr uint16_t FAULT_NOISE_STOP = 0x08;
constexpr uint16_t FAULT_NOISE_SW_AUTO = 0x10;
constexpr uint16_t FAULT_NOISE_SW_MANUAL = 0x20;
constexpr uint16_t FAULT_NOISE_IR = 0x40;
