// SH1106 1.3" OLED for the Containing station (U8g2). Layout per docs/modules/containing.md.
#pragma once

#include <Arduino.h>
#include <TileProtocol.h>

/** Why a container is in FAULT (shown on its row). */
enum class FaultKind : uint8_t { NONE, JAM, TIMEOUT, LOAD_CELL, NOT_CALIBRATED, SERVO_DRIVER };

struct RowView {
  bool hxOk;           // load cell gives valid readings
  bool calibrated;     // a CALIBRATE factor is stored
  int32_t weightG;
  tile::ContainerState state;
  uint8_t mode;        // raw: 0 LOADCELL 1 TIME | mixed: 0 MANUAL 1 TIME
  uint8_t selectedKg;  // raw 1..5
  uint8_t progressPct;
  uint32_t remainingMs;
  FaultKind fault;
  bool locked;         // mixed: power-up interlock (selector must go to the middle)
  bool stopLatched;    // mixed: web STOP, selector must go to the middle
};

struct View {
  RowView row[4];
  bool paired;
  bool timeKnown;
  uint32_t epoch;
  int16_t tzOffsetMin;
  bool identify;
  bool interlockShown;
  bool pcaOk;
  bool selectorFault;
};

class Display {
 public:
  bool begin();
  bool present() const { return present_; }
  void draw(const View& v);
  /** Renders every state off-screen; returns how many texts left the screen or their row. */
  int selfTest();

 private:
  bool present_ = false;
};
