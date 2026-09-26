// SH1106 1.3" OLED screens (U8g2). Layout per docs/modules/shredder.md.
#pragma once

#include <Arduino.h>
#include <TileProtocol.h>

struct View {
  tile::ShredderState state;
  tile::ShredderMode mode;
  bool relayOn;
  bool irDetected;
  bool lastCheckLoaded;       // MANUAL_CONFIRM: result of the chute check
  uint32_t countdownMs;       // remaining (warning / confirm)
  uint32_t countdownTotalMs;  // full length, for the progress bar
  bool paired;
  bool timeKnown;
  uint32_t epoch;
  int16_t tzOffsetMin;
  bool identify;              // blink the whole screen (web IDENTIFY)
  const char* flash;          // short message ("STOPPED", "TIMED OUT", ...) or nullptr
  bool switchFault;
  uint16_t testHz;            // buzzer pitch test running: the pitch to show (0 = no test)
};

class Display {
 public:
  /** False if no OLED answers on I2C (the shredder keeps working without it). */
  bool begin();
  bool present() const { return present_; }
  void draw(const View& v);
  /** Renders every screen (worst case) off-screen; returns how many texts left their zone. */
  int selfTest();

 private:
  bool present_ = false;
  float bladeDeg_ = 0;
};
