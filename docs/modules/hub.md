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
6. After NTP time sync, open the `/commands` stream (server-sent events), so every command's age
   can be checked and old commands are never replayed. Poll `modules/*/config/version` every 3 s
   and download a config only when its version changed. Streaming `/modules` would echo back
   the hub's own state writes.

## Clock: DS3231 RTC + NTP

- **Boot:** read the DS3231 (I2C 0x68) and set the system clock **before WiFi**. The hub knows the
  time with no internet, so the `/commands` stream (which needs time to reject old commands)
  and event timestamps work immediately.
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
  **separate** requests.
- **Don't use Arduino `HTTPClient`/`WiFiClientSecure` for the REST writes.** In core 2.0.17,
  `WiFiClientSecure::connected()`/`available()` call `mbedtls_ssl_read()` on a blocking socket.
  On an idle keep-alive connection they block for the full socket timeout (10 s), which stalled
  every write and froze the task. REST therefore uses ESP-IDF `esp_http_client`: one persistent
  connection to the DB, and the sign-in connection is closed right after use to free about 40 KB
  of heap. The `/commands` stream uses `WiFiClientSecure` in its **own task**, where blocking reads
  are harmless.
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
| Offline for 30 s (no WiFi, or WiFi but no cloud) | opens and stays open while offline; once back online the 3-min rule applies |

  "Use" = any request from the setup page (it polls every 2 s while open) or a phone joining.
- **Setup page:** status, saved networks (forget), scan, connect to a new network. A new network is
  tested for 20 s: if it connects it becomes first choice, if not the page shows why (wrong
  password, not found) and offers "Save anyway", and the hub goes back to its saved networks.
- **One radio:** the hotspot always sits on the router's channel (or the last known one while
  offline), so ESP-NOW modules keep working while it is open. While a phone is on the hotspot the
  hub only retries the last network on that channel, so the hotspot doesn't jump and drop the phone.
  Connecting to a network on another channel moves the hotspot: the phone rejoins, and the modules
  re-scan and re-pair by themselves.
- **LED:** slow blink = WiFi connecting, fast blink = cloud problem, solid = all good, double blink
  = all good and setup hotspot open.
- Reported in `/hub`: `wifiSsid` (network) and `portal` (hotspot open), shown on the Dashboard.

## Registry (NVS)

`moduleId → {mac, lastConfigVersionAcked}`. A HELLO from a new MAC for a known moduleId replaces
the entry (`MODULE_REPLACED`). Holding the BOOT button for 5 s clears the registry (a short press opens the setup hotspot).

## Important implementation notes

- ESP-NOW callbacks run in the WiFi task. They must copy the packet into a FreeRTOS queue and
  return. **No Firebase calls inside callbacks.**
- The hub can never change channel (it's bound to the router). The modules follow the hub, also
  when the hub moves to another network from the setup page.
- The ESP32 has one radio, so WiFi traffic and ESP-NOW share airtime. Keep Firebase writes
  throttled as specified.
