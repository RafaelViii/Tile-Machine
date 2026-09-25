#include "buzzer.h"

namespace {
constexpr uint8_t RES_BITS = 10;
constexpr uint32_t DUTY_MAX = (1u << RES_BITS) - 1;  // Arduino core writes this as 100 %
constexpr uint32_t DUTY_HALF = 1u << (RES_BITS - 1);  // 50 % = loudest square wave
}  // namespace

void Buzzer::begin(uint8_t pin, uint8_t ledcChannel, bool activeLow) {
  pin_ = pin;
  ch_ = ledcChannel;
  activeLow_ = activeLow;
  ledcSetup(ch_, 2000, RES_BITS);
  ledcAttachPin(pin_, ch_);
  output(0, Level::NORMAL);
}

void Buzzer::output(uint16_t hz, Level level) {
  if (hz == 0) {
    ledcWrite(ch_, activeLow_ ? DUTY_MAX : 0);  // idle level: no current through the buzzer
    return;
  }
  uint8_t pct = level == Level::FULL ? 100 : level == Level::QUIET ? (volume_ ? max<uint8_t>(3, volume_ / 4) : 0) : volume_;
  if (pct == 0) {
    ledcWrite(ch_, activeLow_ ? DUTY_MAX : 0);
    return;
  }
  ledcWriteTone(ch_, hz);  // sets frequency (and 50 % duty)
  const uint32_t on = DUTY_HALF * pct / 100;
  ledcWrite(ch_, activeLow_ ? DUTY_MAX - on : on);
}

void Buzzer::play(const Tone* tones, uint8_t count, Level level, bool urgent) {
  if (!tones || !count) return;
  if (urgent) {  // drop everything queued, start now
    count_ = 0;
    active_ = false;
  }
  if (count_ < QUEUE) {
    queue_[(head_ + count_) % QUEUE] = {tones, count, level};
    count_++;
  }
  if (!active_) startNext();
}

void Buzzer::startNext() {
  if (count_ == 0) {
    active_ = false;
    output(0, Level::NORMAL);
    return;
  }
  cur_ = queue_[head_];
  head_ = (head_ + 1) % QUEUE;
  count_--;
  step_ = 0;
  active_ = true;
  stepStartMs_ = millis();
  output(cur_.tones[0].hz, cur_.level);
}

void Buzzer::update() {
  if (!active_) return;
  if (millis() - stepStartMs_ < cur_.tones[step_].ms) return;
  step_++;
  if (step_ >= cur_.count) {
    startNext();
    return;
  }
  stepStartMs_ = millis();
  output(cur_.tones[step_].hz, cur_.level);
}
