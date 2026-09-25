# Firebase Realtime Database — Data Model

This is the contract between the **hub** and the **web**. It is mirrored in
`web/src/shared/types/rtdb.ts` and the hub's JSON mapping. Change all three together.

Timestamps are milliseconds since epoch, written with `{".sv": "timestamp"}` (server time). The
web compares them against server time (`.info/serverTimeOffset`), never the browser clock alone.
**Exception:** `events/*/ts` is the hub's own clock (DS3231 RTC / NTP) at the moment the event
happened, so events queued while offline keep their real time. It falls back to server time only
while the hub doesn't know the time.

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
    "protocolVersion": 1,
    "timeSource": "ntp" | "rtc" | "none",        // where the hub clock currently comes from
    "rtc": "ok" | "lost-power" | "missing"        // DS3231 state
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
  ],
  "calFactor": [1.0, 1.0, 1.0, 1.0] }   // owned by the module (CALIBRATE command), read-only on the web

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
  "servo": [ { "stopUs": 1500, "runUs": 1300 } ] }  // 8 entries, PCA9685 ch 0..7
// Load-cell calibration is NOT config (see state.calFactor), so a config save can't overwrite it.

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

## Reading `events` (web)

Server-side **cursor (keyset) pagination** on `ts` (`.indexOn: ["ts"]` in the rules, so the
server filters and returns only one page):

- Page 1: `orderByChild('ts').limitToLast(N+1)`, **live** (`onValue`, only changes are transferred).
- Older page: `orderByChild('ts').endBefore(oldest.ts, oldest.key).limitToLast(N+1)`, **fetched
  once** (`get`). Ties on `ts` are broken by key.
- The extra (+1) row only says whether an older page exists, so nothing is ever counted.
- RTDB has no offsets, so there's no "jump to page 7". Navigation is Newest / Newer / Older.
- Page size 25/50/100. Verified with 179 events: pages 50/50/50/29, each shown exactly once, newest first.
- Possible next step: a server-side filter per module needs a combined field (e.g. `mts` =
  `module|ts`) written by the hub and indexed, because RTDB orders by one field per query.
