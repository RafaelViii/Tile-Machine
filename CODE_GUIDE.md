# Code guide: where everything is

A map of the Tile Machine's source code: which language each part uses, where each module's code lives,
and how the hardware and software pieces connect. Every path below is a link.

> **To click the links in VS Code:** open this file and press **Ctrl+Shift+V** (preview). In the editor
> itself, **Ctrl+Click** a link. On GitHub the links work as normal.
> Links with `#L123` jump to that line. If code changes later and a line moves, search the file for the
> function name shown next to the link (**Ctrl+Shift+O** lists every function in a file).

---

## 1. The system in one picture

```
 HARDWARE (each machine has its own ESP32 microcontroller)                 SOFTWARE (internet)

 ┌──────────────┐
 │ esp1 Shredder│──┐
 └──────────────┘  │   ESP-NOW radio          ┌───────────┐   WiFi + HTTPS   ┌──────────────────┐        ┌─────────────┐
 ┌──────────────┐  │   (binary messages,      │ esp0 Hub  │ ───────────────▶ │ Firebase Realtime │ ◀────▶ │ Tile Console│
 │ esp2 Contain.│──┼── no router needed) ───▶│ (bridge)  │ ◀─────────────── │ Database (cloud)  │        │ website     │
 └──────────────┘  │                          └───────────┘                  └──────────────────┘        └─────────────┘
 ┌──────────────┐  │
 │ esp3 HotPress│──┘   Machines send their status up; settings and STOP / Identify commands come down.
 └──────────────┘      The website can never START a machine: that needs a person at the machine.
```

Process line: **Shredder → Containing → Hot Press → Designing → Curing** (esp3 runs the last three).

---

## 2. Languages and tools

| Part | Language | Framework / tools | Size (lines) |
|---|---|---|---|
| Machine firmware (esp1, esp2, esp3) | **C++** (Arduino style) | Arduino-ESP32 core 2.0.17, PlatformIO, U8g2 (OLED) | shredder 1,097 · containing 1,237 · hotpress 573 |
| Hub firmware (esp0) | **C++** (Arduino + ESP-IDF APIs) | FreeRTOS tasks, `esp_http_client`, ArduinoJson 7 | 2,905 |
| Shared firmware libraries | **C++** | used by all boards | TileProtocol 1,189 · TileIO 154 |
| Website (Tile Console) | **TypeScript** + TSX (React) | React 19, Vite, Tailwind CSS v4, Firebase JS SDK | 5,034 TS/TSX + 182 CSS |
| Cloud security rules | **JSON** (Firebase rules language) | Firebase Realtime Database | 128 |
| Build settings | INI / JSON | `platformio.ini`, `package.json`, `firebase.json` | – |

Microcontroller: **ESP32** (dual-core, 240 MHz, WiFi). Board-to-board radio: **ESP-NOW**. Cloud: **Firebase**
(Realtime Database + Authentication + Hosting).

---

## 3. Each module's code

Every board is one PlatformIO project with the same layout:

| File | What it holds |
|---|---|
| `include/pins.h` | Which ESP32 pin is wired to what (mirrors [docs/HARDWARE.md](docs/HARDWARE.md)) |
| `include/config.h` | Fixed settings: firmware version, timings, defaults |
| `src/main.cpp` | The program: `setup()` runs once at power-on, `loop()` runs forever; the **state machine** lives here |
| `src/inputs.h` | Buttons, switches and sensors (with debouncing) |
| `src/display.cpp` / `.h` | What the OLED screen shows |
| `platformio.ini` | Build settings (board type, libraries) |

### esp1 · Shredder

Folder: [firmware/shredder/](firmware/shredder/) · Behaviour spec: [docs/modules/shredder.md](docs/modules/shredder.md)
· Website page: [ShredderPage.tsx](web/src/features/shredder/ShredderPage.tsx)

| File | Role |
|---|---|
| [main.cpp](firmware/shredder/src/main.cpp) | Program and state machine (OFF / MANUAL / AUTO, countdown, run, E-STOP) |
| ↳ [updateLogic()](firmware/shredder/src/main.cpp#L172) | The state machine: reads inputs, decides when the motor runs |
| ↳ [setRelay()](firmware/shredder/src/main.cpp#L111) | Switches the motor relay (the only place it happens) |
| ↳ [onCommand()](firmware/shredder/src/main.cpp#L340) | Handles STOP / IDENTIFY / REBOOT from the website |
| ↳ [onConfig()](firmware/shredder/src/main.cpp#L321) | Receives new settings (applied only when idle) |
| ↳ [publishStatus()](firmware/shredder/src/main.cpp#L374) | Sends live status to the hub |
| [inputs.h](firmware/shredder/src/inputs.h) | START/STOP buttons, 3-way switch, IR sensor |
| [buzzer.cpp](firmware/shredder/src/buzzer.cpp) · [buzzer.h](firmware/shredder/src/buzzer.h) | Passive buzzer sounds (all tones listed in buzzer.h) |
| [display.cpp](firmware/shredder/src/display.cpp) | OLED screens (spinning blade, countdown, clock) |
| [pins.h](firmware/shredder/include/pins.h) · [config.h](firmware/shredder/include/config.h) | Pin map · version and fixed settings |

### esp2 · Containing (4 containers)

Folder: [firmware/containing/](firmware/containing/) · Behaviour spec: [docs/modules/containing.md](docs/modules/containing.md)
· Website page: [ContainingPage.tsx](web/src/features/containing/ContainingPage.tsx)

| File | Role |
|---|---|
| [main.cpp](firmware/containing/src/main.cpp) | Program: raw containers C1/C2 (buttons) and mixed C3/C4 (selector) |
| ↳ [updateRaw()](firmware/containing/src/main.cpp#L218) · [tryStartRaw()](firmware/containing/src/main.cpp#L201) | Raw state machine: select kg, start, LOADCELL / TIME stop, jam and time-limit safety |
| ↳ [updateMixed()](firmware/containing/src/main.cpp#L311) | Mixed state machine: MANUAL while on its side, TIME then hold |
| ↳ [doStop()](firmware/containing/src/main.cpp#L370) | Website STOP: all servos off, outputs hard-disabled 200 ms |
| ↳ [onConfig()](firmware/containing/src/main.cpp#L411) · [applyPendingConfig()](firmware/containing/src/main.cpp#L391) | New settings, applied per container only when it's idle |
| ↳ [onCommand()](firmware/containing/src/main.cpp#L446) · [tare()](firmware/containing/src/main.cpp#L424) · [calibrate()](firmware/containing/src/main.cpp#L432) | Website commands, load-cell zero and calibration (saved on the board) |
| ↳ [publishStatus()](firmware/containing/src/main.cpp#L513) | Sends live status (weights, states, progress) to the hub |
| [hx711.cpp](firmware/containing/src/hx711.cpp) · [poll()](firmware/containing/src/hx711.cpp#L23) | Load-cell reader, rejects a missing HX711 |
| [pca9685.cpp](firmware/containing/src/pca9685.cpp) · [begin()](firmware/containing/src/pca9685.cpp#L27) | Servo driver (50 Hz) |
| [display.cpp](firmware/containing/src/display.cpp) · [row()](firmware/containing/src/display.cpp#L94) · [selfTest()](firmware/containing/src/display.cpp#L174) | OLED rows (always 21 characters) and the overlap self-test |
| [inputs.h](firmware/containing/src/inputs.h) | SELECT/CONFIRM buttons and the 3-way selector |
| [pins.h](firmware/containing/include/pins.h) · [config.h](firmware/containing/include/config.h) | Pin map · version, timings, fault bits |

### esp3 · Hot Press + Designing + Curing

Folder: [firmware/hotpress/](firmware/hotpress/) · Behaviour spec: [docs/modules/hotpress.md](docs/modules/hotpress.md)
· Website page: [HotpressPage.tsx](web/src/features/hotpress/HotpressPage.tsx)

| File | Role |
|---|---|
| [main.cpp](firmware/hotpress/src/main.cpp) | Program: two solid-state relays (Hot Press, Designing + Curing) |
| ↳ [updateLogic()](firmware/hotpress/src/main.cpp#L98) | Decides each output from the ON button and the selector |
| ↳ [setSsr()](firmware/hotpress/src/main.cpp#L62) | Switches a solid-state relay |
| ↳ [onCommand()](firmware/hotpress/src/main.cpp#L148) · [onConfig()](firmware/hotpress/src/main.cpp#L131) | Website commands · new settings |
| ↳ [publishStatus()](firmware/hotpress/src/main.cpp#L166) | Sends live status to the hub |
| [inputs.h](firmware/hotpress/src/inputs.h) | ON button, 3-way selector |
| [display.cpp](firmware/hotpress/src/display.cpp) | OLED screen |
| [pins.h](firmware/hotpress/include/pins.h) · [config.h](firmware/hotpress/include/config.h) | Pin map · version and fixed settings |

### esp0 · Main Hub (bridge between the machines and the internet)

Folder: [firmware/hub/](firmware/hub/) · Behaviour spec: [docs/modules/hub.md](docs/modules/hub.md)
· Website: Dashboard → Devices → Main hub ([DeviceCards.tsx](web/src/features/dashboard/DeviceCards.tsx))

| File | Role |
|---|---|
| [main.cpp](firmware/hub/src/main.cpp) | Pairing, presence (online/offline), forwarding status, settings and commands |
| ↳ [handleFrame()](firmware/hub/src/main.cpp#L322) | Every radio message from a machine arrives here |
| ↳ [pushConfigs()](firmware/hub/src/main.cpp#L408) · [runCommands()](firmware/hub/src/main.cpp#L457) | Sends settings · commands down to the machines |
| [cloud.cpp](firmware/hub/src/cloud.cpp) | All Firebase traffic (own FreeRTOS task, one secure connection) |
| ↳ [flushPending()](firmware/hub/src/cloud.cpp#L330) | Uploads status/events in one batch every 0.5 s |
| ↳ [pollSignal()](firmware/hub/src/cloud.cpp#L515) | Checks every 0.5 s whether the website sent something |
| ↳ [pollCommands()](firmware/hub/src/cloud.cpp#L492) · [handleCommand()](firmware/hub/src/cloud.cpp#L442) | Downloads commands, rejects old ones |
| ↳ [fetchConfig()](firmware/hub/src/cloud.cpp#L381) · [ensureAuth()](firmware/hub/src/cloud.cpp#L245) | Downloads new settings · keeps the login fresh |
| [codec.cpp](firmware/hub/src/codec.cpp) | Converts machine binary ↔ website JSON ([statusToJson](firmware/hub/src/codec.cpp#L45), [configFromJson](firmware/hub/src/codec.cpp#L103)) |
| [wifimgr.cpp](firmware/hub/src/wifimgr.cpp) · [portal_page.h](firmware/hub/src/portal_page.h) | WiFi reconnects, saved networks, setup hotspot + its web page |
| [rtc.cpp](firmware/hub/src/rtc.cpp) | DS3231 real-time clock (time without internet) |
| [diag.cpp](firmware/hub/src/diag.cpp) | Remote health and log (`/hub/diag`, `/hubLog`), crash reports |
| [config.h](firmware/hub/include/config.h) · [pins.h](firmware/hub/include/pins.h) | Version and timings · pin map |

Not code, don't paste into a paper: `include/secrets.h` (WiFi and login passwords, never shared, not in git) and
[ca_bundle.h](firmware/hub/include/ca_bundle.h) (security certificates).

### Shared firmware libraries (used by every board)

| File | Role |
|---|---|
| [TileProtocol.h](firmware/lib/TileProtocol/src/TileProtocol.h) | **The message format** between machines and hub: every message type and data structure |
| [TileConfig.h](firmware/lib/TileProtocol/src/TileConfig.h) | Setting defaults and allowed ranges (same numbers as the website) |
| [ModuleLink.cpp](firmware/lib/TileProtocol/src/ModuleLink.cpp) | Machine side of the link: automatic pairing, heartbeat, receiving settings/commands/time |
| [EspNowTransport.cpp](firmware/lib/TileProtocol/src/EspNowTransport.cpp) | ESP-NOW radio setup and receive queue |
| [Reliable.cpp](firmware/lib/TileProtocol/src/Reliable.cpp) | Delivery confirmation (ACK) and retries |
| [TileTime.h](firmware/lib/TileProtocol/src/TileTime.h) | Date/time maths for the clocks |
| [Debounce.h](firmware/lib/TileIO/src/Debounce.h) · [PinNoise.cpp](firmware/lib/TileIO/src/PinNoise.cpp) | Input filtering and electrical-noise detection |

Also: [firmware/linktest/](firmware/linktest/src/main.cpp) is a small test program that only checks pairing (drives nothing).

---

## 4. Website (Tile Console)

Folder: [web/src/](web/src/) · Live at https://tile-machine-92345.web.app

| File | Role |
|---|---|
| [main.tsx](web/src/main.tsx) · [App.tsx](web/src/App.tsx) | Start-up and the page routes (which URL shows which page) |
| [lib/firebase.ts](web/src/lib/firebase.ts) | The connection to Firebase (the only file that sets it up) |
| [shared/types/rtdb.ts](web/src/shared/types/rtdb.ts) | Type definitions of every database record (mirror of [DATA_MODEL.md](docs/DATA_MODEL.md)) |
| [shared/machine.tsx](web/src/shared/machine.tsx) · [hooks/useValue.ts](web/src/shared/hooks/useValue.ts) | Live data: subscribes to the hub and machine records |
| [hooks/useCommand.ts](web/src/shared/hooks/useCommand.ts) | Sends STOP / Identify / Reboot and tracks the result |
| [shared/configDrafts.tsx](web/src/shared/configDrafts.tsx) · [configDefaults.ts](web/src/shared/configDefaults.ts) | Editing and saving machine settings · defaults and limits |
| Pages: [Dashboard](web/src/features/dashboard/DashboardPage.tsx) · [Shredder](web/src/features/shredder/ShredderPage.tsx) · [Containing](web/src/features/containing/ContainingPage.tsx) · [Hot Press](web/src/features/hotpress/HotpressPage.tsx) · [Events](web/src/features/events/EventsPage.tsx) · [Users](web/src/features/users/UsersPage.tsx) · [Activity](web/src/features/activity/ActivityPage.tsx) · [Login](web/src/features/auth/LoginPage.tsx) | One folder per page under `features/` |
| [components/](web/src/components/) | Shared building blocks: [CommandButton](web/src/components/CommandButton.tsx), [ModulePanel](web/src/components/ModulePanel.tsx), [ui.tsx](web/src/components/ui.tsx) (buttons, cards) … |
| [index.css](web/src/index.css) | Look and feel (dark/light themes) |

## 5. Cloud (Firebase)

| File | Role |
|---|---|
| [firebase/database.rules.json](firebase/database.rules.json) | Security rules: who may read/write what (e.g. only the hub writes machine status; the website can't start machines) |
| [firebase.json](firebase.json) · [.firebaserc](.firebaserc) | Deploy settings (rules + website hosting) |
| [docs/DATA_MODEL.md](docs/DATA_MODEL.md) | Layout of every record in the database (the contract between hub and website) |

---

## 6. How the code connects: three journeys, step by step

### A. Live status: machine → website (e.g. the Shredder's switch position)

1. The shredder reads its switch in [inputs.h](firmware/shredder/src/inputs.h) and its state machine
   [updateLogic()](firmware/shredder/src/main.cpp#L172) decides what happens.
2. [publishStatus()](firmware/shredder/src/main.cpp#L374) packs it into a `StatusShredder`
   ([TileProtocol.h](firmware/lib/TileProtocol/src/TileProtocol.h#L237)) and
   [ModuleLink::publishStatus()](firmware/lib/TileProtocol/src/ModuleLink.cpp#L200) sends it over ESP-NOW.
3. The hub receives it in [handleFrame() → STATUS](firmware/hub/src/main.cpp#L348), converts it to JSON with
   [codec::statusToJson()](firmware/hub/src/codec.cpp#L45) and queues `modules/shredder/state`.
4. [cloud::flushPending()](firmware/hub/src/cloud.cpp#L330) uploads it to Firebase (allowed by the
   [`modules` rules](firebase/database.rules.json#L49)).
5. The website listens through [useValue()](web/src/shared/hooks/useValue.ts) →
   [machine.tsx](web/src/shared/machine.tsx#L26) and the [Shredder page](web/src/features/shredder/ShredderPage.tsx)
   shows it under "Live status".

### B. A command: website → machine (e.g. STOP or Identify)

1. The button is a [CommandButton](web/src/components/CommandButton.tsx), placed by [ModulePanel](web/src/components/ModulePanel.tsx#L45).
2. [useCommand()](web/src/shared/hooks/useCommand.ts#L57) writes the command, its Activity-log entry and
   `signal/commands + 1` in one update (checked by the [`signal` rules](firebase/database.rules.json#L71)).
3. The hub's [pollSignal()](firmware/hub/src/cloud.cpp#L515) notices the signal,
   [pollCommands()](firmware/hub/src/cloud.cpp#L492) downloads the command and
   [handleCommand()](firmware/hub/src/cloud.cpp#L442) refuses anything older than 30 s.
4. [runCommands()](firmware/hub/src/main.cpp#L457) sends it to the machine over ESP-NOW with delivery confirmation
   ([Reliable.cpp](firmware/lib/TileProtocol/src/Reliable.cpp)).
5. The shredder runs it in [onCommand()](firmware/shredder/src/main.cpp#L340) and confirms; the hub writes
   `done` and the button shows a check mark.

### C. New settings: website → machine (e.g. AUTO warning time)

1. The Shredder page's "Rules & timing" fields are saved by
   [configDrafts saveAll()](web/src/shared/configDrafts.tsx#L112), which also writes
   [`signal/config/shredder`](web/src/shared/configDrafts.tsx#L125).
2. The hub's [pollSignal()](firmware/hub/src/cloud.cpp#L515) sees the new version and
   [fetchConfig()](firmware/hub/src/cloud.cpp#L381) downloads it; [codec::configFromJson()](firmware/hub/src/codec.cpp#L103)
   checks it and turns it into a binary `ConfigShredder`.
3. [pushConfigs()](firmware/hub/src/main.cpp#L408) sends it to the shredder.
4. The shredder's [onConfig()](firmware/shredder/src/main.cpp#L321) validates it against
   [TileConfig.h](firmware/lib/TileProtocol/src/TileConfig.h) and applies it **only when idle**; the website then shows
   "Synced to module".

### Pieces that must always match each other

| This … | … must match this | Why |
|---|---|---|
| [docs/HARDWARE.md](docs/HARDWARE.md) | each board's `pins.h` | the real wiring |
| [docs/PROTOCOL.md](docs/PROTOCOL.md) | [TileProtocol.h](firmware/lib/TileProtocol/src/TileProtocol.h) | the radio message format |
| [docs/DATA_MODEL.md](docs/DATA_MODEL.md) | [rtdb.ts](web/src/shared/types/rtdb.ts) + [codec.cpp](firmware/hub/src/codec.cpp) | the database record layout |
| [TileConfig.h](firmware/lib/TileProtocol/src/TileConfig.h) | [configDefaults.ts](web/src/shared/configDefaults.ts) | setting defaults and limits |

---

## 7. Design documents (for the methodology chapter)

| Document | Contents |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | System design, data flows, failure behaviour |
| [docs/PROTOCOL.md](docs/PROTOCOL.md) | ESP-NOW message specification |
| [docs/DATA_MODEL.md](docs/DATA_MODEL.md) | Firebase database layout |
| [docs/HARDWARE.md](docs/HARDWARE.md) | Pin maps, wiring, power notes per board |
| [docs/modules/](docs/modules/) | Behaviour spec and state machine of [hub](docs/modules/hub.md), [shredder](docs/modules/shredder.md), [containing](docs/modules/containing.md), [hotpress](docs/modules/hotpress.md) |
| [docs/SETUP.md](docs/SETUP.md) | How to set up GitHub, Firebase, PlatformIO and the website |
| [CLAUDE.md](CLAUDE.md) | Project rules, safety invariants, progress log |

## 8. Good excerpts to show in a paper

Short, self-contained pieces that each show one idea:

- **State machine:** the shredder's [updateLogic()](firmware/shredder/src/main.cpp#L172) (with its states in
  [TileProtocol.h](firmware/lib/TileProtocol/src/TileProtocol.h#L104)).
- **Safety on start-up:** relay forced OFF first thing in [setup()](firmware/shredder/src/main.cpp#L422).
- **Message format:** one struct, e.g. `StatusShredder` in [TileProtocol.h](firmware/lib/TileProtocol/src/TileProtocol.h#L237).
- **Hub bridging:** [handleFrame()](firmware/hub/src/main.cpp#L322) and [pollSignal()](firmware/hub/src/cloud.cpp#L515).
- **Website command:** [useCommand.ts](web/src/shared/hooks/useCommand.ts).
- **Security:** the `commands` and `signal` sections of [database.rules.json](firebase/database.rules.json#L71).
