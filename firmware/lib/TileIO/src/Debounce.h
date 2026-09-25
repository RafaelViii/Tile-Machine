// Integrating debounce filter shared by all module inputs.
//
// Sampled once per millisecond: +1 while the raw input is active, -1 while inactive, clamped to
// [0, windowMs]. The filtered value turns ON when the score reaches the top and OFF when it
// reaches 0. Contact chatter and noise spikes only nudge the score, whereas a restart-on-change
// timer can be fooled by them: on the real shredder, a 3-way switch producing up to 7,453 edges/s
// while being flipped passed five fake MANUAL/OFF changes through a 60 ms restart-on-change filter.
#pragma once

#include <Arduino.h>

class IntegratingFilter {
 public:
  void begin(bool initial, uint32_t windowMs) {
    max_ = windowMs ? windowMs : 1;
    value_ = initial;
    score_ = initial ? max_ : 0;
    lastMs_ = millis();
    sinceMs_ = lastMs_;
  }

  /** Change the window, keeping the current filtered value. */
  void setWindow(uint32_t windowMs) {
    max_ = windowMs ? windowMs : 1;
    score_ = value_ ? max_ : 0;
  }

  /** Feed the current raw reading; returns true when the filtered value changed. */
  bool update(bool raw) {
    const uint32_t now = millis();
    uint32_t steps = now - lastMs_;
    if (!steps) return false;
    lastMs_ = now;
    if (steps > max_) steps = max_;
    if (raw) score_ = (score_ + steps >= max_) ? max_ : score_ + steps;
    else score_ = (score_ <= steps) ? 0 : score_ - steps;
    const bool before = value_;
    if (score_ >= max_) value_ = true;
    else if (score_ == 0) value_ = false;
    if (value_ != before) {
      sinceMs_ = now;
      return true;
    }
    return false;
  }

  bool value() const { return value_; }
  /** How long the filtered value has been unchanged. */
  uint32_t ageMs() const { return millis() - sinceMs_; }

 private:
  uint32_t max_ = 1, score_ = 0, lastMs_ = 0, sinceMs_ = 0;
  bool value_ = false;
};
