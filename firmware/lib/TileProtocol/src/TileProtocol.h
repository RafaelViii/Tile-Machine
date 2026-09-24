// Tile Machine ESP-NOW wire format (v1).
// Single source of truth: docs/PROTOCOL.md. Change both together and bump PROTOCOL_VERSION on any
// breaking change.
#pragma once

#include <Arduino.h>

namespace tile {

// ---------- Identity ----------
constexpr uint16_t MAGIC = 0x4D54;           // 'T','M'
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr uint32_t NETWORK_ID = 0x54494C45;  // "TILE" — change to isolate a second installation
constexpr size_t MAX_FRAME = 250;            // ESP-NOW payload limit

// ---------- Timing (docs/PROTOCOL.md §4) ----------
constexpr uint32_t HEARTBEAT_MS = 1000;
constexpr uint32_t STATUS_MIN_GAP_MS = 100;
constexpr uint32_t HUB_OFFLINE_AFTER_MS = 3500;
constexpr uint8_t MODULE_LOST_AFTER_FAILS = 5;
constexpr uint32_t SCAN_DWELL_MS = 150;
constexpr uint32_t PROBE_TIMEOUT_MS = 600;
constexpr uint32_t ACK_TIMEOUT_MS = 200;
constexpr uint8_t ACK_RETRIES = 3;
constexpr uint8_t MIN_CHANNEL = 1;
constexpr uint8_t MAX_CHANNEL = 13;

// ---------- Enums ----------
enum class ModuleId : uint8_t { HUB = 0, SHREDDER = 1, CONTAINING = 2, HOTPRESS = 3 };
constexpr uint8_t MODULE_ID_COUNT = 4;

inline bool isModuleId(uint8_t v) { return v >= 1 && v <= 3; }

/** RTDB key for a module ("shredder", ...). */
inline const char* moduleKey(ModuleId id) {
  switch (id) {
    case ModuleId::HUB: return "hub";
    case ModuleId::SHREDDER: return "shredder";
    case ModuleId::CONTAINING: return "containing";
    case ModuleId::HOTPRESS: return "hotpress";
  }
  return "unknown";
}

enum class MsgType : uint8_t {
  HELLO = 0x01,
  WELCOME = 0x02,
  TIME = 0x03,  // hub -> module, no ACK (added in v1; older modules ignore unknown types)
  STATUS = 0x10,
  EVENT = 0x11,
  CONFIG = 0x20,
  COMMAND = 0x21,
  ACK = 0x30,
};

constexpr uint8_t FLAG_ACK_REQUESTED = 0x01;

enum class AckResult : uint8_t { OK = 0, REJECTED = 1, BUSY_QUEUED = 2, UNSUPPORTED = 3 };

enum class Cmd : uint8_t { STOP = 1, IDENTIFY = 2, TARE = 3, CALIBRATE = 4, REBOOT = 5 };
constexpr uint8_t TARGET_ALL = 0xFF;

inline const char* cmdName(uint8_t c) {
  switch (c) {
    case 1: return "STOP";
    case 2: return "IDENTIFY";
    case 3: return "TARE";
    case 4: return "CALIBRATE";
    case 5: return "REBOOT";
  }
  return "UNKNOWN";
}

enum class EventCode : uint16_t {
  ESTOP_PRESSED = 1,
  RUN_STARTED = 2,
  RUN_FINISHED = 3,
  RUN_CANCELLED = 4,
  JAM_DETECTED = 5,
  TIMEOUT = 6,
  NOT_ENOUGH_MATERIAL = 7,
  SENSOR_FAULT = 8,
  CONFIG_APPLIED = 9,
};

inline const char* eventName(uint16_t c) {
  switch (c) {
    case 1: return "ESTOP_PRESSED";
    case 2: return "RUN_STARTED";
    case 3: return "RUN_FINISHED";
    case 4: return "RUN_CANCELLED";
    case 5: return "JAM_DETECTED";
    case 6: return "TIMEOUT";
    case 7: return "NOT_ENOUGH_MATERIAL";
    case 8: return "SENSOR_FAULT";
    case 9: return "CONFIG_APPLIED";
  }
  return "UNKNOWN_EVENT";
}

// ---------- Module states (docs/modules/*.md) ----------
enum class ShredderMode : uint8_t { OFF = 0, MANUAL = 1, AUTO = 2 };

enum class ShredderState : uint8_t {
  INTERLOCK = 0,
  OFF = 1,
  MANUAL_IDLE = 2,
  MANUAL_CONFIRM = 3,
  MANUAL_RUNNING = 4,
  AUTO_WAITING = 5,
  AUTO_WARNING = 6,
  AUTO_RUNNING = 7,
  AUTO_ESTOP = 8,
};

inline const char* shredderModeName(uint8_t m) {
  switch (m) {
    case 0: return "OFF";
    case 1: return "MANUAL";
    case 2: return "AUTO";
  }
  return "OFF";
}

inline const char* shredderStateName(uint8_t s) {
  switch (s) {
    case 0: return "INTERLOCK";
    case 1: return "OFF";
    case 2: return "MANUAL_IDLE";
    case 3: return "MANUAL_CONFIRM";
    case 4: return "MANUAL_RUNNING";
    case 5: return "AUTO_WAITING";
    case 6: return "AUTO_WARNING";
    case 7: return "AUTO_RUNNING";
    case 8: return "AUTO_ESTOP";
  }
  return "UNKNOWN";
}

enum class ContainerState : uint8_t {
  IDLE = 0,
  DISPENSING = 1,
  DONE = 2,
  CANCELLED = 3,
  NOT_ENOUGH = 4,
  FAULT = 5,
};

inline const char* containerStateName(uint8_t s) {
  switch (s) {
    case 0: return "IDLE";
    case 1: return "DISPENSING";
    case 2: return "DONE";
    case 3: return "CANCELLED";
    case 4: return "NOT_ENOUGH";
    case 5: return "FAULT";
  }
  return "UNKNOWN";
}

enum class Selector : uint8_t { NEUTRAL = 0, LEFT = 1, RIGHT = 2 };

inline const char* selectorName(uint8_t s) {
  switch (s) {
    case 1: return "LEFT";
    case 2: return "RIGHT";
  }
  return "NEUTRAL";
}

// Raw containers: 0 LOADCELL, 1 TIME. Mixed containers: 0 MANUAL, 1 TIME.
inline const char* rawModeName(uint8_t m) { return m == 1 ? "TIME" : "LOADCELL"; }
inline const char* mixedModeName(uint8_t m) { return m == 1 ? "TIME" : "MANUAL"; }

// ---------- Firmware version: M.m.p packed into 16 bits (4.6.6 bits) ----------
constexpr uint16_t fwEncode(uint8_t major, uint8_t minor, uint8_t patch) {
  return (uint16_t)(((major & 0x0F) << 12) | ((minor & 0x3F) << 6) | (patch & 0x3F));
}
inline void fwDecode(uint16_t v, char* out, size_t n) {
  snprintf(out, n, "%u.%u.%u", (unsigned)(v >> 12), (unsigned)((v >> 6) & 0x3F), (unsigned)(v & 0x3F));
}

// ---------- Wire structs ----------
#pragma pack(push, 1)

struct MsgHeader {
  uint16_t magic;
  uint8_t version;
  uint8_t type;       // MsgType
  uint8_t module;     // ModuleId of the SENDER
  uint8_t flags;      // FLAG_ACK_REQUESTED
  uint16_t seq;
  uint32_t networkId;
};

struct HelloPayload {
  uint16_t fwVersion;
  uint32_t configVersion;
};

struct WelcomePayload {
  uint8_t channel;
  uint16_t hubFw;
  uint32_t desiredConfigVersion;
};

struct TimePayload {
  uint32_t epoch;       // UTC seconds, from the hub's DS3231 RTC / NTP
  int16_t tzOffsetMin;  // local time = UTC + this (e.g. +480 = UTC+8), for OLED clocks
};

struct EventPayload {
  uint16_t code;
  int32_t arg0;
  int32_t arg1;
};

struct CommandPayload {
  uint8_t cmd;     // Cmd
  uint8_t target;  // unit index or TARGET_ALL
  int32_t arg;
  uint32_t cmdRef;
};

struct AckPayload {
  uint16_t ackSeq;
  uint8_t result;  // AckResult
};

struct StatusCommon {
  uint32_t uptimeS;
  uint32_t configVersion;  // version currently APPLIED
  uint16_t faults;
  uint8_t interlock;
};

struct StatusShredder {
  StatusCommon c;
  uint8_t mode;  // ShredderMode (switch position)
  uint8_t state; // ShredderState
  uint8_t relayOn;
  uint8_t irDetected;
  uint8_t estopLatched;
  uint16_t countdownMs;
};

struct ContainerStatus {
  int32_t weightG;
  uint8_t state;       // ContainerState
  uint8_t mode;        // raw: 0 LOADCELL 1 TIME | mixed: 0 MANUAL 1 TIME
  uint8_t selectedKg;  // raw 1..5, mixed 0
  uint8_t progressPct;
  uint32_t remainingMs;
  int32_t dispensedG;
};

struct StatusContaining {
  StatusCommon c;
  uint8_t selector;  // Selector
  uint8_t hxOkMask;
  uint8_t pcaOk;
  ContainerStatus ct[4];
  float calFactor[4];  // owned by the module (set by CALIBRATE, kept in NVS), reported for display
};

struct StatusHotpress {
  StatusCommon c;
  uint8_t onButton;
  uint8_t selector;  // Selector: NEUTRAL cooling, LEFT hotpress, RIGHT auto
  uint8_t relayDesignCure;
  uint8_t relayHotpress;
  uint8_t stopLatched;
};

struct ConfigShredder {
  uint32_t configVersion;
  uint16_t autoStartDelayMs;
  uint16_t autoEmptyStopDelayMs;
  uint16_t manualConfirmTimeoutMs;
  uint16_t irDebounceMs;
  uint8_t buzzerVolumePct;
};

struct RawContainerCfg {
  uint8_t mode;  // 0 LOADCELL, 1 TIME
  uint32_t timeTableMs[5];
  uint32_t maxDispenseMs;
  uint32_t jamTimeoutMs;
  uint16_t toleranceG;
};

struct MixedContainerCfg {
  uint8_t mode;  // 0 MANUAL, 1 TIME
  uint32_t runTimeMs;
};

struct ServoCfg {
  uint16_t stopUs;
  uint16_t runUs;
};

// Load-cell calibration is NOT part of config: the module owns it (see StatusContaining.calFactor),
// so saving config from the web can never overwrite a calibration.
struct ConfigContaining {
  uint32_t configVersion;
  RawContainerCfg raw[2];
  MixedContainerCfg mixed[2];
  ServoCfg servo[8];
};

struct ConfigHotpress {
  uint32_t configVersion;
  uint8_t autoModeBehaviour;
};

template <typename P>
struct Packet {
  MsgHeader h;
  P p;
};

#pragma pack(pop)

// ---------- Size guarantees ----------
static_assert(sizeof(MsgHeader) == 12, "header must be 12 bytes");
static_assert(sizeof(TimePayload) == 6, "TimePayload layout changed");
static_assert(sizeof(StatusCommon) == 11, "StatusCommon layout changed");
static_assert(sizeof(RawContainerCfg) == 31, "RawContainerCfg layout changed");
static_assert(sizeof(ConfigContaining) == 108, "ConfigContaining layout changed (docs/PROTOCOL.md §6)");
static_assert(sizeof(ContainerStatus) == 16, "ContainerStatus layout changed");
static_assert(sizeof(StatusContaining) == 94, "StatusContaining layout changed (docs/PROTOCOL.md §5)");
static_assert(sizeof(Packet<StatusShredder>) <= MAX_FRAME, "too big");
static_assert(sizeof(Packet<StatusContaining>) <= MAX_FRAME, "too big");
static_assert(sizeof(Packet<StatusHotpress>) <= MAX_FRAME, "too big");
static_assert(sizeof(Packet<ConfigShredder>) <= MAX_FRAME, "too big");
static_assert(sizeof(Packet<ConfigContaining>) <= MAX_FRAME, "too big");
static_assert(sizeof(Packet<ConfigHotpress>) <= MAX_FRAME, "too big");

inline size_t statusSizeFor(ModuleId id) {
  switch (id) {
    case ModuleId::SHREDDER: return sizeof(StatusShredder);
    case ModuleId::CONTAINING: return sizeof(StatusContaining);
    case ModuleId::HOTPRESS: return sizeof(StatusHotpress);
    default: return 0;
  }
}

inline size_t configSizeFor(ModuleId id) {
  switch (id) {
    case ModuleId::SHREDDER: return sizeof(ConfigShredder);
    case ModuleId::CONTAINING: return sizeof(ConfigContaining);
    case ModuleId::HOTPRESS: return sizeof(ConfigHotpress);
    default: return 0;
  }
}

inline void fillHeader(MsgHeader& h, MsgType type, ModuleId sender, uint16_t seq, bool ackRequested) {
  h.magic = MAGIC;
  h.version = PROTOCOL_VERSION;
  h.type = (uint8_t)type;
  h.module = (uint8_t)sender;
  h.flags = ackRequested ? FLAG_ACK_REQUESTED : 0;
  h.seq = seq;
  h.networkId = NETWORK_ID;
}

/** True if the frame carries a valid v1 header for our network. */
inline bool headerValid(const uint8_t* data, size_t len) {
  if (len < sizeof(MsgHeader)) return false;
  const MsgHeader* h = reinterpret_cast<const MsgHeader*>(data);
  return h->magic == MAGIC && h->version == PROTOCOL_VERSION && h->networkId == NETWORK_ID;
}

inline void macToStr(const uint8_t* mac, char* out, size_t n) {
  snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

}  // namespace tile
