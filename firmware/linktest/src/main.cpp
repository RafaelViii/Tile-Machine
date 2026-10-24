// Link test for module boards: pairs with the hub, reports a safe idle STATUS, accepts CONFIG
// (validated exactly like the real firmware will) and answers commands. No machinery is driven.
//
// Status LED (GPIO2): slow blink = searching for hub, solid = paired, fast blink = IDENTIFY.
// I2C (SDA 21 / SCL 22): scanned at boot (serial + OLED list every chip found, a wiring check), and the
// SH1106 OLED (0x3C, or 0x3D if that's where it answers) shows the link status.

#include <Arduino.h>
#include <ModuleLink.h>
#include <TileConfig.h>
#include <U8g2lib.h>
#include <Wire.h>

#ifndef LINKTEST_MODULE
#error "build with -DLINKTEST_MODULE=1|2|3 (see platformio.ini)"
#endif

using namespace tile;

namespace {

constexpr ModuleId MODULE = (ModuleId)LINKTEST_MODULE;
constexpr uint16_t FW = fwEncode(0, 1, 1);
constexpr uint8_t PIN_LED = 2;
constexpr uint8_t PIN_I2C_SDA = 21;
constexpr uint8_t PIN_I2C_SCL = 22;

ModuleLink hubLink(MODULE, FW);
uint32_t appliedVersion = 0;
uint32_t identifyUntilMs = 0;
uint32_t rebootAtMs = 0;

U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
uint8_t oledAddr = 0;    // 0 = no OLED answered on I2C
char i2cFound[40] = "";  // "3C 40" = addresses that answered the boot scan
uint32_t lastFrameMs = 0;

const char* i2cName(uint8_t a) {
  switch (a) {
    case 0x3C:
    case 0x3D: return "OLED SH1106";
    case 0x40: return "PCA9685 servo driver";
    case 0x70: return "PCA9685 all-call";
    case 0x57: return "AT24C32 EEPROM";
    case 0x68: return "DS3231 RTC";
    default: return "unknown";
  }
}

/** Lists every I2C chip that answers (wiring check), and picks the OLED's address. */
void scanI2c() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(100000);
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() != 0) continue;
    n++;
    Serial.printf("[STATE] I2C 0x%02X answers: %s\n", a, i2cName(a));
    char hex[4];
    snprintf(hex, sizeof(hex), "%02X ", a);
    strlcat(i2cFound, hex, sizeof(i2cFound));
    if ((a == 0x3C || a == 0x3D) && !oledAddr) oledAddr = a;
  }
  if (!n) Serial.println("[ERROR] no I2C device answers on SDA 21 / SCL 22: check wiring and 3.3 V");
  if (!oledAddr) Serial.println("[ERROR] no OLED at 0x3C/0x3D: screen stays blank");
  if (oledAddr) {
    oled.setI2CAddress(oledAddr << 1);
    oled.begin();
    oled.setBusClock(400000);
    Serial.printf("[STATE] OLED found at 0x%02X\n", oledAddr);
  }
}

void drawOled() {
  const uint32_t now = millis();
  if (!oledAddr || now - lastFrameMs < 200) return;
  lastFrameMs = now;
  char line[48];  // fits "I2C: " + the whole scan list (the screen shows the first 21 characters)
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tr);
  snprintf(line, sizeof(line), "LINK TEST %s", moduleKey(MODULE));
  oled.drawStr(0, 10, line);
  oled.drawHLine(0, 13, 128);
  oled.setFont(u8g2_font_helvB12_tr);
  oled.drawStr(0, 31, hubLink.paired() ? "PAIRED" : "SEARCHING HUB");
  oled.setFont(u8g2_font_6x10_tr);
  snprintf(line, sizeof(line), hubLink.paired() ? "Hub on channel %u" : "Trying channel %u", hubLink.channel());
  oled.drawStr(0, 44, line);
  snprintf(line, sizeof(line), "I2C: %s", i2cFound[0] ? i2cFound : "none");
  oled.drawStr(0, 54, line);
  oled.drawStr(0, 64, "Test only: outputs OFF");
  if ((int32_t)(identifyUntilMs - now) > 0 && (now / 200) % 2) {  // IDENTIFY: blink the whole screen
    oled.setDrawColor(2);
    oled.drawBox(0, 0, 128, 64);
    oled.setDrawColor(1);
  }
  oled.sendBuffer();
}

// Hold every actuator of this board in its safe OFF state (pins from docs/HARDWARE.md).
void holdOutputsSafe() {
  switch (MODULE) {
    case ModuleId::SHREDDER:  // relay GPIO27, RELAY_ACTIVE_LOW = false -> LOW = off
      pinMode(27, OUTPUT);
      digitalWrite(27, LOW);
      break;
    case ModuleId::CONTAINING:  // PCA9685 OE GPIO23, active LOW -> HIGH = all servo outputs disabled
      pinMode(23, OUTPUT);
      digitalWrite(23, HIGH);
      break;
    case ModuleId::HOTPRESS:  // relays GPIO18 / GPIO19, RELAY_ACTIVE_LOW = false -> LOW = off
      pinMode(18, OUTPUT);
      digitalWrite(18, LOW);
      pinMode(19, OUTPUT);
      digitalWrite(19, LOW);
      break;
    default:
      break;
  }
}

AckResult handleConfig(const uint8_t* payload, size_t len) {
  if (len != configSizeFor(MODULE)) return AckResult::REJECTED;
  bool ok = false;
  uint32_t version = 0;
  switch (MODULE) {
    case ModuleId::SHREDDER: {
      ConfigShredder c;
      memcpy(&c, payload, sizeof(c));
      ok = validShredderConfig(c);
      version = c.configVersion;
      break;
    }
    case ModuleId::CONTAINING: {
      ConfigContaining c;
      memcpy(&c, payload, sizeof(c));
      ok = validContainingConfig(c);
      version = c.configVersion;
      break;
    }
    case ModuleId::HOTPRESS: {
      ConfigHotpress c;
      memcpy(&c, payload, sizeof(c));
      ok = validHotpressConfig(c);
      version = c.configVersion;
      break;
    }
    default:
      break;
  }
  Serial.printf("[NET] config v%u %s\n", (unsigned)version, ok ? "accepted" : "REJECTED (out of range)");
  if (!ok) return AckResult::REJECTED;
  appliedVersion = version;
  hubLink.setConfigVersion(version);
  return AckResult::OK;
}

AckResult handleCommand(const CommandPayload& c) {
  Serial.printf("[NET] command %s target %u arg %ld\n", cmdName(c.cmd), c.target, (long)c.arg);
  switch ((Cmd)c.cmd) {
    case Cmd::STOP:
      holdOutputsSafe();  // nothing runs in the link test, outputs are already off
      return AckResult::OK;
    case Cmd::IDENTIFY:
      identifyUntilMs = millis() + 3000;
      return AckResult::OK;
    case Cmd::TARE:
    case Cmd::CALIBRATE:
      return MODULE == ModuleId::CONTAINING ? AckResult::OK : AckResult::UNSUPPORTED;
    case Cmd::REBOOT:
      rebootAtMs = millis() + 300;  // after the ACK has gone out
      return AckResult::OK;
  }
  return AckResult::UNSUPPORTED;
}

void fillCommon(StatusCommon& c) {
  c.uptimeS = millis() / 1000;
  c.configVersion = appliedVersion;
  c.faults = 0;
  c.interlock = 0;
}

void publish() {
  switch (MODULE) {
    case ModuleId::SHREDDER: {
      StatusShredder s = {};
      fillCommon(s.c);
      s.mode = (uint8_t)ShredderMode::OFF;
      s.state = (uint8_t)ShredderState::OFF;
      hubLink.publishStatus(&s, sizeof(s));
      break;
    }
    case ModuleId::CONTAINING: {
      StatusContaining s = {};
      fillCommon(s.c);
      s.selector = (uint8_t)Selector::NEUTRAL;
      s.hxOkMask = 0;  // no load cells read in the link test
      s.pcaOk = 0;
      for (int i = 0; i < 4; i++) {
        s.ct[i].state = (uint8_t)ContainerState::IDLE;
        s.ct[i].selectedKg = i < 2 ? 1 : 0;
        s.calFactor[i] = 1.0f;
      }
      hubLink.publishStatus(&s, sizeof(s));
      break;
    }
    case ModuleId::HOTPRESS: {
      StatusHotpress s = {};
      fillCommon(s.c);
      s.selector = (uint8_t)Selector::NEUTRAL;
      hubLink.publishStatus(&s, sizeof(s));
      break;
    }
    default:
      break;
  }
}

}  // namespace

void setup() {
  holdOutputsSafe();  // first thing: machinery stays off
  pinMode(PIN_LED, OUTPUT);
  Serial.begin(115200);
  delay(200);
  char fw[12];
  fwDecode(FW, fw, sizeof(fw));
  Serial.printf("\n=== Tile Machine LINK TEST: %s, fw %s ===\n", moduleKey(MODULE), fw);
  scanI2c();

  hubLink.onConfig(handleConfig);
  hubLink.onCommand(handleCommand);
  hubLink.begin();
}

void loop() {
  hubLink.loop();
  publish();
  drawOled();

  const uint32_t now = millis();
  bool led;
  if ((int32_t)(identifyUntilMs - now) > 0) led = (now / 100) % 2;
  else if (hubLink.paired()) led = true;
  else led = (now / 500) % 2;
  digitalWrite(PIN_LED, led ? HIGH : LOW);

  if (rebootAtMs && (int32_t)(now - rebootAtMs) >= 0) ESP.restart();
}
