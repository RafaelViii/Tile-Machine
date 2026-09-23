# Firmware

One PlatformIO project per ESP32. All share `lib/TileProtocol` (ESP-NOW wire format, see `docs/PROTOCOL.md`).

| Folder | Board | Spec |
|---|---|---|
| `hub/` | esp0 Main Hub | `docs/modules/hub.md` |
| `shredder/` | esp1 | `docs/modules/shredder.md` |
| `containing/` | esp2 | `docs/modules/containing.md` |
| `hotpress/` | esp3 (Hot Press, Designing, Curing) | `docs/modules/hotpress.md` |

Platform: `espressif32` 6.x (Arduino-ESP32 core 2.0.x). Pin maps: `docs/HARDWARE.md`.
