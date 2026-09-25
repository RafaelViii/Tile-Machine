// Shredder inputs: buttons (press edges), the 3-way mode switch and the IR sensor.
// Every input goes through the shared integrating filter (lib/TileIO/Debounce.h): the hardware
// test showed heavy contact chatter on all of them while being operated.
#pragma once

#include <Arduino.h>
#include <Debounce.h>
#include <TileProtocol.h>

/** Momentary button. `activeHigh` = pressed when the pin reads HIGH (normally-closed wiring). */
class Button {
 public:
  void begin(uint8_t pin, bool activeHigh, uint32_t windowMs) {
    pin_ = pin;
    activeHigh_ = activeHigh;
    pinMode(pin_, INPUT_PULLUP);
    f_.begin(raw(), windowMs);
  }
  /** True exactly once per (filtered) press. */
  bool pressed() { return f_.update(raw()) && f_.value(); }
  bool isDown() const { return f_.value(); }
  void setDebounce(uint32_t ms) { f_.setWindow(ms); }

 private:
  bool raw() const { return (digitalRead(pin_) == HIGH) == activeHigh_; }
  uint8_t pin_ = 0;
  bool activeHigh_ = false;
  IntegratingFilter f_;
};

/** 3-way switch on two pull-up pins. Center (neither LOW) = OFF; both LOW = wiring fault -> OFF. */
class ModeSwitch {
 public:
  void begin(uint8_t pinAuto, uint8_t pinManual, uint32_t windowMs) {
    pa_ = pinAuto;
    pm_ = pinManual;
    pinMode(pa_, INPUT_PULLUP);
    pinMode(pm_, INPUT_PULLUP);
    fa_.begin(digitalRead(pa_) == LOW, windowMs);
    fm_.begin(digitalRead(pm_) == LOW, windowMs);
    pos_ = compute();
  }
  /** True when the filtered position changed since the last call. */
  bool update() {
    fa_.update(digitalRead(pa_) == LOW);
    fm_.update(digitalRead(pm_) == LOW);
    const tile::ShredderMode p = compute();
    if (p == pos_) return false;
    pos_ = p;
    return true;
  }
  tile::ShredderMode position() const { return pos_; }
  bool wiringFault() const { return fa_.value() && fm_.value(); }
  void setDebounce(uint32_t ms) {
    fa_.setWindow(ms);
    fm_.setWindow(ms);
  }

 private:
  tile::ShredderMode compute() const {
    const bool a = fa_.value(), m = fm_.value();
    if (a && !m) return tile::ShredderMode::AUTO;
    if (m && !a) return tile::ShredderMode::MANUAL;
    return tile::ShredderMode::OFF;  // center, or both closed (wiring fault)
  }
  uint8_t pa_ = 0, pm_ = 0;
  IntegratingFilter fa_, fm_;
  tile::ShredderMode pos_ = tile::ShredderMode::OFF;
};

/** IR obstacle sensor, filter window from the web config (irDebounceMs). */
class IrSensor {
 public:
  void begin(uint8_t pin, bool activeLow) {
    pin_ = pin;
    activeLow_ = activeLow;
    pinMode(pin_, INPUT_PULLUP);
    f_.begin(raw(), 200);
  }
  void setDebounce(uint32_t ms) { f_.setWindow(ms); }
  /** True when the filtered reading changed. */
  bool update() { return f_.update(raw()); }
  bool detected() const { return f_.value(); }
  /** How long the current filtered state has lasted. */
  uint32_t stateAgeMs() const { return f_.ageMs(); }

 private:
  bool raw() const { return (digitalRead(pin_) == LOW) == activeLow_; }
  uint8_t pin_ = 0;
  bool activeLow_ = true;
  IntegratingFilter f_;
};
