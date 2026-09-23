#include "Reliable.h"

namespace tile {

bool ReliableSender::send(const uint8_t* mac, const void* buf, size_t len, uint32_t cookie) {
  if (len < sizeof(MsgHeader) || len > MAX_FRAME) return false;
  for (auto& s : slots_) {
    if (s.used) continue;
    const MsgHeader* h = static_cast<const MsgHeader*>(buf);
    s.used = true;
    memcpy(s.mac, mac, 6);
    s.seq = h->seq;
    s.type = h->type;
    s.cookie = cookie;
    s.tries = 1;
    s.lastSendMs = millis();
    s.len = (uint8_t)len;
    memcpy(s.buf, buf, len);
    t_.send(mac, s.buf, s.len);
    return true;
  }
  return false;
}

void ReliableSender::handleAck(const uint8_t* mac, const AckPayload& ack) {
  for (auto& s : slots_) {
    if (s.used && s.seq == ack.ackSeq && memcmp(s.mac, mac, 6) == 0) {
      s.used = false;  // free before callback so the callback may send again
      if (done_) done_(s, true, (AckResult)ack.result);
      return;
    }
  }
}

void ReliableSender::cancelFor(const uint8_t* mac) {
  for (auto& s : slots_) {
    if (s.used && memcmp(s.mac, mac, 6) == 0) {
      s.used = false;
      if (done_) done_(s, false, AckResult::OK);
    }
  }
}

bool ReliableSender::hasPendingFor(const uint8_t* mac, uint8_t type) const {
  for (const auto& s : slots_)
    if (s.used && s.type == type && memcmp(s.mac, mac, 6) == 0) return true;
  return false;
}

void ReliableSender::loop() {
  const uint32_t now = millis();
  for (auto& s : slots_) {
    if (!s.used || now - s.lastSendMs < ACK_TIMEOUT_MS) continue;
    if (s.tries > ACK_RETRIES) {  // 1 original + ACK_RETRIES retries, all unanswered
      s.used = false;
      if (done_) done_(s, false, AckResult::OK);
      continue;
    }
    s.tries++;
    s.lastSendMs = now;
    t_.send(s.mac, s.buf, s.len);
  }
}

bool DedupCache::lookup(const uint8_t* mac, uint16_t seq, AckResult& result) const {
  const uint32_t now = millis();
  for (const auto& e : ring_) {
    if (e.used && now - e.atMs < EXPIRY_MS && e.seq == seq && memcmp(e.mac, mac, 6) == 0) {
      result = e.result;
      return true;
    }
  }
  return false;
}

void DedupCache::remember(const uint8_t* mac, uint16_t seq, AckResult result) {
  Entry& e = ring_[next_];
  e.used = true;
  memcpy(e.mac, mac, 6);
  e.seq = seq;
  e.result = result;
  e.atMs = millis();
  next_ = (next_ + 1) % SIZE;
}

void sendAck(EspNowTransport& t, ModuleId self, uint16_t& seqCounter, const uint8_t* mac, uint16_t ackSeq,
             AckResult result) {
  Packet<AckPayload> pkt;
  fillHeader(pkt.h, MsgType::ACK, self, seqCounter++, false);
  pkt.p.ackSeq = ackSeq;
  pkt.p.result = (uint8_t)result;
  t.send(mac, &pkt, sizeof(pkt));
}

}  // namespace tile
