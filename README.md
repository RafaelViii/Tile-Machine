# Tile Machine

Modular plastic-recycling tile machine: **Shredder → Containing → Hot Press → Designing → Curing**.

Each process module runs on its own ESP32 and works on its own. A Main Hub ESP32 pairs with the
modules automatically over ESP-NOW and links them to a web dashboard through Firebase Realtime
Database.

| Folder | What |
|---|---|
| [`docs/`](docs/) | Architecture, protocol, data model, hardware, module specs, setup |
| [`firmware/`](firmware/) | PlatformIO projects: `hub`, `shredder`, `containing`, `hotpress` + shared `lib/TileProtocol` |
| [`web/`](web/) | React + Vite + TypeScript dashboard |
| [`firebase/`](firebase/) | Realtime Database security rules |
| [`legacy/`](legacy/) | Old standalone shredder sketch (reference only) |

Start with [`CLAUDE.md`](CLAUDE.md) (project guide) and [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).
Setup steps: [`docs/SETUP.md`](docs/SETUP.md).
