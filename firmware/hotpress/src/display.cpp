#include "display.h"

#include <TileTime.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "pins.h"

using namespace tile;

// Layout (128 x 64):
//   y 0..11   top bar: HOT PRESS | hub link | clock
//   y 13..37  block 1: "DESIGNING + CURING" label, big value, lamp on the right
//   y 39..63  block 2: "HOT PRESS" label (+ selector position), big value, lamp on the right
// Text stays left of the lamp column (x < 110).

namespace {

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

constexpr int TEXT_MAX_X = 110;  // lamp column starts here
int overflows = 0;
bool testing = false;  // only the boot self-test reports overflows (not every frame)
const char* where = "";

void put(int x, int y, const char* s, int maxX = 128) {
  const int w = u8g2.getStrWidth(s);
  if (testing && (x < 0 || x + w > maxX)) {
    overflows++;
    Serial.printf("[ERROR] OLED layout (%s): \"%s\" spans x %d..%d, allowed 0..%d\n", where, s, x, x + w - 1, maxX - 1);
  }
  u8g2.drawStr(x, y, s);
}

void lamp(int cx, int cy, bool on) {
  if (on) {
    u8g2.drawDisc(cx, cy, 6);
    if ((millis() / 500) % 2) u8g2.drawCircle(cx, cy, 8);  // gentle pulse while energised
  } else {
    u8g2.drawCircle(cx, cy, 6);
  }
}

const char* valueText(OutState s, bool isPress, Selector sel) {
  switch (s) {
    case OutState::ON:
      if (!isPress) return "ON";
      return sel == Selector::RIGHT ? "AUTO: ON" : "HEATING";
    case OutState::LOCKED: return isPress ? "SET TO MID" : "TURN BTN OFF";
    case OutState::STOPPED: return "STOPPED";
    default: return isPress ? "COOLING" : "OFF";
  }
}

void block(int yLabel, int yValue, int lampY, const char* label, OutState s, bool isPress, Selector sel) {
  u8g2.setFont(u8g2_font_6x10_tr);
  put(0, yLabel, label, TEXT_MAX_X);
  const char* v = valueText(s, isPress, sel);
  // Long hint texts use a smaller bold font so they fit left of the lamp.
  u8g2.setFont((s == OutState::LOCKED) ? u8g2_font_helvB10_tr : u8g2_font_helvB12_tr);
  put(0, yValue, v, TEXT_MAX_X);
  lamp(119, lampY, s == OutState::ON);
}

void render(const View& v) {
  u8g2.clearBuffer();

  where = "top bar";
  u8g2.setFont(u8g2_font_6x10_tr);
  put(0, 9, v.selectorFault ? "SEL FAULT" : "HOT PRESS");
  if (v.paired) {
    u8g2.drawLine(82, 3, 89, 3);
    u8g2.drawLine(87, 1, 89, 3);
    u8g2.drawLine(82, 7, 89, 7);
    u8g2.drawLine(82, 7, 84, 9);
  } else if ((millis() / 500) % 2) {
    u8g2.drawDisc(85, 5, 2);
  }
  if (v.timeKnown) {
    const CivilTime c = civilFromEpoch((int64_t)v.epoch + (int64_t)v.tzOffsetMin * 60);
    char t[6];
    snprintf(t, sizeof(t), "%02u:%02u", c.hour % 24u, c.minute % 60u);
    put(98, 9, t);
  }
  u8g2.drawHLine(0, 11, 128);

  where = "designing+curing";
  block(22, 36, 27, v.designCure == OutState::STOPPED ? "DES+CURE: btn OFF" : "DESIGNING + CURING", v.designCure,
        false, v.selector);

  u8g2.drawHLine(0, 39, 128);

  where = "hot press";
  const char* sel = v.selector == Selector::LEFT ? "HOT PRESS  sel:L" : v.selector == Selector::RIGHT ? "HOT PRESS  sel:R" : "HOT PRESS  sel:M";
  block(49, 63, 55, v.hotpress == OutState::STOPPED ? "HOT PRESS: to MID" : sel, v.hotpress, true, v.selector);

  if (v.identify && (millis() / 200) % 2) {
    u8g2.setDrawColor(2);
    u8g2.drawBox(0, 0, 128, 64);
    u8g2.setDrawColor(1);
  }
}

}  // namespace

bool Display::begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.beginTransmission(OLED_I2C_ADDR);
  present_ = Wire.endTransmission() == 0;
  if (!present_) return false;
  u8g2.setI2CAddress(OLED_I2C_ADDR << 1);
  u8g2.begin();
  u8g2.setBusClock(400000);
  return true;
}

void Display::draw(const View& v) {
  if (!present_) return;
  render(v);
  u8g2.sendBuffer();
}

int Display::selfTest() {
  View v = {};
  v.paired = true;
  v.timeKnown = true;
  v.epoch = 1790294109;
  v.tzOffsetMin = 480;
  overflows = 0;
  testing = true;
  const OutState all[] = {OutState::OFF, OutState::ON, OutState::LOCKED, OutState::STOPPED};
  const Selector sels[] = {Selector::NEUTRAL, Selector::LEFT, Selector::RIGHT};
  for (OutState a : all)
    for (OutState b : all)
      for (Selector s : sels)
        for (int fault = 0; fault < 2; fault++) {
          v.designCure = a;
          v.hotpress = b;
          v.selector = s;
          v.selectorFault = fault;
          render(v);
        }
  testing = false;
  u8g2.clearBuffer();
  return overflows;
}
