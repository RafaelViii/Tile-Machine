// Firebase Realtime Database bridge (REST over ONE keep-alive TLS connection), running in its own FreeRTOS task
// so slow HTTPS never blocks ESP-NOW handling in loop().
//
//  - Writes are merged into ONE multi-path PATCH every CLOUD_FLUSH_MS. Paths in one batch never
//    overlap (RTDB rejects "a" together with "a/b").
//  - Command status updates go in separate requests: if a command was deleted meanwhile, that write
//    fails on its own instead of taking the whole batch down.
//  - /signal is read every CLOUD_SIGNAL_POLL_MS (a few bytes). The web bumps signal/commands with every
//    command and writes signal/config/<module> with every config save; a change makes the hub fetch
//    /commands or that config. Full fallback polls catch anything that didn't bump the signal.
//    (fw <= 0.3.4 streamed /commands on a 2nd TLS connection; 2-3 TLS sessions didn't fit in RAM.)
//  - Commands are only handled once the hub knows the time, so their age can be checked (no replays).
//
// All functions below are thread-safe and non-blocking.
#pragma once

#include <ArduinoJson.h>
#include <TileProtocol.h>

namespace cloud {

struct Command {
  char mkey[12];    // "shredder" | "containing" | "hotpress" | "all"
  char id[40];      // RTDB push id
  uint8_t module;   // ModuleId, or tile::TARGET_ALL for "all"
  uint8_t cmd;      // tile::Cmd
  uint8_t target;   // unit index or tile::TARGET_ALL
  int32_t arg;
};

struct DesiredConfig {
  bool valid = false;  // false = no config saved in Firebase yet
  uint32_t version = 0;
  uint8_t len = 0;
  uint8_t bin[tile::MAX_FRAME];
};

void begin();

/** Signed in and the last request reached Firebase (a failed write or read = offline). */
bool online();

/** Increments every time the cloud (re)connects; on change, re-send a full snapshot. */
uint32_t session();

/** Queue a value for the next batched write (copied). Pass a null variant to delete the path. */
void set(const char* path, JsonVariantConst value);
void setNull(const char* path);
/** Queue {".sv":"timestamp"} at path. */
void setServerTime(const char* path);

/** Queue an /events entry {ts, module, code, args}. */
void pushEvent(const char* module, const char* code, int32_t arg0 = 0, int32_t arg1 = 0);

/** Queue a /hubLog entry {ts, lvl, msg} (lvl 'E' error, 'I' info, 'C' crash report). Loop context only. */
void pushLog(char lvl, const char* msg);

/** Separate write of commands/{mkey}/{id}/{status, updatedAt}. */
void setCommandStatus(const char* mkey, const char* id, const char* status);

/** Next command from /commands to execute (already checked for age and duplicates). */
bool nextCommand(Command& out);

/** Latest config saved in Firebase for a module, converted to its CONFIG payload. */
bool desiredConfig(tile::ModuleId id, DesiredConfig& out);

}  // namespace cloud
