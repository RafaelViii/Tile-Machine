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
