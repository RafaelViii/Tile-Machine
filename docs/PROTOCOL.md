# ESP-NOW Protocol (v1)

Implemented in `firmware/lib/TileProtocol`. This document and that library **must stay in sync**.
Bump `PROTOCOL_VERSION` on any breaking change.

## 1. Transport

- ESP-NOW, max payload 250 bytes. All structs are `__attribute__((packed))`, little-endian (native
  ESP32).
- All devices are in `WIFI_STA`. The hub is connected to the router, and the modules are not
  connected (channel set with `esp_wifi_set_channel`).
- Broadcast address `FF:FF:FF:FF:FF:FF` is used **only** for `HELLO`. Everything else is unicast,
  so the MAC-level ACK gives send-status feedback.

## 2. Header (12 bytes, every message)

```c
struct __attribute__((packed)) MsgHeader {
  uint16_t magic;      // 0x4D54 ('T','M')
  uint8_t  version;    // PROTOCOL_VERSION = 1
  uint8_t  type;       // MsgType
  uint8_t  module;     // ModuleId of the SENDER (hub = 0)
  uint8_t  flags;      // bit0 = ACK_REQUESTED
  uint16_t seq;        // per-sender counter, wraps
  uint32_t networkId;  // per-installation id (config), foreign packets ignored
};
```

```c
enum class ModuleId : uint8_t { HUB = 0, SHREDDER = 1, CONTAINING = 2, HOTPRESS = 3 };
```

## 3. Message types

| Code | Name | Direction | ACK | Payload |
|---|---|---|---|---|
| 0x01 | HELLO | module → broadcast/hub | no | `fwVersion u16, configVersion u32` |
| 0x02 | WELCOME | hub → module | no | `channel u8, hubFw u16, desiredConfigVersion u32` |
| 0x10 | STATUS | module → hub | no | module-specific (§5), sent every 1000 ms **and** on change (min gap 100 ms) |
| 0x11 | EVENT | module → hub | yes | `code u16, arg0 i32, arg1 i32` |
| 0x20 | CONFIG | hub → module | yes | `configVersion u32` + module-specific (§6) |
| 0x21 | COMMAND | hub → module | yes | `cmd u8, target u8, arg i32, cmdRef u32` |
| 0x30 | ACK | either | — | `ackSeq u16, result u8` |

**ACK result**: `0 OK`, `1 REJECTED` (invalid values), `2 BUSY_QUEUED` (config accepted, applies
when idle), `3 UNSUPPORTED`.

**Reliability**: ACK-requested messages are retried up to 3 times, 200 ms apart. The receiver
de-duplicates by `(sender, seq)` using the last 8 seqs, and re-ACKs a duplicate without
re-executing it.

## 4. Timing constants

| Name | Value |
|---|---|
| `HEARTBEAT_MS` | 1000 |
| `HUB_OFFLINE_AFTER_MS` (hub marks module offline) | 3500 |
| `MODULE_LOST_AFTER_FAILS` (module re-scans) | 5 consecutive send failures |
| `SCAN_DWELL_MS` per channel | 150 |
| `ACK_TIMEOUT_MS` / retries | 200 / 3 |

## 5. STATUS payloads (v1)

Common prefix for every module:
```c
struct __attribute__((packed)) StatusCommon {
  uint32_t uptimeS;
  uint32_t configVersion;   // version currently APPLIED
  uint16_t faults;          // bitmask, module-specific
  uint8_t  interlock;       // 1 = waiting for switch to OFF/neutral after boot
};
```

**Shredder**
```c
struct __attribute__((packed)) StatusShredder {
  StatusCommon c;
  uint8_t  mode;         // 0 OFF, 1 MANUAL, 2 AUTO   (switch position)
  uint8_t  state;        // ShredderState (docs/modules/shredder.md)
  uint8_t  relayOn;
  uint8_t  irDetected;
  uint8_t  estopLatched;
  uint16_t countdownMs;  // AUTO warning / MANUAL confirm timeout remaining
};
```

**Containing**
```c
struct __attribute__((packed)) ContainerStatus {
  int32_t  weightG;      // current net weight, grams
  uint8_t  state;        // ContainerState (docs/modules/containing.md)
  uint8_t  mode;         // raw: 0 LOADCELL 1 TIME | mixed: 0 MANUAL 1 TIME
  uint8_t  selectedKg;   // raw only (1..5), 0 for mixed
  uint8_t  progressPct;  // 0..100
  uint32_t remainingMs;
  int32_t  dispensedG;   // this cycle
};
struct __attribute__((packed)) StatusContaining {
  StatusCommon c;
  uint8_t selector;          // 0 NEUTRAL, 1 LEFT (Mixed HDPE), 2 RIGHT (Mixed PP)
  uint8_t hxOkMask;          // bit n = HX711 n responding
  uint8_t pcaOk;
  ContainerStatus ct[4];     // 0 Raw HDPE, 1 Raw PP, 2 Mixed HDPE, 3 Mixed PP
};
```

**Hotpress**
```c
struct __attribute__((packed)) StatusHotpress {
  StatusCommon c;
  uint8_t onButton;          // latching button state
  uint8_t selector;          // 0 NEUTRAL (COOLING), 1 LEFT (HOTPRESS), 2 RIGHT (AUTO)
  uint8_t relayDesignCure;
  uint8_t relayHotpress;
  uint8_t stopLatched;       // remote STOP latched until switches cycled
};
```

## 6. CONFIG payloads (v1)

**Shredder**
```c
struct __attribute__((packed)) ConfigShredder {
  uint32_t configVersion;
  uint16_t autoStartDelayMs;      // default 5000  (range 2000..30000)
  uint16_t autoEmptyStopDelayMs;  // default 1500  (range 0..10000)
  uint16_t manualConfirmTimeoutMs;// default 15000 (range 3000..60000)
  uint16_t irDebounceMs;          // default 200   (range 20..2000)
  uint8_t  buzzerVolumePct;       // default 100   (range 0..100; 0 = mute except E-STOP)
};
```

**Containing**
```c
struct __attribute__((packed)) RawContainerCfg {     // containers 0,1
  uint8_t  mode;               // 0 LOADCELL, 1 TIME
  uint32_t timeTableMs[5];     // run time for 1..5 kg (TIME mode)
  uint32_t maxDispenseMs;      // LOADCELL safety timeout, default 120000
  uint32_t jamTimeoutMs;       // LOADCELL: no weight drop for this long → JAM, default 10000
  uint16_t toleranceG;         // LOADCELL stop tolerance, default 50
};
struct __attribute__((packed)) MixedContainerCfg {   // containers 2,3
  uint8_t  mode;               // 0 MANUAL, 1 TIME
  uint32_t runTimeMs;          // TIME mode duration, default 10000
};
struct __attribute__((packed)) ServoCfg {            // 8 servos, PCA9685 ch 0..7
  uint16_t stopUs;             // neutral pulse, default 1500 (trim per servo)
  uint16_t runUs;              // dispensing pulse, default 1300 (direction+speed)
};
struct __attribute__((packed)) ConfigContaining {
  uint32_t configVersion;
  RawContainerCfg   raw[2];
  MixedContainerCfg mixed[2];
  ServoCfg          servo[8];
  float             calFactor[4];   // HX711 counts per gram
};
```
(Size = 4 + 2×31 + 2×5 + 8×4 + 4×4 = 124 bytes + 12 header = 136, which fits in 250. The library
has a `static_assert` for every message, so a struct change that breaks the limit won't compile.)

**Hotpress**
```c
struct __attribute__((packed)) ConfigHotpress {
  uint32_t configVersion;
  uint8_t  autoModeBehaviour;  // 0 = placeholder (Hotpress ON). Reserved for future AUTO logic.
};
```

## 7. COMMAND codes

| cmd | Name | target | arg | Effect |
|---|---|---|---|---|
| 1 | STOP | 0xFF all units / unit index | — | Same as the physical STOP. |
| 2 | IDENTIFY | — | — | Beep/blink/flash the OLED for 3 s, to find which board is which. |
| 3 | TARE | container 0..3 | — | Zero that load cell (containing only). |
| 4 | CALIBRATE | container 0..3 | known grams | Compute and save `calFactor` (containing only). |
| 5 | REBOOT | — | — | `ESP.restart()` only when idle, otherwise `REJECTED`. |

## 8. EVENT codes

| code | Name | args |
|---|---|---|
| 1 | ESTOP_PRESSED | source (0 local, 1 remote) |
| 2 | RUN_STARTED | unit |
| 3 | RUN_FINISHED | unit, dispensedG or runMs |
| 4 | RUN_CANCELLED | unit |
| 5 | JAM_DETECTED | unit |
| 6 | TIMEOUT | unit |
| 7 | NOT_ENOUGH_MATERIAL | unit, availableG |
| 8 | SENSOR_FAULT | sensor index |
| 9 | CONFIG_APPLIED | version |

Hub-only events (written by the hub, never sent over ESP-NOW): `MODULE_ONLINE`, `MODULE_OFFLINE`,
`MODULE_REPLACED`, `HUB_BOOT`.
