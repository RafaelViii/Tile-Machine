# Save point: 2026-10-24 (continue on another computer)

Work paused on the dev PC, to be continued on the laptop. **Claude: read this file and CLAUDE.md first,
then continue with "Next task" below.** Delete this file (and its pointer in CLAUDE.md) once the next task is
done, or update it at the next pause.

---

## 1. Set up the laptop (once)

1. **Get the code:** `git clone https://github.com/RafaelViii/Tile-Machine.git` (or `git pull` if it's
   already there). Everything is pushed; the last commit before this save point is `9b31004`.
2. **Copy the two secret files by hand** (USB stick, not git, not chat): they are git-ignored on purpose.
   - `firmware/hub/include/secrets.h` (WiFi, hub Firebase login, hotspot password)
   - `web/.env.local` (website Firebase keys)
   Only needed to rebuild the hub or the website; the module boards (shredder, hotpress, containing) don't
   use them.
3. **Install:** VS Code + the PlatformIO extension (it installs the ESP32 toolchain and Python),
   Node.js LTS, and the Firebase CLI (`npm i -g firebase-tools`, then `firebase login` with the Google account
   that owns project `tile-machine-92345`). Then `cd web && npm install`.
4. **USB driver:** the boards use Silicon Labs **CP210x** USB-serial chips; Windows usually installs the
   driver by itself.
5. Claude's personal memory on the PC doesn't move to the laptop; everything that matters is in `CLAUDE.md`,
   the `docs/` folder, [CONNECTIVITY_PLAYBOOK.md](CONNECTIVITY_PLAYBOOK.md) and this file.

## 2. The boards right now

COM port numbers are different on every computer: **always identify a board by its MAC** before flashing
(`esptool.py --port COMx read_mac`, or the MAC shown on the website's Devices list).

| Board | MAC | Firmware on the board | Firmware in the repo | Flashing |
|---|---|---|---|---|
| esp0 Hub | F0:24:F9:0C:D2:EC (hotspot `TileHub-D2EC`) | 0.4.0 | 0.4.0 | auto (`pio run -t upload`) |
| esp1 Shredder | 00:70:07:26:95:4C | **0.2.8** | **0.2.9** (12-hour OLED clock, not flashed yet) | **manual:** see below |
| esp2 Containing | B0:CB:D8:C8:09:D8 | link test 0.1.1 (pairing + OLED status only) | no real firmware yet | auto (`pio run -t upload`) |
| esp3 Hotpress | 70:4B:CA:26:AD:F0 | 0.1.5 | 0.1.5 | auto |

All four were online and paired on 2026-10-24 (router WiFi channel is now **11**, it was 1 before; nothing to
do, the modules follow by themselves).

**Shredder flashing (its auto-reset doesn't work):** in `firmware/shredder` run `pio run`, then (bash, with
`COMx` = the shredder's port):

```bash
B=.pio/build/shredder
~/.platformio/penv/Scripts/python.exe ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32 \
  --port COMx --baud 115200 --connect-attempts 0 --before no_reset --after hard_reset --no-stub \
  write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000 $B/bootloader.bin 0x8000 $B/partitions.bin \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 $B/firmware.bin
```

While it waits, press **BOOT + EN once** (hold BOOT, tap EN, release BOOT) and don't touch the board for
~2 min. Pressing it again mid-upload breaks the flash. Stop any old flasher still holding the port first.

**Containing board:** it is mounted and soldered on the machine. **Don't remove it:** to flash, just plug a
USB cable into the board's USB port while it stays in place (~1 min).

---

## 3. Next task: the real Containing firmware (Phase 5)

The user asked to "finalize" the Containing board and to make sure the OLED has **no overlapping text**.
Nothing of it is written yet (only the link test runs). Spec: [docs/modules/containing.md](docs/modules/containing.md),
pins: [docs/HARDWARE.md](docs/HARDWARE.md) esp2, message formats: `StatusContaining` / `ConfigContaining` in
[TileProtocol.h](firmware/lib/TileProtocol/src/TileProtocol.h), limits: [TileConfig.h](firmware/lib/TileProtocol/src/TileConfig.h).
The hub and the website already support the Containing module completely.

**Hardware actually connected now (user, 2026-10-24):** HX711 for C1 on **DT 34 / SCK 16** and C2 on
**DT 35 / SCK 17**. The OLED is on 21/22 (SH1106, 3.3 V); it showed nothing only because the old link test
had no screen code. Not confirmed yet: C3/C4 HX711s (36/18, 39/19), PCA9685 + servos, buttons, selector.
The firmware must work with any of these missing (report it, never crash or hang).

### Design decisions already made (implement these)

Build it like `firmware/hotpress` (same file layout: `include/pins.h`, `include/config.h`, `src/inputs.h`,
`src/display.*`, `src/main.cpp`, NVS config, interlock guard, noise monitor, OLED self-test), plus two small
drivers written in-house (no extra libraries):

- **HX711** (`src/hx711.*`): read when DT is LOW; 24 bits + 1 pulse (channel A, gain 128) bit-banged inside a
  `portENTER_CRITICAL` section (SCK must never stay high > 60 µs). Sign-extend. **Presence check:** after the
  25th pulse DT must read HIGH, else it's a floating pin (no HX711); reject 0x7FFFFF / 0x800000 (saturated,
  load cell unplugged). `hxOk[i]` = a valid reading within the last 1 s. Moving average of 5 readings
  (screws vibrate). Pins 34–39 are input-only with no pull-ups, so a missing HX711 floats: the checks above
  must catch it.
- **Calibration** owned by the module, saved in NVS (`cal[4]` counts-per-gram, `off[4]` tare offsets).
  `TARE c` (container idle only) sets the offset; `CALIBRATE c grams` sets the factor. Factor 0 =
  "not calibrated": weight shown as `NO CAL`, LOADCELL dispensing refused with FAULT (TIME mode still works).
- **PCA9685** (`src/pca9685.*`) at 0x40: prescale 121 (~50 Hz), MODE2 totem-pole, all channels FULL OFF at
  start. OE (GPIO 23) **HIGH from the first line of setup()** until the PCA is initialised, then LOW.
  Run = `runUs` pulse on both servos of the container (servos 2i and 2i+1); after a run, send `stopUs` for
  300 ms (uses the web trim) and then FULL OFF (no pulse = can't creep). `pcaOk` = it ACKs on I2C; retry
  every 2 s (hot-plug). No PCA → any start attempt goes to FAULT.
- **Raw C1/C2:** exactly the state machine in the spec (SELECT 1→5 kg, CONFIRM start/cancel, NOT_ENOUGH,
  LOADCELL stop at `selected − tolerance`, TIME table, `maxDispenseMs` → FAULT/TIMEOUT, jam = lowest weight
  hasn't dropped 20 g within `jamTimeoutMs` → FAULT/JAM, HX711 lost while dispensing → FAULT/SENSOR,
  DONE/CANCELLED back to IDLE after 3 s with kg reset to 1, FAULT cleared by any button of that container).
- **Mixed C3/C4** on the selector (LEFT = C3, RIGHT = C4): MANUAL runs while on that side; TIME runs
  `runTimeMs` then DONE (hold) until the selector visits NEUTRAL. L↔R without NEUTRAL counts as passing
  NEUTRAL. Both contacts closed = wiring fault → NEUTRAL + fault bit. **Power-up interlock:** selector must
  read NEUTRAL for 1 s before C3/C4 can run.
- **Remote STOP:** all servos off, OE HIGH 200 ms (non-blocking), running containers → CANCELLED, mixed
  latched until the selector returns to NEUTRAL; `ESTOP_PRESSED` arg0 = 1 if anything was running.
  REBOOT only when all containers are idle. IDENTIFY = OLED inverts/blinks 3 s.
- **Config** applies **per container only when that container is idle**: apply the idle parts at once, keep
  the rest pending (answer `BUSY_QUEUED`), finish in loop(), then set `configVersion`, save NVS, send
  `CONFIG_APPLIED`.
- **Events:** `RUN_STARTED`/`RUN_FINISHED` (arg1 = dispensed g)/`RUN_CANCELLED`/`JAM_DETECTED`/`TIMEOUT`/
  `NOT_ENOUGH_MATERIAL`/`SENSOR_FAULT`, arg0 = container 0..3.
- **Fault bits** (`StatusCommon.faults`): 0x01 selector wiring, 0x02 OLED missing, then noise on
  SELECT1 0x04, CONFIRM1 0x08, SELECT2 0x10, CONFIRM2 0x20, selector LEFT 0x40, selector RIGHT 0x80.
  **Add these names to `FAULT_BITS.containing` in `web/src/shared/format.ts`** (currently empty) and deploy
  hosting.

### OLED layout (no overlap by construction)

Only the fixed-width `u8g2_font_6x10_tr` (6 px per character, so 21 characters = 126 px) for the rows, so
every width is known exactly; plus the boot self-test that renders every state and logs any text past 128 px
(same as hotpress). Read the self-test result on serial right after flashing.

```
y 9   CONTAINING      ⇄  8:57PM     top bar (title → "SET SEL MID" while interlocked, "NO SERVO DRV" if no PCA)
      ─────────────────────────     line at y 11
y 23  1 R.HDPE 12.4kg 2kg L         row = "%c %-6s %6.6s %-5.5s" = exactly 21 chars
y 35  2 R.PP    8.1kg >40%          + 1 px progress bar at baseline+3 while dispensing
y 47  3 M.HDPE  5.0kg >  7s
y 59  4 M.PP   NO CAL HOLD
```

Weight field (6): `12.4kg`, `NO CAL`, `  ERR ` (no HX711). Status field (5): raw idle `2kg L`/`2kg T`,
dispensing `>40%` (arrow blinks), `DONE`, `CANCL`, `LOW!`, `JAM`, `TIME!`, `ERR`, `NOSRV`; mixed idle
`MAN`/`TIME`, running `RUN` or `>  7s`, `HOLD`, `STOP`, `LOCK`. Rows 12 px apart, so descenders and the
progress bar never touch the next row.

### When done

Docs to update in the same commit: `docs/modules/containing.md` (implementation notes), CLAUDE.md §8 Phase 5,
`firmware/README.md` table, `firmware/containing/README.md`, and **CODE_GUIDE.md** (the Containing section
says the firmware doesn't exist yet; add its files and line links). Flash via USB (board stays on the machine),
read the boot log (I2C devices found, HX711 status, OLED self-test), then test from the website: live weight,
TARE/CALIBRATE, STOP, IDENTIFY, a config save.

---

## 4. Other open items (unchanged)

- Flash shredder **0.2.9** (12-hour OLED clock + 23 s display adjust; hotpress already has it).
- Hotpress hand test is still pending.
- Shredder electrical noise: fit the 10 kΩ pull-ups (3.3 V → GPIO 25, 26, 32, 33), 100 nF to GND, and a
  motor snubber; then check the Devices list for "noisy" warnings while the motor runs.
- XL4016 servo supply is rated ~8 A: avoid running all four containers at once until it's cooled/upgraded.
- Hub: 2–3 day soak test; ESP-NOW encryption and OTA still to do; unsent events in the RTC's EEPROM.

## 5. First message to send on the laptop

> Read SAVEPOINT.md and CLAUDE.md, then continue with the next task: the real Containing firmware.
