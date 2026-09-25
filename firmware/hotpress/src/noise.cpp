#include "inputs.h"

volatile uint32_t PinNoiseMonitor::edges_ = 0;
volatile uint32_t PinNoiseMonitor::lastEdgeUs_ = 0;
volatile uint32_t PinNoiseMonitor::shortestUs_ = UINT32_MAX;

void IRAM_ATTR PinNoiseMonitor::isr() {
  const uint32_t now = micros();
  const uint32_t width = now - lastEdgeUs_;
  lastEdgeUs_ = now;
  edges_ = edges_ + 1;
  if (width < shortestUs_) shortestUs_ = width;
}

void PinNoiseMonitor::begin(uint8_t pin) {
  pin_ = pin;
  lastEdgeUs_ = micros();
  attachInterrupt(digitalPinToInterrupt(pin_), isr, CHANGE);
  lastReportMs_ = millis();
}

void PinNoiseMonitor::report(const char* name, bool filteredOn) {
  const uint32_t now = millis();
  if (now - lastReportMs_ < 1000) return;
  lastReportMs_ = now;
  noInterrupts();
  const uint32_t edges = edges_;
  const uint32_t shortest = shortestUs_;
  edges_ = 0;
  shortestUs_ = UINT32_MAX;
  interrupts();
  // A clean press/release is 1-2 edges per second (a little bounce: up to ~6). More = noise.
  if (edges <= 6) return;
  lastNoisyMs_ = now ? now : 1;
  const char* kind = shortest < 1000 ? "microsecond spikes -> ELECTRICAL NOISE on the wire"
                                     : "millisecond drop-outs -> BAD CONTACT / loose wire";
  Serial.printf("[DIAG] %s pin: %lu edges in 1 s, shortest pulse %lu us (%s); pin now %s, filtered %s\n", name,
                (unsigned long)edges, (unsigned long)shortest, kind, digitalRead(pin_) == LOW ? "LOW" : "HIGH",
                filteredOn ? "ON" : "OFF");
}
