# Containing (esp2)

Spec and screen layout: [docs/modules/containing.md](../../docs/modules/containing.md). Pins:
[docs/HARDWARE.md](../../docs/HARDWARE.md) esp2.

| File | What |
|---|---|
| `src/main.cpp` | Raw (C1, C2) and mixed (C3, C4) state machines, STOP, config per container, TARE/CALIBRATE, status |
| `src/hx711.*` | Load-cell reader with "is a real HX711 there?" checks |
| `src/pca9685.*` | Servo driver (50 Hz), all-off and hot-plug |
| `src/display.*` | OLED rows (fixed-width, 21 characters) + boot layout self-test |
| `src/inputs.h` | SELECT/CONFIRM buttons and the 3-way selector (integrating filter) |
| `include/pins.h` · `include/config.h` | Pin map · version, timings, fault bits |

Flash: `pio run -t upload` (auto-reset works on this board). The board is mounted on the machine: plug a USB
cable into it in place, no need to remove it. Check the boot log (`pio device monitor`): load cells found,
servo driver found, `OLED layout self-test: all screens fit`.

Before the first LOADCELL dispense: empty container → **TARE** on the website, then a known weight →
**CALIBRATE** with its grams.
