# Hardware — Pin Maps & Wiring

Target board: **ESP32-WROOM-32 DevKit** (38-pin or 30-pin). Each board's `include/pins.h` must
match this file.

**Pins avoided on every board**: GPIO 6–11 (flash), strapping pins 0/2/5/12/15 for inputs or
relays (GPIO2 is only used for the on-board status LED), and GPIO 1/3 (USB serial).
**Input-only pins** 34/35/36/39 have **no internal pull-ups**. They're used only for signals that
are actively driven (HX711 DOUT).

I2C bus on every board with an OLED: **SDA = 21, SCL = 22**.

---

## esp0 — HUB

| Function | GPIO | Notes |
|---|---|---|
| Status LED | 2 | On-board LED. Slow blink = WiFi connecting. Fast blink = Firebase error. Solid = OK. Short flicker = ESP-NOW packet. |
| BOOT button | 0 | Hold 5 s **at runtime** to clear pairings and restart. Not read during boot. |
| DS3231 RTC SDA | 21 | I2C, 100 kHz. VCC 3.3 V, GND. RTC 0x68 + AT24C32 EEPROM 0x57 (both detected on the installed board). |
| DS3231 RTC SCL | 22 | I2C. ⚠️ Power the RTC board from **3.3 V only**. The ZS-042 charging circuit (diode + 200 Ω) then gives ~2.6 V, below the installed CR2032, so no current flows into it. **Never 5 V:** that pushes ~4.3 V into the non-rechargeable cell. |

No OLED. Power by USB or 5 V. Place it where both the router and all modules are within range.

---

## esp1 — SHREDDER (pins kept from the legacy sketch)

| Function | GPIO | Mode | Notes |
|---|---|---|---|
| START button | 32 | INPUT_PULLUP | Momentary NO to GND |
| STOP button | 33 | INPUT_PULLUP | Momentary NO to GND. See E-STOP note below. |
| Switch → AUTO | 25 | INPUT_PULLUP | LOW = AUTO |
| Switch → MANUAL | 26 | INPUT_PULLUP | LOW = MANUAL. **Neither LOW = OFF (center).** Both LOW = wiring fault → treated as OFF. |
| IR sensor | 14 | INPUT_PULLUP | `IR_ACTIVE_LOW = true` (LOW = object) |
| Relay (shredder motor) | 27 | OUTPUT | `RELAY_ACTIVE_LOW` flag (default false) |
| Buzzer (**passive module**) | 13 | LEDC PWM | Tones through `ledcWriteTone`. `BUZZER_IDLE_LEVEL` flag for modules that idle HIGH. |
| OLED SH1106 | 21/22 | I2C | addr 0x3C |

**E-STOP note (important):** the firmware STOP is a *software* stop through the relay. For a
shredder, add a real **hardware E-stop**: a latching mushroom switch (NC) that cuts the motor
contactor coil or power directly. Optionally wire its second contact to GPIO33 so the firmware
also knows. If you rewire the STOP button as **NC**, set `STOP_IS_NC = true`. A broken wire then
means STOP (fail-safe).

---

## esp2 — CONTAINING

| Function | GPIO | Mode | Notes |
|---|---|---|---|
| HX711 #1 DT (C1 Raw HDPE) | 34 | INPUT | input-only pin, OK |
| HX711 #1 SCK | 16 | OUTPUT | |
| HX711 #2 DT (C2 Raw PP) | 35 | INPUT | |
| HX711 #2 SCK | 17 | OUTPUT | |
| HX711 #3 DT (C3 Mixed HDPE) | 36 (VP) | INPUT | |
| HX711 #3 SCK | 18 | OUTPUT | |
| HX711 #4 DT (C4 Mixed PP) | 39 (VN) | INPUT | |
| HX711 #4 SCK | 19 | OUTPUT | |
| C1 SELECT button | 32 | INPUT_PULLUP | Raw HDPE kg select |
| C1 CONFIRM button | 33 | INPUT_PULLUP | Raw HDPE confirm / cancel |
| C2 SELECT button | 25 | INPUT_PULLUP | Raw PP kg select |
| C2 CONFIRM button | 26 | INPUT_PULLUP | Raw PP confirm / cancel |
| Selector LEFT (Mixed HDPE) | 27 | INPUT_PULLUP | LOW = LEFT |
| Selector RIGHT (Mixed PP) | 14 | INPUT_PULLUP | LOW = RIGHT. Neither = NEUTRAL (stop). |
| PCA9685 OE | 23 | OUTPUT | Active LOW. **10 kΩ pull-up to 3.3 V**, so servo outputs stay disabled during boot. |
| OLED SH1106 | 21/22 | I2C | addr 0x3C |
| PCA9685 | 21/22 | I2C | addr 0x40 |

GPIO 16/17 are free on **WROOM** modules. On a **WROVER** (PSRAM) board use 4 and 13 instead.

**PCA9685 channels (MG995 360° continuous rotation, 50 Hz)**

| Container | Servo A | Servo B |
|---|---|---|
| C1 Raw HDPE | ch 0 | ch 1 |
| C2 Raw PP | ch 2 | ch 3 |
| C3 Mixed HDPE | ch 4 | ch 5 |
| C4 Mixed PP | ch 6 | ch 7 |

Continuous servos stop at about 1500 µs (trim per servo, set in the web config). Below or above
that they turn one way or the other. If the two servos of a container are mounted mirrored, give
them opposite `runUs` values (e.g. 1300 and 1700).

**Power (critical):**
- The MG995 draws about 1.2 A running and up to 2.5 A stalled. Use a **separate 5–6 V supply,
  ≥ 10 A**, into the PCA9685 **V+ screw terminal**. Never power servos from the ESP32.
- Put a **1000 µF+ capacitor** across V+/GND on the PCA9685.
- **Common GND** between the servo PSU, PCA9685, ESP32 and HX711s.
- PCA9685 VCC (logic) = 3.3 V from the ESP32.

**Load cells:** each container has 4 bathroom half-bridge load cells wired as one full bridge into
one HX711 (E+, E−, A+, A−). Power the HX711 from **3.3 V**, so its DOUT is ESP32-safe. Channel A,
gain 128.

---

## esp3 — HOTPRESS (Hot Press · Designing · Curing)

| Function | GPIO | Mode | Notes |
|---|---|---|---|
| ON latching button | 32 | INPUT_PULLUP | LOW = ON. Controls the Designing + Curing relay **only**. |
| Selector LEFT (HOTPRESS) | 25 | INPUT_PULLUP | LOW = LEFT → Hotpress ON |
| Selector RIGHT (AUTO) | 26 | INPUT_PULLUP | LOW = RIGHT → AUTO (placeholder: Hotpress ON). Neither = NEUTRAL → COOLING (Hotpress OFF). |
| SSR 1 — Designing + Curing | 18 | OUTPUT | SSR-25-DA input "+" (terminal 3). HIGH = ON (`RELAY_ACTIVE_LOW = false`) |
| SSR 2 — Hot Press | 19 | OUTPUT | SSR-25-DA input "+" (terminal 3). HIGH = ON |
| OLED SH1106 | 21/22 | I2C | addr 0x3C |

**Outputs are 2× SSR-25-DA** (DC control 3–32 V, AC load). Input "−" (terminal 4) goes to ESP32 GND, and no
extra supply is needed. 3.3 V is at the low end of the input range: if an SSR switches unreliably, drive
it from 5 V through an NPN transistor. Mount each SSR on a heatsink and keep the load well below 25 A
(clone ratings are optimistic). SSRs usually fail **shorted (stuck ON)**, so the heater circuit must
have a **thermal fuse / thermostat cut-off** independent of the ESP32.

Heater loads must go through **properly rated contactors/SSRs**. The ESP32 only
switches the contactor coil.

---

## Common input handling

- All buttons and switches are debounced in software (30 ms).
- Switch positions are read as states, not edges. The power-up interlock applies (CLAUDE.md
  safety invariant 3).
