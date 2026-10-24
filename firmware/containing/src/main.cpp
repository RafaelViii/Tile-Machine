// Tile Machine — Containing (esp2). Spec: docs/modules/containing.md
//
// C1 Raw HDPE / C2 Raw PP: SELECT picks 1-5 kg, CONFIRM starts (or cancels). LOADCELL mode stops once the
// container's weight dropped by the selected amount; TIME mode runs a time from the web's table.
// C3 Mixed HDPE / C4 Mixed PP: the 3-way selector (LEFT = C3, RIGHT = C4). MANUAL runs while the selector is on
// that side; TIME runs a set time, then holds until the selector has been back to the middle.
// Each container = 1 HX711 load cell + 2 continuous servos (screw extruder) on the PCA9685.
// A missing part (load cell, servo driver, OLED) is reported on the screen and the website, never fatal.

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <math.h>

#include <ModuleLink.h>
#include <PinNoise.h>
#include <TileConfig.h>
#include <TileProtocol.h>

#include "config.h"
#include "display.h"
#include "hx711.h"
#include "inputs.h"
#include "pca9685.h"
#include "pins.h"

using namespace tile;

namespace {

ModuleLink hubLink(ModuleId::CONTAINING, CONTAINING_FW);
Hx711 hx[4];
Pca9685 pca;
Display display;
PinNoiseMonitor noise;
Button selectBtn[2], confirmBtn[2];
SelectorSwitch selector;

ConfigContaining cfg = defaultContainingConfig();  // what each container currently uses
ConfigContaining pendingCfg;
uint8_t pendingMask = 0;  // bit i: container i still has to take pendingCfg (waits until it is idle)

float calFactor[4] = {0, 0, 0, 0};     // raw counts per gram; 0 = not calibrated (NVS, set by CALIBRATE)
int32_t tareOffset[4] = {0, 0, 0, 0};  // raw counts at zero (NVS, set by TARE)

struct Container {
  ContainerState state = ContainerState::IDLE;
  FaultKind fault = FaultKind::NONE;
  uint8_t kg = 1;             // raw: selected amount 1..5
  uint32_t stateMs = 0;       // when the current state began
  uint32_t startMs = 0;       // when the current run began
  int32_t startWeight = 0;
  int32_t lowestWeight = 0;   // jam detection: lowest weight so far and when it was reached
  uint32_t lowestAtMs = 0;
  int32_t dispensedG = 0;
  uint8_t pct = 0;
  uint32_t remainingMs = 0;
  uint32_t brakeUntilMs = 0;  // after a run: stopUs until then, then no pulse at all
  bool armed = false;         // mixed: the selector has been off this side since the last run
  bool stopLatched = false;   // mixed: web STOP holds until the selector goes to the middle
  bool hxWasOk = false;       // for "load cell found / lost" log lines
};
Container ct[4];

// Power-up interlock for the mixed selector (safety invariant 3).
bool selInterlocked = true;
bool selInterlockShown = false;
uint32_t selNeutralSinceMs = 0;

uint32_t bootMs = 0, identifyUntilMs = 0, rebootAtMs = 0, oeHighUntilMs = 0, lastPcaTryMs = 0, lastFrameMs = 0;
bool pcaWasOk = false, bootReportDone = false;

const char* const CT_NAME[4] = {"C1 Raw HDPE", "C2 Raw PP", "C3 Mixed HDPE", "C4 Mixed PP"};

bool isRaw(int i) { return i < 2; }
uint8_t modeOf(int i) { return isRaw(i) ? cfg.raw[i].mode : cfg.mixed[i - 2].mode; }
bool calibrated(int i) { return fabsf(calFactor[i]) > 1e-6f; }
bool weightOk(int i) { return hx[i].ok() && calibrated(i); }
int32_t weightG(int i) {
  if (!hx[i].hasAverage() || !calibrated(i)) return 0;
  return (int32_t)lroundf((float)(hx[i].average() - tareOffset[i]) / calFactor[i]);
}
bool isIdle(int i) { return ct[i].state != ContainerState::DISPENSING; }
bool allIdle() {
  for (int i = 0; i < 4; i++)
    if (!isIdle(i)) return false;
  return true;
}
Selector sideOf(int i) { return i == 2 ? Selector::LEFT : Selector::RIGHT; }

// ---------------- NVS ----------------
void saveConfig() {
  Preferences p;
  p.begin("containing", false);
  p.putBytes("cfg", &cfg, sizeof(cfg));
  p.end();
}

void saveCalibration() {
  Preferences p;
  p.begin("containing", false);
  p.putBytes("cal", calFactor, sizeof(calFactor));
  p.putBytes("off", tareOffset, sizeof(tareOffset));
  p.end();
}

void loadNvs() {
  Preferences p;
  p.begin("containing", false);
  ConfigContaining c;
  if (p.isKey("cfg") && p.getBytesLength("cfg") == sizeof(c) && p.getBytes("cfg", &c, sizeof(c)) == sizeof(c) &&
      validContainingConfig(c))
    cfg = c;  // (an older, different-size saved config is ignored: defaults until the hub sends the current one)
  if (p.isKey("cal") && p.getBytesLength("cal") == sizeof(calFactor)) p.getBytes("cal", calFactor, sizeof(calFactor));
  if (p.isKey("off") && p.getBytesLength("off") == sizeof(tareOffset)) p.getBytes("off", tareOffset, sizeof(tareOffset));
  p.end();
  for (int i = 0; i < 4; i++)
    if (!isfinite(calFactor[i])) calFactor[i] = 0;
}

// ---------------- servos ----------------
void servosRun(int i) {
  ct[i].brakeUntilMs = 0;
  for (int s = 2 * i; s < 2 * i + 2; s++) pca.pulseUs(s, cfg.servo[s].runUs);
}

void servosStop(int i) {
  for (int s = 2 * i; s < 2 * i + 2; s++) pca.pulseUs(s, cfg.servo[s].stopUs);  // web trim, briefly
  const uint32_t t = millis() + SERVO_BRAKE_MS;
  ct[i].brakeUntilMs = t ? t : 1;
}

void serviceBrakes() {
  const uint32_t now = millis();
  for (int i = 0; i < 4; i++) {
    if (!ct[i].brakeUntilMs || (int32_t)(now - ct[i].brakeUntilMs) < 0) continue;
    ct[i].brakeUntilMs = 0;
    pca.off(2 * i);  // no pulse: a continuous servo can't creep
    pca.off(2 * i + 1);
  }
}

// ---------------- container state changes ----------------
void setState(int i, ContainerState s, FaultKind f = FaultKind::NONE) {
  ct[i].state = s;
  ct[i].fault = f;
  ct[i].stateMs = millis();
}

void faultEvent(int i, FaultKind f) {
  switch (f) {
    case FaultKind::JAM: hubLink.sendEvent(EventCode::JAM_DETECTED, i, ct[i].dispensedG); break;
    case FaultKind::TIMEOUT: hubLink.sendEvent(EventCode::TIMEOUT, i, ct[i].dispensedG); break;
    case FaultKind::LOAD_CELL: hubLink.sendEvent(EventCode::SENSOR_FAULT, i, SENSOR_LOAD_CELL); break;
    case FaultKind::NOT_CALIBRATED: hubLink.sendEvent(EventCode::SENSOR_FAULT, i, SENSOR_NOT_CALIBRATED); break;
    case FaultKind::SERVO_DRIVER: hubLink.sendEvent(EventCode::SENSOR_FAULT, i, SENSOR_SERVO_DRIVER); break;
    default: break;
  }
}

/** Start refused before anything moved. */
void refuse(int i, FaultKind f, const char* why) {
  setState(i, ContainerState::FAULT, f);
  Serial.printf("[STATE] %s: can't start, %s\n", CT_NAME[i], why);
  faultEvent(i, f);
}

void startRun(int i) {
  Container& c = ct[i];
  c.startMs = millis();
  c.startWeight = weightOk(i) ? weightG(i) : 0;
  c.lowestWeight = c.startWeight;
  c.lowestAtMs = c.startMs;
  c.dispensedG = 0;
  c.pct = 0;
  c.remainingMs = 0;
  setState(i, ContainerState::DISPENSING);
  servosRun(i);
  if (isRaw(i))
    Serial.printf("[OUTPUT] %s dispensing %u kg (%s)\n", CT_NAME[i], c.kg, rawModeName(modeOf(i)));
  else
    Serial.printf("[OUTPUT] %s running (%s)\n", CT_NAME[i], mixedModeName(modeOf(i)));
  hubLink.sendEvent(EventCode::RUN_STARTED, i, isRaw(i) ? c.kg * 1000 : 0);
}

/** Stop the servos and record why. `result` IDLE = a normal end of a MANUAL mixed run. */
void endRun(int i, ContainerState result, FaultKind f, const char* why) {
  servosStop(i);
  setState(i, result, f);
  Serial.printf("[OUTPUT] %s stopped: %s (dispensed %ld g)\n", CT_NAME[i], why, (long)ct[i].dispensedG);
  switch (result) {
    case ContainerState::DONE:
    case ContainerState::IDLE: hubLink.sendEvent(EventCode::RUN_FINISHED, i, ct[i].dispensedG); break;
    case ContainerState::CANCELLED: hubLink.sendEvent(EventCode::RUN_CANCELLED, i, ct[i].dispensedG); break;
    case ContainerState::FAULT: faultEvent(i, f); break;
    default: break;
  }
}

// ---------------- raw containers C1, C2 ----------------
void tryStartRaw(int i) {
  const RawContainerCfg& rc = cfg.raw[i];
  if (!pca.ok()) return refuse(i, FaultKind::SERVO_DRIVER, "no servo driver (PCA9685) on I2C");
  if (rc.mode == 0) {  // LOADCELL
    if (!hx[i].ok()) return refuse(i, FaultKind::LOAD_CELL, "load cell not working");
    if (!calibrated(i)) return refuse(i, FaultKind::NOT_CALIBRATED, "load cell not calibrated (TARE + CALIBRATE on the website)");
    const int32_t have = weightG(i);
    if (have < (int32_t)ct[i].kg * 1000 - rc.toleranceG) {
      setState(i, ContainerState::NOT_ENOUGH);
      Serial.printf("[STATE] %s: not enough material (%ld g for %u kg)\n", CT_NAME[i], (long)have, ct[i].kg);
      hubLink.sendEvent(EventCode::NOT_ENOUGH_MATERIAL, i, have);
      return;
    }
  }
  startRun(i);
}

void updateRaw(int i) {
  Container& c = ct[i];
  const RawContainerCfg& rc = cfg.raw[i];
  const bool sel = selectBtn[i].pressed();
  const bool conf = confirmBtn[i].pressed();
  const uint32_t now = millis();

  switch (c.state) {
    case ContainerState::IDLE:
      if (sel) {
        c.kg = c.kg % 5 + 1;
        Serial.printf("[INPUT] %s: %u kg selected\n", CT_NAME[i], c.kg);
      }
      if (conf) tryStartRaw(i);
      break;

    case ContainerState::DISPENSING: {
      if (conf) {
        endRun(i, ContainerState::CANCELLED, FaultKind::NONE, "CONFIRM pressed again");
        break;
      }
      const uint32_t elapsed = now - c.startMs;
      if (rc.mode == 0) {  // LOADCELL (safety invariant 7: time limit + jam detection)
        if (!hx[i].ok()) {
          endRun(i, ContainerState::FAULT, FaultKind::LOAD_CELL, "load cell lost");
          break;
        }
        const int32_t w = weightG(i);
        const int32_t target = (int32_t)c.kg * 1000;
        c.dispensedG = c.startWeight - w;
        c.pct = (uint8_t)constrain(c.dispensedG * 100 / target, 0, 100);
        c.remainingMs = elapsed < rc.maxDispenseMs ? rc.maxDispenseMs - elapsed : 0;
        if (w <= c.lowestWeight - JAM_MIN_DROP_G) {
          c.lowestWeight = w;
          c.lowestAtMs = now;
        }
        if (c.dispensedG >= target - rc.toleranceG) endRun(i, ContainerState::DONE, FaultKind::NONE, "target weight reached");
        else if (elapsed >= rc.maxDispenseMs) endRun(i, ContainerState::FAULT, FaultKind::TIMEOUT, "max dispense time");
        else if (now - c.lowestAtMs >= rc.jamTimeoutMs) endRun(i, ContainerState::FAULT, FaultKind::JAM, "weight not dropping (jam?)");
      } else {  // TIME
        const uint32_t total = rc.timeTableMs[c.kg - 1];
        c.dispensedG = weightOk(i) ? c.startWeight - weightG(i) : 0;  // measured for comparison
        c.pct = (uint8_t)min<uint32_t>(100, elapsed * 100 / total);
        c.remainingMs = elapsed < total ? total - elapsed : 0;
        if (elapsed >= total) endRun(i, ContainerState::DONE, FaultKind::NONE, "time elapsed");
      }
      break;
    }

    case ContainerState::DONE:
    case ContainerState::CANCELLED:
      if (now - c.stateMs >= RESULT_SHOW_MS) {
        setState(i, ContainerState::IDLE);
        c.kg = 1;
        c.pct = 0;
        c.remainingMs = 0;
      }
      break;

    case ContainerState::NOT_ENOUGH:
      if (now - c.stateMs >= RESULT_SHOW_MS) setState(i, ContainerState::IDLE);
      break;

    case ContainerState::FAULT:
      if (sel || conf) {  // any button of this container acknowledges
        setState(i, ContainerState::IDLE);
        c.pct = 0;
        Serial.printf("[STATE] %s: fault acknowledged\n", CT_NAME[i]);
      }
      break;
  }
}

// ---------------- mixed containers C3, C4 ----------------
void updateSelectorGuard() {
  if (!selInterlocked) return;
  const uint32_t now = millis();
  const bool neutral = selector.position() == Selector::NEUTRAL && !selector.wiringFault();
  if (!neutral) {
    selNeutralSinceMs = 0;
    if (!selInterlockShown && now - bootMs >= INTERLOCK_OFF_HOLD_MS) {
      selInterlockShown = true;
      Serial.println("[STATE] INTERLOCK: set the selector to the middle first");
    }
  } else if (!selNeutralSinceMs) {
    selNeutralSinceMs = now ? now : 1;
  } else if (now - selNeutralSinceMs >= INTERLOCK_OFF_HOLD_MS) {
    selInterlocked = false;
    selInterlockShown = false;
    Serial.println("[STATE] selector interlock released");
  }
}

void updateMixed(int i) {
  Container& c = ct[i];
  const MixedContainerCfg& mc = cfg.mixed[i - 2];
  const Selector pos = selector.position();
  const bool onSide = pos == sideOf(i) && !selector.wiringFault();
  const uint32_t now = millis();
  if (!onSide) c.armed = true;  // middle, or the other side (L<->R directly counts as passing the middle)

  if (c.stopLatched) {  // web STOP: wait for the middle
    if (pos == Selector::NEUTRAL) {
      c.stopLatched = false;
      setState(i, ContainerState::IDLE);
      Serial.printf("[STATE] %s: STOP latch cleared (selector in the middle)\n", CT_NAME[i]);
    }
    return;
  }

  switch (c.state) {
    case ContainerState::IDLE:
      if (onSide && c.armed && !selInterlocked) {
        c.armed = false;
        if (!pca.ok()) refuse(i, FaultKind::SERVO_DRIVER, "no servo driver (PCA9685) on I2C");
        else startRun(i);
      }
      break;

    case ContainerState::DISPENSING: {
      const uint32_t elapsed = now - c.startMs;
      c.dispensedG = weightOk(i) ? c.startWeight - weightG(i) : 0;
      if (!onSide) {  // selector moved: outputs off first (safety invariant 4)
        if (mc.mode == 0) {
          endRun(i, ContainerState::IDLE, FaultKind::NONE, "selector back to the middle");
        } else {
          endRun(i, ContainerState::CANCELLED, FaultKind::NONE, "selector moved before the time was up");
          setState(i, ContainerState::IDLE);
        }
        c.pct = 0;
        c.remainingMs = 0;
        break;
      }
      if (mc.mode == 1) {  // TIME
        c.pct = (uint8_t)min<uint32_t>(100, elapsed * 100 / mc.runTimeMs);
        c.remainingMs = elapsed < mc.runTimeMs ? mc.runTimeMs - elapsed : 0;
        if (elapsed >= mc.runTimeMs) endRun(i, ContainerState::DONE, FaultKind::NONE, "time elapsed, holding until the middle");
      }
      break;
    }

    default:  // DONE (hold) / FAULT / CANCELLED: wait until the selector leaves this side
      if (!onSide) {
        setState(i, ContainerState::IDLE);
        c.pct = 0;
        c.remainingMs = 0;
      }
      break;
  }
}

// ---------------- remote STOP, config, commands ----------------
void doStop() {
  bool wasRunning = false;
  digitalWrite(PIN_PCA_OE, HIGH);  // hard-disable every servo output first
  const uint32_t t = millis() + STOP_OE_PULSE_MS;
  oeHighUntilMs = t ? t : 1;
  for (int i = 0; i < 4; i++) {
    if (ct[i].state == ContainerState::DISPENSING) {
      wasRunning = true;
      endRun(i, ContainerState::CANCELLED, FaultKind::NONE, "remote STOP");
    }
    if (!isRaw(i) && selector.position() == sideOf(i)) {
      ct[i].stopLatched = true;  // must go back to the middle before it can run again
      setState(i, ContainerState::CANCELLED);
    }
  }
  pca.allOff();
  for (int i = 0; i < 4; i++) ct[i].brakeUntilMs = 0;
  if (wasRunning) hubLink.sendEvent(EventCode::ESTOP_PRESSED, 1);
}

/** Moves each idle container onto pendingCfg; the config version counts as applied once all have it. */
void applyPendingConfig() {
  if (!pendingMask) return;
  for (int i = 0; i < 4; i++) {
    if (!(pendingMask & (1 << i)) || !isIdle(i)) continue;  // safety invariant 6: never mid-run
    if (isRaw(i)) cfg.raw[i] = pendingCfg.raw[i];
    else cfg.mixed[i - 2] = pendingCfg.mixed[i - 2];
    cfg.servo[2 * i] = pendingCfg.servo[2 * i];
    cfg.servo[2 * i + 1] = pendingCfg.servo[2 * i + 1];
    pendingMask &= ~(1 << i);
  }
  if (pendingMask) return;
  cfg.configVersion = pendingCfg.configVersion;
  saveConfig();
  hubLink.setConfigVersion(cfg.configVersion);
  Serial.printf("[STATE] config v%u applied: C1 %s, C2 %s, C3 %s, C4 %s\n", (unsigned)cfg.configVersion,
                rawModeName(cfg.raw[0].mode), rawModeName(cfg.raw[1].mode), mixedModeName(cfg.mixed[0].mode),
                mixedModeName(cfg.mixed[1].mode));
  hubLink.sendEvent(EventCode::CONFIG_APPLIED, (int32_t)cfg.configVersion);
}

AckResult onConfig(const uint8_t* payload, size_t len) {
  if (len != sizeof(ConfigContaining)) return AckResult::REJECTED;
  ConfigContaining c;
  memcpy(&c, payload, sizeof(c));
  if (!validContainingConfig(c)) return AckResult::REJECTED;
  pendingCfg = c;
  pendingMask = 0x0F;
  applyPendingConfig();
  if (!pendingMask) return AckResult::OK;
  Serial.printf("[STATE] config v%u queued until the running containers are idle\n", (unsigned)c.configVersion);
  return AckResult::BUSY_QUEUED;
}

AckResult tare(int i) {
  if (!isIdle(i) || !hx[i].ok() || !hx[i].hasAverage()) return AckResult::REJECTED;
  tareOffset[i] = hx[i].average();
  saveCalibration();
  Serial.printf("[STATE] %s tared (zero = %ld counts)\n", CT_NAME[i], (long)tareOffset[i]);
  return AckResult::OK;
}

AckResult calibrate(int i, int32_t grams) {
  if (grams <= 0 || !isIdle(i) || !hx[i].ok() || !hx[i].hasAverage()) return AckResult::REJECTED;
  const int32_t delta = hx[i].average() - tareOffset[i];
  if (abs(delta) < CAL_MIN_COUNTS) {
    Serial.printf("[ERROR] %s calibrate refused: the %ld g weight barely moved the reading (%ld counts)\n",
                  CT_NAME[i], (long)grams, (long)delta);
    return AckResult::REJECTED;
  }
  calFactor[i] = (float)delta / (float)grams;
  saveCalibration();
  Serial.printf("[STATE] %s calibrated: %.3f counts/g with %ld g\n", CT_NAME[i], calFactor[i], (long)grams);
  return AckResult::OK;
}

AckResult onCommand(const CommandPayload& c) {
  Serial.printf("[NET] command %s target %u arg %ld\n", cmdName(c.cmd), c.target, (long)c.arg);
  switch ((Cmd)c.cmd) {
    case Cmd::STOP:
      doStop();
      return AckResult::OK;
    case Cmd::IDENTIFY:
      identifyUntilMs = millis() + IDENTIFY_MS;
      return AckResult::OK;
    case Cmd::TARE: {
      if (c.target < 4) return tare(c.target);
      if (c.target != TARGET_ALL) return AckResult::REJECTED;
      bool all = true;
      for (int i = 0; i < 4; i++) all = (tare(i) == AckResult::OK) && all;
      return all ? AckResult::OK : AckResult::REJECTED;
    }
    case Cmd::CALIBRATE:
      return c.target < 4 ? calibrate(c.target, c.arg) : AckResult::REJECTED;
    case Cmd::REBOOT:
      if (!allIdle()) return AckResult::REJECTED;
      rebootAtMs = millis() + 300;  // after the ACK has gone out
      return AckResult::OK;
  }
  return AckResult::UNSUPPORTED;
}

// ---------------- hardware services ----------------
void servicePca() {
  const uint32_t now = millis();
  if (!pca.ok() && now - lastPcaTryMs >= PCA_RETRY_MS) {
    lastPcaTryMs = now;
    pca.begin(PCA9685_ADDR);
  }
  if (pca.ok() != pcaWasOk) {
    pcaWasOk = pca.ok();
    Serial.printf(pcaWasOk ? "[STATE] servo driver (PCA9685) found at 0x%02X\n"
                           : "[ERROR] servo driver (PCA9685) lost at 0x%02X\n", PCA9685_ADDR);
  }
  if (!pca.ok())
    for (int i = 0; i < 4; i++)
      if (ct[i].state == ContainerState::DISPENSING)
        endRun(i, ContainerState::FAULT, FaultKind::SERVO_DRIVER, "servo driver lost");
}

void serviceLoadCells() {
  for (int i = 0; i < 4; i++) hx[i].poll();
  const uint32_t now = millis();
  if (now - bootMs < 2000) return;  // give the HX711s time to settle and deliver a few readings
  if (!bootReportDone) {
    bootReportDone = true;
    for (int i = 0; i < 4; i++) {
      ct[i].hxWasOk = hx[i].ok();
      if (hx[i].ok())
        Serial.printf("[STATE] %s load cell OK: raw %ld, zero %ld, %s\n", CT_NAME[i], (long)hx[i].average(),
                      (long)tareOffset[i], calibrated(i) ? "calibrated" : "NOT calibrated (TARE + CALIBRATE on the website)");
      else
        Serial.printf("[STATE] %s load cell not found (DT %u / SCK %u)\n", CT_NAME[i], PIN_HX_DT[i], PIN_HX_SCK[i]);
    }
    return;
  }
  for (int i = 0; i < 4; i++) {
    if (hx[i].ok() == ct[i].hxWasOk) continue;
    ct[i].hxWasOk = hx[i].ok();
    Serial.printf(ct[i].hxWasOk ? "[STATE] %s load cell found\n" : "[ERROR] %s load cell lost\n", CT_NAME[i]);
  }
}

void publishStatus() {
  StatusContaining s = {};
  s.c.uptimeS = millis() / 1000;
  s.c.configVersion = cfg.configVersion;
  s.c.faults = (selector.wiringFault() ? FAULT_SELECTOR_WIRING : 0) | (display.present() ? 0 : FAULT_OLED_MISSING) |
               noise.faultBits();
  s.c.interlock = selInterlocked && selInterlockShown;
  s.selector = (uint8_t)selector.position();
  s.pcaOk = pca.ok();
  for (int i = 0; i < 4; i++) {
    if (hx[i].ok()) s.hxOkMask |= 1 << i;
    ContainerStatus& o = s.ct[i];
    o.weightG = weightOk(i) ? weightG(i) : 0;
    o.state = (uint8_t)ct[i].state;
    o.mode = modeOf(i);
    o.selectedKg = isRaw(i) ? ct[i].kg : 0;
    o.progressPct = ct[i].pct;
    o.remainingMs = ct[i].remainingMs;
    o.dispensedG = ct[i].dispensedG;
    s.calFactor[i] = calFactor[i];
  }
  hubLink.publishStatus(&s, sizeof(s));
}

void updateDisplay() {
  const uint32_t now = millis();
  if (now - lastFrameMs < FRAME_MS) return;
  lastFrameMs = now;
  View v = {};
  for (int i = 0; i < 4; i++) {
    RowView& r = v.row[i];
    r.hxOk = hx[i].ok();
    r.calibrated = calibrated(i);
    r.weightG = weightG(i);
    r.state = ct[i].state;
    r.mode = modeOf(i);
    r.selectedKg = ct[i].kg;
    r.progressPct = ct[i].pct;
    r.remainingMs = ct[i].remainingMs;
    r.fault = ct[i].fault;
    r.locked = !isRaw(i) && selInterlocked && selInterlockShown;
    r.stopLatched = ct[i].stopLatched;
  }
  v.paired = hubLink.paired();
  v.timeKnown = hubLink.timeKnown();
  v.epoch = hubLink.nowEpoch();
  v.tzOffsetMin = hubLink.tzOffsetMin();
  v.identify = (int32_t)(identifyUntilMs - now) > 0;
  v.interlockShown = selInterlocked && selInterlockShown;
  v.pcaOk = pca.ok();
  v.selectorFault = selector.wiringFault();
  display.draw(v);
}

}  // namespace

void setup() {
  // Safety first: servo outputs hard-disabled before anything else (safety invariant 2).
  pinMode(PIN_PCA_OE, OUTPUT);
  digitalWrite(PIN_PCA_OE, HIGH);

  Serial.begin(115200);
  delay(200);  // setup only
  char fw[12];
  fwDecode(CONTAINING_FW, fw, sizeof(fw));
  Serial.printf("\n=== Tile Machine CONTAINING fw %s ===\n", fw);

  pinMode(PIN_STATUS_LED, OUTPUT);
  for (int i = 0; i < 4; i++) hx[i].begin(PIN_HX_DT[i], PIN_HX_SCK[i]);
  selectBtn[0].begin(PIN_SELECT1, BUTTON_DEBOUNCE_MS);
  confirmBtn[0].begin(PIN_CONFIRM1, BUTTON_DEBOUNCE_MS);
  selectBtn[1].begin(PIN_SELECT2, BUTTON_DEBOUNCE_MS);
  confirmBtn[1].begin(PIN_CONFIRM2, BUTTON_DEBOUNCE_MS);
  selector.begin(PIN_SEL_LEFT, PIN_SEL_RIGHT, SELECTOR_DEBOUNCE_MS);
  noise.add(PIN_SELECT1, "C1 SELECT button", FAULT_NOISE_SELECT1);
  noise.add(PIN_CONFIRM1, "C1 CONFIRM button", FAULT_NOISE_CONFIRM1);
  noise.add(PIN_SELECT2, "C2 SELECT button", FAULT_NOISE_SELECT2);
  noise.add(PIN_CONFIRM2, "C2 CONFIRM button", FAULT_NOISE_CONFIRM2);
  noise.add(PIN_SEL_LEFT, "selector LEFT contact", FAULT_NOISE_SEL_LEFT);
  noise.add(PIN_SEL_RIGHT, "selector RIGHT contact", FAULT_NOISE_SEL_RIGHT);
  noise.begin();

  loadNvs();
  hubLink.setConfigVersion(cfg.configVersion);
  Serial.printf("[STATE] config v%u: C1 %s, C2 %s, C3 %s, C4 %s\n", (unsigned)cfg.configVersion,
                rawModeName(cfg.raw[0].mode), rawModeName(cfg.raw[1].mode), mixedModeName(cfg.mixed[0].mode),
                mixedModeName(cfg.mixed[1].mode));

  if (!display.begin()) {
    Serial.println("[ERROR] OLED not found at 0x3C (SDA 21 / SCL 22): running without display");
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);  // the servo driver still needs the bus
  } else {
    const int bad = display.selfTest();
    Serial.printf("[STATE] OLED layout self-test: %s\n", bad ? "OVERFLOW (see errors above)" : "all screens fit");
  }

  pcaWasOk = pca.begin(PCA9685_ADDR);
  lastPcaTryMs = millis();
  Serial.printf(pcaWasOk ? "[STATE] servo driver (PCA9685) found at 0x%02X, all outputs off\n"
                         : "[ERROR] servo driver (PCA9685) not found at 0x%02X: containers can't dispense\n",
                PCA9685_ADDR);
  digitalWrite(PIN_PCA_OE, LOW);  // setup done; every channel is OFF

  bootMs = millis();
  Serial.printf("[STATE] power-up: selector %s; C3/C4 locked until it reads the middle for %lu ms\n",
                selectorName((uint8_t)selector.position()), (unsigned long)INTERLOCK_OFF_HOLD_MS);

  hubLink.onConfig(onConfig);
  hubLink.onCommand(onCommand);
  hubLink.begin();
  Serial.println("[STATE] ready");
}

void loop() {
  const uint32_t now = millis();
  if (oeHighUntilMs && (int32_t)(now - oeHighUntilMs) >= 0) {  // end of the STOP pulse: outputs (all off) enabled
    oeHighUntilMs = 0;
    digitalWrite(PIN_PCA_OE, LOW);
  }
  serviceLoadCells();
  if (selector.update())
    Serial.printf("[INPUT] selector -> %s%s\n", selectorName((uint8_t)selector.position()),
                  selector.wiringFault() ? " (WIRING FAULT: both contacts closed)" : "");
  updateSelectorGuard();
  servicePca();
  for (int i = 0; i < 2; i++) updateRaw(i);
  for (int i = 2; i < 4; i++) updateMixed(i);
  serviceBrakes();
  applyPendingConfig();
  noise.loop();
  hubLink.loop();
  publishStatus();
  updateDisplay();

  digitalWrite(PIN_STATUS_LED, hubLink.paired() ? HIGH : ((now / 500) % 2 ? HIGH : LOW));
  if (rebootAtMs && (int32_t)(now - rebootAtMs) >= 0 && allIdle()) ESP.restart();
}
