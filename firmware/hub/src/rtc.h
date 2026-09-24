// DS3231 real-time clock on the hub's I2C bus (SDA 21 / SCL 22). Holds UTC.
// Used so the hub knows the time at boot and without internet (see docs/modules/hub.md).
// Call only from loop()/setup() (single I2C user).
#pragma once

#include <Arduino.h>

namespace rtc {

enum class Status : uint8_t {
  MISSING,     // no DS3231 answering at 0x68
  LOST_POWER,  // oscillator-stop flag set / time out of range: time unknown until NTP sets it
  OK,
};

/** Probes the chip. Returns its status; `epochOut` is valid only when OK. */
Status begin(uint32_t& epochOut);

/** Reads the current time. False if the chip didn't answer or the time is invalid. */
bool read(uint32_t& epoch);

/** Writes UTC time and clears the lost-power flag. */
bool write(uint32_t epoch);

/** Chip temperature in °C (±3 °C), NAN if unavailable. */
float temperature();

const char* statusName(Status s);

}  // namespace rtc
