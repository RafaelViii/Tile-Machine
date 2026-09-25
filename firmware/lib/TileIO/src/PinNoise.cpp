#include "PinNoise.h"

void IRAM_ATTR PinNoiseMonitor::isr(void* arg) {
  Channel* c = static_cast<Channel*>(arg);
  const uint32_t now = micros();
  const uint32_t width = now - c->lastEdgeUs;
  c->lastEdgeUs = now;
  c->edges = c->edges + 1;
  if (width < c->shortestUs) c->shortestUs = width;
}

bool PinNoiseMonitor::add(uint8_t pin, const char* name, uint16_t faultBit) {
  if (n_ >= MAX_PINS) return false;
  Channel& c = ch_[n_++];
  c.pin = pin;
  c.name = name;
  c.bit = faultBit;
  c.edges = 0;
  c.lastEdgeUs = micros();
  c.shortestUs = UINT32_MAX;
  c.noisyAtMs = 0;
  return true;
}

void PinNoiseMonitor::begin() {
  for (uint8_t i = 0; i < n_; i++) attachInterruptArg(digitalPinToInterrupt(ch_[i].pin), isr, &ch_[i], CHANGE);
  lastReportMs_ = millis();
}

void PinNoiseMonitor::loop() {
  const uint32_t now = millis();
  if (now - lastReportMs_ < 1000) return;
  lastReportMs_ = now;
  for (uint8_t i = 0; i < n_; i++) {
    Channel& c = ch_[i];
    noInterrupts();
    const uint32_t edges = c.edges;
    const uint32_t shortest = c.shortestUs;
    c.edges = 0;
    c.shortestUs = UINT32_MAX;
    interrupts();
    if (edges <= LOG_EDGES) continue;
    if (edges > FAULT_EDGES) c.noisyAtMs = now ? now : 1;
    Serial.printf("[DIAG] %s (GPIO%u): %lu edges in 1 s, shortest pulse %lu us -> %s%s; pin now %s\n", c.name, c.pin,
                  (unsigned long)edges, (unsigned long)shortest,
                  shortest < 1000 ? "electrical noise / fast contact chatter" : "slow drop-outs: bad contact / loose wire",
                  edges > FAULT_EDGES ? " [FAULT]" : "", digitalRead(c.pin) == LOW ? "LOW" : "HIGH");
  }
}

uint16_t PinNoiseMonitor::faultBits() const {
  const uint32_t now = millis();
  uint16_t bits = 0;
  for (uint8_t i = 0; i < n_; i++)
    if (ch_[i].noisyAtMs && now - ch_[i].noisyAtMs < FAULT_HOLD_MS) bits |= ch_[i].bit;
  return bits;
}
