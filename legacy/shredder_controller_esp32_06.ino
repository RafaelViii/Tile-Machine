/*
  Shredder Controller — ESP32 version
  Buzzer driven with plain digitalWrite HIGH/LOW (ACTIVE buzzer — has its
  own internal oscillator, so it only needs power, not a tone frequency).
  If you swap to a passive buzzer later, you'd go back to tone()/noTone().

  - 3-way knob: AUTO / MANUAL (wired to two pins, common to GND)
  - Start button: momentary NO, self-reset
  - Stop button:  momentary NO, self-reset
  - Relay: drives the shredder motor
  - IR sensor: digital output, LOW = object detected (flip IR_ACTIVE_LOW
    below if yours is opposite)
  - OLED: SH1106 128x64 via U8g2, hardware I2C (ESP32 default SDA=21, SCL=22)
  - Buzzer: active buzzer on PIN_BUZZER, driven HIGH = on, LOW = off

  MANUAL mode: Start latches relay ON. Stop turns it OFF.
  AUTO mode:   Relay follows the IR sensor directly — ON while something
               is detected, OFF the instant it clears, ON again next detect.

  Switching modes always drops the relay OFF first (silent beep-wise — the
  mode-change beep covers that), so you never land in MANUAL with the
  motor already spinning from a leftover AUTO state.

  SERIAL: every input (button press, switch change, IR transition) and
  every output/event (relay on/off, mode change) is logged to Serial at
  115200 baud, tagged [INPUT] / [OUTPUT].

  BUZZER: since an active buzzer has only one fixed pitch, events are told
  apart by beep COUNT and LENGTH instead of tone:
    - Power on:        one long beep
    - Start pressed:   one short tick
    - Stop pressed:    two short beeps
    - Relay ON:        one medium beep
    - Relay OFF:       two quick beeps
    - Mode changed:    three quick beeps
    - IR detected:     one very short tick
    - IR cleared:      two very short ticks
    - Still running:   a faint single tick every few seconds
  Patterns are queued (not just interrupted), so two events landing in the
  same loop are both heard in order. Everything is timed with millis(),
  never delay(), so sound never blocks button reads or the display.
*/

#include <U8g2lib.h>

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0);

// ---------- Pin map ----------
// GPIO6-11 are wired to the ESP32's internal flash chip — never use them.
// GPIO0, 2, 5, 12, 15 are strapping pins — avoided below to prevent
// boot-mode issues. I2C uses the ESP32 default SDA=21 / SCL=22 pins
// automatically via U8g2's hardware I2C constructor.
const uint8_t PIN_START     = 32;  // INPUT_PULLUP, momentary to GND
const uint8_t PIN_STOP      = 33;  // INPUT_PULLUP, momentary to GND
const uint8_t PIN_SW_AUTO   = 25;  // INPUT_PULLUP, LOW = knob on AUTO
const uint8_t PIN_SW_MANUAL = 26;  // INPUT_PULLUP, LOW = knob on MANUAL
const uint8_t PIN_RELAY     = 27;
const uint8_t PIN_IR        = 14;  // INPUT_PULLUP, LOW = object detected
const uint8_t PIN_BUZZER    = 13;

const bool RELAY_ACTIVE_LOW = false;  // flip this if the relay behaves backwards on your board
const bool IR_ACTIVE_LOW    = true;   // most IR modules pull LOW on detect

// ---------- Debounce ----------
const unsigned long DEBOUNCE_MS = 30;

struct Button {
  uint8_t pin;
  bool lastReading = HIGH;
  bool stableState = HIGH;
  unsigned long lastChangeMs = 0;

  void begin(uint8_t p) {
    pin = p;
    pinMode(pin, INPUT_PULLUP);
  }

  // returns true exactly once, on the press edge (HIGH -> LOW)
  bool pressed() {
    bool reading = digitalRead(pin);
    if (reading != lastReading) {
      lastChangeMs = millis();
      lastReading = reading;
    }
    bool edge = false;
    if ((millis() - lastChangeMs) > DEBOUNCE_MS && reading != stableState) {
      bool prevStable = stableState;
      stableState = reading;
      if (prevStable == HIGH && stableState == LOW) edge = true;
    }
    return edge;
  }
};

Button startBtn, stopBtn;

// ---------- Mode ----------
enum Mode { MODE_MANUAL, MODE_AUTO };
Mode currentMode = MODE_MANUAL;
Mode lastMode = MODE_MANUAL;

const char* modeName(Mode m) { return m == MODE_MANUAL ? "MANUAL" : "AUTO"; }

bool manualLatch = false;
bool relayOn = false;
bool irLastState = false;
bool autoStopLatched = false;  // true = STOP was hit in AUTO; blocks the
                                // relay even if IR still detects, until
                                // the sensor clears

// ---------- Animation ----------
float bladeAngle = 0.0f;
unsigned long lastFrameMs = 0;
const unsigned long FRAME_MS = 40;      // ~25fps display refresh
const float SPIN_SPEED_DEG = 18.0f;     // degrees per frame while running

// ---------- Buzzer: non-blocking, queued ON/OFF pattern player ----------
// Each step is just "on or off" plus how long to hold it — right for an
// active buzzer, which has no adjustable pitch. Patterns are queued
// rather than interrupting each other, so if two events land in the same
// loop (e.g. IR detect + relay-on) you hear both in order instead of one
// getting cut off. Entirely millis()-based — never blocks.
struct BuzzerStep { bool on; uint16_t durationMs; };
struct QueuedPattern { const BuzzerStep* steps; uint8_t count; };

struct BuzzerPlayer {
  static const uint8_t QUEUE_SIZE = 8;
  QueuedPattern queue[QUEUE_SIZE];
  uint8_t queueHead = 0, queueTail = 0, queueCount = 0;

  const BuzzerStep* steps = nullptr;
  uint8_t stepCount = 0;
  uint8_t currentStep = 0;
  unsigned long stepStartMs = 0;
  bool active = false;

  void play(const BuzzerStep* pattern, uint8_t count) {
    if (queueCount < QUEUE_SIZE) {
      queue[queueTail] = {pattern, count};
      queueTail = (queueTail + 1) % QUEUE_SIZE;
      queueCount++;
    }
    if (!active) advanceQueue();
  }

  void advanceQueue() {
    if (queueCount == 0) {
      active = false;
      digitalWrite(PIN_BUZZER, LOW);
      return;
    }
    QueuedPattern qp = queue[queueHead];
    queueHead = (queueHead + 1) % QUEUE_SIZE;
    queueCount--;
    steps = qp.steps;
    stepCount = qp.count;
    currentStep = 0;
    active = true;
    startStep();
  }

  void startStep() {
    if (!active || currentStep >= stepCount) return;
    digitalWrite(PIN_BUZZER, steps[currentStep].on ? HIGH : LOW);
    stepStartMs = millis();
  }

  void update() {
    if (!active) return;
    if (millis() - stepStartMs >= steps[currentStep].durationMs) {
      currentStep++;
      if (currentStep >= stepCount) advanceQueue();
      else                          startStep();
    }
  }

  bool isPlaying() { return active || queueCount > 0; }
};

BuzzerPlayer buzzer;

#define PATTERN_LEN(p) (sizeof(p) / sizeof(BuzzerStep))

// {on, duration ms} — told apart by beep count/rhythm/length since an
// active buzzer can't change pitch. Made long enough (each pattern runs
// roughly half a second to a second) that you can recognize it by ear
// without having to guess from a single short blip.
const BuzzerStep PATTERN_STARTUP[]      = {{true,120},{false,80},{true,120},{false,80},{true,400}};      // short-short-long "power up"
const BuzzerStep PATTERN_MODE_CHANGE[]  = {{true,150},{false,150},{true,150},{false,150},{true,150}};    // 3 even, well-spaced beeps
const BuzzerStep PATTERN_RELAY_ON[]     = {{true, 300}};                                                  // 1 long, sustained beep
const BuzzerStep PATTERN_RELAY_OFF[]    = {{true,200},{false,150},{true,300}};                            // short then long — "winding down"
const BuzzerStep PATTERN_HEARTBEAT[]    = {{true, 60}};                                                   // brief tick, repeats every few seconds
const BuzzerStep PATTERN_START_PRESS[]  = {{true, 150}};                                                  // 1 clear beep
const BuzzerStep PATTERN_STOP_PRESS[]   = {{true,150},{false,120},{true,150}};                             // 2 beeps
const BuzzerStep PATTERN_IR_DETECT[]    = {{true,80},{false,80},{true,80}};                                // quick double-tap
const BuzzerStep PATTERN_IR_CLEAR[]     = {{true,60},{false,60},{true,60},{false,60},{true,60}};           // quick triple-tap

const unsigned long HEARTBEAT_INTERVAL_MS = 4000;
unsigned long lastHeartbeatMs = 0;

void updateBuzzer() {
  buzzer.update();
  // Only chirp the heartbeat when the queue is empty, so it never
  // talks over a real event's beep.
  if (relayOn && !buzzer.isPlaying() &&
      (millis() - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS)) {
    lastHeartbeatMs = millis();
    Serial.println("[OUTPUT] Heartbeat: relay still running");
    buzzer.play(PATTERN_HEARTBEAT, PATTERN_LEN(PATTERN_HEARTBEAT));
  }
}

// forward declarations (used in setup() before their definitions below)
Mode readMode();
bool irDetecting();

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Shredder Controller booting ===");

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  startBtn.begin(PIN_START);
  stopBtn.begin(PIN_STOP);
  pinMode(PIN_SW_AUTO, INPUT_PULLUP);
  pinMode(PIN_SW_MANUAL, INPUT_PULLUP);
  pinMode(PIN_IR, INPUT_PULLUP);
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, RELAY_ACTIVE_LOW ? HIGH : LOW); // relay off at boot

  currentMode = readMode();
  lastMode = currentMode;
  irLastState = irDetecting();

  Serial.print("[INPUT] Initial mode: ");
  Serial.println(modeName(currentMode));
  Serial.print("[INPUT] Initial IR state: ");
  Serial.println(irLastState ? "DETECTED" : "clear");

  u8g2.begin();

  // Power-on beep — plays immediately, doesn't need the loop() running.
  buzzer.play(PATTERN_STARTUP, PATTERN_LEN(PATTERN_STARTUP));

  Serial.println("=== Ready ===");
}

// silentBeep=true skips the relay-transition beep only; the serial log
// still always records the transition (used when mode-switching forces
// the relay off, so the mode-change beep plays instead of overlapping).
void setRelay(bool on, bool silentBeep = false, const char* reason = "") {
  bool changed = (on != relayOn);
  relayOn = on;
  digitalWrite(PIN_RELAY, on ? (RELAY_ACTIVE_LOW ? LOW : HIGH)
                             : (RELAY_ACTIVE_LOW ? HIGH : LOW));
  if (changed) {
    Serial.print("[OUTPUT] Relay -> ");
    Serial.print(on ? "ON" : "OFF");
    if (reason[0] != '\0') {
      Serial.print(" (");
      Serial.print(reason);
      Serial.print(")");
    }
    Serial.println();
    if (!silentBeep) {
      if (on) buzzer.play(PATTERN_RELAY_ON, PATTERN_LEN(PATTERN_RELAY_ON));
      else    buzzer.play(PATTERN_RELAY_OFF, PATTERN_LEN(PATTERN_RELAY_OFF));
    }
  }
}

Mode readMode() {
  if (digitalRead(PIN_SW_MANUAL) == LOW) return MODE_MANUAL;
  if (digitalRead(PIN_SW_AUTO) == LOW)   return MODE_AUTO;
  return lastMode; // knob between detents / center-off: hold last mode
}

bool irDetecting() {
  bool raw = digitalRead(PIN_IR);
  return IR_ACTIVE_LOW ? (raw == LOW) : (raw == HIGH);
}

void checkIrEvent() {
  bool detecting = irDetecting();
  if (detecting != irLastState) {
    irLastState = detecting;
    if (detecting) {
      Serial.println("[INPUT] IR: object DETECTED");
      buzzer.play(PATTERN_IR_DETECT, PATTERN_LEN(PATTERN_IR_DETECT));
    } else {
      Serial.println("[INPUT] IR: clear");
      buzzer.play(PATTERN_IR_CLEAR, PATTERN_LEN(PATTERN_IR_CLEAR));
    }
  }
}

void updateLogic() {
  checkIrEvent();

  currentMode = readMode();

  if (currentMode != lastMode) {
    Serial.print("[INPUT] Mode switched: ");
    Serial.print(modeName(lastMode));
    Serial.print(" -> ");
    Serial.println(modeName(currentMode));

    manualLatch = false;
    autoStopLatched = false;
    setRelay(false, true, "mode change safety cutoff");
    buzzer.play(PATTERN_MODE_CHANGE, PATTERN_LEN(PATTERN_MODE_CHANGE));
    lastMode = currentMode;
  }

  bool startEdge = startBtn.pressed();
  bool stopEdge  = stopBtn.pressed();

  if (startEdge) {
    Serial.println("[INPUT] START button pressed");
    buzzer.play(PATTERN_START_PRESS, PATTERN_LEN(PATTERN_START_PRESS));
  }
  if (stopEdge) {
    Serial.println("[INPUT] STOP button pressed");
    buzzer.play(PATTERN_STOP_PRESS, PATTERN_LEN(PATTERN_STOP_PRESS));
  }

  if (currentMode == MODE_MANUAL) {
    if (startEdge) manualLatch = true;
    if (stopEdge)  manualLatch = false;   // STOP always wins - safety
    setRelay(manualLatch, false, "manual");
  } else { // MODE_AUTO
    bool detecting = irDetecting();

    if (stopEdge) {
      // STOP is an absolute safety override: kill the relay now, and
      // keep it off even if the IR is still seeing the object, until
      // the sensor clears. No need to press anything else in AUTO.
      autoStopLatched = true;
      setRelay(false, false, "STOP pressed - safety override");
    } else if (!detecting) {
      // Sensor clear: safety latch resets, ready for the next cycle.
      autoStopLatched = false;
      setRelay(false, false, "auto/IR clear");
    } else if (autoStopLatched) {
      // Still latched from a prior STOP, and the object hasn't cleared
      // yet - stay off, no repeat beep since nothing new happened.
      setRelay(false, true, "auto/IR blocked by STOP latch");
    } else {
      setRelay(true, false, "auto/IR");
    }
  }
}

// ---------- Drawing ----------
void drawBlade(int cx, int cy, int r, float angleDeg) {
  // 3-blade fan, drawn as thin lines from center — clean, no fills/bitmaps
  for (int i = 0; i < 3; i++) {
    float a = (angleDeg + i * 120.0f) * PI / 180.0f;
    int x = cx + (int)(r * cos(a));
    int y = cy + (int)(r * sin(a));
    u8g2.drawLine(cx, cy, x, y);
  }
  u8g2.drawCircle(cx, cy, r, U8G2_DRAW_ALL);
  u8g2.drawDisc(cx, cy, 2, U8G2_DRAW_ALL);
}

void updateDisplay() {
  if (millis() - lastFrameMs < FRAME_MS) return;
  lastFrameMs = millis();

  if (relayOn) {
    bladeAngle += SPIN_SPEED_DEG;
    if (bladeAngle >= 360.0f) bladeAngle -= 360.0f;
  }
  // when relay is off, bladeAngle just stays put — a frozen blade

  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(0, 10, currentMode == MODE_MANUAL ? "MODE: MANUAL" : "MODE: AUTO");

  drawBlade(64, 38, 20, bladeAngle);

  if (currentMode == MODE_AUTO) {
    const char* line = relayOn ? "RUNNING - IR DETECT" : "STANDBY - IR CLEAR";
    u8g2.drawStr(0, 62, line);
  } else {
    u8g2.drawStr(0, 62, relayOn ? "RUNNING" : "STANDBY");
  }

  u8g2.sendBuffer();
}

void loop() {
  updateLogic();
  updateDisplay();
  updateBuzzer();
}
