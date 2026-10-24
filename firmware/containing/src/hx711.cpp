#include "hx711.h"

#include "config.h"

namespace {
portMUX_TYPE hxMux = portMUX_INITIALIZER_UNLOCKED;
constexpr uint32_t MIN_GAP_US = 10000;  // the HX711's fastest rate is 80/s (12.5 ms): faster = not a real chip
constexpr int32_t RAW_MAX = 0x7FFFFF;
constexpr int32_t RAW_MIN = -0x800000;
}  // namespace

void Hx711::begin(uint8_t dtPin, uint8_t sckPin) {
  dt_ = dtPin;
  sck_ = sckPin;
  pinMode(dt_, INPUT);
  pinMode(sck_, OUTPUT);
  digitalWrite(sck_, LOW);  // SCK HIGH > 60 us = power-down
  // A real HX711 with a reading already waiting holds DT LOW until it's read: allow that first read
  // (it must still pass the release check below). A floating pin then fails it and stays rejected.
  sawHigh_ = true;
}

bool Hx711::poll() {
  if (digitalRead(dt_) == HIGH) {  // converting (or no chip pulling it low)
    sawHigh_ = true;
    return false;
  }
  if (!sawHigh_) return false;  // DT stuck LOW since the last reading: floating pin, not a conversion

  uint32_t v = 0;
  bool released;
  portENTER_CRITICAL(&hxMux);
  for (int i = 0; i < 24; i++) {
    digitalWrite(sck_, HIGH);
    delayMicroseconds(1);
    v = (v << 1) | (digitalRead(dt_) == HIGH ? 1u : 0u);
    digitalWrite(sck_, LOW);
    delayMicroseconds(1);
  }
  digitalWrite(sck_, HIGH);  // 25th pulse: channel A, gain 128 for the next conversion
  delayMicroseconds(1);
  digitalWrite(sck_, LOW);
  delayMicroseconds(1);
  released = digitalRead(dt_) == HIGH;  // a real HX711 pulls DT back HIGH after the 25th pulse
  portEXIT_CRITICAL(&hxMux);

  sawHigh_ = false;
  const uint32_t nowUs = micros();
  const bool tooFast = lastReadUs_ && nowUs - lastReadUs_ < MIN_GAP_US;
  lastReadUs_ = nowUs;

  int32_t raw = (v & 0x800000) ? (int32_t)(v | 0xFF000000) : (int32_t)v;  // sign-extend 24 bits
  if (!released || tooFast || raw == RAW_MAX || raw == RAW_MIN) return false;

  ring_[head_] = raw;
  head_ = (head_ + 1) % HX_AVG_N;
  if (count_ < HX_AVG_N) count_++;
  lastOkMs_ = millis();
  everOk_ = true;
  return true;
}

bool Hx711::ok() const { return everOk_ && millis() - lastOkMs_ < HX_OK_WINDOW_MS; }

int32_t Hx711::average() const {
  if (!count_) return 0;
  int64_t sum = 0;
  for (uint8_t i = 0; i < count_; i++) sum += ring_[i];
  return (int32_t)(sum / count_);
}
