# Module: Hotpress · Designing · Curing (esp3)

Parts: ON latching button, 3-way selector, 2 relays, OLED SH1106 (added).

| Input | Output |
|---|---|
| ON button = ON | Relay 1 (**Designing + Curing**) ON |
| ON button = OFF | Relay 1 OFF |
| Selector LEFT | Relay 2 (**Hot Press**) ON → `HOTPRESS HEATING` |
| Selector NEUTRAL | Relay 2 OFF → `HOTPRESS COOLING` |
| Selector RIGHT | `AUTO` (**placeholder**: Relay 2 ON, the same as LEFT, until AUTO logic is designed) |

The ON button and the selector are **independent** (the ON button is not a master switch for the
Hot Press).

## Safety

- Power-up interlock: at boot, the ON button must be seen OFF before Relay 1 can turn on, and the
  selector must be seen NEUTRAL before Relay 2 can turn on. OLED: `TURN ON-BUTTON OFF` /
  `SET SELECTOR TO MIDDLE`.
- Remote STOP → both relays OFF and `stopLatched = true`. It clears per relay when its input is
  cycled: ON button → OFF, selector → NEUTRAL.

## OLED layout

```
┌──────────────────────────────┐
│HOTPRESS STATION     ⇄hub     │
│DESIGN+CURE:  ON  ●           │
│HOTPRESS:  HEATING ▲▲         │  or COOLING ▽ / AUTO (placeholder)
│                              │
│Selector: LEFT   Btn: ON      │
└──────────────────────────────┘
```

## Future: AUTO

Reserved `autoModeBehaviour` in config (0 = placeholder). Candidate designs (temperature sensor,
timed press cycle, trigger from Containing "DONE" event through the hub) will be decided later.

## Implementation notes (fw 0.1.0)

- Outputs are recomputed from scratch every loop: an SSR is ON only if its input asks for it **and**
  neither its power-up interlock nor its STOP latch holds it. Anything else is OFF.
- **Per-output power-up interlock:** every boot starts locked. SSR 1 unlocks after the ON button
  has read OFF for 1 s. SSR 2 unlocks after the selector has read the middle position for 1 s. The
  OLED shows `TURN BTN OFF` / `SET TO MID` only if the operator actually needs to act.
- **Web STOP:** both SSRs OFF. Each output whose input is still ON is latched (`STOPPED` on the
  OLED) until that input goes back to OFF / middle.
- Events: `RUN_STARTED` / `RUN_FINISHED` with arg0 = unit (0 Designing+Curing, 1 Hot Press) and
  arg1 = seconds on. `ESTOP_PRESSED` (arg0 = 1, remote) if STOP switched something off.
- Faults: bit0 selector wiring (both contacts closed), bit1 OLED missing.
