// Containing inputs: SELECT / CONFIRM buttons (press edges) and the 3-way selector for the mixed containers.
// All go through the shared integrating filter (lib/TileIO/Debounce.h), like the other modules.
#pragma once

#include <Arduino.h>
#include <Debounce.h>
#include <TileProtocol.h>

/** Momentary button to GND (internal pull-up). */
class Button {
 public:
  void begin(uint8_t pin, uint32_t windowMs) {
    pin_ = pin;
    pinMode(pin_, INPUT_PULLUP);
    f_.begin(raw(), windowMs);  // a button held at power-up gives no press until released and pressed again
  }
  /** True exactly once per (filtered) press. Call every loop. */
  bool pressed() { return f_.update(raw()) && f_.value(); }

 private:
  bool raw() const { return digitalRead(pin_) == LOW; }
  uint8_t pin_ = 0;
  IntegratingFilter f_;
};

/** Selector on two pull-up pins: LEFT, RIGHT, neither = NEUTRAL; both closed = wiring fault -> NEUTRAL. */
class SelectorSwitch {
 public:
  void begin(uint8_t pinLeft, uint8_t pinRight, uint32_t windowMs) {
    pl_ = pinLeft;
    pr_ = pinRight;
    pinMode(pl_, INPUT_PULLUP);
    pinMode(pr_, INPUT_PULLUP);
    fl_.begin(digitalRead(pl_) == LOW, windowMs);
    fr_.begin(digitalRead(pr_) == LOW, windowMs);
    pos_ = compute();
  }
  /** Returns true when the filtered position changed. */
  bool update() {
    fl_.update(digitalRead(pl_) == LOW);
    fr_.update(digitalRead(pr_) == LOW);
    const tile::Selector p = compute();
    if (p == pos_) return false;
    pos_ = p;
    return true;
  }
  tile::Selector position() const { return pos_; }
  bool wiringFault() const { return fl_.value() && fr_.value(); }

 private:
  tile::Selector compute() const {
    const bool l = fl_.value(), r = fr_.value();
    if (l && !r) return tile::Selector::LEFT;
    if (r && !l) return tile::Selector::RIGHT;
    return tile::Selector::NEUTRAL;
  }
  uint8_t pl_ = 0, pr_ = 0;
  IntegratingFilter fl_, fr_;
  tile::Selector pos_ = tile::Selector::NEUTRAL;
};
