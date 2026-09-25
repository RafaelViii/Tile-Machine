// Per-pin noise monitor shared by the module boards.
//
// An interrupt counts every raw edge on each watched input and remembers the shortest pulse. Once
// per second, a pin with more edges than a clean press/flip could make is reported:
//   - a [DIAG] serial line naming the pin, and whether the pulses look like electrical noise
//     (microsecond spikes) or a bad mechanical contact (millisecond drop-outs);
//   - its fault bit in faultBits() for 10 s, which the module puts in STATUS so the website names
//     the noisy input even when USB/serial is dead.
#pragma once

#include <Arduino.h>

class PinNoiseMonitor {
 public:
  static constexpr uint8_t MAX_PINS = 8;
  static constexpr uint32_t LOG_EDGES = 6;     // > this per second: [DIAG] line
  static constexpr uint32_t FAULT_EDGES = 20;  // > this per second: fault bit (normal flips stay below)
  static constexpr uint32_t FAULT_HOLD_MS = 10000;

  /** Watch `pin` (already configured as input). `faultBit` is OR-ed into faultBits() while noisy. */
  bool add(uint8_t pin, const char* name, uint16_t faultBit);
  /** Attach the interrupts. Call once after all add(). */
  void begin();
  /** Call every loop. */
  void loop();
  /** Fault bits of the pins that were noisy within the last FAULT_HOLD_MS. */
  uint16_t faultBits() const;

 private:
  struct Channel {
    uint8_t pin;
    const char* name;
    uint16_t bit;
    volatile uint32_t edges;
    volatile uint32_t lastEdgeUs;
    volatile uint32_t shortestUs;
    uint32_t noisyAtMs;
  };
  static void IRAM_ATTR isr(void* arg);
  Channel ch_[MAX_PINS];
  uint8_t n_ = 0;
  uint32_t lastReportMs_ = 0;
};
