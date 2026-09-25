// Tile Machine — Hot Press · Designing · Curing (esp3). Spec: docs/modules/hotpress.md
//
// Two independent outputs:
//   SSR 1 (Designing + Curing) follows the latching ON button.
//   SSR 2 (Hot Press) follows the selector: LEFT = heating, middle = cooling (off),
//   RIGHT = AUTO (placeholder: on, like LEFT, until the AUTO logic is designed).
// Each output has its own power-up interlock and its own web-STOP latch. Outputs are
// recomputed from scratch every loop, and anything not explicitly allowed is OFF.

#include <Arduino.h>
#include <Preferences.h>

#include <ModuleLink.h>
#include <TileConfig.h>
#include <TileProtocol.h>

#include "config.h"
#include "display.h"
#include "inputs.h"
#include "pins.h"

using namespace tile;

namespace {

ModuleLink hubLink(ModuleId::HOTPRESS, HOTPRESS_FW);
OnButton onBtn;
SelectorSwitch selector;
Display display;

ConfigHotpress cfg = defaultHotpressConfig();

/** Lock state of one output: power-up interlock + web STOP latch. */
struct Guard {
  explicit Guard(const char* n) : name(n) {}
  const char* name;
  bool interlocked = true;       // cleared once the input has read OFF continuously for the hold time
  bool interlockShown = false;   // input was not OFF after the hold time: operator must act
  uint32_t offSinceMs = 0;
  bool stopLatched = false;      // web STOP: cleared when the input goes back to OFF
};

Guard designGuard{"designing+curing"};
Guard pressGuard{"hot press"};

bool ssrDesign = false, ssrPress = false;
uint32_t designOnSinceMs = 0, pressOnSinceMs = 0;
uint32_t bootMs = 0;
uint32_t identifyUntilMs = 0;
uint32_t rebootAtMs = 0;

void writeSsr(uint8_t pin, bool on) { digitalWrite(pin, (on != RELAY_ACTIVE_LOW) ? HIGH : LOW); }

void setSsr(bool& current, uint8_t pin, bool on, uint32_t& sinceMs, uint8_t unit, const char* name, const char* why) {
  if (on == current) return;
  current = on;
  writeSsr(pin, on);
  Serial.printf("[OUTPUT] %s SSR %s (%s)\n", name, on ? "ON" : "OFF", why);
  if (on) {
    sinceMs = millis();
    hubLink.sendEvent(EventCode::RUN_STARTED, unit);
  } else {
    hubLink.sendEvent(EventCode::RUN_FINISHED, unit, (int32_t)((millis() - sinceMs) / 1000));
  }
}

/** Power-up interlock for one output: release only after `inputOff` held for INTERLOCK_OFF_HOLD_MS. */
void updateGuard(Guard& g, bool inputOff, const char* hint) {
  const uint32_t now = millis();
  if (g.interlocked) {
    if (!inputOff) {
      g.offSinceMs = 0;
      if (!g.interlockShown && now - bootMs >= INTERLOCK_OFF_HOLD_MS) {
        g.interlockShown = true;
        Serial.printf("[STATE] %s INTERLOCK: %s first\n", g.name, hint);
      }
    } else if (!g.offSinceMs) {
      g.offSinceMs = now ? now : 1;
    } else if (now - g.offSinceMs >= INTERLOCK_OFF_HOLD_MS) {
      g.interlocked = false;
      Serial.printf("[STATE] %s interlock released\n", g.name);
    }
  }
  if (g.stopLatched && inputOff) {
    g.stopLatched = false;
    Serial.printf("[STATE] %s STOP latch cleared (input back to OFF)\n", g.name);
  }
}

void updateLogic() {
  if (onBtn.update()) Serial.printf("[INPUT] ON button %s\n", onBtn.on() ? "ON" : "OFF");
  if (selector.update())
    Serial.printf("[INPUT] selector -> %s%s\n", selectorName((uint8_t)selector.position()),
                  selector.wiringFault() ? " (WIRING FAULT: both contacts closed)" : "");

  const bool btnOff = !onBtn.on();
  const bool selMiddle = selector.position() == Selector::NEUTRAL && !selector.wiringFault();
  updateGuard(designGuard, btnOff, "turn the ON button OFF");
  updateGuard(pressGuard, selMiddle, "set the selector to the middle");

  // Outputs from scratch: ON only when the input asks for it AND nothing locks it.
  const bool wantDesign = onBtn.on() && !designGuard.interlocked && !designGuard.stopLatched;
  const bool wantPress = !selMiddle && !selector.wiringFault() && !pressGuard.interlocked && !pressGuard.stopLatched;
  // AUTO (RIGHT) placeholder behaves like LEFT until the AUTO logic is designed (cfg.autoModeBehaviour == 0).
  setSsr(ssrDesign, PIN_SSR_DESIGN_CURE, wantDesign, designOnSinceMs, 0, "designing+curing", wantDesign ? "ON button" : "off");
  setSsr(ssrPress, PIN_SSR_HOTPRESS, wantPress, pressOnSinceMs, 1, "hot press",
         wantPress ? (selector.position() == Selector::RIGHT ? "AUTO (placeholder: on)" : "selector LEFT") : "off");
}

// ---------------- hub link ----------------
bool isIdle() { return !ssrDesign && !ssrPress; }

void doStop() {
  const bool wasOn = ssrDesign || ssrPress;
  // Latch each output whose input is still asking for it, so it can't come back on by itself.
  if (onBtn.on()) designGuard.stopLatched = true;
  if (selector.position() != Selector::NEUTRAL) pressGuard.stopLatched = true;
  setSsr(ssrDesign, PIN_SSR_DESIGN_CURE, false, designOnSinceMs, 0, "designing+curing", "remote STOP");
  setSsr(ssrPress, PIN_SSR_HOTPRESS, false, pressOnSinceMs, 1, "hot press", "remote STOP");
  if (wasOn) hubLink.sendEvent(EventCode::ESTOP_PRESSED, 1);
}

AckResult onConfig(const uint8_t* payload, size_t len) {
  if (len != sizeof(ConfigHotpress)) return AckResult::REJECTED;
  ConfigHotpress c;
  memcpy(&c, payload, sizeof(c));
  if (!validHotpressConfig(c)) return AckResult::REJECTED;
  cfg = c;
  Preferences p;
  p.begin("hotpress", false);
  p.putBytes("cfg", &cfg, sizeof(cfg));
  p.end();
  hubLink.setConfigVersion(cfg.configVersion);
  Serial.printf("[STATE] config v%u applied\n", (unsigned)cfg.configVersion);
  hubLink.sendEvent(EventCode::CONFIG_APPLIED, (int32_t)cfg.configVersion);
  return AckResult::OK;  // nothing in this config changes a running output
}

AckResult onCommand(const CommandPayload& c) {
  Serial.printf("[NET] command %s\n", cmdName(c.cmd));
  switch ((Cmd)c.cmd) {
    case Cmd::STOP:
      doStop();
      return AckResult::OK;
    case Cmd::IDENTIFY:
      identifyUntilMs = millis() + IDENTIFY_MS;
      return AckResult::OK;
    case Cmd::REBOOT:
      if (!isIdle()) return AckResult::REJECTED;
      rebootAtMs = millis() + 300;
      return AckResult::OK;
    default:
      return AckResult::UNSUPPORTED;
  }
}

void publishStatus() {
  StatusHotpress s = {};
  s.c.uptimeS = millis() / 1000;
  s.c.configVersion = cfg.configVersion;
  s.c.faults = (selector.wiringFault() ? FAULT_SELECTOR_WIRING : 0) | (display.present() ? 0 : FAULT_OLED_MISSING);
  s.c.interlock = (designGuard.interlocked && designGuard.interlockShown) ||
                  (pressGuard.interlocked && pressGuard.interlockShown);
  s.onButton = onBtn.on();
  s.selector = (uint8_t)selector.position();
  s.relayDesignCure = ssrDesign;
  s.relayHotpress = ssrPress;
  s.stopLatched = designGuard.stopLatched || pressGuard.stopLatched;
  hubLink.publishStatus(&s, sizeof(s));
}

OutState outState(const Guard& g, bool on) {
  if (on) return OutState::ON;
  if (g.interlocked && g.interlockShown) return OutState::LOCKED;
  if (g.stopLatched) return OutState::STOPPED;
  return OutState::OFF;
}

uint32_t lastFrameMs = 0;

void updateDisplay() {
  const uint32_t now = millis();
  if (now - lastFrameMs < FRAME_MS) return;
  lastFrameMs = now;
  View v;
  v.designCure = outState(designGuard, ssrDesign);
  v.hotpress = outState(pressGuard, ssrPress);
  v.selector = selector.position();
  v.onButton = onBtn.on();
  v.paired = hubLink.paired();
  v.timeKnown = hubLink.timeKnown();
  v.epoch = hubLink.nowEpoch();
  v.tzOffsetMin = hubLink.tzOffsetMin();
  v.identify = (int32_t)(identifyUntilMs - now) > 0;
  v.selectorFault = selector.wiringFault();
  display.draw(v);
}

}  // namespace

void setup() {
  // Safety first: both SSRs OFF before anything else (safety invariant 2).
  pinMode(PIN_SSR_DESIGN_CURE, OUTPUT);
  pinMode(PIN_SSR_HOTPRESS, OUTPUT);
  writeSsr(PIN_SSR_DESIGN_CURE, false);
  writeSsr(PIN_SSR_HOTPRESS, false);

  Serial.begin(115200);
  delay(200);  // setup only
  char fw[12];
  fwDecode(HOTPRESS_FW, fw, sizeof(fw));
  Serial.printf("\n=== Tile Machine HOTPRESS fw %s ===\n", fw);

  pinMode(PIN_STATUS_LED, OUTPUT);
  onBtn.begin(PIN_ON_BUTTON, INPUT_SETTLE_MS);
  selector.begin(PIN_SEL_LEFT, PIN_SEL_RIGHT, INPUT_SETTLE_MS);

  Preferences p;
  p.begin("hotpress", false);
  ConfigHotpress c;
  if (p.isKey("cfg") && p.getBytes("cfg", &c, sizeof(c)) == sizeof(c) && validHotpressConfig(c)) cfg = c;
  p.end();
  hubLink.setConfigVersion(cfg.configVersion);

  if (!display.begin()) {
    Serial.println("[ERROR] OLED not found at 0x3C (SDA 21 / SCL 22): running without display");
  } else {
    const int bad = display.selfTest();
    Serial.printf("[STATE] OLED layout self-test: %s\n", bad ? "OVERFLOW (see errors above)" : "all screens fit");
  }

  bootMs = millis();
  Serial.printf("[STATE] power-up: ON button %s, selector %s; both outputs interlocked until OFF/middle held %lu ms\n",
                onBtn.on() ? "ON" : "OFF", selectorName((uint8_t)selector.position()), (unsigned long)INTERLOCK_OFF_HOLD_MS);

  hubLink.onConfig(onConfig);
  hubLink.onCommand(onCommand);
  hubLink.begin();
  Serial.println("[STATE] ready");
}

void loop() {
  updateLogic();  // outputs first
  hubLink.loop();
  publishStatus();
  updateDisplay();

  const uint32_t now = millis();
  digitalWrite(PIN_STATUS_LED, hubLink.paired() ? HIGH : ((now / 500) % 2 ? HIGH : LOW));
  if (rebootAtMs && (int32_t)(now - rebootAtMs) >= 0 && isIdle()) ESP.restart();
}
