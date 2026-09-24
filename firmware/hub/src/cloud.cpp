#include "cloud.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <TileTime.h>
#include <esp_http_client.h>
#include <sys/time.h>
#include <time.h>

#include <string>

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

// Token shared with the stream task (guarded by mtx). tokenGen changes on every new sign-in.
String streamToken;
volatile uint32_t tokenGen = 0;
volatile bool haveToken = false;  // cleared by either task to force a new sign-in

// ---------------- cloud-task-only state ----------------
String dbUrl;   // https://host (no trailing slash)
String dbHost;  // host only
String idToken;
uint32_t tokenValidUntilMs = 0;
uint32_t authBackoffMs = 0;
uint32_t nextAuthAttemptMs = 0;

// ESP-IDF HTTP clients, one persistent keep-alive connection per host. (Arduino's
// WiFiClientSecure/HTTPClient block for the full socket timeout in connected()/available() on an
// idle connection in core 2.0.x, which stalled every write by ~10 s.)
esp_http_client_handle_t authClient = nullptr;
esp_http_client_handle_t dbClient = nullptr;
std::string* respSink = nullptr;

// ---------------- stream-task-only state ----------------
WiFiClientSecure streamTls;
bool streaming = false;
uint32_t streamLastDataMs = 0;
uint32_t streamRetryAtMs = 0;
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
// True once the hub knows the time (DS3231 at boot, or NTP). The command stream waits for it.
bool timeSynced() { return tile::epochValid(time(nullptr)); }
uint64_t epochMs() { return (uint64_t)time(nullptr) * 1000ULL; }

void setOnline(bool v) {  // called from both the cloud task and the stream task
  Lock l;
  if (v && !online_) {
    session_ = session_ + 1;
    Serial.println("[NET] Firebase online");
  } else if (!v && online_) {
    Serial.println("[NET] Firebase offline");
  }
  online_ = v;
}

String url(const String& path) { return dbUrl + "/" + path + ".json?auth=" + idToken; }

esp_err_t onHttpEvent(esp_http_client_event_t* e) {
  if (e->event_id == HTTP_EVENT_ON_DATA && respSink && e->data_len > 0)
    respSink->append(static_cast<const char*>(e->data), e->data_len);
  return ESP_OK;
}

esp_http_client_handle_t makeClient(const char* baseUrl) {
  esp_http_client_config_t cfg = {};
  cfg.url = baseUrl;
  cfg.cert_pem = CA_BUNDLE;
  cfg.timeout_ms = 10000;
  cfg.event_handler = onHttpEvent;
  cfg.keep_alive_enable = true;
  cfg.buffer_size = 2048;
  cfg.buffer_size_tx = 2048;  // the auth token in the URL is ~1 KB
  return esp_http_client_init(&cfg);
}

/** Returns the HTTP status, or -1 on a network/TLS error. */
int request(const char* method, const String& u, const String& body, String* resp) {
  const uint32_t t0 = millis();
  esp_http_client_handle_t c = u.startsWith("https://identitytoolkit") ? authClient : dbClient;
  esp_http_client_method_t m = strcmp(method, "GET") == 0     ? HTTP_METHOD_GET
                               : strcmp(method, "POST") == 0  ? HTTP_METHOD_POST
                               : strcmp(method, "PATCH") == 0 ? HTTP_METHOD_PATCH
                                                              : HTTP_METHOD_PUT;
  std::string sink;
  int code = -1;
  for (int attempt = 0; attempt < 2; attempt++) {  // 2nd attempt = fresh connection if the kept-alive one died
    sink.clear();
    respSink = &sink;
    esp_http_client_set_url(c, u.c_str());
    esp_http_client_set_method(c, m);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_post_field(c, body.length() ? body.c_str() : nullptr, body.length());
    esp_err_t err = esp_http_client_perform(c);
    respSink = nullptr;
    if (err == ESP_OK) {
      code = esp_http_client_get_status_code(c);
      break;
    }
    esp_http_client_close(c);
  }
  if (resp) *resp = sink.c_str();
#ifdef CLOUD_DEBUG
  String p = u.substring(u.indexOf(".app/") + 4, u.indexOf('?') > 0 ? u.indexOf('?') : u.length());
  Serial.printf("[DBG] %lu %s %s -> %d in %lu ms, heap %u (largest block %u)\n", (unsigned long)millis(), method,
                p.c_str(), code, (unsigned long)(millis() - t0), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#else
  (void)t0;
#endif
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
  // Sign-in happens about once an hour: don't keep that TLS session (~40 KB of heap) open, the
  // command stream needs the memory.
  esp_http_client_close(authClient);
  if (code == 200) {
    JsonDocument r;
    if (!deserializeJson(r, resp) && r["idToken"].is<const char*>()) {
      idToken = r["idToken"].as<String>();
      uint32_t exp = (uint32_t)atol(r["expiresIn"] | "3600");
      uint32_t useFor = exp > 600 ? exp - 300 : exp / 2;  // refresh 5 min before expiry
      tokenValidUntilMs = millis() + useFor * 1000UL;
      {
        Lock l;
        streamToken = idToken;
        tokenGen = tokenGen + 1;  // stream task reconnects with the new token
      }
      haveToken = true;
      authBackoffMs = 0;
      nextAuthAttemptMs = 0;
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
    streamRetryAtMs = millis() + 30000;
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

bool openStream(const String& token) {
#ifdef CLOUD_DEBUG
  Serial.printf("[DBG] %lu opening stream\n", (unsigned long)millis());
#endif
  if (!streamTls.connect(dbHost.c_str(), 443)) {
    Serial.printf("[ERROR] command stream: TLS connect failed (heap %u, largest block %u)\n", ESP.getFreeHeap(),
                  ESP.getMaxAllocHeap());
    return false;
  }
  streamTls.print(String("GET /commands.json?auth=") + token + " HTTP/1.1\r\nHost: " + dbHost +
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
  // RTDB sends a keep-alive about every 30 s. A read that waits longer than this means the
  // connection is dead: WiFiClientSecure then errors out and we reconnect.
  streamTls.setTimeout(CLOUD_STREAM_IDLE_MS / 1000);
  streaming = true;
  streamLastDataMs = millis();
  streamLine.reserve(512);
  Serial.println("[NET] listening for commands");
  return true;
}

// Own task: reading the stream blocks until data arrives (fine here, fatal in the cloud task).
void streamTask(void*) {
  uint8_t buf[512];
  for (;;) {
    // Only after NTP sync, so every command's age can be checked (no replays of old commands).
    if (WiFi.status() != WL_CONNECTED || !timeSynced() || !haveToken || tokenGen == 0 ||
        (int32_t)(millis() - streamRetryAtMs) < 0) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    uint32_t gen;
    String token;
    {
      Lock l;
      gen = tokenGen;
      token = streamToken;
    }
    if (!openStream(token)) {
      streamRetryAtMs = millis() + 5000;
      continue;
    }

    while (streaming && gen == tokenGen && WiFi.status() == WL_CONNECTED) {
      int n = streamTls.read(buf, sizeof(buf));  // blocks until data or timeout
      if (n <= 0) {
        if (!streamTls.connected()) {
          Serial.println("[NET] command stream closed, reconnecting");
          break;
        }
        if (millis() - streamLastDataMs > CLOUD_STREAM_IDLE_MS) {
          Serial.println("[NET] command stream idle, reconnecting");
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        continue;
      }
      streamLastDataMs = millis();
      for (int i = 0; i < n && streaming; i++) {
        char c = (char)buf[i];
        if (c == '\n') {
          processStreamLine();
          streamLine = "";
        } else if (c != '\r') {
          if (streamLine.length() >= 16384) {
            Serial.println("[ERROR] command stream line too long, reconnecting");
            streaming = false;
            break;
          }
          streamLine += c;
        }
      }
    }
    closeStream();
    if ((int32_t)(millis() - streamRetryAtMs) >= 0) streamRetryAtMs = millis() + 2000;
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
    if (WiFi.status() != WL_CONNECTED) {  // (the stream task notices WiFi loss on its own)
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

#ifdef CLOUD_DEBUG
#define TIMED(name, stmt)                                                                         \
  do {                                                                                            \
    uint32_t _t = millis();                                                                       \
    stmt;                                                                                         \
    if (millis() - _t > 1000) Serial.printf("[DBG] %s took %lu ms\n", name, (unsigned long)(millis() - _t)); \
  } while (0)
    static bool dnsTested = false;
    if (!dnsTested) {
      dnsTested = true;
      IPAddress ip;
      for (const char* h : {"identitytoolkit.googleapis.com", dbHost.c_str(), "pool.ntp.org"}) {
        uint32_t t = millis();
        bool ok = WiFi.hostByName(h, ip);
        Serial.printf("[DBG] DNS %s -> %s in %lu ms\n", h, ok ? ip.toString().c_str() : "FAIL",
                      (unsigned long)(millis() - t));
      }
      Serial.printf("[DBG] DNS server %s, time synced: %d\n", WiFi.dnsIP().toString().c_str(), timeSynced());
    }
#else
#define TIMED(name, stmt) stmt
#endif
    const uint32_t now = millis();
    if (now - lastFlushMs >= CLOUD_FLUSH_MS) {
      lastFlushMs = now;
      TIMED("flush", flushPending());
    }
    TIMED("statuses", flushStatuses());
    if (now - lastPollMs >= CLOUD_CONFIG_POLL_MS) {
      lastPollMs = now;
      TIMED("poll", pollConfigs());
    }
    TIMED("retention", retention());
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

  authClient = makeClient("https://identitytoolkit.googleapis.com/");
  dbClient = makeClient((dbUrl + "/").c_str());
  streamTls.setCACert(CA_BUNDLE);
  snprintf(bootTag, sizeof(bootTag), "%08lx", (unsigned long)esp_random());

  // Core 0 (with the WiFi stack); loop() keeps core 1 for ESP-NOW handling.
  xTaskCreatePinnedToCore(task, "cloud", 16384, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(streamTask, "cloudStream", 12288, nullptr, 1, nullptr, 0);
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
  // Stamp with the hub clock (DS3231/NTP) at the moment it happened, so events queued while
  // offline keep their real time. Server time only if the hub doesn't know the time yet.
  timeval tv;
  gettimeofday(&tv, nullptr);
  if (tile::epochValid(tv.tv_sec))
    e["ts"] = (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
  else
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
