#include "display.h"

#include <TileTime.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "pins.h"

using namespace tile;

// Screen layout (128 x 64):
//   y 0..11   top bar: mode | IR | hub link | clock
//   y 12..63  main area. Screens with the blade icon keep it in the LEFT zone (x < 46) and all text
//             in the RIGHT zone (x >= 48), so text and animation never overlap.

namespace {

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

constexpr int TEXT_X = 48;  // right zone start (blade zone ends at x = 45)
int overflows = 0;          // counted by put(); reported by the self-test
const char* overflowWhere = "";

/** drawStr that records any text leaving [minX, 128). */
void put(int x, int y, const char* s, int minX = 0) {
  const int w = u8g2.getStrWidth(s);
  if (x < minX || x + w > 128) {
    overflows++;
    Serial.printf("[ERROR] OLED layout (%s): \"%s\" spans x %d..%d, allowed %d..127\n", overflowWhere, s, x,
                  x + w - 1, minX);
  }
  u8g2.drawStr(x, y, s);
}

void centered(int y, const char* s) { put((128 - u8g2.getStrWidth(s)) / 2, y, s); }

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
  put(0, 9, v.switchFault ? "SW FAULT" : shredderModeName((uint8_t)v.mode));

  put(56, 9, "IR");
  if (v.irDetected) u8g2.drawDisc(72, 5, 3);
  else u8g2.drawCircle(72, 5, 3);

  if (v.paired) {  // hub link: two arrows; blinking dot while searching
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
}

void render(const View& v, float bladeDeg) {
  char buf[24];
  u8g2.clearBuffer();
  overflowWhere = "top bar";
  topBar(v);
  overflowWhere = v.flash ? "flash message" : shredderStateName((uint8_t)v.state);

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
        centered(62, "Select AUTO/MANUAL");
        break;

      case ShredderState::MANUAL_IDLE:
        blade(22, 38, 18, bladeDeg);
        u8g2.setFont(u8g2_font_helvB10_tr);
        put(TEXT_X, 29, "READY", TEXT_X);
        u8g2.setFont(u8g2_font_6x10_tr);
        put(TEXT_X, 43, "Press START", TEXT_X);
        put(TEXT_X, 55, "to check", TEXT_X);
        break;

      case ShredderState::MANUAL_CONFIRM:
        u8g2.setFont(u8g2_font_helvB14_tr);
        centered(31, v.lastCheckLoaded ? "LOADED" : "EMPTY");
        u8g2.setFont(u8g2_font_6x10_tr);
        centered(45, "START=Run STOP=No");
        bar(55, v.countdownMs, v.countdownTotalMs);
        break;

      case ShredderState::MANUAL_RUNNING:
      case ShredderState::AUTO_RUNNING:
        blade(22, 38, 18, bladeDeg);
        u8g2.setFont(u8g2_font_helvB10_tr);
        put(TEXT_X, 29, "RUNNING", TEXT_X);
        u8g2.setFont(u8g2_font_6x10_tr);
        if (v.state == ShredderState::AUTO_RUNNING) {
          put(TEXT_X, 43, "Stops when", TEXT_X);
          put(TEXT_X, 55, "chute empty", TEXT_X);
        } else {
          put(TEXT_X, 43, "Press STOP", TEXT_X);
          put(TEXT_X, 55, "to halt", TEXT_X);
        }
        break;

      case ShredderState::AUTO_WAITING: {
        blade(22, 38, 18, bladeDeg);
        u8g2.setFont(u8g2_font_helvB10_tr);
        put(TEXT_X, 29, "WAITING", TEXT_X);
        u8g2.setFont(u8g2_font_6x10_tr);
        put(TEXT_X, 43, "Load the", TEXT_X);
        const uint8_t dots = (millis() / 400) % 4;
        snprintf(buf, sizeof(buf), "chute%.*s", dots, "...");
        put(TEXT_X, 55, buf, TEXT_X);
        break;
      }

      case ShredderState::AUTO_WARNING: {
        const bool blink = (millis() / 250) % 2;
        if (blink) u8g2.drawBox(0, 13, 128, 38);
        u8g2.setDrawColor(blink ? 0 : 1);
        // Countdown number right-aligned inside the left zone, so 1 or 2 digits never reach the text.
        u8g2.setFont(u8g2_font_logisoso28_tn);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)((v.countdownMs + 999) / 1000));
        put(44 - u8g2.getStrWidth(buf), 47, buf);
        u8g2.setFont(u8g2_font_helvB10_tr);
        put(TEXT_X, 29, "STARTING", TEXT_X);
        u8g2.setFont(u8g2_font_6x10_tr);
        put(TEXT_X, 43, "Keep clear!", TEXT_X);
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

  if (v.testHz) {  // buzzer pitch test: the pitch playing now, big
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    centered(12, "BUZZER PITCH TEST");
    char hz[12];
    snprintf(hz, sizeof(hz), "%u Hz", (unsigned)v.testHz);
    u8g2.setFont(u8g2_font_helvB14_tr);
    centered(40, hz);
    u8g2.setFont(u8g2_font_6x10_tr);
    centered(60, "Note the loudest one");
    return;
  }

  if (v.identify && (millis() / 200) % 2) {  // web IDENTIFY: blink the whole screen
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
  if (v.relayOn) {
    bladeDeg_ += 24.0f;
    if (bladeDeg_ >= 360.0f) bladeDeg_ -= 360.0f;
  }
  render(v, bladeDeg_);
  u8g2.sendBuffer();
}

int Display::selfTest() {
  // Worst case for every screen: longest mode name, clock shown, 2-digit countdown, all flashes.
  View v = {};
  v.mode = ShredderMode::MANUAL;
  v.paired = true;
  v.timeKnown = true;
  v.epoch = 1790294109;  // any valid time
  v.tzOffsetMin = 480;
  v.irDetected = true;
  v.countdownMs = 30000;
  v.countdownTotalMs = 30000;
  overflows = 0;

  const ShredderState all[] = {ShredderState::INTERLOCK,      ShredderState::OFF,          ShredderState::MANUAL_IDLE,
                               ShredderState::MANUAL_CONFIRM, ShredderState::MANUAL_RUNNING, ShredderState::AUTO_WAITING,
                               ShredderState::AUTO_WARNING,   ShredderState::AUTO_RUNNING, ShredderState::AUTO_ESTOP};
  for (ShredderState s : all) {
    v.state = s;
    for (int loaded = 0; loaded < 2; loaded++) {
      v.lastCheckLoaded = loaded;
      render(v, 0);
    }
  }
  const char* flashes[] = {"STOPPED", "TIMED OUT", "CANCELLED"};
  for (const char* f : flashes) {
    v.flash = f;
    render(v, 0);
  }
  v.flash = nullptr;
  v.switchFault = true;
  render(v, 0);
  u8g2.clearBuffer();  // nothing from the test reaches the screen
  return overflows;
}
