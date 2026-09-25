// Tile Machine — Shredder (esp1). Spec: docs/modules/shredder.md
//
// Loop order is deliberate: STOP is read first, then the mode switch, then the state machine,
// then a safety net that forces the relay OFF unless the state is a RUNNING state.
// Nothing here blocks; the hub link never influences the shredder except STOP/config/identify.

#include <Arduino.h>
#include <Preferences.h>

#include <ModuleLink.h>
#include <TileConfig.h>
#include <TileProtocol.h>

#include "buzzer.h"
#include "config.h"
#include "display.h"
#include "inputs.h"
#include "pins.h"

using namespace tile;

namespace {

ModuleLink hubLink(ModuleId::SHREDDER, SHREDDER_FW);
Buzzer buzzer;
Button startBtn, stopBtn;
ModeSwitch modeSw;
IrSensor ir;
Display display;

// ---------------- config (NVS) ----------------
ConfigShredder cfg = defaultShredderConfig();
ConfigShredder pendingCfg;
bool hasPending = false;

void saveConfig() {
  Preferences p;
  p.begin("shredder", false);
  p.putBytes("cfg", &cfg, sizeof(cfg));
  p.end();
}

void loadConfig() {
  Preferences p;
  p.begin("shredder", false);
  ConfigShredder c;
  if (p.isKey("cfg") && p.getBytes("cfg", &c, sizeof(c)) == sizeof(c) && validShredderConfig(c)) {
    cfg = c;
    Serial.printf("[STATE] config v%u loaded from flash\n", (unsigned)cfg.configVersion);
  } else {
    Serial.println("[STATE] no saved config, using defaults");
  }
  p.end();
}

void useConfig() {
  ir.setDebounce(cfg.irDebounceMs);
  buzzer.setVolume(cfg.buzzerVolumePct);
  hubLink.setConfigVersion(cfg.configVersion);
}

void applyConfig(const ConfigShredder& c) {
  cfg = c;
  useConfig();
  saveConfig();
  Serial.printf("[STATE] config v%u applied: start delay %u ms, empty stop %u ms, confirm %u ms, IR %u ms, vol %u%%\n",
                (unsigned)cfg.configVersion, cfg.autoStartDelayMs, cfg.autoEmptyStopDelayMs,
                cfg.manualConfirmTimeoutMs, cfg.irDebounceMs, cfg.buzzerVolumePct);
  hubLink.sendEvent(EventCode::CONFIG_APPLIED, (int32_t)cfg.configVersion);
}

// ---------------- state ----------------
ShredderState state = ShredderState::OFF;
uint32_t deadlineMs = 0;       // AUTO_WARNING start time / MANUAL_CONFIRM timeout
uint32_t deadlineTotalMs = 0;
bool lastCheckLoaded = false;
bool relayOn = false;
uint32_t relayOnSinceMs = 0;
uint32_t lastTickMs = 0;
uint32_t lastWarnBeepMs = 0;
uint32_t identifyUntilMs = 0;
const char* flashMsg = nullptr;
uint32_t flashUntilMs = 0;
uint32_t rebootAtMs = 0;

bool isRunningState(ShredderState s) { return s == ShredderState::MANUAL_RUNNING || s == ShredderState::AUTO_RUNNING; }

/** States where nothing is in progress, so a new config may be applied (safety invariant 6). */
bool isIdle() {
  return !relayOn && (state == ShredderState::INTERLOCK || state == ShredderState::OFF ||
                      state == ShredderState::MANUAL_IDLE || state == ShredderState::AUTO_WAITING ||
                      state == ShredderState::AUTO_ESTOP);
}

void writeRelay(bool on) { digitalWrite(PIN_RELAY, (on != RELAY_ACTIVE_LOW) ? HIGH : LOW); }

void setRelay(bool on, const char* why, bool withSound = true) {
  if (on == relayOn) return;
  relayOn = on;
  writeRelay(on);
  const uint32_t now = millis();
  Serial.printf("[OUTPUT] relay %s (%s)\n", on ? "ON" : "OFF", why);
  if (on) {
    relayOnSinceMs = now;
    lastTickMs = now;
    if (withSound) buzzer.play(TONES(sounds::RELAY_ON));
    hubLink.sendEvent(EventCode::RUN_STARTED);
  } else {
    if (withSound) buzzer.play(TONES(sounds::RELAY_OFF));
    hubLink.sendEvent(EventCode::RUN_FINISHED, (int32_t)((now - relayOnSinceMs) / 1000));
  }
}

void enter(ShredderState s, const char* why) {
  if (s == state) return;
  Serial.printf("[STATE] %s -> %s (%s)\n", shredderStateName((uint8_t)state), shredderStateName((uint8_t)s), why);
  state = s;
}

void flash(const char* msg) {
  flashMsg = msg;
  flashUntilMs = millis() + RESULT_FLASH_MS;
}

ShredderState initialFor(ShredderMode m) {
  switch (m) {
    case ShredderMode::MANUAL: return ShredderState::MANUAL_IDLE;
    case ShredderMode::AUTO: return ShredderState::AUTO_WAITING;
    default: return ShredderState::OFF;
  }
}

/** Physical STOP and web STOP are handled identically (safety invariant 5). */
void doStop(bool remote) {
  const bool wasActive = relayOn || state == ShredderState::AUTO_WARNING || state == ShredderState::MANUAL_CONFIRM;
  setRelay(false, remote ? "remote STOP" : "STOP button", false);
  buzzer.play(TONES(sounds::ESTOP), Level::FULL, true);
  if (wasActive) hubLink.sendEvent(EventCode::ESTOP_PRESSED, remote ? 1 : 0);

  switch (state) {
    case ShredderState::MANUAL_CONFIRM:
    case ShredderState::MANUAL_RUNNING:
      enter(ShredderState::MANUAL_IDLE, "STOP");
      flash("STOPPED");
      break;
    case ShredderState::AUTO_WARNING:
    case ShredderState::AUTO_RUNNING:
      enter(ShredderState::AUTO_ESTOP, "STOP");
      break;
    case ShredderState::AUTO_WAITING:
      if (ir.detected()) enter(ShredderState::AUTO_ESTOP, "STOP with material in chute");
      break;
    default:
      break;
  }
}

void updateLogic() {
  const uint32_t now = millis();

  // 1. STOP first, always.
  if (stopBtn.pressed()) {
    Serial.println("[INPUT] STOP pressed");
    doStop(false);
  }
  bool startEdge = startBtn.pressed();
  if (startEdge) Serial.println("[INPUT] START pressed");

  if (ir.update()) Serial.printf("[INPUT] IR %s\n", ir.detected() ? "material DETECTED" : "clear");

  // 2. Mode switch: any change drops the relay first (safety invariant 4).
  if (modeSw.update()) {
    const ShredderMode pos = modeSw.position();
    Serial.printf("[INPUT] switch -> %s%s\n", shredderModeName((uint8_t)pos), modeSw.wiringFault() ? " (WIRING FAULT: both contacts closed)" : "");
    if (state == ShredderState::INTERLOCK) {
      if (pos == ShredderMode::OFF) {
        enter(ShredderState::OFF, "switch seen at OFF, interlock released");
        buzzer.play(TONES(sounds::CLICK));
      }
    } else {
      setRelay(false, "mode change", false);
      buzzer.play(TONES(sounds::MODE_CHANGE));
      enter(initialFor(pos), "mode change");
    }
    startEdge = false;  // never act on a START in the same instant as a mode change
  }

  // 3. State machine.
  switch (state) {
    case ShredderState::INTERLOCK:
    case ShredderState::OFF:
      if (startEdge) buzzer.play(TONES(sounds::INTERLOCK));  // "not now"
      break;

    case ShredderState::MANUAL_IDLE:
      if (startEdge) {
        lastCheckLoaded = ir.detected();
        Serial.printf("[STATE] chute check: %s\n", lastCheckLoaded ? "LOADED" : "EMPTY");
        if (lastCheckLoaded) buzzer.play(TONES(sounds::LOADED));
        else buzzer.play(TONES(sounds::EMPTY));
        deadlineTotalMs = cfg.manualConfirmTimeoutMs;
        deadlineMs = now + deadlineTotalMs;
        enter(ShredderState::MANUAL_CONFIRM, "START: chute checked");
      }
      break;

    case ShredderState::MANUAL_CONFIRM:
      if (startEdge) {
        enter(ShredderState::MANUAL_RUNNING, "START confirmed");
        setRelay(true, "manual run confirmed");
      } else if ((int32_t)(now - deadlineMs) >= 0) {
        buzzer.play(TONES(sounds::CANCEL));
        flash("TIMED OUT");
        enter(ShredderState::MANUAL_IDLE, "no confirmation in time");
      }
      break;

    case ShredderState::MANUAL_RUNNING:  // runs until STOP or mode change; IR is ignored
      break;

    case ShredderState::AUTO_WAITING:
      if (ir.detected()) {
        deadlineTotalMs = cfg.autoStartDelayMs;
        deadlineMs = now + deadlineTotalMs;
        lastWarnBeepMs = now;
        buzzer.play(TONES(sounds::LOADED));
        enter(ShredderState::AUTO_WARNING, "material detected");
      }
      break;

    case ShredderState::AUTO_WARNING: {
      if (!ir.detected()) {
        buzzer.play(TONES(sounds::CANCEL));
        flash("CANCELLED");
        enter(ShredderState::AUTO_WAITING, "material gone before start");
        break;
      }
      const int32_t remaining = (int32_t)(deadlineMs - now);
      if (remaining <= 0) {
        enter(ShredderState::AUTO_RUNNING, "warning countdown done");
        setRelay(true, "auto start");
        break;
      }
      const uint32_t interval = remaining <= 1000 ? 250 : 1000;
      if (now - lastWarnBeepMs >= interval) {
        lastWarnBeepMs = now;
        if (remaining <= 1000) buzzer.play(TONES(sounds::WARN_FAST));
        else buzzer.play(TONES(sounds::WARN_TICK));
      }
      break;
    }

    case ShredderState::AUTO_RUNNING:
      if (!ir.detected() && ir.stateAgeMs() >= cfg.autoEmptyStopDelayMs) {
        setRelay(false, "chute empty");
        enter(ShredderState::AUTO_WAITING, "chute empty");
      }
      break;

    case ShredderState::AUTO_ESTOP:  // latched until the chute is clear (legacy behaviour)
      if (!ir.detected()) {
        buzzer.play(TONES(sounds::CLICK));
        enter(ShredderState::AUTO_WAITING, "chute clear, E-STOP released");
      }
      break;
  }

  // 4. Safety net: the relay may only be on in a RUNNING state. It only ever switches OFF.
  if (relayOn && !isRunningState(state)) setRelay(false, "safety net");
  if (!relayOn && isRunningState(state))
    enter(state == ShredderState::AUTO_RUNNING ? ShredderState::AUTO_WAITING : ShredderState::MANUAL_IDLE,
          "safety net: running state without relay");

  // Quiet reminder while the motor runs.
  if (relayOn && now - lastTickMs >= RUNNING_TICK_MS && !buzzer.busy()) {
    lastTickMs = now;
    buzzer.play(TONES(sounds::RUNNING_TICK), Level::QUIET);
  }

  // Queued web config, applied only when idle (safety invariant 6).
  if (hasPending && isIdle()) {
    hasPending = false;
    applyConfig(pendingCfg);
  }
}

// ---------------- hub link handlers ----------------
AckResult onConfig(const uint8_t* payload, size_t len) {
  if (len != sizeof(ConfigShredder)) return AckResult::REJECTED;
  ConfigShredder c;
  memcpy(&c, payload, sizeof(c));
  if (!validShredderConfig(c)) {
    Serial.printf("[ERROR] config v%u rejected: value out of range\n", (unsigned)c.configVersion);
    return AckResult::REJECTED;
  }
  if (c.configVersion == cfg.configVersion && !hasPending) return AckResult::OK;
  if (isIdle()) {
    applyConfig(c);
    return AckResult::OK;
  }
  pendingCfg = c;
  hasPending = true;
  Serial.printf("[STATE] config v%u queued until the shredder is idle\n", (unsigned)c.configVersion);
  return AckResult::BUSY_QUEUED;
}

AckResult onCommand(const CommandPayload& c) {
  Serial.printf("[NET] command %s\n", cmdName(c.cmd));
  switch ((Cmd)c.cmd) {
    case Cmd::STOP:
      doStop(true);
      return AckResult::OK;
    case Cmd::IDENTIFY:
      identifyUntilMs = millis() + IDENTIFY_MS;
      buzzer.play(TONES(sounds::IDENTIFY));
      return AckResult::OK;
    case Cmd::REBOOT:
      if (!isIdle()) return AckResult::REJECTED;
      rebootAtMs = millis() + 300;  // after the ACK has gone out
      return AckResult::OK;
    default:
      return AckResult::UNSUPPORTED;  // TARE / CALIBRATE are Containing-only
  }
}

// ---------------- status + display ----------------
uint32_t countdownRemaining() {
  if (state != ShredderState::AUTO_WARNING && state != ShredderState::MANUAL_CONFIRM) return 0;
  const int32_t r = (int32_t)(deadlineMs - millis());
  return r > 0 ? (uint32_t)r : 0;
}

void publishStatus() {
  StatusShredder s = {};
  s.c.uptimeS = millis() / 1000;
  s.c.configVersion = cfg.configVersion;
  s.c.faults = (modeSw.wiringFault() ? 0x01 : 0) | (display.present() ? 0 : 0x02);
  s.c.interlock = state == ShredderState::INTERLOCK;
  s.mode = (uint8_t)modeSw.position();
  s.state = (uint8_t)state;
  s.relayOn = relayOn;
  s.irDetected = ir.detected();
  s.estopLatched = state == ShredderState::AUTO_ESTOP;
  s.countdownMs = (uint16_t)min<uint32_t>(countdownRemaining(), 65535);
  hubLink.publishStatus(&s, sizeof(s));
}

uint32_t lastFrameMs = 0;

void updateDisplay() {
  const uint32_t now = millis();
  if (now - lastFrameMs < FRAME_MS) return;
  lastFrameMs = now;
  if (flashMsg && (int32_t)(now - flashUntilMs) >= 0) flashMsg = nullptr;

  View v;
  v.state = state;
  v.mode = modeSw.position();
  v.relayOn = relayOn;
  v.irDetected = ir.detected();
  v.lastCheckLoaded = lastCheckLoaded;
  v.countdownMs = countdownRemaining();
  v.countdownTotalMs = deadlineTotalMs;
  v.paired = hubLink.paired();
  v.timeKnown = hubLink.timeKnown();
  v.epoch = hubLink.nowEpoch();
  v.tzOffsetMin = hubLink.tzOffsetMin();
  v.identify = (int32_t)(identifyUntilMs - now) > 0;
  v.flash = flashMsg;
  v.switchFault = modeSw.wiringFault();
  display.draw(v);
}

}  // namespace

void setup() {
  // Safety first: relay OFF before anything else (safety invariant 2).
  pinMode(PIN_RELAY, OUTPUT);
  writeRelay(false);

  Serial.begin(115200);
  delay(200);  // setup only
  char fw[12];
  fwDecode(SHREDDER_FW, fw, sizeof(fw));
  Serial.printf("\n=== Tile Machine SHREDDER fw %s ===\n", fw);

  pinMode(PIN_STATUS_LED, OUTPUT);
  buzzer.begin(PIN_BUZZER, BUZZER_LEDC_CHANNEL, BUZZER_ACTIVE_LOW);
  startBtn.begin(PIN_START, false, BUTTON_DEBOUNCE_MS);
  stopBtn.begin(PIN_STOP, STOP_IS_NC, BUTTON_DEBOUNCE_MS);
  modeSw.begin(PIN_SW_AUTO, PIN_SW_MANUAL, SWITCH_SETTLE_MS);
  ir.begin(PIN_IR, IR_ACTIVE_LOW);

  loadConfig();
  useConfig();

  if (!display.begin()) {
    Serial.println("[ERROR] OLED not found at 0x3C (SDA 21 / SCL 22): running without display");
  } else {
    const int bad = display.selfTest();
    Serial.printf("[STATE] OLED layout self-test: %s\n", bad ? "OVERFLOW (see errors above)" : "all screens fit");
  }

  // Power-up interlock (safety invariant 3): the switch must be seen at OFF first.
  if (modeSw.position() != ShredderMode::OFF || modeSw.wiringFault()) {
    state = ShredderState::INTERLOCK;
    Serial.printf("[STATE] INTERLOCK: switch is at %s at power-up, turn it to OFF first\n",
                  shredderModeName((uint8_t)modeSw.position()));
    buzzer.play(TONES(sounds::INTERLOCK));
  } else {
    state = ShredderState::OFF;
    buzzer.play(TONES(sounds::BOOT));
  }

  hubLink.onConfig(onConfig);
  hubLink.onCommand(onCommand);
  hubLink.begin();
  Serial.println("[STATE] ready");
}

void loop() {
  updateLogic();  // safety-relevant work first
  buzzer.update();
  hubLink.loop();
  publishStatus();
  updateDisplay();

  const uint32_t now = millis();
  digitalWrite(PIN_STATUS_LED, hubLink.paired() ? HIGH : ((now / 500) % 2 ? HIGH : LOW));
  if (rebootAtMs && (int32_t)(now - rebootAtMs) >= 0 && isIdle()) ESP.restart();
}
