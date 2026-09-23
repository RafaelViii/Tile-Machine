// Thin ESP-NOW wrapper shared by hub and modules (Arduino-ESP32 core 2.0.x API).
// Receive callbacks run in the WiFi task, so frames are copied into a FreeRTOS queue and handled
// from loop() via poll(). Never do real work inside the callbacks.
#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include "TileProtocol.h"

namespace tile {

struct Frame {
  uint8_t mac[6];
  uint8_t len;
  uint8_t data[MAX_FRAME];
};

class EspNowTransport {
 public:
  /** WiFi must already be in STA mode. Returns false if esp_now_init fails. */
  bool begin(uint8_t rxQueueLen = 16);

  /** Pops one received frame. Non-blocking. */
  bool poll(Frame& out);

  /** Unicast (peer added automatically) or broadcast when mac == BROADCAST. */
  bool send(const uint8_t* mac, const void* data, size_t len);

  bool ensurePeer(const uint8_t* mac);
  void removePeer(const uint8_t* mac);

  /** Consecutive failed unicast deliveries (MAC-level ACK), reset by any success. */
  uint8_t failStreak() const { return failStreak_; }
  void resetFailStreak() { failStreak_ = 0; }

  /** Frames dropped because the rx queue was full (diagnostics). */
  uint32_t rxDropped() const { return rxDropped_; }

  /** Sets the radio channel (module side; the hub follows its router instead). */
  static void setChannel(uint8_t ch);
  static uint8_t currentChannel();

  static const uint8_t BROADCAST[6];

 private:
  static void onRecv(const uint8_t* mac, const uint8_t* data, int len);
  static void onSent(const uint8_t* mac, esp_now_send_status_t status);

  static EspNowTransport* self_;
  QueueHandle_t rxQueue_ = nullptr;
  volatile uint8_t failStreak_ = 0;
  volatile uint32_t rxDropped_ = 0;
};

}  // namespace tile
