// HX711 load-cell amplifier, channel A gain 128, non-blocking.
//
// The HX711 pulls DT LOW when a conversion is ready (10 or 80 per second). poll() reads it then: 24 data
// bits + 1 extra clock pulse (selects channel A / gain 128 for the next one), inside a critical section
// because SCK must never stay HIGH longer than 60 us (the HX711 would power down).
//
// A missing HX711 leaves DT floating (pins 34-39 have no pull-ups), so a reading only counts if it looks like
// a real chip: DT was seen HIGH since the last reading (a conversion takes >= 12 ms), DT goes back HIGH after
// the 25th pulse (the chip releases it), the value isn't saturated (load cell unplugged), and readings don't
// arrive faster than the chip's fastest rate.
#pragma once

#include <Arduino.h>

class Hx711 {
 public:
  void begin(uint8_t dtPin, uint8_t sckPin);
  /** Call every loop. Returns true when a new valid reading was taken. */
  bool poll();
  /** A valid reading arrived within HX_OK_WINDOW_MS. */
  bool ok() const;
  /** Moving average of the last valid readings (raw counts). */
  int32_t average() const;
  bool hasAverage() const { return count_ > 0; }

 private:
  uint8_t dt_ = 0, sck_ = 0;
  bool sawHigh_ = false;
  uint32_t lastReadUs_ = 0;
  uint32_t lastOkMs_ = 0;
  bool everOk_ = false;
  int32_t ring_[8] = {};
  uint8_t head_ = 0, count_ = 0;
};
