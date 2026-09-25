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

/**
 * Latching ON button with an INTEGRATING filter, sampled once per millisecond: +1 while the pin
 * reads pressed, -1 while released, clamped to [0, debounceMs]. ON when the score reaches the top,
 * OFF when it reaches 0. Unlike a restart-on-change timer, short noise spikes only nudge the score:
 * a real press still gets through (slower if noisy), and noise alone can't reach the top unless it
 * dominates for the whole debounce time.
 */
class OnButton {
 public:
  void begin(uint8_t pin, uint32_t debounceMs) {
    pin_ = pin;
    pinMode(pin_, INPUT_PULLUP);
    max_ = debounceMs ? debounceMs : 1;
    on_ = raw();
    score_ = on_ ? max_ : 0;
    lastMs_ = millis();
  }
  /** Returns true when the filtered state changed. */
  bool update() {
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
    const bool before = on_;
    if (score_ >= max_) on_ = true;
    else if (score_ == 0) on_ = false;
    return on_ != before;
  }
  bool on() const { return on_; }
  void setDebounce(uint32_t ms) {
    const uint32_t m = ms ? ms : 1;
    score_ = on_ ? m : 0;  // keep the current state, rescale
    max_ = m;
  }

 private:
  bool raw() const { return digitalRead(pin_) == LOW; }
  uint8_t pin_ = 0;
  uint32_t max_ = 200, score_ = 0, lastMs_ = 0;
  bool on_ = false;
};

/**
 * Counts every raw edge on a pin (interrupt) and the shortest pulse, to tell electrical noise
 * (microsecond spikes) from a bad mechanical contact (millisecond drop-outs).
 */
class PinNoiseMonitor {
 public:
  void begin(uint8_t pin);
  /** Call every loop; prints a [DIAG] line each second in which the pin had more edges than expected. */
  void report(const char* name, bool filteredOn);
  /** Noise seen within the last 10 s (reported to the web as a fault). */
  bool noisy() const { return lastNoisyMs_ && millis() - lastNoisyMs_ < 10000; }

 private:
  static void IRAM_ATTR isr();
  static volatile uint32_t edges_;
  static volatile uint32_t lastEdgeUs_;
  static volatile uint32_t shortestUs_;
  uint8_t pin_ = 0;
  uint32_t lastReportMs_ = 0;
  uint32_t lastNoisyMs_ = 0;
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
