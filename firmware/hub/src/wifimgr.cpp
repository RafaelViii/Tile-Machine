#include "wifimgr.h"
#include "diag.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "portal_page.h"
#include "secrets.h"

#ifndef PORTAL_PASSWORD
#error "Add PORTAL_PASSWORD (8 to 63 characters) to include/secrets.h, see secrets.example.h"
#endif

namespace net {
namespace {

// ---------------- saved networks (NVS "wifinets"), index 0 = last one that worked ----------------
struct Net {
  String ssid;
  String pass;
};
Net nets[WIFI_MAX_SAVED];
uint8_t netCount = 0;
uint8_t lastChannel = 1;  // router channel last time we were connected (hotspot uses it while offline)
uint8_t lastBssid[6] = {0};
bool haveBssid = false;

void saveNets() {
  Preferences p;
  p.begin("wifinets", false);
  p.clear();
  p.putUChar("n", netCount);
  for (uint8_t i = 0; i < netCount; i++) {
    p.putString((String("s") + i).c_str(), nets[i].ssid);
    p.putString((String("p") + i).c_str(), nets[i].pass);
  }
  p.putUChar("ch", lastChannel);
  p.end();
}

void loadNets() {
  Preferences p;
  p.begin("wifinets", true);
  netCount = min<uint8_t>(p.getUChar("n", 0), WIFI_MAX_SAVED);
  for (uint8_t i = 0; i < netCount; i++) {
    nets[i].ssid = p.getString((String("s") + i).c_str(), "");
    nets[i].pass = p.getString((String("p") + i).c_str(), "");
  }
  lastChannel = p.getUChar("ch", 1);
  p.end();
  if (lastChannel < 1 || lastChannel > 13) lastChannel = 1;
  if (netCount == 0 && strlen(WIFI_SSID) > 0 && strcmp(WIFI_SSID, "your-wifi-name") != 0) {
    nets[0] = {WIFI_SSID, WIFI_PASSWORD};  // first boot: start from the network in secrets.h
    netCount = 1;
    saveNets();
  }
}

int findNet(const String& ssid) {
  for (uint8_t i = 0; i < netCount; i++)
    if (nets[i].ssid == ssid) return i;
  return -1;
}

/** Put a network first (it worked) or last (saved without testing). Oldest entry drops off when full. */
void remember(const String& ssid, const String& pass, bool first) {
  const int at = findNet(ssid);
  if (first && at == 0 && nets[0].pass == pass) return;  // already first: no flash write
  if (at >= 0) {
    for (uint8_t i = at; i + 1 < netCount; i++) nets[i] = nets[i + 1];
    netCount--;
  }
  if (netCount == WIFI_MAX_SAVED) netCount--;
  if (first) {
    for (int i = netCount; i > 0; i--) nets[i] = nets[i - 1];
    nets[0] = {ssid, pass};
  } else {
    nets[netCount] = {ssid, pass};
  }
  netCount++;
  saveNets();
}

void forget(const String& ssid) {
  const int at = findNet(ssid);
  if (at < 0) return;
  for (uint8_t i = at; i + 1 < netCount; i++) nets[i] = nets[i + 1];
  netCount--;
  saveNets();
}

// ---------------- connection ----------------
uint32_t downSinceMs = 0;  // 0 = connected
uint32_t nextTryMs = 0;
uint8_t tryIdx = 0;
volatile uint8_t lastReason = 0;
uint32_t lastReasonLogMs = 0;

// A network typed into the setup page, being tested.
struct Trial {
  bool active = false;
  bool dropped = false;  // the previous connection went down (so "connected" really is the new one)
  String ssid, pass;
  uint32_t startMs = 0;
} trial;
enum class TrialState : uint8_t { Idle, Trying, Ok, Failed };
TrialState trialState = TrialState::Idle;
String trialSsid;
uint8_t trialReason = 0;

void startAttempt(const String& ssid, const String& pass, bool pinned) {
  WiFi.disconnect(false, false);
  const char* pw = pass.length() ? pass.c_str() : nullptr;
  if (pinned && haveBssid) {
    WiFi.begin(ssid.c_str(), pw, lastChannel, lastBssid);  // this channel only: the hotspot stays put
  } else {
    WiFi.begin(ssid.c_str(), pw);
  }
}

void onEvent(arduino_event_id_t event, arduino_event_info_t info);

// ---------------- setup hotspot ----------------
DNSServer dns;
WebServer server(80);
bool apOn = false;
PortalReason reason = PortalReason::None;
uint32_t lastActivityMs = 0;
uint32_t offlineSinceMs = 0;
uint32_t cloudPortalCooldownUntilMs = 0;  // after an unused "cloud offline" hotspot closed
String apSsid;
bool routesAdded = false;

void touchActivity() { lastActivityMs = millis(); }

const char* reasonName(PortalReason r) {
  switch (r) {
    case PortalReason::Boot: return "boot";
    case PortalReason::Button: return "button";
    case PortalReason::Offline: return "offline";
    default: return "none";
  }
}

void sendJson(JsonDocument& d, int code = 200) {
  String out;
  serializeJson(d, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", out);
}

void handleStatus() {
  touchActivity();
  JsonDocument d;
  char fw[12];
  tile::fwDecode(HUB_FW, fw, sizeof(fw));
  d["fw"] = String(fw);
  d["hotspot"] = apSsid;
  d["reason"] = reasonName(reason);
  if (reason != PortalReason::Offline || WiFi.status() == WL_CONNECTED) {
    const uint32_t idle = millis() - lastActivityMs;
    d["closesInS"] = idle >= PORTAL_IDLE_CLOSE_MS ? 0 : (PORTAL_IDLE_CLOSE_MS - idle) / 1000;
  }
  const bool up = WiFi.status() == WL_CONNECTED;
  d["connected"] = up;
  if (up) {
    d["ssid"] = WiFi.SSID();
    d["ip"] = WiFi.localIP().toString();
    d["rssi"] = WiFi.RSSI();
    d["channel"] = WiFi.channel();
  } else if (downSinceMs) {
    d["downS"] = (millis() - downSinceMs) / 1000;
  }
  d["online"] = up && offlineSinceMs == 0;
  JsonArray saved = d["saved"].to<JsonArray>();
  for (uint8_t i = 0; i < netCount; i++) saved.add(nets[i].ssid);
  JsonObject t = d["trial"].to<JsonObject>();
  t["state"] = trialState == TrialState::Trying ? "trying"
               : trialState == TrialState::Ok   ? "ok"
               : trialState == TrialState::Failed ? "failed"
                                                  : "idle";
  t["ssid"] = trialSsid;
  if (trialState == TrialState::Failed) t["why"] = reasonText(trialReason);
  sendJson(d);
}

uint32_t scanStartedMs = 0;
bool scanPending = false;  // started, results not read yet

void handleScanStart() {
  touchActivity();
  if (!trial.active && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
    WiFi.scanDelete();
    WiFi.scanNetworks(true, false, false, 120);  // async, 120 ms per channel
    scanStartedMs = millis();
    scanPending = true;
  }
  JsonDocument d;
  d["scanning"] = true;
  sendJson(d);
}

void handleScanResult() {
  touchActivity();
  JsonDocument d;
  const int n = WiFi.scanComplete();
  // The driver clears its "scanning" flag a moment before the results are readable, so a scan we
  // started counts as running until it has results (or 8 s have passed: then there really are none).
  const bool waiting = scanPending && n < 1 && millis() - scanStartedMs < 8000;
  if (!waiting && n != WIFI_SCAN_RUNNING) scanPending = false;
  d["scanning"] = n == WIFI_SCAN_RUNNING || waiting;
  JsonArray list = d["networks"].to<JsonArray>();
  // Strongest entry per SSID, hidden networks skipped.
  for (int i = 0; !waiting && i < n; i++) {
    const String s = WiFi.SSID(i);
    if (s.isEmpty()) continue;
    bool dup = false;
    for (int j = 0; j < i; j++)
      if (WiFi.SSID(j) == s && WiFi.RSSI(j) >= WiFi.RSSI(i)) dup = true;
    if (dup) continue;
    for (int j = i + 1; j < n; j++)
      if (WiFi.SSID(j) == s && WiFi.RSSI(j) > WiFi.RSSI(i)) dup = true;
    if (dup) continue;
    JsonObject o = list.add<JsonObject>();
    o["ssid"] = s;
    o["rssi"] = WiFi.RSSI(i);
    o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    o["saved"] = findNet(s) >= 0;
  }
  sendJson(d);
}

bool readCreds(String& ssid, String& pass) {
  ssid = server.arg("ssid");
  pass = server.arg("pass");
  if (ssid.length() < 1 || ssid.length() > 32) return false;
  if (pass.length() != 0 && (pass.length() < 8 || pass.length() > 63)) return false;
  return true;
}

void badRequest(const char* msg) {
  JsonDocument d;
  d["error"] = msg;
  sendJson(d, 400);
}

void handleConnect() {
  touchActivity();
  String ssid, pass;
  if (!readCreds(ssid, pass)) return badRequest("Network name 1-32 characters; password empty or 8-63 characters");
  // An empty password for a saved network means "use the saved one".
  const int at = findNet(ssid);
  if (pass.isEmpty() && at >= 0) pass = nets[at].pass;
  trial.active = true;
  trial.ssid = ssid;
  trial.pass = pass;
  trial.startMs = millis();
  trial.dropped = false;
  trialState = TrialState::Trying;
  trialSsid = ssid;
  lastReason = 0;
  diag::printf("[NET] setup page: trying '%s'\n", ssid.c_str());
  startAttempt(ssid, pass, false);
  JsonDocument d;
  d["ok"] = true;
  sendJson(d);
}

void handleSave() {
  touchActivity();
  String ssid, pass;
  if (!readCreds(ssid, pass)) return badRequest("Network name 1-32 characters; password empty or 8-63 characters");
  remember(ssid, pass, false);
  diag::printf("[NET] setup page: saved '%s' (not tested)\n", ssid.c_str());
  JsonDocument d;
  d["ok"] = true;
  sendJson(d);
}

void handleForget() {
  touchActivity();
  const String ssid = server.arg("ssid");
  forget(ssid);
  diag::printf("[NET] setup page: forgot '%s'\n", ssid.c_str());
  JsonDocument d;
  d["ok"] = true;
  sendJson(d);
}

void handlePage() {
  touchActivity();
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", PORTAL_PAGE);
}

/** Phones probe a known URL after joining; answering with a redirect makes them open the setup page. */
void handleNotFound() {
  touchActivity();
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

void startAp() {
  const uint8_t ch = WiFi.status() == WL_CONNECTED ? WiFi.channel() : lastChannel;
  WiFi.softAP(apSsid.c_str(), PORTAL_PASSWORD, ch, 0, 4);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  server.begin();
  apOn = true;
  diag::printf("[NET] setup hotspot '%s' open on channel %u (http://%s)\n", apSsid.c_str(), ch,
                WiFi.softAPIP().toString().c_str());
}

void stopAp() {
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);  // back to station-only; the router connection and ESP-NOW stay up
  apOn = false;
  reason = PortalReason::None;
  if (WiFi.scanComplete() >= 0) WiFi.scanDelete();
  diag::printf("[NET] setup hotspot closed\n");
}

void serviceWifi(uint32_t now) {
  if (WiFi.status() == WL_CONNECTED) {
    if (downSinceMs) {
      diag::printf("[NET] WiFi '%s' up after %lu s\n", WiFi.SSID().c_str(), (unsigned long)((now - downSinceMs) / 1000));
      downSinceMs = 0;
    }
    if (trial.active && trial.dropped && WiFi.SSID() == trial.ssid) {
      remember(trial.ssid, trial.pass, true);
      trial.active = false;
      trialState = TrialState::Ok;
      diag::printf("[NET] setup page: '%s' works, saved as first choice\n", trial.ssid.c_str());
    } else if (!trial.active) {
      const int at = findNet(WiFi.SSID());
      if (at > 0) remember(nets[at].ssid, nets[at].pass, true);  // the one that works goes first
    }
    return;
  }

  if (!downSinceMs) {
    downSinceMs = now;
    nextTryMs = now + WIFI_FIRST_RETRY_MS;  // brief dropouts come back fast
  }
  if (trial.active) {
    trial.dropped = true;
    if (now - trial.startMs < WIFI_TRIAL_TIMEOUT_MS) return;
    trial.active = false;
    trialState = TrialState::Failed;
    trialReason = lastReason;
    diag::printf("[NET] setup page: '%s' failed (%s), back to saved networks\n", trial.ssid.c_str(),
                  reasonText(trialReason));
    nextTryMs = now;
  }
  if ((int32_t)(now - nextTryMs) < 0 || netCount == 0) return;
  nextTryMs = now + WIFI_RETRY_EVERY_MS;

  // While a phone is on the hotspot, only retry the last network on its own channel: scanning other
  // channels would move the hotspot and drop the phone.
  const bool pinned = apOn && WiFi.softAPgetStationNum() > 0;
  const Net& n = pinned ? nets[0] : nets[tryIdx++ % netCount];
  diag::printf("[NET] WiFi down %lu s, trying '%s'%s\n", (unsigned long)((now - downSinceMs) / 1000),
                n.ssid.c_str(), pinned ? " (this channel only, phone on hotspot)" : "");
  startAttempt(n.ssid, n.pass, pinned);
}

void servicePortal(uint32_t now, bool cloudOnline) {
  const bool wifiUp = WiFi.status() == WL_CONNECTED;
  const bool offline = !wifiUp || !cloudOnline;
  if (offline) {
    if (!offlineSinceMs) offlineSinceMs = now;
    // WiFi up but no cloud: after an unused hotspot closed, give the cloud the memory for a while.
    const bool cooling = wifiUp && (int32_t)(now - cloudPortalCooldownUntilMs) < 0;
    if (now - offlineSinceMs >= PORTAL_OFFLINE_AFTER_MS && reason != PortalReason::Offline && !cooling) {
      diag::printf("[NET] offline for %lu s: opening the setup hotspot until back online\n",
                    (unsigned long)(PORTAL_OFFLINE_AFTER_MS / 1000));
      openPortal(PortalReason::Offline);
    }
  } else if (offlineSinceMs) {
    offlineSinceMs = 0;
    if (reason == PortalReason::Offline) {
      reason = PortalReason::Button;  // back online: the normal 3-minute idle timer takes over
      touchActivity();
      diag::printf("[NET] back online: setup hotspot closes after %lu s without use\n",
                    (unsigned long)(PORTAL_IDLE_CLOSE_MS / 1000));
    }
  }

  if (!apOn) return;
  dns.processNextRequest();
  server.handleClient();
  // Fresh millis(): a request handled just above may have set lastActivityMs after `now` was taken.
  const uint32_t idle = millis() - lastActivityMs;
  // "Stays open while offline" only while WiFi itself is down. With WiFi up, the hotspot (~46 KB) competes
  // with the TLS connection that would bring the cloud back: fw 0.3.0 kept both and, with a fragmented heap
  // after hours, TLS setup failed (-0x7F00) and the hub stayed offline for 5 h. So it closes when unused.
  const bool holdOpen = reason == PortalReason::Offline && !wifiUp;
  if (!holdOpen && !trial.active && idle >= PORTAL_IDLE_CLOSE_MS) {
    if (reason == PortalReason::Offline) cloudPortalCooldownUntilMs = millis() + PORTAL_CLOUD_COOLDOWN_MS;
    stopAp();
  }
}

void onEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      lastChannel = info.wifi_sta_connected.channel;
      memcpy(lastBssid, info.wifi_sta_connected.bssid, 6);
      haveBssid = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      lastReason = info.wifi_sta_disconnected.reason;
      if (millis() - lastReasonLogMs < 5000) break;  // don't flood the log while retrying
      lastReasonLogMs = millis();
      Serial.printf("\n[ERROR] WiFi disconnected, reason %u: %s\n", lastReason, reasonText(lastReason));
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      touchActivity();
      diag::printf("[NET] a phone joined the setup hotspot\n");
      break;
    default:
      break;
  }
}

}  // namespace

const char* reasonText(uint8_t r) {
  switch (r) {
    case 0: return "no answer from the network";
    case 2: return "auth expired";
    case 8: return "left network";
    case 15: return "wrong password (4-way handshake timeout)";
    case 200: return "signal lost (beacon timeout)";
    case 201: return "network not found (2.4 GHz only, name is case-sensitive)";
    case 202: return "wrong password (authentication failed)";
    case 203: return "association failed";
    case 204: return "wrong password (handshake timeout)";
  }
  return "see esp_wifi_types.h";
}

void begin() {
  loadNets();
  WiFi.onEvent(onEvent);
  WiFi.persistent(false);         // networks live in our own NVS list, not the WiFi driver's
  WiFi.setAutoReconnect(false);   // we reconnect ourselves (see serviceWifi)
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char name[20];
  snprintf(name, sizeof(name), "TileHub-%02X%02X", mac[4], mac[5]);
  apSsid = name;
  diag::printf("[NET] %u saved network(s):", netCount);
  for (uint8_t i = 0; i < netCount; i++) Serial.printf(" '%s'", nets[i].ssid.c_str());
  Serial.println();
  if (netCount) startAttempt(nets[0].ssid, nets[0].pass, false);
  tryIdx = 1;
  downSinceMs = millis();
  nextTryMs = millis() + WIFI_RETRY_EVERY_MS;
  openPortal(PortalReason::Boot);
}

void loop(bool cloudOnline) {
  const uint32_t now = millis();
  serviceWifi(now);
  servicePortal(now, cloudOnline);
}

bool connected() { return WiFi.status() == WL_CONNECTED; }
bool portalOpen() { return apOn; }
bool portalInUse() { return apOn && WiFi.softAPgetStationNum() > 0; }
String currentSsid() { return WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String(); }

void openPortal(PortalReason why) {
  if (!routesAdded) {
    routesAdded = true;
    server.on("/", HTTP_GET, handlePage);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/scan", HTTP_POST, handleScanStart);
    server.on("/api/scan", HTTP_GET, handleScanResult);
    server.on("/api/connect", HTTP_POST, handleConnect);
    server.on("/api/save", HTTP_POST, handleSave);
    server.on("/api/forget", HTTP_POST, handleForget);
    server.onNotFound(handleNotFound);
  }
  if (!apOn) startAp();
  if (reason != PortalReason::Offline) reason = why;
  touchActivity();
}

}  // namespace net
