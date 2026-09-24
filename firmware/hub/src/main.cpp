// Tile Machine — Main Hub (esp0). Spec: docs/modules/hub.md
//
// loop() (core 1): ESP-NOW pairing, presence, config push, command routing. Never blocks.
// cloud task (core 0): Firebase REST/stream (see cloud.h).

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <lwip/dns.h>
#include <sys/time.h>

#include <EspNowTransport.h>
#include <Reliable.h>
#include <TileProtocol.h>
#include <TileTime.h>

#include "cloud.h"
#include "codec.h"
#include "config.h"
#include "pins.h"
#include "rtc.h"
#include "secrets.h"

using namespace tile;

namespace {

EspNowTransport transport;
ReliableSender reliable(transport);
DedupCache dedup;
uint16_t seq = 0;

// ---------------- module registry ----------------
struct ModuleEntry {
  bool known = false;
  uint8_t mac[6] = {0};
  uint16_t fw = 0;
  bool online = false;
  uint32_t lastRxMs = 0;
  uint32_t appliedVersion = 0;  // config version the module reports as applied
  bool appliedKnown = false;
  uint8_t status[MAX_FRAME];
  uint8_t statusLen = 0;
  bool statusDirty = false;
  bool infoDirty = false;
  bool presenceDirty = false;
  bool sizeWarned = false;
  uint32_t lastConfigSendMs = 0;
  uint32_t rejectedVersion = UINT32_MAX;
  uint32_t queuedVersion = UINT32_MAX;
  uint32_t reportedAppliedVersion = UINT32_MAX;  // last configApplied written to Firebase
};

ModuleEntry mods[MODULE_ID_COUNT];  // index = ModuleId (0 unused)
bool timeDue[MODULE_ID_COUNT] = {false};  // send TIME to this module on the next loop

// ---------------- clock (DS3231 RTC + NTP) ----------------
enum class TimeSource : uint8_t { NONE, RTC, NTP };
TimeSource timeSource = TimeSource::NONE;
rtc::Status rtcStatus = rtc::Status::MISSING;
volatile bool ntpSynced = false;  // set by the SNTP callback (lwIP task), handled in loop()
bool clockDirty = false;          // hub/timeSource + hub/rtc need writing
uint32_t lastTimeBroadcastMs = 0;

const char* timeSourceName() {
  switch (timeSource) {
    case TimeSource::RTC: return "rtc";
    case TimeSource::NTP: return "ntp";
    default: return "none";
  }
}

void onNtpSync(struct timeval*) { ntpSynced = true; }

bool clockValid() { return epochValid(time(nullptr)); }

void logClock(const char* prefix) {
  char s[24];
  formatLocalTime(time(nullptr), LOCAL_TZ_OFFSET_MIN, s, sizeof(s));
  Serial.printf("[STATE] %s %s (UTC%+d), source %s, RTC %s\n", prefix, s, LOCAL_TZ_OFFSET_MIN / 60, timeSourceName(),
                rtc::statusName(rtcStatus));
}

/** Boot: take the time from the DS3231 so it's right before WiFi/NTP (or with no internet at all). */
void initClock() {
  uint32_t epoch = 0;
  rtcStatus = rtc::begin(epoch);
  if (rtcStatus == rtc::Status::OK) {
    timeval tv = {(time_t)epoch, 0};
    settimeofday(&tv, nullptr);
    timeSource = TimeSource::RTC;
    logClock("clock from RTC:");
    Serial.printf("[STATE] RTC temperature %.2f C\n", rtc::temperature());
  } else if (rtcStatus == rtc::Status::LOST_POWER) {
    Serial.println("[STATE] RTC found but its time is not set (new/removed battery): waiting for internet time");
  } else {
    Serial.println("[ERROR] DS3231 RTC not found on I2C (SDA 21 / SCL 22): time only from internet");
  }
  sntp_set_time_sync_notification_cb(onNtpSync);  // cloud task starts SNTP via configTime()
}

/** After every NTP sync: trust NTP, and correct the RTC if it's off or was never set. */
void serviceClock() {
  if (!ntpSynced) return;
  ntpSynced = false;
  const uint32_t now = time(nullptr);
  if (timeSource != TimeSource::NTP) clockDirty = true;
  timeSource = TimeSource::NTP;

  if (rtcStatus != rtc::Status::MISSING) {
    uint32_t r = 0;
    const bool ok = rtc::read(r);
    const uint32_t drift = ok ? (r > now ? r - now : now - r) : UINT32_MAX;
    if (!ok || drift > RTC_MAX_DRIFT_S) {
      if (rtc::write(now)) {
        if (ok) Serial.printf("[STATE] RTC corrected by %lu s\n", (unsigned long)drift);
        else Serial.println("[STATE] RTC set from internet time");
        if (rtcStatus != rtc::Status::OK) clockDirty = true;
        rtcStatus = rtc::Status::OK;
      } else {
        Serial.println("[ERROR] writing the RTC failed");
      }
    }
  }
  logClock("internet time synced:");
  for (uint8_t id = 1; id < MODULE_ID_COUNT; id++) timeDue[id] = true;  // push the corrected time
}

constexpr const char* REG_NS = "hubreg";

void saveEntry(uint8_t id) {
  Preferences p;
  p.begin(REG_NS, false);
  char k[4] = {'m', char('0' + id), 0};
  char f[4] = {'f', char('0' + id), 0};
  p.putBytes(k, mods[id].mac, 6);
  p.putUShort(f, mods[id].fw);
  p.end();
}

void loadRegistry() {
  Preferences p;
  p.begin(REG_NS, false);  // read-write so a fresh board creates the namespace instead of logging an error
  for (uint8_t id = 1; id < MODULE_ID_COUNT; id++) {
    char k[4] = {'m', char('0' + id), 0};
    char f[4] = {'f', char('0' + id), 0};
    if (p.isKey(k) && p.getBytes(k, mods[id].mac, 6) == 6) {
      mods[id].known = true;
      mods[id].fw = p.getUShort(f, 0);
      transport.ensurePeer(mods[id].mac);
      char m[18];
      macToStr(mods[id].mac, m, sizeof(m));
      Serial.printf("[NET] known module %s at %s\n", moduleKey((ModuleId)id), m);
    }
  }
  p.end();
}

void clearRegistry() {
  Preferences p;
  p.begin(REG_NS, false);
  p.clear();
  p.end();
}

int findByMac(const uint8_t* mac) {
  for (uint8_t id = 1; id < MODULE_ID_COUNT; id++)
    if (mods[id].known && memcmp(mods[id].mac, mac, 6) == 0) return id;
  return -1;
}

uint8_t radioChannel() { return WiFi.status() == WL_CONNECTED ? WiFi.channel() : EspNowTransport::currentChannel(); }

// ---------------- cloud writes ----------------
void writePresence(uint8_t id) {
  JsonDocument d;
  d["online"] = mods[id].online;
  d["lastSeen"][".sv"] = "timestamp";
  char path[40];
  snprintf(path, sizeof(path), "modules/%s/presence", moduleKey((ModuleId)id));
  cloud::set(path, d.as<JsonVariantConst>());
}

void writeInfo(uint8_t id) {
  JsonDocument d;
  char m[18], fw[12];
  macToStr(mods[id].mac, m, sizeof(m));
  fwDecode(mods[id].fw, fw, sizeof(fw));
  d["mac"] = m;
  d["fw"] = fw;
  d["pairedAt"][".sv"] = "timestamp";
  char path[40];
  snprintf(path, sizeof(path), "modules/%s/info", moduleKey((ModuleId)id));
  cloud::set(path, d.as<JsonVariantConst>());
}

void writeState(uint8_t id) {
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  if (!codec::statusToJson((ModuleId)id, mods[id].status, mods[id].statusLen, o)) return;
  char path[40];
  snprintf(path, sizeof(path), "modules/%s/state", moduleKey((ModuleId)id));
  cloud::set(path, d.as<JsonVariantConst>());
}

void writeConfigApplied(uint8_t id, uint32_t version, const char* result) {
  JsonDocument d;
  d["version"] = version;
  d["result"] = result;
  d["at"][".sv"] = "timestamp";
  char path[48];
  snprintf(path, sizeof(path), "modules/%s/configApplied", moduleKey((ModuleId)id));
  cloud::set(path, d.as<JsonVariantConst>());
}

void writeHubFields(bool full) {
  JsonDocument d;
  cloud::setServerTime("hub/lastSeen");
  d.set(true);
  cloud::set("hub/online", d.as<JsonVariantConst>());
  d.set(WiFi.RSSI());
  cloud::set("hub/wifiRssi", d.as<JsonVariantConst>());
  d.set(WiFi.channel());
  cloud::set("hub/wifiChannel", d.as<JsonVariantConst>());
  if (!full) return;
  d.set(timeSourceName());
  cloud::set("hub/timeSource", d.as<JsonVariantConst>());
  d.set(rtc::statusName(rtcStatus));
  cloud::set("hub/rtc", d.as<JsonVariantConst>());
  char fw[12];
  fwDecode(HUB_FW, fw, sizeof(fw));
  d.set(fw);
  cloud::set("hub/fw", d.as<JsonVariantConst>());
  d.set(WiFi.localIP().toString());
  cloud::set("hub/ip", d.as<JsonVariantConst>());
  d.set(PROTOCOL_VERSION);
  cloud::set("hub/protocolVersion", d.as<JsonVariantConst>());
}

// ---------------- module lifecycle ----------------
void touch(uint8_t id, const uint8_t* mac) {
  ModuleEntry& e = mods[id];
  if (!e.known || memcmp(e.mac, mac, 6) != 0) {
    if (e.known) {
      char oldm[18], newm[18];
      macToStr(e.mac, oldm, sizeof(oldm));
      macToStr(mac, newm, sizeof(newm));
      Serial.printf("[NET] %s replaced: %s -> %s\n", moduleKey((ModuleId)id), oldm, newm);
      reliable.cancelFor(e.mac);
      transport.removePeer(e.mac);
      cloud::pushEvent(moduleKey((ModuleId)id), "MODULE_REPLACED");
      e.appliedKnown = false;
      e.rejectedVersion = e.queuedVersion = e.reportedAppliedVersion = UINT32_MAX;
    }
    memcpy(e.mac, mac, 6);
    e.known = true;
    saveEntry(id);
    e.infoDirty = true;
  }
  transport.ensurePeer(mac);
  e.lastRxMs = millis();
  if (!e.online) {
    e.online = true;
    e.presenceDirty = true;
    e.infoDirty = true;
    timeDue[id] = true;  // sent after this loop's WELCOME, so the module is already paired
    cloud::pushEvent(moduleKey((ModuleId)id), "MODULE_ONLINE");
    Serial.printf("[NET] %s ONLINE\n", moduleKey((ModuleId)id));
  }
}

void sendWelcome(uint8_t id) {
  Packet<WelcomePayload> pkt;
  fillHeader(pkt.h, MsgType::WELCOME, ModuleId::HUB, seq++, false);
  pkt.p.channel = radioChannel();
  pkt.p.hubFw = HUB_FW;
  cloud::DesiredConfig d;
  pkt.p.desiredConfigVersion = cloud::desiredConfig((ModuleId)id, d) ? d.version : 0;
  transport.send(mods[id].mac, &pkt, sizeof(pkt));
}

void sendTime(uint8_t id) {
  if (!clockValid() || !mods[id].online) return;
  Packet<TimePayload> pkt;
  fillHeader(pkt.h, MsgType::TIME, ModuleId::HUB, seq++, false);
  pkt.p.epoch = (uint32_t)time(nullptr);
  pkt.p.tzOffsetMin = LOCAL_TZ_OFFSET_MIN;
  transport.send(mods[id].mac, &pkt, sizeof(pkt));
}

void serviceTimeBroadcast() {
  const uint32_t now = millis();
  const bool periodic = now - lastTimeBroadcastMs >= TIME_BROADCAST_MS;
  if (periodic) lastTimeBroadcastMs = now;
  for (uint8_t id = 1; id < MODULE_ID_COUNT; id++) {
    if (!(periodic || timeDue[id]) || !mods[id].online) continue;
    if (!clockValid()) continue;  // keep timeDue set until the hub knows the time
    sendTime(id);
    timeDue[id] = false;
  }
}

void handleFrame(const Frame& f) {
  if (!headerValid(f.data, f.len)) return;
  const MsgHeader* h = reinterpret_cast<const MsgHeader*>(f.data);
  if (!isModuleId(h->module)) return;
  const uint8_t id = h->module;
  const uint8_t* payload = f.data + sizeof(MsgHeader);
  const size_t plen = f.len - sizeof(MsgHeader);
  ModuleEntry& e = mods[id];

  switch ((MsgType)h->type) {
    case MsgType::HELLO: {
      if (plen != sizeof(HelloPayload)) return;
      const HelloPayload* p = reinterpret_cast<const HelloPayload*>(payload);
      touch(id, f.mac);
      if (e.fw != p->fwVersion) {
        e.fw = p->fwVersion;
        saveEntry(id);
        e.infoDirty = true;
      }
      e.appliedVersion = p->configVersion;
      e.appliedKnown = true;
      sendWelcome(id);
      timeDue[id] = true;  // a (re)paired module gets the clock right away
      break;
    }

    case MsgType::STATUS: {
      if (plen != statusSizeFor((ModuleId)id)) {
        if (!e.sizeWarned) {
          Serial.printf("[ERROR] %s STATUS is %u bytes, expected %u: firmware/protocol mismatch\n",
                        moduleKey((ModuleId)id), (unsigned)plen, (unsigned)statusSizeFor((ModuleId)id));
          e.sizeWarned = true;
        }
        return;
      }
      touch(id, f.mac);
      memcpy(e.status, payload, plen);
      e.statusLen = (uint8_t)plen;
      e.statusDirty = true;
      StatusCommon c;
      memcpy(&c, payload, sizeof(c));
      e.appliedVersion = c.configVersion;
      e.appliedKnown = true;
      break;
    }

    case MsgType::EVENT: {
      if (plen != sizeof(EventPayload)) return;
      touch(id, f.mac);
      AckResult r = AckResult::OK;
      if (!dedup.lookup(f.mac, h->seq, r)) {
        const EventPayload* ev = reinterpret_cast<const EventPayload*>(payload);
        cloud::pushEvent(moduleKey((ModuleId)id), eventName(ev->code), ev->arg0, ev->arg1);
        Serial.printf("[NET] %s event %s (%ld, %ld)\n", moduleKey((ModuleId)id), eventName(ev->code),
                      (long)ev->arg0, (long)ev->arg1);
        dedup.remember(f.mac, h->seq, r);
      }
      if (h->flags & FLAG_ACK_REQUESTED) sendAck(transport, ModuleId::HUB, seq, f.mac, h->seq, r);
      break;
    }

    case MsgType::ACK:
      if (plen == sizeof(AckPayload)) reliable.handleAck(f.mac, *reinterpret_cast<const AckPayload*>(payload));
      break;

    default:
      break;
  }
}

void checkPresence() {
  const uint32_t now = millis();
  for (uint8_t id = 1; id < MODULE_ID_COUNT; id++) {
    ModuleEntry& e = mods[id];
    if (e.online && now - e.lastRxMs > HUB_OFFLINE_AFTER_MS) {
      e.online = false;
      e.presenceDirty = true;
      reliable.cancelFor(e.mac);
      cloud::pushEvent(moduleKey((ModuleId)id), "MODULE_OFFLINE");
      Serial.printf("[NET] %s OFFLINE (no packets for %lu ms)\n", moduleKey((ModuleId)id),
                    (unsigned long)(now - e.lastRxMs));
    }
  }
}

// ---------------- config push ----------------
void pushConfigs() {
  const uint32_t now = millis();
  for (uint8_t id = 1; id < MODULE_ID_COUNT; id++) {
    ModuleEntry& e = mods[id];
    if (!e.online) continue;
    cloud::DesiredConfig d;
    if (!cloud::desiredConfig((ModuleId)id, d)) continue;

    if (e.appliedKnown && e.appliedVersion == d.version) {  // module runs the desired config
      if (e.reportedAppliedVersion != d.version) {
        writeConfigApplied(id, d.version, "ok");
        e.reportedAppliedVersion = d.version;
      }
      continue;
    }
    if (d.version == e.rejectedVersion) continue;
    if (d.version == e.queuedVersion && now - e.lastConfigSendMs < CONFIG_QUEUED_RESEND_MS) continue;
    if (reliable.hasPendingFor(e.mac, (uint8_t)MsgType::CONFIG)) continue;
    if (e.lastConfigSendMs && now - e.lastConfigSendMs < CONFIG_RESEND_MS) continue;

    uint8_t buf[MAX_FRAME];
    fillHeader(*reinterpret_cast<MsgHeader*>(buf), MsgType::CONFIG, ModuleId::HUB, seq++, true);
    memcpy(buf + sizeof(MsgHeader), d.bin, d.len);
    if (reliable.send(e.mac, buf, sizeof(MsgHeader) + d.len, d.version)) {
      e.lastConfigSendMs = now;
      Serial.printf("[NET] sending config v%u to %s\n", (unsigned)d.version, moduleKey((ModuleId)id));
    }
  }
}

// ---------------- commands ----------------
struct InFlight {
  bool used = false;
  char mkey[12];
  char id[40];
  uint8_t remaining = 0;
  bool failed = false;
};
constexpr uint8_t INFLIGHT = 8;
InFlight inflight[INFLIGHT];

void finishIfDone(uint8_t slot) {
  InFlight& f = inflight[slot];
  if (!f.used || f.remaining > 0) return;
  cloud::setCommandStatus(f.mkey, f.id, f.failed ? "failed" : "done");
  Serial.printf("[NET] command %s/%s -> %s\n", f.mkey, f.id, f.failed ? "failed" : "done");
  f.used = false;
}

void runCommands() {
  cloud::Command c;
  while (cloud::nextCommand(c)) {
    int slot = -1;
    for (uint8_t i = 0; i < INFLIGHT; i++)
      if (!inflight[i].used) {
        slot = i;
        break;
      }
    Serial.printf("[NET] command %s %s (target %u, arg %ld)\n", c.mkey, cmdName(c.cmd), c.target, (long)c.arg);
    if (slot < 0) {
      cloud::setCommandStatus(c.mkey, c.id, "failed");
      continue;
    }
    InFlight& f = inflight[slot];
    f.used = true;
    strlcpy(f.mkey, c.mkey, sizeof(f.mkey));
    strlcpy(f.id, c.id, sizeof(f.id));
    f.remaining = 0;
    f.failed = false;

    Packet<CommandPayload> pkt;
    pkt.p.cmd = c.cmd;
    pkt.p.target = c.target;
    pkt.p.arg = c.arg;
    pkt.p.cmdRef = slot;
    for (uint8_t id = 1; id < MODULE_ID_COUNT; id++) {
      if (c.module != TARGET_ALL && c.module != id) continue;
      if (!mods[id].online) continue;
      fillHeader(pkt.h, MsgType::COMMAND, ModuleId::HUB, seq++, true);
      if (reliable.send(mods[id].mac, &pkt, sizeof(pkt), slot))
        f.remaining++;
      else
        f.failed = true;
    }
    if (f.remaining == 0) {
      f.failed = true;  // nobody to deliver to (module offline) or outbox full
      finishIfDone(slot);
    } else {
      cloud::setCommandStatus(f.mkey, f.id, "sent");
    }
  }
}

void onReliableDone(const ReliableSender::Pending& p, bool acked, AckResult r) {
  const int id = findByMac(p.mac);
  if (p.type == (uint8_t)MsgType::CONFIG) {
    if (id < 0) return;
    ModuleEntry& e = mods[id];
    const uint32_t version = p.cookie;
    if (!acked) {
      Serial.printf("[NET] config v%u to %s not acknowledged, will retry\n", (unsigned)version, moduleKey((ModuleId)id));
      return;
    }
    switch (r) {
      case AckResult::OK:
        writeConfigApplied(id, version, "ok");
        e.reportedAppliedVersion = version;
        break;
      case AckResult::BUSY_QUEUED:
        writeConfigApplied(id, version, "queued");
        e.queuedVersion = version;
        break;
      default:
        writeConfigApplied(id, version, "rejected");
        e.rejectedVersion = version;
        Serial.printf("[ERROR] %s rejected config v%u\n", moduleKey((ModuleId)id), (unsigned)version);
        break;
    }
    return;
  }

  if (p.type == (uint8_t)MsgType::COMMAND) {
    const uint32_t slot = p.cookie;
    if (slot >= INFLIGHT || !inflight[slot].used) return;
    InFlight& f = inflight[slot];
    if (!acked || (r != AckResult::OK && r != AckResult::BUSY_QUEUED)) f.failed = true;
    if (f.remaining) f.remaining--;
    finishIfDone(slot);
  }
}

// ---------------- periodic cloud sync ----------------
uint32_t lastSession = 0;
uint32_t lastHeartbeatMs = 0;

void syncCloud() {
  const uint32_t now = millis();
  const uint32_t s = cloud::session();
  const bool reconnected = s != lastSession;
  if (reconnected) {  // (re)connected: write a full snapshot
    lastSession = s;
    writeHubFields(true);
    if (s == 1) cloud::setServerTime("hub/bootAt");
    for (uint8_t id = 1; id < MODULE_ID_COUNT; id++) {
      mods[id].presenceDirty = true;
      if (mods[id].known) mods[id].infoDirty = true;
      if (mods[id].online && mods[id].statusLen) mods[id].statusDirty = true;
      mods[id].reportedAppliedVersion = UINT32_MAX;
    }
    lastHeartbeatMs = now;
  } else if (clockDirty) {
    clockDirty = false;
    JsonDocument d;
    d.set(timeSourceName());
    cloud::set("hub/timeSource", d.as<JsonVariantConst>());
    d.set(rtc::statusName(rtcStatus));
    cloud::set("hub/rtc", d.as<JsonVariantConst>());
  } else if (now - lastHeartbeatMs >= HUB_HEARTBEAT_MS) {
    lastHeartbeatMs = now;
    writeHubFields(false);
    for (uint8_t id = 1; id < MODULE_ID_COUNT; id++)
      if (mods[id].online) mods[id].presenceDirty = true;  // refresh lastSeen
  }

  for (uint8_t id = 1; id < MODULE_ID_COUNT; id++) {
    ModuleEntry& e = mods[id];
    if (e.presenceDirty) {
      writePresence(id);
      e.presenceDirty = false;
    }
    if (e.infoDirty && e.known) {
      writeInfo(id);
      e.infoDirty = false;
    }
    if (e.statusDirty) {  // cloud merges repeated writes; it flushes every CLOUD_FLUSH_MS
      writeState(id);
      e.statusDirty = false;
    }
  }
}

// ---------------- status LED + BOOT button ----------------
uint32_t flickerUntilMs = 0;

void setLed(bool on) { digitalWrite(PIN_STATUS_LED, (on ^ STATUS_LED_ACTIVE_LOW) ? HIGH : LOW); }

void updateLed() {
  const uint32_t now = millis();
  bool on;
  if (WiFi.status() != WL_CONNECTED) on = (now / 500) % 2;  // slow blink: WiFi connecting
  else if (!cloud::online()) on = (now / 125) % 2;          // fast blink: Firebase problem
  else on = true;                                            // solid: all good
  if ((int32_t)(flickerUntilMs - now) > 0) on = !on;         // short flicker per ESP-NOW packet
  setLed(on);
}

uint32_t bootPressedSinceMs = 0;

void checkBootButton() {
  if (digitalRead(PIN_BOOT_BUTTON) == LOW) {
    if (!bootPressedSinceMs) bootPressedSinceMs = millis();
    if (millis() - bootPressedSinceMs >= BOOT_BUTTON_HOLD_MS) {
      Serial.println("[STATE] BOOT held 5 s: clearing module pairings and restarting");
      clearRegistry();
      delay(100);
      ESP.restart();
    }
  } else {
    bootPressedSinceMs = 0;
  }
}

// ---------------- WiFi diagnostics ----------------
const char* wifiReason(uint8_t r) {
  switch (r) {
    case 2: return "auth expired";
    case 15: return "4-way handshake timeout (WRONG PASSWORD?)";
    case 201: return "network not found (check WIFI_SSID spelling/capitals, 2.4 GHz only)";
    case 202: return "authentication failed (WRONG PASSWORD?)";
    case 203: return "association failed";
    case 204: return "handshake timeout (WRONG PASSWORD?)";
    case 200: return "beacon timeout (weak signal / router off)";
    case 8: return "left network";
  }
  return "see esp_wifi_types.h";
}

uint32_t lastWifiReasonLogMs = 0;

void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    // Some routers answer DNS very slowly (7 s measured on the first install), which stalls every
    // new HTTPS connection. Use Google DNS first, keep the router's DNS as fallback.
    IPAddress router = WiFi.dnsIP(0);
    ip_addr_t primary = IPADDR4_INIT_BYTES(8, 8, 8, 8);
    ip_addr_t fallback = IPADDR4_INIT((uint32_t)router);
    dns_setserver(0, &primary);
    dns_setserver(1, &fallback);
    return;
  }
  if (event != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) return;
  const uint8_t r = info.wifi_sta_disconnected.reason;
  if (millis() - lastWifiReasonLogMs < 5000) return;  // WiFi retries fast; don't flood the log
  lastWifiReasonLogMs = millis();
  Serial.printf("\n[ERROR] WiFi disconnected, reason %u: %s\n", r, wifiReason(r));
}

/** After a failed connect: is the SSID visible? Catches capitalization typos and 5 GHz-only names. */
void diagnoseWifi() {
  Serial.println("[NET] scanning to diagnose WiFi...");
  const int n = WiFi.scanNetworks();
  bool exact = false;
  for (int i = 0; i < n; i++) {
    const String s = WiFi.SSID(i);
    if (s == WIFI_SSID) {
      exact = true;
      Serial.printf("[NET] '%s' found: channel %d, signal %d dBm -> SSID is right, check the PASSWORD\n", s.c_str(),
                    WiFi.channel(i), WiFi.RSSI(i));
    } else if (s.equalsIgnoreCase(WIFI_SSID)) {
      Serial.printf("[ERROR] '%s' not found, but '%s' exists: WIFI_SSID is case-sensitive, fix secrets.h\n", WIFI_SSID,
                    s.c_str());
    }
  }
  if (!exact) Serial.printf("[ERROR] '%s' is not visible (%d networks seen). Check spelling, and that it's 2.4 GHz\n",
                            WIFI_SSID, n);
  WiFi.scanDelete();
}

uint32_t lastPresenceMs = 0, lastPushMs = 0, lastReportMs = 0;

void printReport() {
  Serial.printf("[STATE] WiFi %s ch %u rssi %d | Firebase %s | modules:", WiFi.status() == WL_CONNECTED ? "up" : "DOWN",
                radioChannel(), WiFi.RSSI(), cloud::online() ? "online" : "OFFLINE");
  for (uint8_t id = 1; id < MODULE_ID_COUNT; id++)
    Serial.printf(" %s=%s", moduleKey((ModuleId)id), mods[id].online ? "ON" : "off");
  if (transport.rxDropped()) Serial.printf(" | rx dropped %lu", (unsigned long)transport.rxDropped());
  Serial.println();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  char fw[12];
  fwDecode(HUB_FW, fw, sizeof(fw));
  Serial.printf("\n=== Tile Machine HUB fw %s, protocol v%u ===\n", fw, PROTOCOL_VERSION);

  pinMode(PIN_STATUS_LED, OUTPUT);
  setLed(false);
  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

  initClock();  // DS3231 first: correct time before WiFi, even with no internet

  WiFi.onEvent(onWifiEvent);  // before begin(), so the first GOT_IP already switches DNS
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[NET] connecting to WiFi '%s'", WIFI_SSID);
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_BOOT_WAIT_MS) {
    setLed((millis() / 500) % 2);
    delay(100);  // setup only: ESP-NOW isn't running yet
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[NET] WiFi connected, IP %s, channel %u\n", WiFi.localIP().toString().c_str(), WiFi.channel());
  } else {
    diagnoseWifi();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);  // keep trying in the background
    Serial.println("[ERROR] WiFi not connected yet: running ESP-NOW anyway, Firebase will follow when WiFi is up");
  }

  if (!transport.begin(24)) {
    Serial.println("[ERROR] ESP-NOW init failed, restarting in 5 s");
    delay(5000);
    ESP.restart();
  }
  seq = initialSeq();
  reliable.onDone(onReliableDone);
  loadRegistry();

  cloud::begin();
  cloud::pushEvent("hub", "HUB_BOOT");
  for (uint8_t id = 1; id < MODULE_ID_COUNT; id++) mods[id].presenceDirty = true;  // everyone offline until heard
  Serial.println("[NET] ESP-NOW ready, waiting for modules");
}

void loop() {
  Frame f;
  while (transport.poll(f)) {
    flickerUntilMs = millis() + 30;
    handleFrame(f);
  }
  reliable.loop();
  runCommands();

  const uint32_t now = millis();
  if (now - lastPresenceMs >= PRESENCE_CHECK_MS) {
    lastPresenceMs = now;
    checkPresence();
  }
  if (now - lastPushMs >= CONFIG_PUSH_CHECK_MS) {
    lastPushMs = now;
    pushConfigs();
  }
  serviceClock();
  serviceTimeBroadcast();
  syncCloud();
  updateLed();
  checkBootButton();
  if (now - lastReportMs >= 10000) {
    lastReportMs = now;
    printReport();
  }
}
