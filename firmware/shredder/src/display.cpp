#include "display.h"

#include <TileTime.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "pins.h"

using namespace tile;

namespace {

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

void centered(int y, const char* s) { u8g2.drawStr((128 - u8g2.getStrWidth(s)) / 2, y, s); }

void blade(int cx, int cy, int r, float deg) {  // 3-blade fan from the legacy sketch
  for (int i = 0; i < 3; i++) {
    const float a = (deg + i * 120.0f) * PI / 180.0f;
    u8g2.drawLine(cx, cy, cx + (int)(r * cos(a)), cy + (int)(r * sin(a)));
  }
  u8g2.drawCircle(cx, cy, r, U8G2_DRAW_ALL);
  u8g2.drawDisc(cx, cy, 2, U8G2_DRAW_ALL);
}

void bar(int y, uint32_t remaining, uint32_t total) {
  u8g2.drawFrame(0, y, 128, 7);
  if (total) u8g2.drawBox(2, y + 2, (int)(124UL * min(remaining, total) / total), 3);
}

void topBar(const View& v) {
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 9, v.switchFault ? "SW FAULT" : shredderModeName((uint8_t)v.mode));

  // IR indicator
  u8g2.drawStr(56, 9, "IR");
  if (v.irDetected) u8g2.drawDisc(72, 5, 3);
  else u8g2.drawCircle(72, 5, 3);

  // Hub link: two arrows when paired, blinking dot while searching
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
    u8g2.drawStr(98, 9, t);
  }
  u8g2.drawHLine(0, 11, 128);
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
  if (v.relayOn) {
    bladeDeg_ += 24.0f;
    if (bladeDeg_ >= 360.0f) bladeDeg_ -= 360.0f;
  }
  char buf[24];

  u8g2.clearBuffer();
  topBar(v);

  if (v.flash) {
    u8g2.setFont(u8g2_font_helvB14_tr);
    centered(44, v.flash);
  } else {
    switch (v.state) {
      case ShredderState::INTERLOCK:
        u8g2.setFont(u8g2_font_helvB12_tr);
        centered(32, "SET SWITCH");
        centered(49, "TO OFF");
        u8g2.setFont(u8g2_font_6x10_tr);
        centered(62, "safety start");
        break;

      case ShredderState::OFF:
        u8g2.setFont(u8g2_font_helvB18_tr);
        centered(42, "OFF");
        u8g2.setFont(u8g2_font_6x10_tr);
        centered(62, "Select AUTO / MANUAL");
        break;

      case ShredderState::MANUAL_IDLE:
        blade(20, 38, 15, bladeDeg_);
        u8g2.setFont(u8g2_font_helvB10_tr);
        u8g2.drawStr(44, 30, "Press START");
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(44, 44, "to check the");
        u8g2.drawStr(44, 55, "chute");
        break;

      case ShredderState::MANUAL_CONFIRM:
        u8g2.setFont(u8g2_font_helvB14_tr);
        centered(31, v.lastCheckLoaded ? "LOADED" : "EMPTY");
        u8g2.setFont(u8g2_font_6x10_tr);
        centered(45, "START=Run STOP=Cancel");
        bar(55, v.countdownMs, v.countdownTotalMs);
        break;

      case ShredderState::MANUAL_RUNNING:
      case ShredderState::AUTO_RUNNING:
        blade(24, 38, 20, bladeDeg_);
        u8g2.setFont(u8g2_font_helvB12_tr);
        u8g2.drawStr(52, 33, "RUNNING");
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(52, 48, v.state == ShredderState::AUTO_RUNNING ? "stops when" : "STOP = halt");
        if (v.state == ShredderState::AUTO_RUNNING) u8g2.drawStr(52, 59, "chute empty");
        break;

      case ShredderState::AUTO_WAITING: {
        blade(20, 38, 15, bladeDeg_);
        u8g2.setFont(u8g2_font_helvB10_tr);
        u8g2.drawStr(44, 32, "WAITING");
        u8g2.setFont(u8g2_font_6x10_tr);
        const uint8_t dots = (millis() / 400) % 4;
        snprintf(buf, sizeof(buf), "for material%.*s", dots, "...");
        u8g2.drawStr(44, 46, buf);
        break;
      }

      case ShredderState::AUTO_WARNING: {
        const bool blink = (millis() / 250) % 2;
        if (blink) u8g2.drawBox(0, 13, 128, 38);
        u8g2.setDrawColor(blink ? 0 : 1);
        u8g2.setFont(u8g2_font_logisoso28_tn);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)((v.countdownMs + 999) / 1000));
        u8g2.drawStr(8, 47, buf);
        u8g2.setFont(u8g2_font_helvB12_tr);
        u8g2.drawStr(40, 30, "STARTING");
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(40, 44, "keep clear!");
        u8g2.setDrawColor(1);
        bar(55, v.countdownMs, v.countdownTotalMs);
        break;
      }

      case ShredderState::AUTO_ESTOP:
        u8g2.drawBox(0, 14, 128, 30);
        u8g2.setDrawColor(0);
        u8g2.setFont(u8g2_font_helvB14_tr);
        centered(36, "E-STOP");
        u8g2.setDrawColor(1);
        u8g2.setFont(u8g2_font_6x10_tr);
        centered(58, "Clear chute to reset");
        break;
    }
  }

  if (v.identify && (millis() / 200) % 2) {  // web IDENTIFY: blink the whole screen
    u8g2.setDrawColor(2);
    u8g2.drawBox(0, 0, 128, 64);
    u8g2.setDrawColor(1);
  }
  u8g2.sendBuffer();
}
