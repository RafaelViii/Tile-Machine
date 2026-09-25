// SH1106 1.3" OLED screen for the Hot Press station (U8g2). Layout per docs/modules/hotpress.md.
#pragma once

#include <Arduino.h>
#include <TileProtocol.h>

/** What one output block shows. */
enum class OutState : uint8_t {
  OFF,             // input off
  ON,              // output energised
  LOCKED,          // power-up interlock: input must go to OFF / middle first
  STOPPED,         // web STOP latched: input must be cycled
};

struct View {
  OutState designCure;
  OutState hotpress;
  tile::Selector selector;
  bool onButton;
  bool paired;
  bool timeKnown;
  uint32_t epoch;
  int16_t tzOffsetMin;
  bool identify;
  bool selectorFault;
};

class Display {
 public:
  bool begin();
  bool present() const { return present_; }
  void draw(const View& v);
  /** Renders every combination off-screen; returns how many texts left their area. */
  int selfTest();

 private:
  bool present_ = false;
};
