// UTC epoch <-> calendar conversion (no timezone database needed), shared by hub and modules.
// Algorithms: Howard Hinnant, "chrono-Compatible Low-Level Date Algorithms".
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace tile {

// Any clock reading outside this window is treated as "time unknown".
constexpr uint32_t EPOCH_VALID_MIN = 1735689600UL;  // 2025-01-01 00:00:00 UTC
constexpr uint32_t EPOCH_VALID_MAX = 4102444800UL;  // 2100-01-01 00:00:00 UTC

inline bool epochValid(int64_t t) { return t >= EPOCH_VALID_MIN && t < EPOCH_VALID_MAX; }

struct CivilTime {
  int year;
  uint8_t month;    // 1..12
  uint8_t day;      // 1..31
  uint8_t hour;     // 0..23
  uint8_t minute;   // 0..59
  uint8_t second;   // 0..59
  uint8_t weekday;  // 0 = Sunday .. 6 = Saturday
};

inline int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

inline CivilTime civilFromEpoch(int64_t t) {
  int64_t days = t / 86400;
  int64_t rem = t % 86400;
  if (rem < 0) {
    rem += 86400;
    days--;
  }
  CivilTime c;
  c.hour = (uint8_t)(rem / 3600);
  c.minute = (uint8_t)((rem % 3600) / 60);
  c.second = (uint8_t)(rem % 60);
  c.weekday = (uint8_t)(((days % 7) + 11) % 7);  // 1970-01-01 was a Thursday (4)

  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned doe = (unsigned)(days - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  c.day = (uint8_t)(doy - (153 * mp + 2) / 5 + 1);
  c.month = (uint8_t)(mp < 10 ? mp + 3 : mp - 9);
  c.year = (int)(yoe + era * 400) + (c.month <= 2);
  return c;
}

inline int64_t epochFromCivil(const CivilTime& c) {
  return daysFromCivil(c.year, c.month, c.day) * 86400 + c.hour * 3600 + c.minute * 60 + c.second;
}

/** "2026-09-25 14:03:07" in local time (tzOffsetMin = minutes east of UTC, e.g. +480 for UTC+8). Needs n >= 20. */
inline void formatLocalTime(int64_t epoch, int16_t tzOffsetMin, char* out, size_t n) {
  const CivilTime c = civilFromEpoch(epoch + (int64_t)tzOffsetMin * 60);
  snprintf(out, n, "%04u-%02u-%02u %02u:%02u:%02u", (unsigned)(c.year % 10000), c.month % 13u, c.day % 32u,
           c.hour % 24u, c.minute % 60u, c.second % 60u);
}

}  // namespace tile
