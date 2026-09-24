// Module-side link to the hub: automatic pairing, heartbeat STATUS, EVENTs, and CONFIG/COMMAND
// reception with ACKs. See docs/ARCHITECTURE.md §2–3.
//
// Pairing: on boot, try the hub stored in NVS (unicast HELLO on the stored channel). If there is no
// answer within PROBE_TIMEOUT_MS, scan channels 1..13 broadcasting HELLO until a hub answers
// WELCOME. After MODULE_LOST_AFTER_FAILS failed deliveries in a row, scan again. The process the
// module runs is never affected by any of this.
#pragma once

#include <Arduino.h>
#include <functional>
#include "EspNowTransport.h"
#include "Reliable.h"
#include "TileTime.h"

namespace tile {

class ModuleLink {
 public:
  enum class State : uint8_t { PROBING, SCANNING, PAIRED };

  /** payload starts with the uint32 configVersion; len is the full config struct size. */
  using ConfigHandler = std::function<AckResult(const uint8_t* payload, size_t len)>;
  using CommandHandler = std::function<AckResult(const CommandPayload& cmd)>;

  ModuleLink(ModuleId id, uint16_t fwVersion) : id_(id), fw_(fwVersion), reliable_(transport_) {}

  /** Puts WiFi in STA mode (not connected) and starts pairing. Call once in setup(). */
  bool begin();
  /** Call every loop(). Non-blocking. */
  void loop();

  void onConfig(ConfigHandler h) { onConfig_ = h; }
  void onCommand(CommandHandler h) { onCommand_ = h; }

  /** Config version the module currently has APPLIED (announced in HELLO). */
  void setConfigVersion(uint32_t v) { configVersion_ = v; }

  /** Latest status struct. Sent on change (min gap 100 ms) and at least every HEARTBEAT_MS. */
  void publishStatus(const void* status, size_t len);

  /** Reliable EVENT to the hub. False if not paired or the outbox is full. */
  bool sendEvent(EventCode code, int32_t arg0 = 0, int32_t arg1 = 0);

  State state() const { return state_; }
  bool paired() const { return state_ == State::PAIRED; }
  uint8_t channel() const { return channel_; }

  /** Wall-clock time from the hub (DS3231 RTC / NTP), kept running locally between TIME messages. */
  bool timeKnown() const { return timeKnown_; }
  uint32_t nowEpoch() const { return timeKnown_ ? epochBase_ + (millis() - epochBaseMs_) / 1000 : 0; }
  int16_t tzOffsetMin() const { return tzOffsetMin_; }

  /** Forgets the stored hub (NVS) and starts scanning. */
  void forgetHub();

 private:
  void handleFrame(const Frame& f);
  void startScan(uint8_t fromChannel);
  void sendHello(const uint8_t* dest);
  void becomePaired(const uint8_t* hubMac, uint8_t channel);
  void maybeSendStatus();

  ModuleId id_;
  uint16_t fw_;
  EspNowTransport transport_;
  ReliableSender reliable_;
  DedupCache dedup_;
  ConfigHandler onConfig_;
  CommandHandler onCommand_;

  State state_ = State::SCANNING;
  uint8_t hubMac_[6] = {0};
  bool hubKnown_ = false;
  uint8_t channel_ = 1;
  uint32_t stateSinceMs_ = 0;
  uint16_t seq_ = 0;
  uint32_t configVersion_ = 0;

  uint8_t status_[MAX_FRAME];
  uint8_t lastSent_[MAX_FRAME];
  uint8_t statusLen_ = 0;
  uint32_t lastStatusMs_ = 0;
  bool forceStatus_ = false;

  bool timeKnown_ = false;
  uint32_t epochBase_ = 0;
  uint32_t epochBaseMs_ = 0;
  int16_t tzOffsetMin_ = 0;
};

}  // namespace tile
