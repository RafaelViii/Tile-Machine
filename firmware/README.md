# Firmware

One PlatformIO project per ESP32. All of them share `lib/TileProtocol` (the ESP-NOW wire format,
see `docs/PROTOCOL.md`).

| Folder | Board | Status | Spec |
|---|---|---|---|
| `hub/` | esp0 Main Hub | ✅ Phase 1+2 | `docs/modules/hub.md` |
| `linktest/` | esp1/esp2/esp3 (temporary) | ✅ pairing test, drives no machinery | — |
| `shredder/` | esp1 | Phase 3 | `docs/modules/shredder.md` |
| `containing/` | esp2 | Phase 5 | `docs/modules/containing.md` |
| `hotpress/` | esp3 (Hot Press, Designing, Curing) | Phase 4 | `docs/modules/hotpress.md` |

Platform: `espressif32@6.13.0` (Arduino-ESP32 core 2.0.17). Pin maps: `docs/HARDWARE.md`.

## `lib/TileProtocol`

| File | What |
|---|---|
| `TileProtocol.h` | Header, message types, all payload structs (+ `static_assert` sizes), state/enum names |
| `TileConfig.h` | Config defaults + range validation (same numbers as the web) |
| `EspNowTransport.*` | ESP-NOW init, peers, rx queue (callback → `loop()`), delivery-failure streak, channel set |
| `Reliable.*` | ACK/retry outbox + duplicate cache |
| `ModuleLink.*` | Module side: auto-pairing (stored hub → channel scan), heartbeat STATUS, EVENTs, CONFIG/COMMAND + ACK |

## Flashing (VS Code + PlatformIO)

**Hub (esp0)**
1. `hub/include/secrets.h` must hold your WiFi and the hub account (template: `secrets.example.h`).
2. PlatformIO sidebar → **Project Tasks → hub → Upload**, or in a terminal:
   `cd firmware/hub && pio run -t upload && pio device monitor`
3. Serial should show: WiFi connected → `Firebase signed in as hub` → `Firebase online` →
   `listening for commands`. LED: slow blink = WiFi, fast blink = Firebase problem, solid = OK.

**Link test on a module board (esp1/esp2/esp3)**
`cd firmware/linktest && pio run -e shredder -t upload` (or `-e containing` / `-e hotpress`).
Serial: `scanning channels` → `paired with hub … on channel N`. LED solid = paired. The website
tile for that module lights up within a few seconds.

⚠️ The link test **replaces** whatever sketch is on that board (for example your old shredder
sketch). It holds relays OFF and servo outputs disabled, so the machine won't run until the real
module firmware (Phases 3–5) is flashed.

**Upload fails with `Wrong boot mode detected (0x13)`?** That board's USB auto-download circuit
only drives reset, not GPIO0. This is the case for the Shredder board (esp1, COM8 on the dev PC). Start
the upload with retries, then **hold BOOT for ~5 s** while it retries:
```
python ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32 --port COM8 --baud 460800 \
  --before default_reset --after hard_reset --connect-attempts 0 write_flash -z --flash_mode dio \
  --flash_freq 40m --flash_size detect 0x1000 .pio/build/<env>/bootloader.bin \
  0x8000 .pio/build/<env>/partitions.bin \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/<env>/firmware.bin
```
(Or simply hold BOOT while `pio run -t upload` prints `Connecting...`.)

**Reset pairing**: hold the hub's **BOOT** button for 5 s while it runs. It forgets all modules and
restarts, and the modules re-pair on their own.
