#include "cloud.h"
#include "diag.h"

#include <WiFi.h>
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

// ---------------- shared state (guarded by mtx: loop() queues, the cloud task sends) ----------------
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
uint32_t logCounter = 0;
char bootTag[9];

// ---------------- cloud-task-only state ----------------
String dbUrl;  // https://host (no trailing slash)
String idToken;
bool haveToken = false;
uint32_t tokenIssuedMs = 0;
uint32_t tokenRefreshAtMs = 0;  // refresh from here on (5 min before expiry)
uint32_t tokenExpiresAtMs = 0;  // the old token still works until here if a refresh fails
uint32_t authBackoffMs = 0;
uint32_t nextAuthAttemptMs = 0;

// ONE secure connection at a time (fw 0.4.0). Up to 0.3.4 the hub kept two TLS connections (writes +
// /commands stream) and a third for the hourly sign-in; they didn't reliably fit in RAM, TLS setups failed
// for minutes (mbedtls -0x7F00), the sign-in failed, the token expired and writes were refused. Now:
//  - dbClient: one keep-alive connection for every database request (writes, polls, command statuses);
//  - authClient: only for the hourly sign-in, and dbClient is closed first.
// ESP-IDF clients, not Arduino's WiFiClientSecure/HTTPClient: those block for the full socket timeout in
// connected()/available() on an idle connection in core 2.0.x (stalled every write by ~10 s).
esp_http_client_handle_t authClient = nullptr;
esp_http_client_handle_t dbClient = nullptr;
// Self-repair: connection failures in a row (HTTP -1). At CLOUD_TLS_FAILS_RESET both clients are rebuilt,
// freeing whatever TLS state they hold. The hub restart stays the last resort (main.cpp).
uint8_t connFails = 0;
uint32_t lastClientResetMs = 0;
std::string* respSink = nullptr;

bool timeStarted = false;
uint32_t lastFlushMs = 0;
uint32_t lastSignalMs = 0;
uint32_t lastCmdFullMs = 0;
uint32_t lastCfgFullMs = 0;
bool cfgPolledOnce = false;
uint32_t lastRetentionMs = 0;
bool retentionDone = false;
int64_t seenCmdSignal = -1;  // /signal/commands last handled (-1 = not read yet)
uint32_t seenCfgSignal[MODULE_ID_COUNT] = {};  // /signal/config/<module> last downloaded

constexpr int HANDLED_RING = 16;
char handled[HANDLED_RING][40];
int handledNext = 0;

// ---------------- helpers ----------------
// True once the hub knows the time (DS3231 at boot, or NTP). Commands wait for it (age checks).
bool timeSynced() { return tile::epochValid(time(nullptr)); }
uint64_t epochMs() { return (uint64_t)time(nullptr) * 1000ULL; }

void setOnline(bool v) {
  Lock l;
  if (v && !online_) {
    session_ = session_ + 1;
    diag::printf("[NET] Firebase online\n");
  } else if (!v && online_) {
    diag::printf("[NET] Firebase offline\n");
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
#ifdef HUB_TEST_FAIL_REQUESTS_FROM_MS  // test build only: every request fails for 90 s (self-repair test)
  if (millis() > HUB_TEST_FAIL_REQUESTS_FROM_MS && millis() < HUB_TEST_FAIL_REQUESTS_FROM_MS + 90000) {
    if (connFails < 255) connFails++;
    return -1;
  }
#endif
#ifdef HUB_TEST_REJECT_TOKEN_FROM_MS  // test build only: from then on the token in use is refused (401), like a
  // revoked/expired one, until the hub signs in again
  if (c == dbClient && millis() > HUB_TEST_REJECT_TOKEN_FROM_MS && tokenIssuedMs < HUB_TEST_REJECT_TOKEN_FROM_MS) {
    if (resp) *resp = "{\"error\":\"Permission denied\"}";
    return 401;
  }
#endif
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
  if (code < 0) {
    if (connFails < 255) connFails++;
  } else {
    connFails = 0;
  }
#ifdef CLOUD_DEBUG
  String p = u.substring(u.indexOf(".app/") + 4, u.indexOf('?') > 0 ? u.indexOf('?') : u.length());
  diag::printf("[DBG] %lu %s %s -> %d in %lu ms, heap %u (largest block %u)\n", (unsigned long)millis(), method,
               p.c_str(), code, (unsigned long)(millis() - t0), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#else
  (void)t0;
#endif
  return code;
}

/** 401 on a token older than CLOUD_REAUTH_MIN_AGE_MS: most likely the token, not the data. */
bool tokenSuspect(int code) { return code == 401 && haveToken && millis() - tokenIssuedMs > CLOUD_REAUTH_MIN_AGE_MS; }

/**
 * A failed database request. Firebase answers an expired or unknown token with 401 "Permission denied", the
 * same words as a rule violation (fw <= 0.3.4 only looked for "expired": after a missed token refresh it
 * dropped every batch as "rejected" and still counted itself online). So:
 *  - network error / 5xx: offline;
 *  - 401 on an older token: sign in again (offline until that works). A fresh token getting 401 means the
 *    DATA was refused by a rule: no sign-in loop, and the connection itself is fine.
 * Returns true if the request is worth repeating.
 */
bool noteFailure(int code) {
  if (tokenSuspect(code)) {
    diag::printf("[NET] the database refused the login token (401): signing in again\n");
    haveToken = false;
    nextAuthAttemptMs = 0;
    setOnline(false);
    return true;
  }
  if (code < 0 || code >= 500) {
    setOnline(false);
    return true;
  }
  return false;
}

void queueStatus(const char* mkey, const char* id, const char* status) {
  Lock l;
  if (statusCount >= STATUS_RING) {
    diag::printf("[ERROR] command status queue full, dropping update\n");
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
  const uint32_t now = millis();
  if (haveToken && (int32_t)(now - tokenRefreshAtMs) < 0) return true;
  const bool oldStillValid = haveToken && (int32_t)(now - tokenExpiresAtMs) < 0;
  if (nextAuthAttemptMs && (int32_t)(now - nextAuthAttemptMs) < 0) return oldStillValid;

  // Keep ONE TLS connection at a time: close the database connection while signing in.
  esp_http_client_close(dbClient);
  JsonDocument req;
  req["email"] = HUB_EMAIL;
  req["password"] = HUB_PASSWORD;
  req["returnSecureToken"] = true;
  String body, resp;
  serializeJson(req, body);
  int code = request("POST", String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") +
                                 FIREBASE_API_KEY,
                     body, &resp);
  esp_http_client_close(authClient);
  if (code == 200) {
    JsonDocument r;
    if (!deserializeJson(r, resp) && r["idToken"].is<const char*>()) {
      idToken = r["idToken"].as<String>();
      uint32_t exp = (uint32_t)atol(r["expiresIn"] | "3600");
      tokenIssuedMs = millis();
      tokenExpiresAtMs = tokenIssuedMs + (exp > 60 ? exp - 60 : exp) * 1000UL;
      tokenRefreshAtMs = tokenIssuedMs + (exp > 600 ? exp - 300 : exp / 2) * 1000UL;
      haveToken = true;
      authBackoffMs = 0;
      nextAuthAttemptMs = 0;
      diag::printf("[NET] Firebase signed in as hub\n");
      return true;
    }
  }
  diag::printf("[ERROR] Firebase sign-in failed (HTTP %d) %s\n", code, resp.substring(0, 160).c_str());
  authBackoffMs = authBackoffMs ? min<uint32_t>(authBackoffMs * 2, 60000) : 5000;
  nextAuthAttemptMs = millis() + authBackoffMs;
  if (oldStillValid) return true;  // a failed early refresh: the current token still works for a while
  haveToken = false;
  setOnline(false);
  return false;
}

// ---------------- batched writes ----------------
void requeue(const JsonDocument& snap) {
  Lock l;
  for (JsonPairConst kv : snap.as<JsonObjectConst>()) {
    String k = kv.key().c_str();
    if (pending[k].isNull()) pending[k] = kv.value();  // newer values win
  }
}

/**
 * The database refused a batch because of its DATA (400, or 401/403 with a fresh token): send each path
 * alone, drop only the refused ones and name them in the log. One bad value never costs the whole batch.
 */
void isolateBatch(const JsonDocument& snap, int batchCode) {
  diag::printf("[ERROR] Firebase refused a batch (HTTP %d): checking each path\n", batchCode);
  bool reached = false;
  JsonDocument rest;
  bool stopped = false;
  for (JsonPairConst kv : snap.as<JsonObjectConst>()) {
    if (stopped) {
      rest[String(kv.key().c_str())] = kv.value();
      continue;
    }
    JsonDocument one;
    one[String(kv.key().c_str())] = kv.value();
    String body, resp;
    serializeJson(one, body);
    const int code = request("PATCH", dbUrl + "/.json?print=silent&auth=" + idToken, body, &resp);
    if (code == 200 || code == 204) {
      reached = true;
    } else if (code < 0 || code >= 500) {
      stopped = true;  // network trouble: keep this and the rest for later
      rest[String(kv.key().c_str())] = kv.value();
    } else {
      reached = true;
      diag::printf("[ERROR] Firebase refused %s (HTTP %d), dropped: %s\n", kv.key().c_str(), code,
                   resp.substring(0, 80).c_str());
    }
  }
  if (rest.as<JsonObjectConst>().size()) requeue(rest);
  if (reached) setOnline(true);
}

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
  const int code = request("PATCH", dbUrl + "/.json?print=silent&auth=" + idToken, body, &resp);
  if (code == 200 || code == 204) {
    setOnline(true);
    return;
  }
  if (noteFailure(code)) {  // network, or the login token: keep everything, send again (after a new sign-in)
    requeue(snap);
    diag::printf("[ERROR] Firebase write will retry (HTTP %d)\n", code);
    return;
  }
  isolateBatch(snap, code);  // the data itself was refused
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
  const bool transient = !ok && noteFailure(code);
  Lock l;
  if (ok || !transient || statusRing[statusHead].tries >= 5) {
    if (!ok) diag::printf("[ERROR] command %s status '%s' not written (HTTP %d)\n", u.id, u.status, code);
    statusHead = (statusHead + 1) % STATUS_RING;
    statusCount--;
  } else {
    statusRing[statusHead].tries++;
  }
}

// ---------------- configs ----------------
/** Downloads one module's config (after its version changed). false = not reached, try again. */
bool fetchConfig(uint8_t i) {
  const ModuleId id = (ModuleId)i;
  String resp;
  const int code = request("GET", url(String("modules/") + moduleKey(id) + "/config"), "", &resp);
  if (code != 200) {
    noteFailure(code);
    return false;
  }
  setOnline(true);
  resp.trim();
  if (resp == "null") {
    Lock l;
    desired[i].valid = false;
    return true;
  }
  JsonDocument doc;
  if (deserializeJson(doc, resp) || !doc.is<JsonObject>()) {
    diag::printf("[ERROR] %s config is not valid JSON\n", moduleKey(id));
    return true;
  }
  DesiredConfig d;
  size_t len = 0;
  if (!codec::configFromJson(id, doc.as<JsonObjectConst>(), d.bin, len)) return true;
  d.valid = true;
  d.version = doc["version"] | 0u;
  d.len = (uint8_t)len;
  {
    Lock l;
    desired[i] = d;
  }
  diag::printf("[NET] %s config v%u loaded from Firebase\n", moduleKey(id), (unsigned)d.version);
  return true;
}

bool configIsCurrent(uint8_t i, uint32_t v) {
  Lock l;
  return desired[i].valid && desired[i].version == v;
}

/** Fallback (every CLOUD_CONFIG_POLL_MS): read each version directly, in case a signal was missed. */
void pollConfigVersions() {
  for (uint8_t i = 1; i < MODULE_ID_COUNT; i++) {
    String resp;
    const int code =
        request("GET", url(String("modules/") + moduleKey((ModuleId)i) + "/config/version"), "", &resp);
    if (code != 200) {
      noteFailure(code);
      return;
    }
    setOnline(true);
    resp.trim();
    if (resp == "null") {
      Lock l;
      desired[i].valid = false;
      continue;
    }
    if (!configIsCurrent(i, strtoul(resp.c_str(), nullptr, 10))) fetchConfig(i);
  }
}

// ---------------- commands ----------------
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
        diag::printf("[NET] command %s/%s %s is %us old -> expired\n", mkey.c_str(), id.c_str(), type,
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

/** Reads the whole /commands tree ({module: {id: command}}) and handles every command in it.
 *  false = not reached (or the time isn't known yet): try again. */
bool pollCommands() {
  if (!timeSynced()) return false;  // the age check needs the time
  String resp;
  const int code = request("GET", url("commands"), "", &resp);
  if (code != 200) {
    noteFailure(code);
    return false;
  }
  setOnline(true);
  JsonDocument doc;
  if (deserializeJson(doc, resp) || !doc.is<JsonObject>()) return true;
  for (JsonPairConst mod : doc.as<JsonObjectConst>()) {
    if (!mod.value().is<JsonObjectConst>()) continue;
    const String mkey = mod.key().c_str();
    for (JsonPairConst cmd : mod.value().as<JsonObjectConst>()) handleCommand(mkey, cmd.key().c_str(), cmd.value());
  }
  return true;
}

/**
 * /signal (written by the web in the same multi-path update as the change): {commands: n,
 * config: {shredder: v, ...}}. Read every second; a few bytes. A change triggers the real download.
 */
void pollSignal() {
  String resp;
  const int code = request("GET", url("signal"), "", &resp);
  if (code != 200) {
    noteFailure(code);
    return;
  }
  setOnline(true);
  JsonDocument doc;
  if (deserializeJson(doc, resp)) return;
  const int64_t cmds = doc["commands"] | (int64_t)0;
  // A signal counts as seen only once its download worked (else: again next second).
  if (cmds != seenCmdSignal && pollCommands()) {
    seenCmdSignal = cmds;
    lastCmdFullMs = millis();
  }
  JsonObjectConst cfg = doc["config"];
  for (uint8_t i = 1; i < MODULE_ID_COUNT; i++) {
    JsonVariantConst v = cfg[moduleKey((ModuleId)i)];
    if (!v.is<uint32_t>()) continue;
    const uint32_t ver = v.as<uint32_t>();
    // seenCfgSignal: a config the hub can't use (bad JSON, unknown fields) isn't downloaded again every second.
    if (ver != seenCfgSignal[i] && !configIsCurrent(i, ver) && fetchConfig(i)) seenCfgSignal[i] = ver;
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
    diag::printf("[ERROR] event retention query failed (HTTP %d)\n", code);
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, resp) || !doc.is<JsonObject>()) return;
  int n = 0;
  for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
    setNull((String("events/") + kv.key().c_str()).c_str());
    n++;
  }
  if (n) diag::printf("[NET] deleting %d events older than 30 days\n", n);

  // Hub log (/hubLog): 7 days.
  snprintf(q, sizeof(q), "&orderBy=%%22ts%%22&endAt=%llu&limitToFirst=100",
           (unsigned long long)(epochMs() - CLOUD_HUBLOG_KEEP_MS));
  code = request("GET", url("hubLog") + q, "", &resp);
  if (code != 200) return;
  JsonDocument logs;
  if (deserializeJson(logs, resp) || !logs.is<JsonObject>()) return;
  for (JsonPairConst kv : logs.as<JsonObjectConst>()) setNull((String("hubLog/") + kv.key().c_str()).c_str());
}

void resetClients() {
  diag::printf("[NET] %u connection failures in a row: rebuilding the HTTP clients (heap %u, largest block %u)\n",
               connFails, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  esp_http_client_cleanup(authClient);
  esp_http_client_cleanup(dbClient);
  authClient = makeClient("https://identitytoolkit.googleapis.com/");
  dbClient = makeClient((dbUrl + "/").c_str());
  connFails = 0;
  lastClientResetMs = millis();
  nextAuthAttemptMs = 0;  // try again right away with the fresh clients
}

void task(void*) {
  for (;;) {
    if (connFails >= CLOUD_TLS_FAILS_RESET && WiFi.status() == WL_CONNECTED &&
        (!lastClientResetMs || millis() - lastClientResetMs >= CLOUD_CLIENT_RESET_GAP_MS))
      resetClients();
    if (WiFi.status() != WL_CONNECTED) {
      setOnline(false);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (!timeStarted) {
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      timeStarted = true;
    }
    diag::phase(diag::Task::Cloud, "auth");
    if (!ensureAuth()) {
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    const uint32_t now = millis();
    if (now - lastFlushMs >= CLOUD_FLUSH_MS) {
      lastFlushMs = now;
      diag::phase(diag::Task::Cloud, "flush");
      flushPending();
    }
    diag::phase(diag::Task::Cloud, "statuses");
    flushStatuses();
    if (now - lastSignalMs >= CLOUD_SIGNAL_POLL_MS) {
      lastSignalMs = now;
      diag::phase(diag::Task::Cloud, "signal");
      pollSignal();
    }
    if (now - lastCmdFullMs >= CLOUD_CMD_FULL_POLL_MS) {  // backstop for a missed signal + expiring old ones
      lastCmdFullMs = now;
      diag::phase(diag::Task::Cloud, "commands");
      pollCommands();
    }
    if (!cfgPolledOnce || now - lastCfgFullMs >= CLOUD_CONFIG_POLL_MS) {  // right away after boot, then fallback
      cfgPolledOnce = true;
      lastCfgFullMs = now;
      diag::phase(diag::Task::Cloud, "config-poll");
      pollConfigVersions();
    }
    diag::phase(diag::Task::Cloud, "retention");
    retention();
    diag::phase(diag::Task::Cloud, "idle");
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

  authClient = makeClient("https://identitytoolkit.googleapis.com/");
  dbClient = makeClient((dbUrl + "/").c_str());
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
    diag::printf("[ERROR] cloud backlog full, event %s dropped\n", code);
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

void pushLog(char lvl, const char* msg) {
  Lock l;
  if (measureJson(pending) > CLOUD_PENDING_MAX_BYTES) return;  // offline backlog full: keep events, drop logs
  char key[40];
  snprintf(key, sizeof(key), "hubLog/h%s_%06lu", bootTag, (unsigned long)logCounter++);
  JsonObject e = pending[String(key)].to<JsonObject>();
  timeval tv;
  gettimeofday(&tv, nullptr);
  if (tile::epochValid(tv.tv_sec))
    e["ts"] = (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
  else
    e["ts"][".sv"] = "timestamp";
  // String(): ArduinoJson keeps only a POINTER to char arrays (it takes them for literals). A local
  // array here was garbage by the time of the flush, and the whole batch was refused (HTTP 400).
  e["lvl"] = String(lvl);
  e["msg"] = String(msg);
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
