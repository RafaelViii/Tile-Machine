// Hot Press compile-time settings.
#pragma once

#include <TileProtocol.h>

constexpr uint16_t HOTPRESS_FW = tile::fwEncode(0, 1, 5);

// OLED clock only: seconds added to the hub's time before it is shown (to match the operator's watch; the
// shop's reference clock read 23 s ahead of internet time, 2026-09-27). Events and the website keep the real time.
constexpr int32_t OLED_CLOCK_ADJUST_S = 23;


constexpr uint32_t INPUT_SETTLE_MS = 60;        // initial settle only; the web config (button/selector debounce) replaces it
// Power-up interlock (safety invariant 3), per output: after boot the ON button must read OFF and
// the selector must read middle *continuously* this long before that output may turn on.
// (Lesson from the shredder: a single first reading at power-up can be wrong.)
constexpr uint32_t INTERLOCK_OFF_HOLD_MS = 1000;
constexpr uint32_t FRAME_MS = 100;
constexpr uint32_t IDENTIFY_MS = 3000;

// Fault bits reported in StatusCommon.faults (web: shared/format.ts)
constexpr uint16_t FAULT_SELECTOR_WIRING = 0x01;  // both selector contacts closed
constexpr uint16_t FAULT_OLED_MISSING = 0x02;
constexpr uint16_t FAULT_NOISE_ON_BUTTON = 0x04;  // noisy input signals (PinNoiseMonitor)
constexpr uint16_t FAULT_NOISE_SEL_LEFT = 0x08;
constexpr uint16_t FAULT_NOISE_SEL_RIGHT = 0x10;
