// Containing compile-time settings. Runtime settings (modes, time tables, servo pulses) come from the web
// config (ConfigContaining, kept in NVS). Load-cell calibration is owned by this module (NVS, set by TARE /
// CALIBRATE from the web) and is never part of config.
#pragma once

#include <TileProtocol.h>

constexpr uint16_t CONTAINING_FW = tile::fwEncode(0, 2, 0);

// OLED clock only: seconds added to the hub's time before it is shown (to match the operator's watch; the
// shop's reference clock read 23 s ahead of internet time, 2026-09-27). Events and the website keep the real time.
constexpr int32_t OLED_CLOCK_ADJUST_S = 23;

constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;    // SELECT / CONFIRM (integrating filter window)
constexpr uint32_t SELECTOR_DEBOUNCE_MS = 150; // 3-way selector contacts
// Power-up interlock (safety invariant 3): after boot the selector must read NEUTRAL *continuously* this
// long before C3/C4 may run (a single first reading at power-up can be wrong, seen on the shredder).
constexpr uint32_t INTERLOCK_OFF_HOLD_MS = 1000;

constexpr uint32_t RESULT_SHOW_MS = 3000;      // DONE / CANCELLED / NOT ENOUGH stay on screen this long, then IDLE
constexpr uint32_t HX_OK_WINDOW_MS = 1000;     // a load cell counts as working if it gave a valid reading this recently
constexpr uint8_t HX_AVG_N = 5;                // moving average (the screws vibrate)
constexpr int32_t JAM_MIN_DROP_G = 20;         // LOADCELL: weight must drop at least this much within jamTimeoutMs
constexpr int32_t CAL_MIN_COUNTS = 1000;       // CALIBRATE refused if the known weight moves the reading less than this
constexpr uint32_t SERVO_BRAKE_MS = 300;       // after a run: stopUs (web trim) this long, then no pulse at all
constexpr uint32_t STOP_OE_PULSE_MS = 200;     // remote STOP: PCA9685 outputs hard-disabled this long
constexpr uint32_t PCA_RETRY_MS = 2000;        // look for a missing PCA9685 again this often (hot-plug)
constexpr uint32_t FRAME_MS = 100;
constexpr uint32_t IDENTIFY_MS = 3000;

// Fault bits reported in StatusCommon.faults (web: shared/format.ts FAULT_BITS.containing)
constexpr uint16_t FAULT_SELECTOR_WIRING = 0x01;  // both selector contacts closed
constexpr uint16_t FAULT_OLED_MISSING = 0x02;
constexpr uint16_t FAULT_NOISE_SELECT1 = 0x04;    // noisy input signals (PinNoiseMonitor)
constexpr uint16_t FAULT_NOISE_CONFIRM1 = 0x08;
constexpr uint16_t FAULT_NOISE_SELECT2 = 0x10;
constexpr uint16_t FAULT_NOISE_CONFIRM2 = 0x20;
constexpr uint16_t FAULT_NOISE_SEL_LEFT = 0x40;
constexpr uint16_t FAULT_NOISE_SEL_RIGHT = 0x80;

// SENSOR_FAULT event arg1: what was missing
constexpr int32_t SENSOR_LOAD_CELL = 0;
constexpr int32_t SENSOR_NOT_CALIBRATED = 1;
constexpr int32_t SENSOR_SERVO_DRIVER = 2;
