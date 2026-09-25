// Hub compile-time settings. Secrets (WiFi, Firebase login) live in secrets.h (git-ignored).
#pragma once

#include <TileProtocol.h>

#define HUB_FW_MAJOR 0
#define HUB_FW_MINOR 2
#define HUB_FW_PATCH 1
constexpr uint16_t HUB_FW = tile::fwEncode(HUB_FW_MAJOR, HUB_FW_MINOR, HUB_FW_PATCH);

// ---- Clock ----
constexpr int16_t LOCAL_TZ_OFFSET_MIN = 480;     // UTC+8 (Philippines), sent to modules for OLED clocks
constexpr uint32_t TIME_BROADCAST_MS = 60000;    // TIME to every online module this often
constexpr uint32_t RTC_MAX_DRIFT_S = 2;          // re-write the RTC after NTP if it drifted more than this

// ---- Timing ----
constexpr uint32_t WIFI_BOOT_WAIT_MS = 20000;     // wait this long for WiFi before starting ESP-NOW anyway
constexpr uint32_t PRESENCE_CHECK_MS = 250;
constexpr uint32_t CONFIG_PUSH_CHECK_MS = 250;
constexpr uint32_t CONFIG_RESEND_MS = 3000;       // min gap between CONFIG sends to one module
constexpr uint32_t CONFIG_QUEUED_RESEND_MS = 30000;
constexpr uint32_t HUB_HEARTBEAT_MS = 10000;      // /hub/lastSeen + presence/lastSeen refresh
constexpr uint32_t BOOT_BUTTON_HOLD_MS = 5000;    // hold BOOT this long to clear pairings
// WiFi watchdog: core 2.0.x auto-reconnect can stop retrying after "network not found" (seen when
// the router's 2.4 GHz radio dropped out). The hub retries itself and reboots as a last resort
// (safe: the hub drives no machinery, modules keep running and re-pair).
constexpr uint32_t WIFI_RETRY_EVERY_MS = 20000;
constexpr uint32_t WIFI_REBOOT_AFTER_MS = 10UL * 60 * 1000;

// ---- Cloud (Firebase) ----
constexpr uint32_t CLOUD_FLUSH_MS = 500;          // batched PATCH of state/presence/events
constexpr uint32_t CLOUD_CONFIG_POLL_MS = 3000;   // check modules/*/config/version
constexpr uint32_t CLOUD_STREAM_IDLE_MS = 70000;  // RTDB sends keep-alive every ~30 s
constexpr uint32_t CLOUD_CMD_MAX_AGE_MS = 30000;  // older commands are marked "expired", never executed
constexpr uint32_t CLOUD_CMD_KEEP_MS = 24UL * 3600 * 1000;       // delete finished commands after 24 h
constexpr uint64_t CLOUD_EVENT_KEEP_MS = 30ULL * 24 * 3600 * 1000;  // delete events after 30 days
constexpr uint32_t CLOUD_RETENTION_EVERY_MS = 24UL * 3600 * 1000;
constexpr size_t CLOUD_PENDING_MAX_BYTES = 12000;  // drop new events beyond this while offline
