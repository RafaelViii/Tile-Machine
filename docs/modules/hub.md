# Module: Main Hub (esp0)

The hub runs **no process logic** and drives **no actuators**. It is a bridge and supervisor.

## Boot sequence

1. Status LED slow blink. Load `secrets.h` (WiFi SSID/pass, Firebase API key, DB URL, hub
   email/password).
2. WiFi STA connect. Read the channel (`WiFi.channel()`).
3. `esp_now_init()`, register callbacks, add the broadcast peer, and add stored module peers from
   NVS.
4. NTP time sync (for log timestamps). Firebase sign-in as the `hub` user.
5. Write `/hub` (online, bootAt, fw, ip, rssi, channel). Log `HUB_BOOT`. Set every
   `modules/*/presence/online=false` (fresh start, modules re-announce within a second).
6. Open streams on `/modules/*/config` and `/commands`.

## Main loop tasks (all non-blocking)

| Task | Period |
|---|---|
| Handle incoming ESP-NOW (queued from the callback, handled in `loop`, never in the callback) | continuous |
| Presence timeout check | 250 ms |
| `/hub/lastSeen` + online modules' `presence/lastSeen` | 10 s |
| Flush throttled `state` writes (latest STATUS per module) | 500 ms |
| ACK/retry engine for CONFIG/COMMAND | 50 ms |
| WiFi / Firebase reconnect with backoff | on failure |
| Retention cleanup (events 30 d, commands 24 h) | boot + 24 h |

## Registry (NVS)

`moduleId → {mac, lastConfigVersionAcked}`. A HELLO from a new MAC for a known moduleId replaces
the entry (`MODULE_REPLACED`). Holding the BOOT button for 5 s clears the registry.

## Important implementation notes

- ESP-NOW callbacks run in the WiFi task. They must copy the packet into a FreeRTOS queue and
  return. **No Firebase calls inside callbacks.**
- The hub can never change channel (it's bound to the router). The modules follow the hub.
- The ESP32 has one radio, so WiFi traffic and ESP-NOW share airtime. Keep Firebase writes
  throttled as specified.
