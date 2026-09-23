#include "EspNowTransport.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

namespace tile {

const uint8_t EspNowTransport::BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
EspNowTransport* EspNowTransport::self_ = nullptr;

bool EspNowTransport::begin(uint8_t rxQueueLen) {
  self_ = this;
  if (!rxQueue_) rxQueue_ = xQueueCreate(rxQueueLen, sizeof(Frame));
  if (!rxQueue_) return false;

  // Modem sleep makes ESP-NOW miss packets; keep the radio awake.
  WiFi.setSleep(false);

  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(&EspNowTransport::onRecv);
  esp_now_register_send_cb(&EspNowTransport::onSent);
  return ensurePeer(BROADCAST);
}

void EspNowTransport::onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (!self_ || !self_->rxQueue_ || len <= 0 || len > (int)MAX_FRAME) return;
  Frame f;
  memcpy(f.mac, mac, 6);
  f.len = (uint8_t)len;
  memcpy(f.data, data, len);
  if (xQueueSend(self_->rxQueue_, &f, 0) != pdTRUE) self_->rxDropped_++;
}

void EspNowTransport::onSent(const uint8_t* mac, esp_now_send_status_t status) {
  if (!self_ || memcmp(mac, BROADCAST, 6) == 0) return;  // broadcasts are never ACKed
  if (status == ESP_NOW_SEND_SUCCESS) {
    self_->failStreak_ = 0;
  } else if (self_->failStreak_ < 255) {
    self_->failStreak_++;
  }
}

bool EspNowTransport::poll(Frame& out) {
  return rxQueue_ && xQueueReceive(rxQueue_, &out, 0) == pdTRUE;
}

bool EspNowTransport::ensurePeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;  // 0 = whatever channel the radio is on right now
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

void EspNowTransport::removePeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) esp_now_del_peer(mac);
}

bool EspNowTransport::send(const uint8_t* mac, const void* data, size_t len) {
  if (len == 0 || len > MAX_FRAME) return false;
  if (!ensurePeer(mac)) return false;
  return esp_now_send(mac, static_cast<const uint8_t*>(data), len) == ESP_OK;
}

void EspNowTransport::setChannel(uint8_t ch) {
  // Changing channel while not associated needs promiscuous mode toggled around the call.
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

uint8_t EspNowTransport::currentChannel() {
  uint8_t primary = 0;
  wifi_second_chan_t second;
  esp_wifi_get_channel(&primary, &second);
  return primary;
}

}  // namespace tile
