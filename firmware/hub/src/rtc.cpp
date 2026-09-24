#include "rtc.h"

#include <TileTime.h>
#include <Wire.h>
#include <math.h>

#include "pins.h"

namespace rtc {

namespace {

constexpr uint8_t ADDR = 0x68;
constexpr uint8_t REG_TIME = 0x00;
constexpr uint8_t REG_CONTROL = 0x0E;
constexpr uint8_t REG_STATUS = 0x0F;
constexpr uint8_t REG_TEMP = 0x11;
constexpr uint8_t STATUS_OSF = 0x80;  // oscillator stopped (battery removed/dead) -> time invalid

bool present = false;

uint8_t fromBcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
uint8_t toBcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

bool readRegs(uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(ADDR, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

bool writeReg(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

bool readRaw(uint32_t& epoch, bool& osf) {
  uint8_t r[7], st;
  if (!readRegs(REG_TIME, r, 7) || !readRegs(REG_STATUS, &st, 1)) return false;
  osf = st & STATUS_OSF;
  tile::CivilTime c;
  c.second = fromBcd(r[0] & 0x7F);
  c.minute = fromBcd(r[1] & 0x7F);
  if (r[2] & 0x40) {  // 12-hour mode (we always write 24 h, but be safe)
    uint8_t h = fromBcd(r[2] & 0x1F) % 12;
    c.hour = (r[2] & 0x20) ? h + 12 : h;
  } else {
    c.hour = fromBcd(r[2] & 0x3F);
  }
  c.day = fromBcd(r[4] & 0x3F);
  c.month = fromBcd(r[5] & 0x1F);
  c.year = 2000 + fromBcd(r[6]) + ((r[5] & 0x80) ? 100 : 0);
  if (c.month < 1 || c.month > 12 || c.day < 1 || c.day > 31 || c.hour > 23 || c.minute > 59 || c.second > 59)
    return false;
  const int64_t t = tile::epochFromCivil(c);
  if (t < 0 || t > 0xFFFFFFFFLL) return false;
  epoch = (uint32_t)t;
  return true;
}

}  // namespace

Status begin(uint32_t& epochOut) {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(100000);
  Wire.beginTransmission(ADDR);
  present = Wire.endTransmission() == 0;
  if (!present) return Status::MISSING;

  uint8_t ctrl;
  if (readRegs(REG_CONTROL, &ctrl, 1) && (ctrl & 0x80)) writeReg(REG_CONTROL, ctrl & ~0x80);  // make sure EOSC runs

  uint32_t t;
  bool osf;
  if (!readRaw(t, osf) || osf || !tile::epochValid(t)) return Status::LOST_POWER;
  epochOut = t;
  return Status::OK;
}

bool read(uint32_t& epoch) {
  if (!present) return false;
  bool osf;
  return readRaw(epoch, osf) && !osf && tile::epochValid(epoch);
}

bool write(uint32_t epoch) {
  if (!present || !tile::epochValid(epoch)) return false;
  const tile::CivilTime c = tile::civilFromEpoch(epoch);
  Wire.beginTransmission(ADDR);
  Wire.write(REG_TIME);
  Wire.write(toBcd(c.second));
  Wire.write(toBcd(c.minute));
  Wire.write(toBcd(c.hour));             // 24-hour mode (bit 6 = 0)
  Wire.write(toBcd(c.weekday + 1));      // DS3231 day-of-week 1..7
  Wire.write(toBcd(c.day));
  Wire.write(toBcd(c.month) | (c.year >= 2100 ? 0x80 : 0));
  Wire.write(toBcd((uint8_t)(c.year % 100)));
  if (Wire.endTransmission() != 0) return false;
  uint8_t st;
  if (!readRegs(REG_STATUS, &st, 1)) return false;
  return writeReg(REG_STATUS, st & ~STATUS_OSF);  // time is now valid
}

float temperature() {
  uint8_t r[2];
  if (!present || !readRegs(REG_TEMP, r, 2)) return NAN;
  return (int8_t)r[0] + (r[1] >> 6) * 0.25f;
}

const char* statusName(Status s) {
  switch (s) {
    case Status::MISSING: return "missing";
    case Status::LOST_POWER: return "lost-power";
    case Status::OK: return "ok";
  }
  return "missing";
}

}  // namespace rtc
