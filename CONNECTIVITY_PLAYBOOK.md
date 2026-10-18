# Connectivity playbook: keeping ESP32 hardware and a cloud website connected 24/7

Everything we learned getting the Tile Machine's hardware and website to stay connected around the clock:
the architecture, every setting, every bug we hit (with its root cause and fix), and how to test it.
Use it to build the same kind of system again without repeating those bugs.

**Stack:** ESP32 (Arduino core 2.0.x, PlatformIO) machine modules → ESP-NOW radio → one ESP32 hub →
WiFi + HTTPS → Firebase Realtime Database → React website. Code map: [CODE_GUIDE.md](CODE_GUIDE.md).

> **For Claude in another workspace:** copy **section 1** into that project's `CLAUDE.md` (under its
> conventions) so the rules apply every session. Sections 2–6 are the reasons and the numbers behind them;
> keep this file in the project too and read the matching section before changing networking code.

---

## 1. Rules to paste into CLAUDE.md

```markdown
## Connectivity rules (from the Tile Machine project, each one fixes a real outage)

### ESP32 ↔ cloud (hub)
1. ONE TLS connection at a time on an ESP32 without PSRAM. Each TLS session needs ~40 KB, partly in one
   block. Close the database connection before opening the sign-in connection, and close sign-in right
   after. Never keep a second always-open connection (no SSE/stream, no second HTTPS client).
2. No live streams. To get changes from the website quickly, POLL a tiny "signal" node (every 500 ms)
   on the same keep-alive connection. The website writes the signal in the SAME multi-path update as the
   change. Mark a signal as seen only after its download succeeded. Keep slow fallback polls
   (full commands every 15 s, config versions at boot + every 30 s).
3. Use ESP-IDF `esp_http_client` with keep-alive for REST, never Arduino `HTTPClient`/`WiFiClientSecure`
   (on core 2.0.x they block ~10 s on an idle keep-alive connection).
4. Firebase answers an expired/invalid token AND a rule violation with the same 401 "Permission denied".
   A 401 on a token older than 5 min = sign in again and resend the same data. A 401/400 on a fresh token
   = the data is refused: retry path by path, drop only the refused paths, log each one by name.
5. Never drop data on a network error or 5xx: requeue it (newer values win) and resend.
6. "Online" means the LAST request actually succeeded. Any failed read or write sets offline. Watchdogs,
   LEDs and the website must see the truth.
7. Batch writes into one multi-path PATCH every 500 ms. Never put a path and its parent in one batch
   (HTTP 400 for the whole batch). Send per-item status updates (e.g. command results) separately.
8. Refresh the auth token 5 min before expiry; if the refresh fails, keep using the old token until
   1 min before its real expiry while retrying (5 s doubling to 60 s).
9. Self-repair before restart: 3 connection failures in a row (HTTP -1) → rebuild the HTTP clients
   (at most every 2 min). Restart only as a last resort: 15 min since the last SUCCESSFUL cloud contact
   while WiFi has been up 20 s+ and nobody is on the setup page; double the wait each time in a row
   (15, 30, 60, 120 min). Count from the last success, never from "WiFi up" (WiFi blips reset that).
10. On every WiFi GOT_IP set DNS to 8.8.8.8 with the router's DNS as fallback (slow router DNS = 7 s lookups).

### WiFi and setup hotspot
11. The hub owns WiFi reconnects (Arduino auto-reconnect OFF): retry after 1 s, then every 15 s through
    up to 5 saved networks (NVS), forever. Never reboot because of WiFi loss.
12. Setup hotspot: open at boot and on a short BOOT press, close after 3 min unused. Open after 30 s
    offline. WiFi down → stay open until WiFi is back. WiFi up but no cloud → close after 3 min unused and
    don't reopen for 10 min (the hotspot's ~46 KB is needed to rebuild the TLS connection).
13. The hotspot always sits on the router's channel so ESP-NOW keeps working while it is open.

### ESP-NOW (modules ↔ hub)
14. ESP-NOW runs on the router's channel (the hub cannot choose it). Modules pair automatically: try the
    stored hub, else scan channels 1–13 broadcasting HELLO; after 5 failed sends in a row, scan again.
15. ESP-NOW receive callbacks only copy the packet into a FreeRTOS queue and return. All handling,
    and every network call, happens in loop() or a task.
16. Binary packed structs with a version number and static_assert sizes; ACK + 3 retries (200 ms) for
    config and commands; heartbeat status every 1 s; a module is offline after 3.5 s of silence.

### Commands, config and time (safety)
17. The website can STOP and change settings but can never START anything. Local STOP always wins.
18. Commands older than 30 s are marked expired, never run. Commands are only handled once the hub knows
    the time (read the RTC at boot, before WiFi; NTP corrects it later).
19. Config is versioned, stored in the module's NVS, and applied only when that unit is idle.
20. Stamp events with the hub's clock when they happen, not when they are uploaded.

### Firmware coding traps
21. ArduinoJson 7 stores `const char[]` by POINTER: wrap local buffers in `String(...)` before putting
    them in a JsonDocument, or a later write sends garbage (whole batch refused, HTTP 400).
22. Never copy a multi-KB String on the ESP32: move it and check that allocations (concat/reserve)
    succeeded, else reconnect cleanly instead of crashing.
23. Inputs: integrating filter (score ±1 per ms), per-pin noise monitor reported to the website, and a
    power-up interlock that needs the OFF/neutral position held for 1 s (never trust the first reading).
24. No delay() in loop(); everything millis()-based.

### Observability and testing
25. Remote diagnostics so the hub can run without a PC: health every 10 s (free heap, lowest heap,
    largest free block, uptime, slowest loop), an error/important-event log in the cloud (rate-limited,
    7 days), and crash breadcrumbs in RTC_NOINIT memory reported after a crash restart.
26. Test commands through the REAL website path (its multi-path write), never only with a CLI push.
27. Test every failure path with compile-time test flags (fake cloud down, failing requests, refused
    token, forced crash), then flash the normal build. Never leave a test flag in a deployed build.
28. Verify on the real hardware and report the heap numbers; then run a 2–3 day soak test and read the
    cloud log and health figures before calling it done.
```

---

## 2. Architecture

### 2.1 The whole path

```
 Module ESP32s (shredder, hotpress, …)          Hub ESP32                     Cloud              Website
 ┌───────────────────────────┐   ESP-NOW   ┌──────────────────────┐  HTTPS   ┌──────────┐  live  ┌────────┐
 │ state machine (local)     │ ──STATUS──▶ │ loop(): radio,        │ ───────▶ │ Firebase │ ◀────▶ │ React  │
 │ NVS: last config          │ ◀─CONFIG─── │ presence, pairing     │ ◀─────── │ Realtime │        │ site   │
 │ runs alone if hub is lost │ ◀─COMMAND── │ cloud task (core 0):  │  1 TLS   │ Database │        │        │
 │ local STOP always wins    │ ◀─TIME───── │ batch, poll, auth     │  conn.   └──────────┘        └────────┘
 └───────────────────────────┘   ACK+retry └──────────────────────┘
```

- **Modules never touch WiFi or the cloud.** They only talk ESP-NOW to the hub and keep running on
  their stored config if the hub, WiFi or internet disappears.
- **The hub runs two jobs:** `loop()` (ESP-NOW, presence, LED, WiFi manager, setup hotspot) and one
  FreeRTOS **cloud task** pinned to core 0 (all HTTPS). They share data through a mutex-protected queue,
  so slow internet never blocks the radio.

### 2.2 Hub ↔ cloud: one connection, polling a signal

| Direction | How | When |
|---|---|---|
| Hub → cloud | One multi-path PATCH with all pending changes (status, presence, events, log) | every 500 ms if anything changed |
| Hub → cloud | Command status (`sent`, `done`, `failed`, `expired`) | separate small PATCH each |
| Cloud → hub | GET `/signal` (a few bytes) | every 500 ms |
| Cloud → hub | GET `/commands` | when `signal/commands` changed, and every 15 s anyway |
| Cloud → hub | GET one module's config | when `signal/config/<module>` changed, at boot, and version check every 30 s |
| Sign-in | POST to Firebase Auth, **after closing the DB connection** | about hourly (5 min before expiry) |

The website's writes that the hub must notice:

```
command:  { commands/<module>/<id>: {...},  audit/<id>: {...},  signal/commands: increment(1) }   ← one update
config:   { modules/<module>/config: {...version: n},  audit/<id>: {...},  signal/config/<module>: n }
```

Measured result: a website command is carried out in about 1–1.5 s; free heap ~136 KB with the largest
block 65–73 KB even with the hotspot open (two TLS connections had left 47 KB and failed ~11 times a minute).

### 2.3 Failure handling in the cloud task

| Result of a request | Meaning | Action |
|---|---|---|
| 200 / 204 | OK | set **online** |
| -1 (no connection), 5xx | network / server | requeue, set **offline**, count toward self-repair |
| 401 on a token older than 5 min | token expired or revoked | sign in again, resend, **offline** until signed in |
| 401 / 400 on a fresh token | the data broke a rule | retry path by path, drop only refused paths, log them |
| 3× -1 in a row (WiFi up) | stuck TLS state | rebuild both HTTP clients (max every 2 min) |
| 15 min no success (WiFi up 20 s+, nobody on hotspot) | stuck stack | restart hub; next limit 30, 60, 120 min |

### 2.4 WiFi manager and setup hotspot (`TileHub-XXXX`, http://192.168.4.1)

| Situation | Hotspot |
|---|---|
| Every boot, or a short BOOT press (< 1.5 s) | opens; closes after 3 min without use |
| Offline 30 s, WiFi down | opens and stays open until WiFi is back |
| Offline 30 s, WiFi up but cloud down | opens; closes after 3 min unused; won't reopen for 10 min |
| A phone is connected | hub only retries the network on the hotspot's channel (phone isn't dropped) |

The setup page lists saved networks (up to 5, the last working one first), scans, and tests a new
network for 20 s before saving it. Passwords are never sent back to the page. Status LED: slow blink =
connecting to WiFi, fast blink = cloud problem, solid = all good, double blink = hotspot open.

### 2.5 ESP-NOW link and pairing

- Every frame has a magic number, protocol version, network ID and module ID; anything else is ignored.
- **Pairing (no buttons):** a module tries its stored hub on its stored channel (600 ms). If there's
  no answer it scans channels 1–13 (150 ms each) broadcasting HELLO; the hub answers WELCOME with the
  channel; the module stores both in NVS.
- **Presence:** modules send status at least every 1 s; the hub marks a module offline after 3.5 s of
  silence. A module that fails 5 sends in a row assumes the hub moved (e.g. new router channel) and scans.
- **Reliable messages:** CONFIG and COMMAND need an ACK; 3 retries 200 ms apart, duplicates ignored.
- **Time:** the hub sends TIME every 60 s; modules use it only for their screens.

### 2.6 Website side

- The website treats the hub as offline if `hub/lastSeen` (written every 10 s with server time) is older
  than 25 s, and greys out every machine tile then. It uses Firebase's server-time offset, not the PC clock.
- A command still `pending` after 10 s shows "No answer from the hub" (the user isn't left waiting).
- Database rules: only the hub writes machine status; commands must carry the sender's uid; the signal
  accepts numbers only; "all modules" accepts only STOP.

---

## 3. Settings that keep it running (current values)

| Setting | Value | Where |
|---|---|---|
| Batched upload | 500 ms | `CLOUD_FLUSH_MS` (hub `config.h`) |
| Signal poll | 500 ms | `CLOUD_SIGNAL_POLL_MS` |
| Full commands poll (fallback) | 15 s | `CLOUD_CMD_FULL_POLL_MS` |
| Config version poll (fallback) | 30 s | `CLOUD_CONFIG_POLL_MS` |
| Command max age | 30 s | `CLOUD_CMD_MAX_AGE_MS` |
| Re-login on 401 if token older than | 5 min | `CLOUD_REAUTH_MIN_AGE_MS` |
| Self-repair after | 3 failures in a row, max every 2 min | `CLOUD_TLS_FAILS_RESET`, `CLOUD_CLIENT_RESET_GAP_MS` |
| Restart watchdog | 15 min, doubling; reset after 10 min online | `CLOUD_STUCK_RESTART_MS`, `CLOUD_STUCK_RESET_AFTER_MS` |
| Offline upload backlog cap | 12 KB; beyond it new events and log lines are dropped (each dropped event is logged) | `CLOUD_PENDING_MAX_BYTES` |
| WiFi retries | 1 s, then every 15 s, forever | `WIFI_FIRST_RETRY_MS`, `WIFI_RETRY_EVERY_MS` |
| Hotspot idle close / offline open / cooldown | 3 min / 30 s / 10 min | `PORTAL_*` |
| Hub heartbeat (`lastSeen`) | 10 s | `HUB_HEARTBEAT_MS` |
| Module heartbeat / offline after | 1 s / 3.5 s | `HEARTBEAT_MS`, `HUB_OFFLINE_AFTER_MS` (TileProtocol.h) |
| ACK timeout / retries | 200 ms / 3 | `ACK_TIMEOUT_MS`, `ACK_RETRIES` |
| Module re-scan after | 5 failed sends | `MODULE_LOST_AFTER_FAILS` |
| Website: hub stale / command no-answer | 25 s / 10 s | `HUB_STALE_MS`, `COMMAND_NO_ANSWER_MS` |
| Retention | events 30 d, hub log 7 d, finished commands 24 h | `CLOUD_*_KEEP_MS` |
| Log rate limit | same message 1/min, max 30 lines per 10 min | `DIAG_*` |

---

## 4. Bug history: symptom → root cause → fix

Each row became one of the rules in section 1. Versions are the hub firmware unless noted.

| # | Symptom | Root cause | Fix |
|---|---|---|---|
| 1 | Every cloud write took ~10 s, the hub froze | Arduino `WiFiClientSecure` blocks for the full timeout on an idle keep-alive connection (core 2.0.x); router DNS took ~7 s per lookup | ESP-IDF `esp_http_client` keep-alive; DNS 8.8.8.8 with router fallback on every GOT_IP |
| 2 | Hub never reconnected after the router's 2.4 GHz radio dropped out | Core 2.0.x auto-reconnect stops after "network not found" (reason 201) | Hub owns retries: 1 s, then every 15 s, forever (0.2.0 rebooted after 10 min; 0.3.0 replaced that) |
| 3 | Changing WiFi needed a reflash | WiFi only in `secrets.h` | Setup hotspot with saved networks (0.3.0) |
| 4 | Offline for 5 hours with the hotspot open | Hotspot (~46 KB) + TLS didn't fit a fragmented heap (mbedtls -0x7F00) | Hotspot closes when unused if WiFi is up, 10-min cooldown; cloud watchdog (0.3.1) |
| 5 | Hub crashed (LoadProhibited) after an offline period | Copying a multi-KB stream line failed to allocate, the copy read address 0 | Move instead of copy, check allocations, reconnect on failure (0.3.1; the stream is gone since 0.4.0) |
| 6 | Watchdog never fired, hub offline 36 min+ | It counted "WiFi up and cloud down"; WiFi blips every 20–60 s reset it | Count from the last successful cloud contact; self-repair rebuilds clients after 3 failures (0.3.3) |
| 7 | Hub log batches refused (HTTP 400) | ArduinoJson 7 kept a pointer to a local `char[]` that was gone by upload time | Wrap in `String(...)` (0.3.2) |
| 8 | Website commands silently ignored (CLI tests passed) | A multi-path write arrives on the stream as a patch whose keys are paths | Split path keys (0.3.4); always test through the website; site shows "No answer" after 10 s |
| 9 | Recurring offline; data dropped while "online" | 2–3 TLS sessions didn't fit (stream failed ~11/min, sign-in failed, token expired), then 401 "Permission denied" was treated as bad data | **0.4.0:** one TLS connection, `/signal` polling, any 401 on an old token = re-login and resend, per-path isolation, online = last request succeeded |
| 10 | Shredder changed mode / relay by itself | Weak internal pull-ups + motor noise; switch read OFF for an instant at power-up | Integrating input filter, noise monitor shown on the site, debounce adjustable from the site, interlock needs OFF held 1 s (shredder 0.2.1–0.2.4, hotpress 0.1.2); hardware RC filters recommended |
| 11 | Shredder board wouldn't flash / flash failed halfway | No working auto-reset; USB drops from electrical noise; pressing BOOT+EN again mid-upload; an old flasher still holding the port | `esptool --before no_reset --no-stub` at 115200, press BOOT+EN **once**; stop stale flasher processes first |
| 12 | OLED clock 23 s behind the operator's watch | The hub was within 0.5 s of internet time; the watch was fast | Display-only offset per module (`OLED_CLOCK_ADJUST_S`); logged times stay true |

---

## 5. Testing: proving it stays up

**Test builds (hub)**, set with `PLATFORMIO_BUILD_FLAGS`, never left in a deployed build:

| Flag | What it simulates |
|---|---|
| `-DHUB_TEST_SHORT_TIMERS` | minutes become seconds (hotspot and watchdog timers) |
| `-DHUB_TEST_FAKE_CLOUD_DOWN_MS=60000` | cloud "gone" 60 s after boot while WiFi stays up |
| `-DHUB_TEST_FAIL_REQUESTS_FROM_MS=60000` | every cloud request fails for 90 s (self-repair) |
| `-DHUB_TEST_REJECT_TOKEN_FROM_MS=360000` | the database refuses the current token from 6 min on (401 re-login) |
| `-DHUB_TEST_CRASH_AFTER_MS=60000` | deliberate crash (crash report) |
| `-DCLOUD_DEBUG` | logs every request's time and the heap |

**Every change to networking code:**
1. Build, flash, watch the serial log: WiFi → signed in → online, heap and largest block.
2. Send commands and save a setting **through the website**, with the hotspot open and closed; check the
   result appears in about 1–2 s and that the setting was applied.
3. Run the matching test flag and confirm recovery without data loss (lines logged during the outage
   arrive afterwards).
4. Flash the normal build again.
5. Soak test 2–3 days, then read the health figures and hub log: uptime with no unexplained restarts,
   lowest heap stable, offline moments short and self-recovered.

**What a healthy hub looked like after the fix (13 h run):** no restarts, heap 140 KB (lowest 100 KB),
largest block 74 KB, slowest loop 12 ms, two short offline moments (2 s and 7 s), both recovered by
themselves, all website commands done in 0.8–2.2 s.

---

## 6. Limits that remain

- **Power loss:** nothing runs; everything recovers on its own when power returns. Unsent events in RAM
  are lost (planned: keep them in the RTC board's EEPROM).
- **Router or internet down:** the machines keep working locally; the website shows the hub offline;
  the hub reconnects by itself when the network returns.
- **WiFi password change:** use the setup hotspot (opens by itself after 30 s without WiFi).
- **More memory headroom** (for OTA updates or long outages) needs an ESP32 with PSRAM (WROVER, or
  ESP32-S3 "N16R8"); pins change (no GPIO 22 on the S3).
- **Electrical noise** from the shredder motor still needs the hardware filters (pull-ups, 100 nF
  capacitors, motor snubber); the software filters only hide it.
