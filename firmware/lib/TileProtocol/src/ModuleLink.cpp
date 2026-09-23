#include "ModuleLink.h"

#include <Preferences.h>
#include <WiFi.h>

namespace tile {

namespace {
constexpr const char* NVS_NS = "tilelink";

void logMac(const char* what, const uint8_t* mac, uint8_t ch) {
  char m[18];
  macToStr(mac, m, sizeof(m));
  Serial.printf("[NET] %s hub %s on channel %u\n", what, m, ch);
}
}  // namespace

bool ModuleLink::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  seq_ = initialSeq();

  if (!transport_.begin()) {
    Serial.println("[ERROR] ESP-NOW init failed");
    return false;
  }

  reliable_.onDone([](const ReliableSender::Pending& p, bool acked, AckResult) {
    if (!acked) Serial.printf("[NET] message type 0x%02X seq %u not acknowledged by hub\n", p.type, p.seq);
  });

  Preferences prefs;
  prefs.begin(NVS_NS, true);
  hubKnown_ = prefs.getBytes("hubmac", hubMac_, 6) == 6;
  uint8_t ch = prefs.getUChar("chan", 0);
  prefs.end();

  if (hubKnown_ && ch >= MIN_CHANNEL && ch <= MAX_CHANNEL) {
    channel_ = ch;
    EspNowTransport::setChannel(channel_);
    state_ = State::PROBING;
    stateSinceMs_ = millis();
    logMac("probing stored", hubMac_, channel_);
    sendHello(hubMac_);
  } else {
    Serial.println("[NET] no stored hub, scanning channels");
    startScan(MIN_CHANNEL);
  }
  return true;
}

void ModuleLink::startScan(uint8_t fromChannel) {
  state_ = State::SCANNING;
  channel_ = (fromChannel >= MIN_CHANNEL && fromChannel <= MAX_CHANNEL) ? fromChannel : MIN_CHANNEL;
  EspNowTransport::setChannel(channel_);
  stateSinceMs_ = millis();
  sendHello(EspNowTransport::BROADCAST);
}

void ModuleLink::sendHello(const uint8_t* dest) {
  Packet<HelloPayload> pkt;
  fillHeader(pkt.h, MsgType::HELLO, id_, seq_++, false);
  pkt.p.fwVersion = fw_;
  pkt.p.configVersion = configVersion_;
  transport_.send(dest, &pkt, sizeof(pkt));
}

void ModuleLink::becomePaired(const uint8_t* hubMac, uint8_t channel) {
  const bool changed = !hubKnown_ || memcmp(hubMac_, hubMac, 6) != 0 || channel_ != channel;
  if (channel != EspNowTransport::currentChannel()) EspNowTransport::setChannel(channel);
  memcpy(hubMac_, hubMac, 6);
  hubKnown_ = true;
  channel_ = channel;
  transport_.ensurePeer(hubMac_);
  transport_.resetFailStreak();
  state_ = State::PAIRED;
  stateSinceMs_ = millis();
  forceStatus_ = true;

  if (changed) {
    Preferences prefs;
    prefs.begin(NVS_NS, false);
    prefs.putBytes("hubmac", hubMac_, 6);
    prefs.putUChar("chan", channel_);
    prefs.end();
  }
  logMac("paired with", hubMac_, channel_);
}

void ModuleLink::forgetHub() {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.clear();
  prefs.end();
  if (hubKnown_) transport_.removePeer(hubMac_);
  hubKnown_ = false;
  Serial.println("[NET] hub forgotten, scanning");
  startScan(MIN_CHANNEL);
}

void ModuleLink::loop() {
  Frame f;
  while (transport_.poll(f)) handleFrame(f);
  reliable_.loop();

  const uint32_t now = millis();
  switch (state_) {
    case State::PROBING:
      if (now - stateSinceMs_ >= PROBE_TIMEOUT_MS) {
        Serial.println("[NET] stored hub not answering, scanning channels");
        startScan(channel_);
      }
      break;

    case State::SCANNING:
      if (now - stateSinceMs_ >= SCAN_DWELL_MS) {
        uint8_t next = channel_ >= MAX_CHANNEL ? MIN_CHANNEL : channel_ + 1;
        channel_ = next;
        EspNowTransport::setChannel(channel_);
        stateSinceMs_ = now;
        sendHello(EspNowTransport::BROADCAST);
      }
      break;

    case State::PAIRED:
      if (transport_.failStreak() >= MODULE_LOST_AFTER_FAILS) {
        Serial.println("[NET] hub lost, scanning channels");
        reliable_.cancelFor(hubMac_);
        startScan(channel_);
        break;
      }
      maybeSendStatus();
      break;
  }
}

void ModuleLink::handleFrame(const Frame& f) {
  if (!headerValid(f.data, f.len)) return;
  const MsgHeader* h = reinterpret_cast<const MsgHeader*>(f.data);
  if (h->module != (uint8_t)ModuleId::HUB) return;  // only the hub talks to modules
  const uint8_t* payload = f.data + sizeof(MsgHeader);
  const size_t plen = f.len - sizeof(MsgHeader);

  if (h->type == (uint8_t)MsgType::WELCOME) {
    if (plen != sizeof(WelcomePayload)) return;
    const WelcomePayload* w = reinterpret_cast<const WelcomePayload*>(payload);
    uint8_t ch = (w->channel >= MIN_CHANNEL && w->channel <= MAX_CHANNEL) ? w->channel : channel_;
    becomePaired(f.mac, ch);
    return;
  }

  // Everything else only from the hub we are paired with.
  if (state_ != State::PAIRED || memcmp(f.mac, hubMac_, 6) != 0) return;

  switch ((MsgType)h->type) {
    case MsgType::ACK:
      if (plen == sizeof(AckPayload)) reliable_.handleAck(f.mac, *reinterpret_cast<const AckPayload*>(payload));
      break;

    case MsgType::CONFIG:
    case MsgType::COMMAND: {
      AckResult result;
      if (!dedup_.lookup(f.mac, h->seq, result)) {
        if (h->type == (uint8_t)MsgType::CONFIG) {
          result = onConfig_ ? onConfig_(payload, plen) : AckResult::UNSUPPORTED;
        } else if (plen == sizeof(CommandPayload)) {
          result = onCommand_ ? onCommand_(*reinterpret_cast<const CommandPayload*>(payload)) : AckResult::UNSUPPORTED;
        } else {
          result = AckResult::REJECTED;
        }
        dedup_.remember(f.mac, h->seq, result);
      }
      if (h->flags & FLAG_ACK_REQUESTED) sendAck(transport_, id_, seq_, f.mac, h->seq, result);
      break;
    }

    default:
      break;
  }
}

void ModuleLink::publishStatus(const void* status, size_t len) {
  if (len == 0 || len > MAX_FRAME - sizeof(MsgHeader)) return;
  memcpy(status_, status, len);
  statusLen_ = (uint8_t)len;
}

void ModuleLink::maybeSendStatus() {
  if (statusLen_ == 0) return;
  const uint32_t now = millis();
  const uint32_t since = now - lastStatusMs_;
  const bool changed = memcmp(status_, lastSent_, statusLen_) != 0;
  if (!(forceStatus_ || since >= HEARTBEAT_MS || (changed && since >= STATUS_MIN_GAP_MS))) return;

  uint8_t buf[MAX_FRAME];
  MsgHeader* h = reinterpret_cast<MsgHeader*>(buf);
  fillHeader(*h, MsgType::STATUS, id_, seq_++, false);
  memcpy(buf + sizeof(MsgHeader), status_, statusLen_);
  transport_.send(hubMac_, buf, sizeof(MsgHeader) + statusLen_);

  memcpy(lastSent_, status_, statusLen_);
  lastStatusMs_ = now;
  forceStatus_ = false;
}

bool ModuleLink::sendEvent(EventCode code, int32_t arg0, int32_t arg1) {
  if (state_ != State::PAIRED) return false;
  Packet<EventPayload> pkt;
  fillHeader(pkt.h, MsgType::EVENT, id_, seq_++, true);
  pkt.p.code = (uint16_t)code;
  pkt.p.arg0 = arg0;
  pkt.p.arg1 = arg1;
  return reliable_.send(hubMac_, &pkt, sizeof(pkt));
}

}  // namespace tile
