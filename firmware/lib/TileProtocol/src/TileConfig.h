// Config defaults and allowed ranges. Mirrors docs/PROTOCOL.md §6 and web/src/shared/configDefaults.ts.
// Modules must reject (AckResult::REJECTED) any config where valid*() returns false.
#pragma once

#include "TileProtocol.h"

namespace tile {

template <typename T>
inline bool inRange(T v, T lo, T hi) {
  return v >= lo && v <= hi;
}

// ---------- Shredder ----------
inline ConfigShredder defaultShredderConfig() {
  ConfigShredder c{};
  c.configVersion = 0;
  c.autoStartDelayMs = 5000;
  c.autoEmptyStopDelayMs = 1500;
  c.manualConfirmTimeoutMs = 15000;
  c.irDebounceMs = 200;
  c.buzzerVolumePct = 100;
  return c;
}

inline bool validShredderConfig(const ConfigShredder& c) {
  return inRange<uint16_t>(c.autoStartDelayMs, 2000, 30000) &&
         inRange<uint16_t>(c.autoEmptyStopDelayMs, 0, 10000) &&
         inRange<uint16_t>(c.manualConfirmTimeoutMs, 3000, 60000) &&
         inRange<uint16_t>(c.irDebounceMs, 20, 2000) && c.buzzerVolumePct <= 100;
}

// ---------- Containing ----------
inline ConfigContaining defaultContainingConfig() {
  ConfigContaining c{};
  c.configVersion = 0;
  for (int i = 0; i < 2; i++) {
    c.raw[i].mode = 0;  // LOADCELL
    for (int k = 0; k < 5; k++) c.raw[i].timeTableMs[k] = 10000u * (k + 1);
    c.raw[i].maxDispenseMs = 120000;
    c.raw[i].jamTimeoutMs = 10000;
    c.raw[i].toleranceG = 50;
    c.mixed[i].mode = 0;  // MANUAL
    c.mixed[i].runTimeMs = 10000;
  }
  for (int s = 0; s < 8; s++) {
    c.servo[s].stopUs = 1500;
    c.servo[s].runUs = 1300;
  }
  return c;
}

inline bool validContainingConfig(const ConfigContaining& c) {
  for (int i = 0; i < 2; i++) {
    const RawContainerCfg& r = c.raw[i];
    if (r.mode > 1) return false;
    for (int k = 0; k < 5; k++)
      if (!inRange<uint32_t>(r.timeTableMs[k], 1000, 600000)) return false;
    if (!inRange<uint32_t>(r.maxDispenseMs, 10000, 600000)) return false;
    if (!inRange<uint32_t>(r.jamTimeoutMs, 2000, 60000)) return false;
    if (r.toleranceG > 500) return false;
    if (c.mixed[i].mode > 1) return false;
    if (!inRange<uint32_t>(c.mixed[i].runTimeMs, 1000, 600000)) return false;
  }
  for (int s = 0; s < 8; s++) {
    if (!inRange<uint16_t>(c.servo[s].stopUs, 1000, 2000)) return false;
    if (!inRange<uint16_t>(c.servo[s].runUs, 500, 2500)) return false;
  }
  return true;
}

// ---------- Hotpress ----------
inline ConfigHotpress defaultHotpressConfig() {
  ConfigHotpress c{};
  c.configVersion = 0;
  c.autoModeBehaviour = 0;
  return c;
}

inline bool validHotpressConfig(const ConfigHotpress& c) { return c.autoModeBehaviour == 0; }

}  // namespace tile
