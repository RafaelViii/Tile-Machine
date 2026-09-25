// Debounced state inputs: the latching ON button and the 3-way selector.
#pragma once

#include <Arduino.h>
#include <TileProtocol.h>

/** A value that only changes after the raw reading has been stable for `settleMs`. */
template <typename T>
class Settled {
 public:
  void begin(T initial, uint32_t settleMs) {
    stable_ = last_ = initial;
    settleMs_ = settleMs;
    changedMs_ = millis();
  }
  /** Feed the raw reading; returns true when the settled value changed. */
  bool feed(T raw) {
    if (raw != last_) {
      last_ = raw;
      changedMs_ = millis();
    }
    if (millis() - changedMs_ >= settleMs_ && raw != stable_) {
      stable_ = raw;
      return true;
    }
    return false;
  }
  T value() const { return stable_; }
  void setSettle(uint32_t ms) { settleMs_ = ms; }

 private:
  T stable_{}, last_{};
  uint32_t settleMs_ = 60, changedMs_ = 0;
};

class OnButton {
 public:
  void begin(uint8_t pin, uint32_t settleMs) {
    pin_ = pin;
    pinMode(pin_, INPUT_PULLUP);
    s_.begin(raw(), settleMs);
  }
  bool update() { return s_.feed(raw()); }
  bool on() const { return s_.value(); }
  void setDebounce(uint32_t ms) { s_.setSettle(ms); }

 private:
  bool raw() const { return digitalRead(pin_) == LOW; }
  uint8_t pin_ = 0;
  Settled<bool> s_;
};

/** Selector on two pull-up pins: LEFT, RIGHT, neither = NEUTRAL; both LOW = wiring fault -> NEUTRAL. */
class SelectorSwitch {
 public:
  void begin(uint8_t pinLeft, uint8_t pinRight, uint32_t settleMs) {
    pl_ = pinLeft;
    pr_ = pinRight;
    pinMode(pl_, INPUT_PULLUP);
    pinMode(pr_, INPUT_PULLUP);
    s_.begin(raw(), settleMs);
  }
  bool update() { return s_.feed(raw()); }
  tile::Selector position() const { return s_.value(); }
  void setDebounce(uint32_t ms) { s_.setSettle(ms); }
  bool wiringFault() const { return digitalRead(pl_) == LOW && digitalRead(pr_) == LOW; }

 private:
  tile::Selector raw() const {
    const bool l = digitalRead(pl_) == LOW;
    const bool r = digitalRead(pr_) == LOW;
    if (l && !r) return tile::Selector::LEFT;
    if (r && !l) return tile::Selector::RIGHT;
    return tile::Selector::NEUTRAL;
  }
  uint8_t pl_ = 0, pr_ = 0;
  Settled<tile::Selector> s_;
};
