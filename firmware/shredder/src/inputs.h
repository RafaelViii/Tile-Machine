// Debounced inputs: buttons (press edges), the 3-way mode switch and the IR sensor.
#pragma once

#include <Arduino.h>
#include <TileProtocol.h>

/**
 * Momentary button with an INTEGRATING filter sampled once per ms: +1 while the pin reads pressed,
 * -1 while released, clamped to [0, debounceMs]; pressed at the top, released at 0. Contact chatter
 * and noise spikes only nudge the score (a restart-on-change timer can be fooled or starved by
 * them). `activeHigh` = pressed when the pin reads HIGH (normally-closed wiring).
 */
class Button {
 public:
  void begin(uint8_t pin, bool activeHigh, uint32_t debounceMs) {
    pin_ = pin;
    activeHigh_ = activeHigh;
    pinMode(pin_, INPUT_PULLUP);
    max_ = debounceMs ? debounceMs : 1;
    down_ = raw();
    score_ = down_ ? max_ : 0;
    lastMs_ = millis();
  }
  /** True exactly once per (filtered) press. */
  bool pressed() {
    const uint32_t now = millis();
    uint32_t steps = now - lastMs_;
    if (!steps) return false;
    lastMs_ = now;
    if (steps > max_) steps = max_;
    const bool r = raw();
    for (uint32_t i = 0; i < steps; i++) {
      if (r) {
        if (score_ < max_) score_++;
      } else if (score_) {
        score_--;
      }
    }
    const bool before = down_;
    if (score_ >= max_) down_ = true;
    else if (score_ == 0) down_ = false;
    return down_ && !before;
  }
  bool isDown() const { return down_; }

 private:
  bool raw() const { return (digitalRead(pin_) == HIGH) == activeHigh_; }
  uint8_t pin_ = 0;
  bool activeHigh_ = false;
  uint32_t max_ = 50, score_ = 0, lastMs_ = 0;
  bool down_ = false;
};

/** 3-way switch on two pull-up pins. Center (neither LOW) = OFF; both LOW = wiring fault -> OFF. */
class ModeSwitch {
 public:
  void begin(uint8_t pinAuto, uint8_t pinManual, uint32_t settleMs) {
    pa_ = pinAuto;
    pm_ = pinManual;
    settleMs_ = settleMs;
    pinMode(pa_, INPUT_PULLUP);
    pinMode(pm_, INPUT_PULLUP);
    stable_ = read(fault_);
    last_ = stable_;
  }
  /** True when the (settled) position changed since the last call. */
  bool update() {
    bool f;
    const tile::ShredderMode r = read(f);
    if (r != last_) {
      last_ = r;
      changedMs_ = millis();
    }
    if (millis() - changedMs_ >= settleMs_ && r != stable_) {
      stable_ = r;
      fault_ = f;
      return true;
    }
    if (r == stable_) fault_ = f;
    return false;
  }
  tile::ShredderMode position() const { return stable_; }
  bool wiringFault() const { return fault_; }

 private:
  tile::ShredderMode read(bool& fault) const {
    const bool a = digitalRead(pa_) == LOW;
    const bool m = digitalRead(pm_) == LOW;
    fault = a && m;
    if (a && !m) return tile::ShredderMode::AUTO;
    if (m && !a) return tile::ShredderMode::MANUAL;
    return tile::ShredderMode::OFF;
  }
  uint8_t pa_ = 0, pm_ = 0;
  uint32_t settleMs_ = 60;
  tile::ShredderMode stable_ = tile::ShredderMode::OFF, last_ = tile::ShredderMode::OFF;
  bool fault_ = false;
  uint32_t changedMs_ = 0;
};

/** IR obstacle sensor with a configurable debounce (web: irDebounceMs). */
class IrSensor {
 public:
  void begin(uint8_t pin, bool activeLow) {
    pin_ = pin;
    activeLow_ = activeLow;
    pinMode(pin_, INPUT_PULLUP);
    stable_ = raw();
    last_ = stable_;
  }
  void setDebounce(uint32_t ms) { debounceMs_ = ms; }
  /** True when the debounced reading changed. */
  bool update() {
    const bool r = raw();
    if (r != last_) {
      last_ = r;
      changedMs_ = millis();
    }
    if (millis() - changedMs_ >= debounceMs_ && r != stable_) {
      stable_ = r;
      sinceMs_ = millis();
      return true;
    }
    return false;
  }
  bool detected() const { return stable_; }
  /** How long the current debounced state has lasted. */
  uint32_t stateAgeMs() const { return millis() - sinceMs_; }

 private:
  bool raw() const { return (digitalRead(pin_) == LOW) == activeLow_; }
  uint8_t pin_ = 0;
  bool activeLow_ = true;
  uint32_t debounceMs_ = 200;
  bool stable_ = false, last_ = false;
  uint32_t changedMs_ = 0, sinceMs_ = 0;
};
