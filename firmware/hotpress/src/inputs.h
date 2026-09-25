// Hot Press inputs: the latching ON button and the 3-way selector.
// Both use the shared integrating filter (lib/TileIO/Debounce.h): the hardware test showed heavy
// contact chatter (1000-2000 edges/s) while they are operated.
#pragma once

#include <Arduino.h>
#include <Debounce.h>
#include <TileProtocol.h>

class OnButton {
 public:
  void begin(uint8_t pin, uint32_t windowMs) {
    pin_ = pin;
    pinMode(pin_, INPUT_PULLUP);
    f_.begin(raw(), windowMs);
  }
  /** Returns true when the filtered state changed. */
  bool update() { return f_.update(raw()); }
  bool on() const { return f_.value(); }
  void setDebounce(uint32_t ms) { f_.setWindow(ms); }

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
  void setDebounce(uint32_t ms) {
    fl_.setWindow(ms);
    fr_.setWindow(ms);
  }
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
