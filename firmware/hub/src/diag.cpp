#include "diag.h"

#include <esp_system.h>
#include <stdarg.h>

#include "cloud.h"
#include "config.h"

namespace diag {
namespace {

// ---------------- queued log lines (any task -> loop) ----------------
constexpr uint8_t RING = 16;
constexpr size_t MSG_LEN = 120;
struct Line {
  char lvl;  // 'E' error, 'I' info, 'C' crash report
  char msg[MSG_LEN];
};
Line ring[RING];
volatile uint8_t head = 0, count = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t ringDropped = 0;

// Important non-error lines worth keeping remotely (everything else stays on Serial only).
const char* const KEEP[] = {"setup hotspot", "offline for", "back online", "WiFi '", "WiFi down",
                            "Firebase online", "Firebase offline", "reset reason", "restarting",
                            "BOOT", "setup page:", "command stream closed", "listening for commands"};

// ---------------- crash breadcrumbs (RTC RAM: kept across a crash restart, not power loss) ----------------
constexpr uint32_t MAGIC = 0x44494147u;  // "DIAG"
struct Crumbs {
  uint32_t magic;
  uint32_t uptimeS, heap, minHeap, block;
  char phase[(int)Task::COUNT][16];
  char last[MSG_LEN];
};
RTC_NOINIT_ATTR Crumbs crumbs;

// ---------------- rate limit (loop only) ----------------
uint32_t windowStartMs = 0;
uint8_t sentInWindow = 0;
uint32_t suppressed = 0;
struct Recent {
  uint32_t hash, atMs;
};
Recent recent[8];
uint8_t recentNext = 0;

bool started = false;  // begin() ran: crumbs belong to this boot from now on

uint32_t loopLastMs = 0, loopMaxMs = 0, loopMaxShownMs = 0, loopWindowStartMs = 0;

uint32_t fnv(const char* s) {
  uint32_t h = 2166136261u;
  for (; *s; s++) {
    if (*s >= '0' && *s <= '9') continue;  // "down 20 s" and "down 35 s" count as the same message
    h = (h ^ (uint8_t)*s) * 16777619u;
  }
  return h;
}

void enqueue(char lvl, const char* msg) {
  portENTER_CRITICAL(&mux);
  if (count == RING) {
    ringDropped++;
  } else {
    Line& l = ring[(head + count) % RING];
    l.lvl = lvl;
    strlcpy(l.msg, msg, MSG_LEN);
    count++;
  }
  portEXIT_CRITICAL(&mux);
}

bool dequeue(Line& out) {
  bool ok = false;
  portENTER_CRITICAL(&mux);
  if (count) {
    out = ring[head];
    head = (head + 1) % RING;
    count--;
    ok = true;
  }
  portEXIT_CRITICAL(&mux);
  return ok;
}

/** true = send it; false = same message within DIAG_REPEAT_MS, or the window budget is used up. */
bool allow(const char* msg, uint32_t now) {
  const uint32_t h = fnv(msg);
  for (Recent& r : recent)
    if (r.hash == h && r.atMs && now - r.atMs < DIAG_REPEAT_MS) return false;
  if (now - windowStartMs >= DIAG_WINDOW_MS) {
    windowStartMs = now;
    sentInWindow = 0;
  }
  if (sentInWindow >= DIAG_MAX_PER_WINDOW) return false;
  sentInWindow++;
  recent[recentNext] = {h, now};
  recentNext = (recentNext + 1) % 8;
  return true;
}

}  // namespace

void printf(const char* fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);

  // Keep: [ERROR] lines, and [NET]/[STATE] lines that match KEEP.
  const char* p = buf;
  while (*p == '\n' || *p == ' ') p++;
  char lvl = 0;
  if (strncmp(p, "[ERROR] ", 8) == 0) {
    lvl = 'E';
    p += 8;
  } else if (strncmp(p, "[NET] ", 6) == 0 || strncmp(p, "[STATE] ", 8) == 0) {
    const char* body = strchr(p, ']') + 2;
    // The 10 s status report ("WiFi up ch 1 | ...") is Serial-only: /hub/diag already carries its figures.
    if (strncmp(body, "WiFi up ch", 10) == 0 || strncmp(body, "WiFi DOWN ch", 12) == 0) return;
    for (const char* k : KEEP)
      if (strstr(body, k)) {
        lvl = 'I';
        p = body;
        break;
      }
  }
  if (!lvl) return;
  char msg[MSG_LEN];
  strlcpy(msg, p, sizeof(msg));
  for (char* c = msg + strlen(msg); c > msg && (c[-1] == '\n' || c[-1] == '\r' || c[-1] == ' ');) *--c = 0;
  if (started) strlcpy(crumbs.last, msg, sizeof(crumbs.last));  // before begin(): keep the pre-crash one
  enqueue(lvl, msg);
}

void phase(Task t, const char* what) { strlcpy(crumbs.phase[(int)t], what, sizeof(crumbs.phase[0])); }

void begin(int resetReason) {
  const bool crashed = resetReason == ESP_RST_PANIC || resetReason == ESP_RST_INT_WDT ||
                       resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_WDT;
  if (crumbs.magic == MAGIC && crashed) {
    char m[MSG_LEN * 3];  // room for the phases + figures + the last message (MSG_LEN)
    const uint32_t up = crumbs.uptimeS;
    char upTxt[16];
    if (up < 3600)
      snprintf(upTxt, sizeof(upTxt), "%lum%02lus", (unsigned long)(up / 60), (unsigned long)(up % 60));
    else
      snprintf(upTxt, sizeof(upTxt), "%luh%02lum", (unsigned long)(up / 3600), (unsigned long)(up / 60 % 60));
    snprintf(m, sizeof(m), "CRASH after %s. Doing: loop=%s cloud=%s stream=%s. Heap %luK (min %luK, block %luK). Last: %s",
             upTxt, crumbs.phase[0], crumbs.phase[1],
             crumbs.phase[2], (unsigned long)(crumbs.heap / 1024), (unsigned long)(crumbs.minHeap / 1024),
             (unsigned long)(crumbs.block / 1024), crumbs.last);
    Serial.printf("[ERROR] %s\n", m);
    cloud::pushLog('C', m);  // straight to the cloud queue (loop context, cloud lock not held)
  }
  memset(&crumbs, 0, sizeof(crumbs));
  crumbs.magic = MAGIC;
  started = true;
}

void loop() {
  const uint32_t now = millis();
  // Loop timing: the longest loop() pass in each 10 s window (a blocked loop delays ESP-NOW handling).
  if (loopLastMs) loopMaxMs = max<uint32_t>(loopMaxMs, now - loopLastMs);
  loopLastMs = now;
  if (now - loopWindowStartMs >= 10000) {
    loopWindowStartMs = now;
    loopMaxShownMs = loopMaxMs;
    loopMaxMs = 0;
    const Health h = health();
    crumbs.uptimeS = h.uptimeS;
    crumbs.heap = h.heap;
    crumbs.minHeap = h.minHeap;
    crumbs.block = h.block;
  }

  Line l;
  while (dequeue(l)) {
    if (allow(l.msg, now)) {
      if (suppressed) {
        char m[MSG_LEN];
        snprintf(m, sizeof(m), "(%lu similar messages not sent, rate limit)", (unsigned long)suppressed);
        cloud::pushLog('I', m);
        suppressed = 0;
      }
      cloud::pushLog(l.lvl, l.msg);
    } else {
      suppressed++;
    }
  }
  if (ringDropped) {
    portENTER_CRITICAL(&mux);
    suppressed += ringDropped;
    ringDropped = 0;
    portEXIT_CRITICAL(&mux);
  }
}

Health health() {
  return {ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), (uint32_t)(millis() / 1000),
          loopMaxShownMs, suppressed};
}

}  // namespace diag
