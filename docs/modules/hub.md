# Module: Main Hub (esp0)

The hub runs **no process logic** and drives **no actuators**. It is a bridge and supervisor.

## Boot sequence

1. Status LED slow blink. Load `secrets.h` (Firebase API key, DB URL, hub email/password, hotspot
   password) and the saved WiFi networks from NVS (seeded from `WIFI_SSID`/`WIFI_PASSWORD` the first time).
2. Open the setup hotspot and connect to the first saved network (see "WiFi and setup hotspot").
   Wait up to 20 s for WiFi, answering the setup page meanwhile. Read the channel (`WiFi.channel()`).
3. `esp_now_init()`, register callbacks, add the broadcast peer, and add stored module peers from
   NVS.
4. NTP time sync (for log timestamps). Firebase sign-in as the `hub` user.
5. Write `/hub` (online, bootAt, fw, ip, rssi, channel). Log `HUB_BOOT`. Set every
   `modules/*/presence/online=false` (fresh start, modules re-announce within a second).
6. Read `/signal` every 500 ms (see "One secure connection and /signal"). Commands are only handled
   once the hub knows the time, so every command's age can be checked and old commands are never
   replayed. `modules/*/config/version` is read once at boot, then every 30 s as a fallback.

## Clock: DS3231 RTC + NTP

- **Boot:** read the DS3231 (I2C 0x68) and set the system clock **before WiFi**. The hub knows the
  time with no internet, so commands (the time is needed to reject old ones) and event timestamps
  work immediately.
- **After every NTP sync** (at start, then hourly): NTP wins. The RTC is rewritten if it was never
  set (oscillator-stop flag, e.g. new battery) or drifted more than `RTC_MAX_DRIFT_S` (2 s).
- **Events** are stamped with the hub clock when they happen (not when uploaded).
- **Modules** get a `TIME` message (UTC + `LOCAL_TZ_OFFSET_MIN` = +480, UTC+8) for their OLED clocks.
- The website hub card shows **Clock (DS3231)**: `hub/rtc` = ok / lost-power / missing and
  `hub/timeSource` = ntp / rtc / none.
- Verified on the hub: an unset RTC was set from NTP 1.2 s after WiFi. On the next boot the RTC gave
  the correct time 0.7 s after reset, and NTP agreed within 1 s.
- The DS3231 board also has a 4 KB AT24C32 EEPROM (0x57), unused so far. Planned: keep unsent events
  through a power cut.
- ⚠️ Installed with a **CR2032, unmodified board, powered from 3.3 V**: safe, because the charging circuit only
  reaches ~2.6 V < cell voltage. Never power the RTC board from 5 V (it would charge the CR2032).

## Firebase write rules (learned by testing the real database)

- All state, presence, info, hub fields and events go in **one multi-path PATCH** every 500 ms.
- A batch must **never contain a path and its parent** (e.g. `hub` and `hub/lastSeen`), or RTDB
  rejects the whole batch (HTTP 400). The hub only writes leaf paths such as `hub/lastSeen` and
  `modules/X/state`.
- One forbidden path fails the **whole** batch (HTTP 401), so command status updates go in
  **separate** requests. Since fw 0.4.0 a refused batch is retried path by path: only the refused paths
  are dropped, and each one is named in the hub log (`Firebase refused <path> (HTTP n), dropped`).
- **Don't use Arduino `HTTPClient`/`WiFiClientSecure` for the REST writes.** In core 2.0.17,
  `WiFiClientSecure::connected()`/`available()` call `mbedtls_ssl_read()` on a blocking socket.
  On an idle keep-alive connection they block for the full socket timeout (10 s), which stalled
  every write and froze the task. REST therefore uses ESP-IDF `esp_http_client`: one persistent
  connection to the DB. The sign-in connection is only opened after closing the DB connection, and is
  closed right after use (see "One secure connection and /signal").
- **DNS**: the first installation's router took about 7 s per lookup. On every `GOT_IP` the hub
  sets DNS to 8.8.8.8, with the router's DNS as fallback.
- Measured on the real hub: sign-in about 2 s, batched PATCH about 105 ms, STOP round trip
  (web → hub → Firebase status) about 125 ms, free heap stable around 87 KB.
- Build with `PLATFORMIO_BUILD_FLAGS=-DCLOUD_DEBUG` to log every request's timing and the heap.
- TLS is verified against `include/ca_bundle.h` (GTS Root R1–R4 + GlobalSign Root CA, checked
  with openssl against both Google hosts). GlobalSign R1 expires 2028-01-28. Re-check the bundle
  before then.

## Main loop tasks (all non-blocking)

| Task | Period |
|---|---|
| Handle incoming ESP-NOW (queued from the callback, handled in `loop`, never in the callback) | continuous |
| Presence timeout check | 250 ms |
| `/hub/lastSeen` + online modules' `presence/lastSeen` | 10 s |
| Flush throttled `state` writes (latest STATUS per module) | 500 ms |
| ACK/retry engine for CONFIG/COMMAND | 50 ms |
| WiFi reconnect (saved networks in turn), setup hotspot timers + web requests | continuous |
| Firebase reconnect with backoff (5 s doubling to 60 s) | on failure |
| Retention cleanup (events 30 d, commands 24 h) | boot + 24 h |

## WiFi and setup hotspot (fw 0.3.0, src/wifimgr.cpp)

- **Saved networks:** up to 5 in NVS (`wifinets`), the one that worked last is tried first. The first
  boot seeds the list from `secrets.h`. Passwords are never sent to the setup page.
- **Reconnect, never restart:** after a drop the hub retries after 1 s, then every 15 s through the
  saved networks in turn, forever. The old "reboot after 10 min without WiFi" is gone: the setup
  hotspot needs the hub running, and a retry recovers from the router's 2.4 GHz dropouts anyway.
  Arduino auto-reconnect is off: the hub owns the retries.
- **Setup hotspot** `TileHub-XXXX` (last 4 hex digits of the MAC), WPA2 with `PORTAL_PASSWORD`,
  captive page at http://192.168.4.1 (phones open it by themselves after joining).

| When | Hotspot |
|---|---|
| Every boot (power-on, EN/reset button) | opens, closes after 3 min without use |
| Short BOOT press (< 1.5 s) | opens without restarting, same 3-min rule |
| Offline for 30 s, WiFi down | opens and stays open until WiFi is back; then the 3-min rule applies |
| Offline for 30 s, WiFi up but no cloud | opens, closes after 3 min without use, then stays closed 10 min (fw 0.3.1) |

  "Use" = any request from the setup page (it polls every 2 s while open) or a phone joining.
- **Setup page:** status, saved networks (forget), scan, connect to a new network. A new network is
  tested for 20 s: if it connects it becomes first choice, if not the page shows why (wrong
  password, not found) and offers "Save anyway", and the hub goes back to its saved networks.
- **One radio:** the hotspot always sits on the router's channel (or the last known one while
  offline), so ESP-NOW modules keep working while it is open. While a phone is on the hotspot the
  hub only retries the last network on that channel, so the hotspot doesn't jump and drop the phone.
  Connecting to a network on another channel moves the hotspot: the phone rejoins, and the modules
  re-scan and re-pair by themselves.
- **Why the difference (fw 0.3.1):** the hotspot uses ~46 KB. After hours of running, with the heap
  fragmented, the TLS connection to Firebase could no longer be set up next to it (mbedtls -0x7F00,
  out of memory), so fw 0.3.0 stayed "offline with the hotspot open" for 5 h. Found 2026-09-26.
- **Self-repair (fw 0.3.3):** 3 connection failures in a row (HTTP -1) while WiFi is up: the cloud task
  rebuilds both HTTP clients (frees their TLS state), at most every 2 min.
- **Cloud watchdog (last resort, fw 0.3.1, fixed in 0.3.3):** 15 min since the LAST SUCCESSFUL cloud
  contact while WiFi works (up 20 s+) and nobody on the setup page = stuck network/TLS stack: the hub
  restarts (modules keep running). 0.3.1 counted "WiFi up and cloud down" continuously, so every WiFi
  blip reset it; with the router dropping every 20-60 s it never fired (hub offline 36 min+, 2026-09-26). In a row the wait doubles
  (15, 30, 60, 120 min) so an internet outage doesn't cause restart loops; 10 min online resets it.
  `HUB_BOOT` arg1 = 1 marks these restarts (Events page: "restarted by the hub itself"). WiFi loss
  alone never restarts the hub.
- **Test builds:** `PLATFORMIO_BUILD_FLAGS="-DHUB_TEST_SHORT_TIMERS"` (minutes → seconds) and
  `-DHUB_TEST_FAKE_CLOUD_DOWN_MS=60000` (cloud "gone" 60 s after boot, WiFi up). Never flash them for use.
  `-DHUB_TEST_FAIL_REQUESTS_FROM_MS=60000`: every cloud request fails for 90 s (self-repair test).
  `-DHUB_TEST_REJECT_TOKEN_FROM_MS=360000`: from 6 min on, the database refuses the login token in use
  (401) until the hub signs in again (401 re-login test, fw 0.4.0).
- **LED:** slow blink = WiFi connecting, fast blink = cloud problem, solid = all good, double blink
  = all good and setup hotspot open.
- Reported in `/hub`: `wifiSsid` (network) and `portal` (hotspot open), shown on the Dashboard.

## Remote diagnostics (fw 0.3.2, src/diag.cpp)

So the hub can run for days without a PC on its USB port. Shown in the Dashboard → Devices → Main hub.

- `/hub/diag` every 10 s: free heap, lowest since boot, largest free block (a new TLS connection
  needs ~40 KB in one piece), uptime, slowest `loop()` pass in the last 10 s, log lines not sent.
- `/hubLog` (7 days): every `[ERROR]` line plus important `[NET]`/`[STATE]` lines (WiFi, cloud,
  hotspot, reset reason, watchdog). Log lines go through `diag::printf` (prints to Serial too), wait in a
  small ring buffer and are handed to the cloud from `loop()`, so logging never takes the cloud mutex
  (many error lines are printed while cloud.cpp holds it). Rate limit: the same message (digits ignored)
  at most once a minute, at most 30 lines per 10 min; skipped lines are counted and reported.
  The 10 s status report stays Serial-only.
- **Crash report:** each task marks what it is doing (`diag::phase`); the marks, the last log line and
  the memory figures live in RTC RAM, which survives a crash restart. After a panic or watchdog reset
  the hub writes a `C` entry: uptime, what loop/cloud were doing, memory, last message.
- Lesson (bug found while testing): ArduinoJson 7 stores `const char[]` arrays by POINTER (it takes
  them for string literals). Always `String(...)` local buffers before putting them in a JsonDocument;
  a dangling one made Firebase refuse the whole batch (HTTP 400). A refused batch is now printed.
- Test build: `-DHUB_TEST_CRASH_AFTER_MS=60000` crashes on purpose to check the crash report.

## One secure connection and /signal (fw 0.4.0)

**Why (found 2026-09-26):** up to fw 0.3.4 the hub kept two TLS connections open (writes + the
`/commands` SSE stream) and opened a third for the hourly sign-in. Each needs ~40 KB, partly in one block.
Next to WiFi (and the hotspot) they didn't reliably fit: the hub log showed `command stream: TLS connect
failed` about 11 times a minute (largest free block always 47 KB), so web commands weren't received, and
sign-ins failed until the token expired. Firebase then answered every write with 401 `Permission denied`
(it says the same for an expired token and for a rule violation); 0.3.4 only looked for "expired", so it
dropped those batches and still counted itself online, which also blinded the cloud watchdog.

**Now:**
- **One TLS connection at a time.** Every database request uses one keep-alive connection. Before the
  hourly sign-in that connection is closed; the sign-in connection is closed right after. Measured: 136 KB
  free, largest block 65 KB, with the setup hotspot open.
- **No stream.** The website writes `/signal` in the same multi-path update as the change
  (docs/DATA_MODEL.md "Signal"). The hub reads that tiny node every 500 ms and only then downloads
  `/commands` or the one module config that changed. A signal counts as seen only once its download worked.
  Fallbacks (for anything that didn't bump the signal): all of `/commands` every 15 s (this also expires and
  cleans old commands), config versions at boot and every 30 s. A web command is done in about 1-1.5 s.
- **Token:** refreshed 5 min before it expires. If a refresh fails, the old token is used until 1 min
  before its real expiry while sign-in retries (5 s doubling to 60 s).
- **Any 401 on a token older than 5 min = log in again:** the batch is kept and sent again after the new
  sign-in; "offline" until then. A 401 on a fresh token means the DATA was refused by a rule: then the
  batch is retried path by path and only the refused paths are dropped (and logged). No sign-in loops.
- **"Online" = the last request reached Firebase.** Any failed write or read (network, 5xx, bad token) sets
  offline, so the cloud watchdog and the LED see the truth.
- The hotspot no longer changes how commands arrive (0.3.x polled only while it was open).

## Registry (NVS)

`moduleId → {mac, lastConfigVersionAcked}`. A HELLO from a new MAC for a known moduleId replaces
the entry (`MODULE_REPLACED`). Holding the BOOT button for 5 s clears the registry (a short press opens the setup hotspot).

## Important implementation notes

- ESP-NOW callbacks run in the WiFi task. They must copy the packet into a FreeRTOS queue and
  return. **No Firebase calls inside callbacks.**
- **Test commands through the website**, never with a CLI push: the website's write (command + /audit +
  /signal in one multi-path update) is what the hub must handle. (fw <= 0.3.3 dropped every web command
  for this reason while CLI tests passed.)
- The hub can never change channel (it's bound to the router). The modules follow the hub, also
  when the hub moves to another network from the setup page.
- The ESP32 has one radio, so WiFi traffic and ESP-NOW share airtime. Keep Firebase writes
  throttled as specified.
