// Remote diagnostics: the hub's own health and messages in Firebase, so it can run for days without a
// PC on its USB port. Spec: docs/modules/hub.md "Remote diagnostics".
//  - diag::printf() replaces Serial.printf for log lines: always printed to Serial; [ERROR] lines and
//    important [NET]/[STATE] lines are also kept for /hubLog (rate-limited, never blocks, never takes the
//    cloud lock, so it is safe to call from any task, even while cloud.cpp holds its mutex).
//  - diag::phase() marks what each task is doing. The marks, the last message and the memory figures live
//    in RTC RAM, which survives a crash restart: after a crash the hub uploads a crash report.
#pragma once

#include <Arduino.h>

namespace diag {

enum class Task : uint8_t { Loop, Cloud, Stream, COUNT };

/** printf to Serial; the line may also go to /hubLog (see above). */
void printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

/** What `t` is doing right now (a short string literal, kept by pointer). */
void phase(Task t, const char* what);

/** Call once at boot, after the cloud is started: queues the crash report if the last reset was a crash. */
void begin(int resetReason);

/** Call from loop(): moves queued lines to the cloud and tracks loop timing. */
void loop();

/** Health figures for /hub/diag (written with the 10 s heartbeat). */
struct Health {
  uint32_t heap, minHeap, block, uptimeS, loopMaxMs, suppressed;
};
Health health();

}  // namespace diag
