// Link test for module boards: pairs with the hub, reports a safe idle STATUS, accepts CONFIG
// (validated exactly like the real firmware will) and answers commands. No machinery is driven.
//
// Status LED (GPIO2): slow blink = searching for hub, solid = paired, fast blink = IDENTIFY.

#include <Arduino.h>
#include <ModuleLink.h>
#include <TileConfig.h>

#ifndef LINKTEST_MODULE
#error "build with -DLINKTEST_MODULE=1|2|3 (see platformio.ini)"
#endif

using namespace tile;

namespace {

constexpr ModuleId MODULE = (ModuleId)LINKTEST_MODULE;
constexpr uint16_t FW = fwEncode(0, 1, 0);
constexpr uint8_t PIN_LED = 2;

ModuleLink hubLink(MODULE, FW);
uint32_t appliedVersion = 0;
uint32_t identifyUntilMs = 0;
uint32_t rebootAtMs = 0;

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

  hubLink.onConfig(handleConfig);
  hubLink.onCommand(handleCommand);
  hubLink.begin();
}

void loop() {
  hubLink.loop();
  publish();

  const uint32_t now = millis();
  bool led;
  if ((int32_t)(identifyUntilMs - now) > 0) led = (now / 100) % 2;
  else if (hubLink.paired()) led = true;
  else led = (now / 500) % 2;
  digitalWrite(PIN_LED, led ? HIGH : LOW);

  if (rebootAtMs && (int32_t)(now - rebootAtMs) >= 0) ESP.restart();
}
