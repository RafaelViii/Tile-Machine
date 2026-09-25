# Tile Machine — Project Guide (CLAUDE.md)

This file keeps every work session on track. Read it first. When a decision changes, update this
file **in the same commit** as the change.

## 1. What we are building

A plastic-recycling **Tile Machine** made of independent process modules. Each module has its own
ESP32 and keeps working on its own. A **Main Hub** ESP32 manages them over **ESP-NOW** and links
them to a **web dashboard** through **Firebase Realtime Database**.

```
Process line:  Shredder → Containing → Hot Press → Designing → Curing

Devices:
  esp0  HUB         WiFi + ESP-NOW bridge to Firebase  (4th ESP32, no OLED, status LED)
  esp1  shredder    Shredder
  esp2  containing  4 containers (Raw HDPE, Raw PP, Mixed HDPE, Mixed PP)
  esp3  hotpress    Hot Press + Designing + Curing (relays only)
```

The web dashboard shows all 5 processes. A process tile is **greyed out (unhighlighted)** until its
ESP32 is paired and online. Then it lights up with a **connected** indicator. `hotpress` (esp3)
lights up three tiles: Hot Press, Designing and Curing.

## 2. Core decisions (locked — ask before changing)

| Topic | Decision |
|---|---|
| Hub | Dedicated 4th ESP32 (`hub`). Modules never talk to WiFi or Firebase directly. |
| Hub ↔ cloud | Hub joins WiFi (STA) and uses Firebase **Realtime Database** (not Firestore). |
| Module ↔ hub | ESP-NOW, binary packed structs, shared library `firmware/lib/TileProtocol`. |
| Pairing | Fully automatic. A module scans WiFi channels 1–13 broadcasting `HELLO`. The hub answers `WELCOME`. The module stores the hub MAC and channel in NVS. No buttons are pressed. |
| Channel | ESP-NOW runs on the **router's WiFi channel** (the hub can't change it). Modules re-scan if the hub is lost. Recommend fixing the router to one channel. |
| Autonomy | Every module runs fully standalone with its **last-applied config stored in NVS**. Losing the hub or WiFi never stops or changes a running process. |
| Remote control | The web can **change config** and send **STOP** (per module or all), plus IDENTIFY/TARE/CALIBRATE/REBOOT. The web can **never START** a motor, heater or dispenser. Starting needs a person at the machine. |
| Firmware | PlatformIO, `espressif32` platform 6.x → **Arduino-ESP32 core 2.0.x**. Use the core-2 APIs: ESP-NOW recv callback `(const uint8_t* mac, const uint8_t* data, int len)`, and LEDC `ledcSetup` / `ledcAttachPin` / `ledcWriteTone`. Don't mix in core-3 APIs. |
| Web | React + Vite + TypeScript + Tailwind, hosted on Firebase Hosting. |
| Firebase project | `tile-machine-92345`, RTDB region **asia-southeast1** (`https://tile-machine-92345-default-rtdb.asia-southeast1.firebasedatabase.app`). Analytics is not used. |
| Auth | Firebase Auth email/password. The web admin and the hub each have an account. Roles live at `/roles/{uid}` = `"admin"` or `"hub"` and are set by hand in the console. |
| Displays | SH1106 128x64 I2C (U8g2) on shredder, containing and hotpress. The hub has no display. |
| Repo | GitHub `RafaelViii/Tile-Machine` (https://github.com/RafaelViii/Tile-Machine), default branch `main`. Currently **public**, so never commit secrets (see §6). |

## 3. Safety invariants (never break these)

1. **Local STOP always wins** and is handled locally, never through the network.
2. **Boot state is safe.** All relays are OFF and PCA9685 outputs are disabled (OE HIGH) until
   setup finishes.
3. **Power-up interlock.** A state-based input (3-way switch, latching ON button, mixed selector)
   that is not in its OFF/neutral position at boot is ignored until it has been seen in OFF/neutral
   once. The OLED shows `Set switch to OFF`.
4. **Mode change means outputs OFF.** Changing a mode or selector always drops the affected outputs
   first.
5. **Remote STOP** acts exactly like the physical STOP. The web cannot START anything (see §2).
6. **Config changes are applied only when the affected unit is idle.** Never apply one in the middle
   of a cycle. It is queued and applied at the next idle.
7. The Containing load-cell mode always has a **max-time safety timeout** and **jam detection**
   (no weight drop).
8. No `delay()` in `loop()` code paths. Everything is `millis()`-based and non-blocking.

## 4. Repo layout

```
CLAUDE.md                  ← this file
README.md
firebase.json              ← Firebase CLI config (RTDB rules + Hosting → web/dist)
.firebaserc               ← Firebase project id (not secret)
docs/
  ARCHITECTURE.md          ← system design, flows, diagrams
  PROTOCOL.md              ← ESP-NOW message spec (binary)
  DATA_MODEL.md            ← Firebase RTDB tree (JSON), the web ↔ hub contract
  HARDWARE.md              ← pin maps, wiring, power notes per board
  SETUP.md                 ← GitHub, Firebase, PlatformIO, web setup steps
  modules/                 ← behaviour spec + state machine per device
    hub.md  shredder.md  containing.md  hotpress.md
firmware/
  lib/TileProtocol/        ← shared ESP-NOW protocol library (used by all 4 boards)
  hub/  shredder/  containing/  hotpress/   ← one PlatformIO project each
  linktest/                ← temporary pairing-test firmware for module boards (drives nothing)
web/                       ← React dashboard
firebase/
  database.rules.json      ← RTDB security rules
legacy/
  shredder_controller_esp32_06.ino   ← old standalone sketch (reference only, don't build)
```

## 5. Single sources of truth

- The **ESP-NOW wire format** is defined in `docs/PROTOCOL.md` and implemented in
  `firmware/lib/TileProtocol`. Change both together and bump `PROTOCOL_VERSION` on any
  breaking change.
- The **Firebase tree** is defined in `docs/DATA_MODEL.md` and mirrored in
  `web/src/shared/types/rtdb.ts` and the hub's JSON mapping. Change all three together.
- **Pins** are defined in `docs/HARDWARE.md` and mirrored in each board's `include/pins.h`.

## 6. Conventions

- Firmware: C++ (Arduino). One `pins.h`, one `config.h` (defaults), and state machines as `enum
  class` + `switch`. Serial log at 115200 with tags `[INPUT]`, `[OUTPUT]`, `[NET]`, `[STATE]`,
  `[ERROR]` (kept from the legacy sketch).
- Polarity flags per board (`RELAY_ACTIVE_LOW`, `IR_ACTIVE_LOW`, `BUZZER_ACTIVE_LOW`), never
  hard-coded logic levels.
- Secrets never go in git: `firmware/hub/include/secrets.h` and `web/.env.local` are git-ignored,
  with committed `*.example` templates.
- Web: feature folders under `web/src/features/*`, Firebase access only through
  `web/src/lib/firebase.ts` and hooks.
- Commits: small and focused, imperative subject (`shredder: add auto warning countdown`).

## 7. Commands

```bash
# Firmware (from firmware/<board>)
pio run                 # build
pio run -t upload       # flash
pio device monitor      # serial monitor (115200)

# Web (from web/)
npm install
npm run dev
npm run build

# Firebase (from repo root, CLI logged in as the admin Google account)
firebase deploy --only database     # rules
cd web && npm run build && cd .. && firebase deploy --only hosting   # dashboard
```

## 8. Roadmap / status

- [x] Phase 0: Requirements, architecture docs, repo skeleton, CLAUDE.md
- [x] Firebase: project, auth users, roles, rules deployed and verified
- [x] Web dashboard live at https://tile-machine-92345.web.app. All pages, config forms and
      commands. Checked end-to-end with a headless browser against simulated hub data.
- [x] Phase 1: `TileProtocol` lib + hub auto-pairing + presence over ESP-NOW
- [x] Phase 2: hub → RTDB (batched state/presence/events, config polling + push, `/commands` stream).
      **Verified on the real hub (COM7)**: steady `lastSeen` < 11 s, STOP ALL round trip about 125 ms.
      See docs/modules/hub.md for the HTTPClient-blocking and slow-DNS fixes. Hub fw 0.2.0 adds a WiFi
      watchdog (retry every 20 s, reboot after 10 min): core 2.0.x stopped reconnecting after the
      router's 2.4 GHz radio dropped out (reason 201).
- [x] Link test on the real Shredder board (COM8): paired with the hub 0.1 s after boot, got the RTC
      clock, and Firebase shows it online. Auto-pairing is proven end to end.
- [x] Phase 3: Shredder firmware (firmware/shredder, fw 0.2.1) on the real board (COM8). Hand-tested: MANUAL check/confirm/run/STOP, AUTO countdown/run/stop-when-empty, E-STOP during countdown, CANCELLED, switch flip while running, web STOP + IDENTIFY + config, power-up interlock (fixed in 0.2.1: needs OFF held 1 s). **Pending: passive buzzer not wired yet, so sounds are untested.** fw 0.2.4: 3-way switch (default 250 ms) and START/STOP (default 50 ms) debounce adjustable from the web (software workaround until the RC filters are fitted).
- [~] Phase 4: Hotpress firmware (firmware/hotpress, fw 0.1.2, integrating ON-button filter + noise monitor (fault bit 2), debounce adjustable from the web, default 200/150 ms; ON button chatters when pressed, RC filter recommended) on the real board (COM9, auto-upload works, no BOOT needed): 2x SSR-25-DA (HIGH = ON), per-output power-up interlock (OFF/middle held 1 s), web STOP latch per output, OLED + self-test, paired with hub. **Hand test pending.**
- [ ] Phase 5: Containing firmware (4× HX711, PCA9685 8× servo, buttons, selector, OLED)
- [~] Phase 6: Web: Events page uses server-side cursor pagination (docs/DATA_MODEL.md "Reading events"). Original modern design kept (user rejected an "industrial" restyle as too robotic) + Auto/Light/Dark theme (tm-theme in localStorage; light mode reverses the zinc palette via CSS vars, dark = Tailwind defaults). Dashboard "Devices": one compact line per ESP32 (status, last seen, warning only when something is wrong), details on click. Checked dark/light, desktop + 400 px.
- [ ] Phase 7: Hardening: ESP-NOW encryption, WiFi provisioning portal, OTA

## 9. Open questions / pending decisions

- **OPEN BUG (shredder, 2026-09-25):** random screen/relay changes, and the board's USB serial keeps dropping
  ("device not functioning"), both signs of EMI. Shredder fw 0.2.2 + hotpress 0.1.3 have per-input noise
  detectors (lib/TileIO): the web Devices list names the noisy input. Next: check which fault shows
  during motor/relay use, then add an RC filter to that input and/or a flyback diode/snubber at the relay/motor.
  Hotpress ON button: contact chatter confirmed, 4.7k pull-up + 1k + 100nF recommended.

- **DS3231 RTC on the hub** is done (docs/modules/hub.md "Clock"): time at boot without internet,
  real event timestamps, `TIME` to modules for OLED clocks. Still open: keep unsent events in its
  AT24C32 EEPROM through power cuts.

- Hotpress **AUTO** logic is not defined yet (placeholder: display `AUTO`, Hotpress relay ON).
- Real shredder dispense/IR thresholds and container **time tables** are measured on the hardware.
- Load-cell calibration factors are measured per container (TARE + known weight from the web).
  **Decision:** calibration is owned by the Containing module (NVS) and only reported in its state.
  It is not part of config, so a web config save can never overwrite a calibration.
