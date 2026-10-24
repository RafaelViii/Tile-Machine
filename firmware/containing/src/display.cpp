#include "display.h"

#include <TileTime.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "config.h"
#include "pins.h"

using namespace tile;

// Layout (128 x 64), no overlap by construction:
//   y 0..11   top bar: title | hub link | clock (12-hour + small AM/PM), line at y 11
//   rows      baselines 23, 35, 47, 59 (12 px apart). Font 6x10 is fixed-width (6 px), so a row is always
//             exactly 21 characters = 126 px: "N NAME__ WEIGHT STATS"  ("%c %-6s %6s %-5s").
//             Glyphs span baseline-7 .. baseline+2; the progress bar sits at baseline+3, the next row starts
//             at baseline+5. The boot self-test renders every state and reports anything past 128 px.
//
//   CONTAINING      ⇄  8:57PM
//   1 R.HDPE 12.4kg 2kg L
//   2 R.PP    8.1kg > 40%
//   3 M.HDPE  5.0kg >  7s
//   4 M.PP   NO CAL HOLD

namespace {

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

constexpr int ROW_BASE[4] = {23, 35, 47, 59};
constexpr int ROW_CHARS = 21;
const char* const NAME[4] = {"R.HDPE", "R.PP", "M.HDPE", "M.PP"};

int overflows = 0;
bool testing = false;  // only the boot self-test reports overflows (not every frame)
const char* where = "";

void put(int x, int y, const char* s) {
  const int w = u8g2.getStrWidth(s);
  if (testing && (x < 0 || x + w > 128)) {
    overflows++;
    Serial.printf("[ERROR] OLED layout (%s): \"%s\" spans x %d..%d, allowed 0..127\n", where, s, x, x + w - 1);
  }
  u8g2.drawStr(x, y, s);
}

void weightText(const RowView& r, char* out, size_t n) {
  char t[12];
  if (!r.hxOk) strcpy(t, "ERR");
  else if (!r.calibrated) strcpy(t, "NO CAL");
  else if (r.weightG > 99949) strcpy(t, ">99kg");
  else if (r.weightG < -9949) strcpy(t, "<-9kg");
  else snprintf(t, sizeof(t), "%.1fkg", r.weightG / 1000.0f);
  snprintf(out, n, "%6.6s", t);  // always exactly 6 characters, right-aligned
}

const char* faultText(FaultKind f) {
  switch (f) {
    case FaultKind::JAM: return "JAM";
    case FaultKind::TIMEOUT: return "TIME!";
    case FaultKind::LOAD_CELL: return "ERR";
    case FaultKind::NOT_CALIBRATED: return "NOCAL";
    case FaultKind::SERVO_DRIVER: return "NOSRV";
    default: return "FAULT";
  }
}

void statusText(int i, const RowView& r, char* out, size_t n) {
  const bool anim = (millis() / 300) % 2;
  const bool blinkOn = (millis() / 400) % 2;
  const bool raw = i < 2;
  char t[12] = "";
  switch (r.state) {
    case ContainerState::IDLE:
      if (raw) snprintf(t, sizeof(t), "%ukg %c", (unsigned)(r.selectedKg % 10), r.mode ? 'T' : 'L');
      else strcpy(t, r.locked ? "LOCK" : r.mode ? "TIME" : "MAN");
      break;
    case ContainerState::DISPENSING:
      if (raw || r.mode) {
        if (raw) snprintf(t, sizeof(t), "%c%3u%%", anim ? '>' : ' ', (unsigned)min<uint8_t>(r.progressPct, 100));
        else snprintf(t, sizeof(t), "%c%3lus", anim ? '>' : ' ', (unsigned long)min<uint32_t>((r.remainingMs + 999) / 1000, 999));
      } else {
        strcpy(t, anim ? ">RUN" : " RUN");
      }
      break;
    case ContainerState::DONE: strcpy(t, raw ? (blinkOn ? "DONE" : "") : "HOLD"); break;
    case ContainerState::CANCELLED: strcpy(t, raw ? (blinkOn ? "CANCL" : "") : "STOP"); break;
    case ContainerState::NOT_ENOUGH: strcpy(t, blinkOn ? "LOW!" : ""); break;
    case ContainerState::FAULT: strcpy(t, faultText(r.fault)); break;
  }
  if (!raw && r.stopLatched) strcpy(t, "STOP");
  snprintf(out, n, "%-5.5s", t);  // always exactly 5 characters
}

void row(int i, const RowView& r) {
  char w[8], s[8], line[32];
  weightText(r, w, sizeof(w));
  statusText(i, r, s, sizeof(s));
  snprintf(line, sizeof(line), "%c %-6s %s %s", '1' + i, NAME[i], w, s);
  if (testing && (int)strlen(line) != ROW_CHARS) {
    overflows++;
    Serial.printf("[ERROR] OLED layout (row %d): \"%s\" is %u characters, must be %d\n", i + 1, line,
                  (unsigned)strlen(line), ROW_CHARS);
  }
  where = "container row";
  put(0, ROW_BASE[i], line);
  if (r.state == ContainerState::DISPENSING) {  // progress bar under the row
    const int w = (int)(128L * min<uint8_t>(r.progressPct, 100) / 100);
    if (w > 0) u8g2.drawHLine(0, ROW_BASE[i] + 3, w);
  }
}

void render(const View& v) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);

  where = "top bar";
  const char* title = v.interlockShown  ? "SET SEL MID"
                      : v.selectorFault ? "SEL FAULT"
                      : !v.pcaOk        ? "NO SERVO DRV"
                                        : "CONTAINING";
  put(0, 9, title);  // longest is 12 characters = 72 px, the link icon starts at x 79
  if (v.paired) {
    u8g2.drawLine(79, 3, 86, 3);
    u8g2.drawLine(84, 1, 86, 3);
    u8g2.drawLine(79, 7, 86, 7);
    u8g2.drawLine(79, 7, 81, 9);
  } else if ((millis() / 500) % 2) {
    u8g2.drawDisc(82, 5, 2);
  }
  if (v.timeKnown) {  // 12-hour clock ("8:57" + small "PM"), right-aligned
    const CivilTime c = civilFromEpoch((int64_t)v.epoch + OLED_CLOCK_ADJUST_S + (int64_t)v.tzOffsetMin * 60);
    const unsigned h = c.hour % 24u, h12 = h % 12u ? h % 12u : 12u;
    char t[6];
    snprintf(t, sizeof(t), "%u:%02u", h12, c.minute % 60u);
    const char* ap = h < 12 ? "AM" : "PM";
    u8g2.setFont(u8g2_font_4x6_tr);
    const int wAp = u8g2.getStrWidth(ap);
    u8g2.setFont(u8g2_font_6x10_tr);
    put(127 - wAp - 1 - u8g2.getStrWidth(t), 9, t);
    u8g2.setFont(u8g2_font_4x6_tr);
    put(128 - wAp, 9, ap);
    u8g2.setFont(u8g2_font_6x10_tr);
  }
  u8g2.drawHLine(0, 11, 128);

  for (int i = 0; i < 4; i++) row(i, v.row[i]);

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
  render(v);
  u8g2.sendBuffer();
}

int Display::selfTest() {
  View v = {};
  v.paired = true;
  v.timeKnown = true;
  v.epoch = 1790398717;  // 12:59 PM local after the adjust: the widest clock text
  v.tzOffsetMin = 480;
  overflows = 0;
  testing = true;
  const ContainerState states[] = {ContainerState::IDLE, ContainerState::DISPENSING, ContainerState::DONE,
                                   ContainerState::CANCELLED, ContainerState::NOT_ENOUGH, ContainerState::FAULT};
  const FaultKind faults[] = {FaultKind::NONE, FaultKind::JAM, FaultKind::TIMEOUT, FaultKind::LOAD_CELL,
                              FaultKind::NOT_CALIBRATED, FaultKind::SERVO_DRIVER};
  const int32_t weights[] = {0, -50, 12345, 99949, 99950, 123456, -9949, -9950, -123456};
  auto setRows = [&](ContainerState st, FaultKind f, int32_t w, int flags) {
    for (int i = 0; i < 4; i++) {
      RowView& r = v.row[i];
      r.state = st;
      r.fault = f;
      r.weightG = w;
      r.hxOk = flags & 1;
      r.calibrated = flags & 2;
      r.mode = (flags >> 2) & 1;
      r.locked = r.stopLatched = flags & 8;
      r.selectedKg = 5;
      r.progressPct = 100;
      r.remainingMs = 600000;
    }
  };
  // 1) every title (all rows busy), 2) every state x fault x flag, 3) every weight format.
  for (int title = 0; title < 4; title++) {
    v.interlockShown = title == 1;
    v.selectorFault = title == 2;
    v.pcaOk = title != 3;
    setRows(ContainerState::DISPENSING, FaultKind::NONE, 12345, 3);
    render(v);
  }
  v.interlockShown = v.selectorFault = false;
  v.pcaOk = true;
  for (ContainerState st : states)
    for (FaultKind f : faults)
      for (int flags = 0; flags < 16; flags++) {
        setRows(st, f, 12345, flags);
        render(v);
      }
  for (int32_t w : weights)
    for (int flags = 0; flags < 4; flags++) {
      setRows(ContainerState::IDLE, FaultKind::NONE, w, flags);
      render(v);
    }
  testing = false;
  u8g2.clearBuffer();
  return overflows;
}
