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
  "roles": {                       // superadmin + hub set by hand (console/CLI); operators by the superadmin (Users page)
    "<uid>": "superadmin" | "operator" | "hub"
  },

  "users": {                       // superadmin (Users page); each person can read their own
    "<uid>": { "email": "ana@tile-machine.local", "name": "Ana", "createdAt": 1758600000000, "createdBy": "<uid>" }
  },

  "presence": {                    // each person writes their own; only the superadmin reads
    "<uid>": {
      "connections": { "<pushId>": { "device": "Edge on Windows", "page": "Shredder", "since": 1758600000000 } },
      "lastSeen": 1758600000000    // every 60 s while online + on disconnect
    }
  },

  "audit": {                       // Activity log: append-only for staff, read/delete by the superadmin only
    "<pushId>": {
      "ts": 1758600000000, "uid": "<uid>", "email": "ana@tile-machine.local",
      "action": "SIGN_IN" | "SIGN_OUT" | "CONFIG_SAVE" | "COMMAND" | "PRESET_CREATE" | "PRESET_UPDATE"
              | "PRESET_RENAME" | "PRESET_DELETE" | "USER_ADD" | "USER_RENAME" | "USER_ACCESS" | "PASSWORD_CHANGE" | "USER_PASSWORD",
      "summary": "Saved Shredder settings (v25)",
      "module": "shredder",                                         // optional
      "changes": [ { "label": "3-way switch debounce", "from": "500 ms", "to": "400 ms" } ],  // CONFIG_SAVE
      "cmdId": "<pushId>", "result": "done" | "failed" | "expired"  // COMMAND
    }
  },

  "hub": {                         // written by hub
    "online": true,
    "lastSeen": 1758600000000,     // refreshed every 10 s
    "bootAt": 1758590000000,
    "fw": "1.0.0",
    "ip": "192.168.1.50",
    "wifiRssi": -58,
    "wifiChannel": 6,
    "wifiSsid": "speedxfiber_2.4Ghz",   // network the hub is on (fw 0.3.0+)
    "portal": false,                    // setup hotspot open (fw 0.3.0+), refreshed every 10 s
    "diag": { "heap": 115000, "minHeap": 60000, "block": 51000, "uptimeS": 3600,
              "loopMaxMs": 12, "logSuppressed": 0 },   // health every 10 s (fw 0.3.2+)
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
      "config": { "version": 7, "editedBy": "<uid>", "editedAt": 1758600000000, /* module-specific */ },   // web
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
        "by": "<uid>",             // rules: must be the sender
        "status": "pending" | "sent" | "done" | "failed" | "expired",
        "updatedAt": 1758600000000
      }
    }
  },

  "presets": {                     // web (admin only), whole-machine setups
    "<pushId>": {
      "name": "HDPE thick tiles",  // 1..40 chars, unique (case-insensitive) in the web UI
      "createdAt": 1758600000000,
      "updatedAt": 1758600000000,
      "by": "<uid>",
      "shredder":   { /* module config without "version" */ },
      "containing": { /* ... */ },
      "hotpress":   { /* ... */ }
    }
  },

  "signal": {                       // web, read by the hub every 500 ms (hub fw 0.4.0+), see "Signal"
    "commands": 42,                 // increment(1) in the same update as every command
    "config": { "shredder": 27 }    // = the config version, in the same update as every config save
  },

  "hubLog": {                       // hub's own messages (fw 0.3.2+): errors + important lines, 7 days
    "h<bootTag>_000001": { "ts": 1758600000000, "lvl": "E" | "I" | "C", "msg": "Firebase sign-in failed (HTTP -1)" }
  },                               // "C" = crash report written after a crash restart

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
  "manualConfirmTimeoutMs": 15000, "irDebounceMs": 200, "buzzerVolumePct": 100,
  "switchDebounceMs": 250, "buttonDebounceMs": 50 }

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
{ "version": 1, "autoModeBehaviour": 0, "buttonDebounceMs": 200, "selectorDebounceMs": 150 }
```

## People, presence and the Activity log (web only, the hub never reads these)

- Roles: `superadmin` (owner: everything + Users + Activity pages), `operator` (everything on the machine
  pages), `hub`. The rules let the superadmin set other accounts to `operator` or remove the role
  (= access off, locks open sessions at once); never their own role, never `hub`/`superadmin`.
- New accounts are created from the Users page on a second, in-memory Firebase app instance, so the
  superadmin is never signed out or switched to the new account.
- Passwords: only the superadmin changes them. Own password: account menu (and "Forgot password" to the
  Gmail). Operator passwords: Users page → Password, with the operator's CURRENT password (the helper
  instance signs in as the operator and calls updatePassword; without Cloud Functions/Blaze Firebase allows
  nothing else). Logged as USER_PASSWORD. Operators have no password or account UI at all. Limit: Firebase
  Auth itself lets any signed-in user change their own password through its API; only the UI prevents it.
- Identity is enforced by the rules on every write: `config.editedBy/editedAt` = saver / server time,
  `commands.by` = sender, `presets.by` (create) / `updatedBy` (edit) = writer, `audit.uid/email/ts` =
  signed-in user / server time. Nobody can write under someone else's name.
- Every change is written in ONE multi-path update together with its `audit` entry. Operators can't
  read, edit or delete the log. The owner of a COMMAND entry may add its final `result` once.
  Limit: the website writes the log, so a person scripting raw database calls could skip the entry
  (not the identity stamps). Server-enforced logging would need Cloud Functions (Blaze plan).
- Retention: the Activity page deletes entries older than 90 days when the superadmin opens it.
- The whole signed-in app is rebuilt per account (keyed by uid): no unsaved edits, picked preset or
  page state carries over when a different person signs in on the same browser.

## Presets (web only)

A preset is a named copy of all three module configs, managed on the web **Dashboard** ("Machine
presets"). The hub never reads `/presets`. Picking a preset only loads it into the browser's
unsaved drafts and lists every value that would change (old → new); the admin then presses **Save to
machine**, which writes each changed `modules/{id}/config` with a new `version` as usual. So a preset can never bypass the normal
versioned config path or the module's apply-when-idle rule. A missing module or field in a preset
falls back to the defaults (`normalizeConfig`), so presets saved before a new field existed still load.

## Web ↔ process mapping (dashboard tiles)

| Tile | Lit when |
|---|---|
| Shredder | `hubOnline && modules/shredder/presence/online` |
| Containing | `hubOnline && modules/containing/presence/online` |
| Hot Press | `hubOnline && modules/hotpress/presence/online` |
| Designing | same as Hot Press (same ESP32) |
| Curing | same as Hot Press (same ESP32) |

`hubOnline = hub.online && (serverNow − hub.lastSeen) < 25000`

## Signal (hub fw 0.4.0+)

The hub has no live stream (one TLS connection only, see docs/modules/hub.md "One secure connection and
/signal"). It reads `/signal` every 500 ms and downloads only what changed:

- **Every command** write adds `'signal/commands': increment(1)` to the same multi-path update
  (`useCommand.ts`).
- **Every config save** adds `signal/config/<module> = version` to the same update (`configDrafts.tsx`).
- Rules: staff write, numbers only; hub and staff read. Anything else under `/signal` is refused.
- Something written without the signal (CLI, old page still open) is still picked up by the fallbacks:
  `/commands` every 15 s, config versions every 30 s. Commands older than 30 s are expired, never run.

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
- Page size 25 (default)/50/100. Verified with 179 events: pages 50/50/50/29, each shown exactly once, newest first.
- Possible next step: a server-side filter per module needs a combined field (e.g. `mts` =
  `module|ts`) written by the hub and indexed, because RTDB orders by one field per query.
