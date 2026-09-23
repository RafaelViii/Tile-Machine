// Reliable delivery for ACK-requested messages (docs/PROTOCOL.md §3):
//  - ReliableSender retries up to ACK_RETRIES times, ACK_TIMEOUT_MS apart, then reports failure.
//  - DedupCache lets a receiver re-ACK a duplicate without executing it twice.
#pragma once

#include <Arduino.h>
#include <functional>
#include "EspNowTransport.h"

namespace tile {

class ReliableSender {
 public:
  struct Pending {
    bool used = false;
    uint8_t mac[6];
    uint16_t seq;
    uint8_t type;        // MsgType
    uint32_t cookie;     // caller context (e.g. config version, command slot)
    uint8_t tries;
    uint32_t lastSendMs;
    uint8_t len;
    uint8_t buf[MAX_FRAME];
  };

  /** acked=false means all retries were used without an ACK. */
  using DoneFn = std::function<void(const Pending& p, bool acked, AckResult result)>;

  explicit ReliableSender(EspNowTransport& t) : t_(t) {}

  void onDone(DoneFn fn) { done_ = fn; }

  /** buf must start with a MsgHeader that has FLAG_ACK_REQUESTED set. False if the outbox is full. */
  bool send(const uint8_t* mac, const void* buf, size_t len, uint32_t cookie = 0);

  /** Feed every received ACK here. */
  void handleAck(const uint8_t* mac, const AckPayload& ack);

  /** Drops everything pending for a peer (e.g. it went offline) and reports it as not acked. */
  void cancelFor(const uint8_t* mac);

  bool hasPendingFor(const uint8_t* mac, uint8_t type) const;

  void loop();

  static constexpr uint8_t SLOTS = 8;

 private:
  EspNowTransport& t_;
  DoneFn done_;
  Pending slots_[SLOTS];
};

class DedupCache {
 public:
  /** If (mac, seq) was seen within EXPIRY_MS, returns true and the stored result. */
  bool lookup(const uint8_t* mac, uint16_t seq, AckResult& result) const;
  void remember(const uint8_t* mac, uint16_t seq, AckResult result);

  // Retries of one message span ~ACK_TIMEOUT_MS * ACK_RETRIES (800 ms). Entries expire well after
  // that, so a peer that reboots and restarts its seq counter can never have a NEW message (e.g.
  // a STOP) mistaken for an old duplicate and skipped.
  static constexpr uint32_t EXPIRY_MS = 3000;

 private:
  struct Entry {
    bool used = false;
    uint8_t mac[6];
    uint16_t seq;
    AckResult result;
    uint32_t atMs;
  };
  static constexpr uint8_t SIZE = 16;
  Entry ring_[SIZE];
  uint8_t next_ = 0;
};

/** Random starting sequence number, so seqs after a reboot don't repeat recent ones. */
inline uint16_t initialSeq() { return (uint16_t)(esp_random() & 0xFFFF); }

/** Sends an ACK for `ackSeq` to `mac`. */
void sendAck(EspNowTransport& t, ModuleId self, uint16_t& seqCounter, const uint8_t* mac, uint16_t ackSeq,
             AckResult result);

}  // namespace tile
