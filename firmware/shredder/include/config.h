// Shredder compile-time settings. Runtime rules (delays, volume) come from the web config
// (ConfigShredder, stored in NVS); tile::defaultShredderConfig() is used until one arrives.
#pragma once

#include <TileProtocol.h>

constexpr uint16_t SHREDDER_FW = tile::fwEncode(0, 2, 1);

constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t SWITCH_SETTLE_MS = 60;         // 3-way switch must be stable this long
// Power-up interlock: after every boot the switch must read OFF *continuously* this long before
// the shredder unlocks. A single first reading is not trusted: on the real board it read OFF for
// an instant at power-up while the switch was on AUTO, which bypassed the interlock.
constexpr uint32_t INTERLOCK_OFF_HOLD_MS = 1000;
constexpr uint32_t FRAME_MS = 80;                 // OLED refresh (~12 fps; one frame ~25 ms of I2C)
constexpr uint32_t RUNNING_TICK_MS = 4000;        // quiet reminder beep while the motor runs
constexpr uint32_t IDENTIFY_MS = 3000;
constexpr uint32_t RESULT_FLASH_MS = 1500;        // "cancelled"/"timeout" message time on the OLED
constexpr uint8_t BUZZER_LEDC_CHANNEL = 0;
