#include "cloud.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "ca_bundle.h"
#include "codec.h"
#include "config.h"
#include "secrets.h"

using namespace tile;

namespace cloud {

namespace {

// ---------------- shared state (guarded by mtx) ----------------
SemaphoreHandle_t mtx = nullptr;
struct Lock {
  Lock() { xSemaphoreTake(mtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mtx); }
};

JsonDocument pending;  // "path" -> value, flushed as one multi-path PATCH
DesiredConfig desired[MODULE_ID_COUNT];

struct StatusUpdate {
  char mkey[12];
  char id[40];
  char status[10];
  uint8_t tries;
};
constexpr int STATUS_RING = 16;
StatusUpdate statusRing[STATUS_RING];
int statusHead = 0;
int statusCount = 0;

QueueHandle_t cmdQueue = nullptr;
volatile bool online_ = false;
volatile uint32_t session_ = 0;
uint32_t eventCounter = 0;
char bootTag[9];

// ---------------- task-only state ----------------
String dbUrl;   // https://host (no trailing slash)
String dbHost;  // host only
String idToken;
bool haveToken = false;
uint32_t tokenValidUntilMs = 0;
uint32_t authBackoffMs = 0;
uint32_t nextAuthAttemptMs = 0;

WiFiClientSecure restTls;
HTTPClient http;

WiFiClientSecure streamTls;
bool streaming = false;
uint32_t streamLastDataMs = 0;
uint32_t nextStreamAttemptMs = 0;
String streamLine, streamEvent, streamData;

bool timeStarted = false;
uint32_t lastFlushMs = 0;
uint32_t lastPollMs = 0;
uint32_t lastRetentionMs = 0;
bool retentionDone = false;

constexpr int HANDLED_RING = 16;
char handled[HANDLED_RING][40];
int handledNext = 0;

// ---------------- helpers ----------------
bool timeSynced() { return time(nullptr) > 1700000000; }
uint64_t epochMs() { return (uint64_t)time(nullptr) * 1000ULL; }

void setOnline(bool v) {
  if (v && !online_) {
    session_ = session_ + 1;
    Serial.println("[NET] Firebase online");
  } else if (!v && online_) {
    Serial.println("[NET] Firebase offline");
  }
  online_ = v;
}

String url(const String& path) { return dbUrl + "/" + path + ".json?auth=" + idToken; }

int request(const char* method, const String& u, const String& body, String* resp) {
  http.setReuse(true);
  http.setConnectTimeout(10000);
  http.setTimeout(10000);
  if (!http.begin(restTls, u)) return -1;
  http.addHeader("Content-Type", "application/json");
  int code = strcmp(method, "GET") == 0 ? http.GET() : http.sendRequest(method, body);
  String r = code > 0 ? http.getString() : String();  // always drain so the connection can be reused
  if (resp) *resp = r;
  http.end();
  return code;
}

bool isTokenExpired(int code, const String& resp) { return code == 401 && resp.indexOf("xpired") >= 0; }

void queueStatus(const char* mkey, const char* id, const char* status) {
  Lock l;
  if (statusCount >= STATUS_RING) {
    Serial.println("[ERROR] command status queue full, dropping update");
    return;
  }
  StatusUpdate& u = statusRing[(statusHead + statusCount) % STATUS_RING];
  strlcpy(u.mkey, mkey, sizeof(u.mkey));
  strlcpy(u.id, id, sizeof(u.id));
  strlcpy(u.status, status, sizeof(u.status));
  u.tries = 0;
  statusCount++;
}

bool wasHandled(const char* id) {
  for (auto& h : handled)
    if (strcmp(h, id) == 0) return true;
  return false;
}
void markHandled(const char* id) {
  strlcpy(handled[handledNext], id, sizeof(handled[0]));
  handledNext = (handledNext + 1) % HANDLED_RING;
}

int moduleFromKey(const String& k) {
  for (uint8_t i = 1; i < MODULE_ID_COUNT; i++)
    if (k == moduleKey((ModuleId)i)) return i;
  return -1;
}

int cmdFromName(const char* s) {
  for (uint8_t c = 1; c <= 5; c++)
    if (strcmp(s, cmdName(c)) == 0) return c;
  return -1;
}

// ---------------- auth ----------------
bool ensureAuth() {
  if (haveToken && (int32_t)(millis() - tokenValidUntilMs) < 0) return true;
  if (nextAuthAttemptMs && (int32_t)(millis() - nextAuthAttemptMs) < 0) return false;

  JsonDocument req;
  req["email"] = HUB_EMAIL;
  req["password"] = HUB_PASSWORD;
  req["returnSecureToken"] = true;
  String body, resp;
  serializeJson(req, body);
  int code = request("POST", String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") +
                                 FIREBASE_API_KEY,
                     body, &resp);
  if (code == 200) {
    JsonDocument r;
    if (!deserializeJson(r, resp) && r["idToken"].is<const char*>()) {
      idToken = r["idToken"].as<String>();
      uint32_t exp = (uint32_t)atol(r["expiresIn"] | "3600");
      uint32_t useFor = exp > 600 ? exp - 300 : exp / 2;  // refresh 5 min before expiry
      tokenValidUntilMs = millis() + useFor * 1000UL;
      haveToken = true;
      authBackoffMs = 0;
      nextAuthAttemptMs = 0;
      if (streaming) {  // stream must reconnect with the new token
        streamTls.stop();
        streaming = false;
      }
      Serial.println("[NET] Firebase signed in as hub");
      return true;
    }
  }
  Serial.printf("[ERROR] Firebase sign-in failed (HTTP %d) %s\n", code, resp.substring(0, 160).c_str());
  haveToken = false;
  authBackoffMs = authBackoffMs ? min<uint32_t>(authBackoffMs * 2, 60000) : 5000;
  nextAuthAttemptMs = millis() + authBackoffMs;
  setOnline(false);
  return false;
}

// ---------------- batched writes ----------------
void flushPending() {
  JsonDocument snap;
  {
    Lock l;
    if (pending.as<JsonObjectConst>().size() == 0) return;
    snap = pending;
    pending.clear();
  }
  String body, resp;
  serializeJson(snap, body);
  int code = request("PATCH", dbUrl + "/.json?print=silent&auth=" + idToken, body, &resp);
  if (code == 200 || code == 204) {
    setOnline(true);
    return;
  }

  const bool retry = code < 0 || code >= 500 || isTokenExpired(code, resp);
  if (retry) {
    Lock l;
    for (JsonPairConst kv : snap.as<JsonObjectConst>()) {
      String k = kv.key().c_str();
      if (pending[k].isNull()) pending[k] = kv.value();  // newer values win
    }
  }
  if (isTokenExpired(code, resp)) haveToken = false;
  if (code < 0 || code >= 500) setOnline(false);
  Serial.printf("[ERROR] Firebase write %s (HTTP %d) %s\n", retry ? "will retry" : "rejected, dropped", code,
                resp.substring(0, 160).c_str());
}

void flushStatuses() {
  StatusUpdate u;
  {
    Lock l;
    if (statusCount == 0) return;
    u = statusRing[statusHead];
  }
  JsonDocument d;
  d["status"] = u.status;
  d["updatedAt"][".sv"] = "timestamp";
  String body, resp;
  serializeJson(d, body);
  int code = request("PATCH", url(String("commands/") + u.mkey + "/" + u.id) + "&print=silent", body, &resp);

  const bool ok = code == 200 || code == 204;
  const bool transient = code < 0 || code >= 500 || isTokenExpired(code, resp);
  if (isTokenExpired(code, resp)) haveToken = false;
  Lock l;
  if (ok || !transient || statusRing[statusHead].tries >= 3) {
    if (!ok) Serial.printf("[ERROR] command %s status '%s' not written (HTTP %d)\n", u.id, u.status, code);
    statusHead = (statusHead + 1) % STATUS_RING;
    statusCount--;
  } else {
    statusRing[statusHead].tries++;
  }
}

// ---------------- config polling ----------------
void pollConfigs() {
  for (uint8_t i = 1; i < MODULE_ID_COUNT; i++) {
    const ModuleId id = (ModuleId)i;
    const String base = String("modules/") + moduleKey(id) + "/config";
    String resp;
    int code = request("GET", url(base + "/version"), "", &resp);
    if (code != 200) {
      if (isTokenExpired(code, resp)) haveToken = false;
      if (code < 0 || code >= 500) setOnline(false);
      return;
    }
    setOnline(true);
    resp.trim();
    if (resp == "null") {
      Lock l;
      desired[i].valid = false;
      continue;
    }
    const uint32_t v = strtoul(resp.c_str(), nullptr, 10);
    {
      Lock l;
      if (desired[i].valid && desired[i].version == v) continue;
    }
    code = request("GET", url(base), "", &resp);
    if (code != 200) return;
    JsonDocument doc;
    if (deserializeJson(doc, resp) || !doc.is<JsonObject>()) {
      Serial.printf("[ERROR] %s config is not valid JSON\n", moduleKey(id));
      continue;
    }
    DesiredConfig d;
    size_t len = 0;
    if (!codec::configFromJson(id, doc.as<JsonObjectConst>(), d.bin, len)) continue;
    d.valid = true;
    d.version = doc["version"] | 0u;
    d.len = (uint8_t)len;
    {
      Lock l;
      desired[i] = d;
    }
    Serial.printf("[NET] %s config v%u loaded from Firebase\n", moduleKey(id), (unsigned)d.version);
  }
}

// ---------------- /commands stream ----------------
void closeStream() {
  streamTls.stop();
  streaming = false;
  streamLine = "";
  streamEvent = "";
  streamData = "";
}

void handleCommand(const String& mkey, const String& id, JsonVariantConst v) {
  if (!v.is<JsonObjectConst>()) return;
  const char* type = v["type"];
  const char* status = v["status"];
  if (!type || !status) return;

  const uint64_t now = epochMs();
  const uint64_t createdAt = v["createdAt"].as<uint64_t>();
  const uint64_t age = now > createdAt ? now - createdAt : 0;
  const bool isPending = strcmp(status, "pending") == 0;
  const bool isSent = strcmp(status, "sent") == 0;

  if (isPending || isSent) {
    if (age > CLOUD_CMD_MAX_AGE_MS) {  // too old: never execute (e.g. queued while the hub was down)
      if (!wasHandled(id.c_str())) {
        markHandled(id.c_str());
        Serial.printf("[NET] command %s/%s %s is %us old -> expired\n", mkey.c_str(), id.c_str(), type,
                      (unsigned)(age / 1000));
        queueStatus(mkey.c_str(), id.c_str(), "expired");
      }
      return;
    }
    if (!isPending || wasHandled(id.c_str())) return;  // "sent" = already in flight from this boot
    markHandled(id.c_str());

    Command c = {};
    strlcpy(c.mkey, mkey.c_str(), sizeof(c.mkey));
    strlcpy(c.id, id.c_str(), sizeof(c.id));
    const int m = mkey == "all" ? TARGET_ALL : moduleFromKey(mkey);
    const int cmd = cmdFromName(type);
    if (m < 0 || cmd < 0 || (m == TARGET_ALL && cmd != (int)Cmd::STOP)) {
      queueStatus(c.mkey, c.id, "failed");
      return;
    }
    c.module = (uint8_t)m;
    c.cmd = (uint8_t)cmd;
    c.target = v["target"].isNull() ? TARGET_ALL : v["target"].as<uint8_t>();
    c.arg = v["arg"] | 0;
    if (xQueueSend(cmdQueue, &c, 0) != pdTRUE) queueStatus(c.mkey, c.id, "failed");
    return;
  }

  // Finished command: delete it once it's older than CLOUD_CMD_KEEP_MS.
  const uint64_t updatedAt = v["updatedAt"].isNull() ? createdAt : v["updatedAt"].as<uint64_t>();
  if (updatedAt && now > updatedAt && now - updatedAt > CLOUD_CMD_KEEP_MS)
    setNull((String("commands/") + mkey + "/" + id).c_str());
}

// segs: path below /commands. Depth 0 = whole tree, 1 = one module, 2 = one command, 3+ = a field.
void handleNode(const String* segs, int n, JsonVariantConst v) {
  if (v.isNull()) return;
  if (n >= 3) return;  // field update (e.g. our own status write echoed back)
  if (n == 2) {
    handleCommand(segs[0], segs[1], v);
    return;
  }
  if (!v.is<JsonObjectConst>()) return;
  for (JsonPairConst kv : v.as<JsonObjectConst>()) {
    String next[2];
    if (n == 1) next[0] = segs[0];
    next[n] = kv.key().c_str();
    handleNode(next, n + 1, kv.value());
  }
}

void dispatchStreamEvent() {
  if (streamEvent == "put" || streamEvent == "patch") {
    JsonDocument doc;
    if (deserializeJson(doc, streamData)) {
      Serial.println("[ERROR] bad stream JSON");
      return;
    }
    String segs[3];
    int n = 0;
    String path = doc["path"] | "/";
    int start = 0;
    while (start < (int)path.length() && n < 3) {
      int slash = path.indexOf('/', start);
      if (slash < 0) slash = path.length();
      if (slash > start) segs[n++] = path.substring(start, slash);
      start = slash + 1;
    }
    if (start < (int)path.length()) return;  // deeper than 3 levels: a field, ignore

    JsonVariantConst data = doc["data"];
    if (streamEvent == "put") {
      handleNode(segs, n, data);
    } else if (data.is<JsonObjectConst>() && n < 3) {
      for (JsonPairConst kv : data.as<JsonObjectConst>()) {
        String s2[3] = {segs[0], segs[1], segs[2]};
        s2[n] = kv.key().c_str();
        handleNode(s2, n + 1, kv.value());
      }
    }
    setOnline(true);
  } else if (streamEvent == "cancel") {
    Serial.println("[ERROR] command stream cancelled by Firebase (permission?)");
    closeStream();
    nextStreamAttemptMs = millis() + 30000;
  } else if (streamEvent == "auth_revoked") {
    haveToken = false;
    closeStream();
  }
  // keep-alive: nothing to do
}

void processStreamLine() {
  if (streamLine.startsWith("event:")) {
    streamEvent = streamLine.substring(6);
    streamEvent.trim();
  } else if (streamLine.startsWith("data:")) {
    streamData = streamLine.substring(5);
    streamData.trim();
  } else if (streamLine.length() == 0 && streamEvent.length()) {
    dispatchStreamEvent();
    streamEvent = "";
    streamData = "";
  }
}

bool openStream() {
  if (!streamTls.connect(dbHost.c_str(), 443)) {
    Serial.println("[ERROR] command stream: TLS connect failed");
    return false;
  }
  streamTls.print(String("GET /commands.json?auth=") + idToken + " HTTP/1.1\r\nHost: " + dbHost +
                  "\r\nAccept: text/event-stream\r\nConnection: keep-alive\r\n\r\n");
  streamTls.setTimeout(10);  // seconds (WiFiClientSecure) for the header phase
  String status = streamTls.readStringUntil('\n');
  if (!status.startsWith("HTTP/1.1 200")) {
    status.trim();
    Serial.printf("[ERROR] command stream refused: %s\n", status.c_str());
    if (status.indexOf("401") >= 0) haveToken = false;
    streamTls.stop();
    return false;
  }
  for (int i = 0; i < 40; i++) {  // skip headers
    String h = streamTls.readStringUntil('\n');
    h.trim();
    if (h.length() == 0) break;
  }
  streaming = true;
  streamLastDataMs = millis();
  streamLine.reserve(512);
  Serial.println("[NET] listening for commands");
  return true;
}

void serviceStream() {
  if (!streaming) {
    // Only after NTP sync, so every command's age can be checked (no replays of old commands).
    if (!timeSynced() || (int32_t)(millis() - nextStreamAttemptMs) < 0) return;
    if (!openStream()) {
      nextStreamAttemptMs = millis() + 5000;
      return;
    }
  }
  if (!streamTls.connected() && !streamTls.available()) {
    Serial.println("[NET] command stream closed, reconnecting");
    closeStream();
    nextStreamAttemptMs = millis() + 2000;
    return;
  }
  uint8_t buf[512];
  int budget = 8;  // max 4 KB per pass, keep the task responsive
  while (budget-- > 0 && streamTls.available()) {
    int n = streamTls.read(buf, sizeof(buf));
    if (n <= 0) break;
    streamLastDataMs = millis();
    for (int i = 0; i < n; i++) {
      char c = (char)buf[i];
      if (c == '\n') {
        processStreamLine();
        streamLine = "";
        if (!streaming) return;  // dispatch closed the stream
      } else if (c != '\r') {
        if (streamLine.length() >= 16384) {
          Serial.println("[ERROR] command stream line too long, reconnecting");
          closeStream();
          return;
        }
        streamLine += c;
      }
    }
  }
  if (millis() - streamLastDataMs > CLOUD_STREAM_IDLE_MS) {
    Serial.println("[NET] command stream idle, reconnecting");
    closeStream();
  }
}

// ---------------- retention ----------------
void retention() {
  if (!timeSynced()) return;
  if (retentionDone && millis() - lastRetentionMs < CLOUD_RETENTION_EVERY_MS) return;
  retentionDone = true;
  lastRetentionMs = millis();

  char q[96];
  snprintf(q, sizeof(q), "&orderBy=%%22ts%%22&endAt=%llu&limitToFirst=100",
           (unsigned long long)(epochMs() - CLOUD_EVENT_KEEP_MS));
  String resp;
  int code = request("GET", url("events") + q, "", &resp);
  if (code != 200) {
    Serial.printf("[ERROR] event retention query failed (HTTP %d)\n", code);
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, resp) || !doc.is<JsonObject>()) return;
  int n = 0;
  for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
    setNull((String("events/") + kv.key().c_str()).c_str());
    n++;
  }
  if (n) Serial.printf("[NET] deleting %d events older than 30 days\n", n);
}

void task(void*) {
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      if (streaming) closeStream();
      setOnline(false);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (!timeStarted) {
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      timeStarted = true;
    }
    if (!ensureAuth()) {
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    serviceStream();

    const uint32_t now = millis();
    if (now - lastFlushMs >= CLOUD_FLUSH_MS) {
      lastFlushMs = now;
      flushPending();
    }
    flushStatuses();
    if (now - lastPollMs >= CLOUD_CONFIG_POLL_MS) {
      lastPollMs = now;
      pollConfigs();
    }
    retention();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace

// ---------------- public API ----------------
void begin() {
  mtx = xSemaphoreCreateMutex();
  cmdQueue = xQueueCreate(8, sizeof(Command));

  dbUrl = FIREBASE_DATABASE_URL;
  while (dbUrl.endsWith("/")) dbUrl.remove(dbUrl.length() - 1);
  dbHost = dbUrl;
  dbHost.replace("https://", "");
  int slash = dbHost.indexOf('/');
  if (slash >= 0) dbHost = dbHost.substring(0, slash);

  restTls.setCACert(CA_BUNDLE);
  streamTls.setCACert(CA_BUNDLE);
  snprintf(bootTag, sizeof(bootTag), "%08lx", (unsigned long)esp_random());

  // Core 0 (with the WiFi stack); loop() keeps core 1 for ESP-NOW handling.
  xTaskCreatePinnedToCore(task, "cloud", 16384, nullptr, 1, nullptr, 0);
}

bool online() { return online_; }
uint32_t session() { return session_; }

void set(const char* path, JsonVariantConst value) {
  Lock l;
  pending[String(path)] = value;
}

void setNull(const char* path) {
  Lock l;
  pending[String(path)] = nullptr;
}

void setServerTime(const char* path) {
  Lock l;
  JsonObject o = pending[String(path)].to<JsonObject>();
  o[".sv"] = "timestamp";
}

void pushEvent(const char* module, const char* code, int32_t arg0, int32_t arg1) {
  Lock l;
  if (measureJson(pending) > CLOUD_PENDING_MAX_BYTES) {
    Serial.printf("[ERROR] cloud backlog full, event %s dropped\n", code);
    return;
  }
  char key[40];
  snprintf(key, sizeof(key), "events/h%s_%06lu", bootTag, (unsigned long)eventCounter++);
  JsonObject e = pending[String(key)].to<JsonObject>();
  e["ts"][".sv"] = "timestamp";
  e["module"] = String(module);
  e["code"] = String(code);
  JsonArray a = e["args"].to<JsonArray>();
  a.add(arg0);
  a.add(arg1);
}

void setCommandStatus(const char* mkey, const char* id, const char* status) { queueStatus(mkey, id, status); }

bool nextCommand(Command& out) { return cmdQueue && xQueueReceive(cmdQueue, &out, 0) == pdTRUE; }

bool desiredConfig(ModuleId id, DesiredConfig& out) {
  const uint8_t i = (uint8_t)id;
  if (i == 0 || i >= MODULE_ID_COUNT) return false;
  Lock l;
  out = desired[i];
  return out.valid;
}

}  // namespace cloud
