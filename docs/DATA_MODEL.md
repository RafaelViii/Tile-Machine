# Firebase Realtime Database — Data Model

This is the contract between the **hub** and the **web**. It is mirrored in
`web/src/shared/types/rtdb.ts` and the hub's JSON mapping. Change all three together.

Timestamps are milliseconds since epoch, written with `{".sv": "timestamp"}` (server time). The
web compares them against server time (`.info/serverTimeOffset`), never the browser clock alone.

## Tree

```jsonc
{
  "roles": {                       // set manually in Firebase console
    "<uid>": "admin" | "hub"
  },

  "hub": {                         // written by hub
    "online": true,
    "lastSeen": 1758600000000,     // refreshed every 10 s
    "bootAt": 1758590000000,
    "fw": "1.0.0",
    "ip": "192.168.1.50",
    "wifiRssi": -58,
    "wifiChannel": 6,
    "protocolVersion": 1
  },

  "modules": {
    "shredder" | "containing" | "hotpress": {
      "info": {                    // hub, on pairing
        "mac": "24:6F:28:AA:BB:CC",
        "fw": "1.0.0",
        "pairedAt": 1758600000000
      },
      "presence": {                // hub
        "online": true,
        "lastSeen": 1758600000000  // refreshed every 10 s while online, and on transitions
      },
      "state": { /* module-specific, see below */ },   // hub, ≤ 2 writes/s
      "config": { "version": 7, /* module-specific */ },   // web (admin)
      "configApplied": {           // hub
        "version": 7,
        "result": "ok" | "queued" | "rejected",
        "at": 1758600000000
      }
    }
  },

  "commands": {
    "shredder" | "containing" | "hotpress" | "all": {
      "<pushId>": {
        "type": "STOP" | "IDENTIFY" | "TARE" | "CALIBRATE" | "REBOOT",
        "target": 0,               // optional unit/container index, omit = all units
        "arg": 1000,               // optional (CALIBRATE: known grams)
        "createdAt": 1758600000000,
        "by": "<uid>",
        "status": "pending" | "sent" | "done" | "failed" | "expired",
        "updatedAt": 1758600000000
      }
    }
  },

  "events": {
    "<pushId>": {
      "ts": 1758600000000,
      "module": "shredder",
      "code": "ESTOP_PRESSED",
      "args": [0, 0]
    }
  }
}
```

## Module `state`

```jsonc
// shredder
{ "mode": "OFF|MANUAL|AUTO", "state": "AUTO_RUNNING", "relayOn": true, "irDetected": true,
  "estopLatched": false, "countdownMs": 0, "interlock": false, "faults": 0,
  "uptimeS": 1234, "configVersion": 7 }

// containing
{ "selector": "NEUTRAL|LEFT|RIGHT", "hxOkMask": 15, "pcaOk": true, "interlock": false,
  "faults": 0, "uptimeS": 1234, "configVersion": 3,
  "containers": [   // index 0 Raw HDPE, 1 Raw PP, 2 Mixed HDPE, 3 Mixed PP
    { "weightG": 12450, "state": "DISPENSING", "mode": "LOADCELL", "selectedKg": 2,
      "progressPct": 40, "remainingMs": 0, "dispensedG": 800 }
  ] }

// hotpress
{ "onButton": true, "selector": "NEUTRAL|LEFT|RIGHT", "relayDesignCure": true,
  "relayHotpress": false, "stopLatched": false, "interlock": false,
  "faults": 0, "uptimeS": 1234, "configVersion": 1 }
```

## Module `config` (written by web)

```jsonc
// shredder
{ "version": 1, "autoStartDelayMs": 5000, "autoEmptyStopDelayMs": 1500,
  "manualConfirmTimeoutMs": 15000, "irDebounceMs": 200, "buzzerVolumePct": 100 }

// containing
{ "version": 1,
  "raw": [   // 0 Raw HDPE, 1 Raw PP
    { "mode": "LOADCELL|TIME", "timeTableMs": [10000, 20000, 30000, 40000, 50000],
      "maxDispenseMs": 120000, "jamTimeoutMs": 10000, "toleranceG": 50 }
  ],
  "mixed": [ // 0 Mixed HDPE, 1 Mixed PP
    { "mode": "MANUAL|TIME", "runTimeMs": 10000 }
  ],
  "servo": [ { "stopUs": 1500, "runUs": 1300 } ],   // 8 entries, PCA9685 ch 0..7
  "calFactor": [1.0, 1.0, 1.0, 1.0] }

// hotpress
{ "version": 1, "autoModeBehaviour": 0 }
```

## Web ↔ process mapping (dashboard tiles)

| Tile | Lit when |
|---|---|
| Shredder | `hubOnline && modules/shredder/presence/online` |
| Containing | `hubOnline && modules/containing/presence/online` |
| Hot Press | `hubOnline && modules/hotpress/presence/online` |
| Designing | same as Hot Press (same ESP32) |
| Curing | same as Hot Press (same ESP32) |

`hubOnline = hub.online && (serverNow − hub.lastSeen) < 25000`

## Retention

- `events`: the hub deletes entries older than 30 days on boot and once a day (indexed on `ts`).
- `commands`: the hub deletes entries in a final status (`done/failed/expired`) older than 24 h.
