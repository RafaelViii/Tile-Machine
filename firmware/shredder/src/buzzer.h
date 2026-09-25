// Non-blocking tone player for a PASSIVE buzzer module (LEDC PWM, Arduino core 2.0.x API).
// Patterns are queued so two events in the same loop are both heard; an urgent pattern (E-STOP)
// clears the queue and plays at full volume regardless of the volume setting.
#pragma once

#include <Arduino.h>

struct Tone {
  uint16_t hz;  // 0 = silence
  uint16_t ms;
};

enum class Level : uint8_t { NORMAL, QUIET, FULL };

class Buzzer {
 public:
  void begin(uint8_t pin, uint8_t ledcChannel, bool activeLow);
  void setVolume(uint8_t pct) { volume_ = pct > 100 ? 100 : pct; }
  void play(const Tone* tones, uint8_t count, Level level = Level::NORMAL, bool urgent = false);
  void update();
  bool busy() const { return active_ || count_ > 0; }

 private:
  struct Item {
    const Tone* tones;
    uint8_t count;
    Level level;
  };
  void startNext();
  void output(uint16_t hz, Level level);

  uint8_t pin_ = 0;
  uint8_t ch_ = 0;
  bool activeLow_ = false;
  uint8_t volume_ = 100;

  static constexpr uint8_t QUEUE = 8;
  Item queue_[QUEUE];
  uint8_t head_ = 0, count_ = 0;

  bool active_ = false;
  Item cur_{};
  uint8_t step_ = 0;
  uint32_t stepStartMs_ = 0;
};

#define TONES(p) (p), (uint8_t)(sizeof(p) / sizeof(Tone))

// ---- Sound vocabulary (docs/modules/shredder.md) ----
namespace sounds {
constexpr Tone BOOT[] = {{523, 120}, {659, 120}, {784, 220}};
constexpr Tone CLICK[] = {{2000, 40}};
// LOADED: two quick rising chirps (high). EMPTY: one long falling tone (low). Deliberately opposite.
constexpr Tone LOADED[] = {{1500, 70}, {2000, 70}, {2500, 90}, {0, 70}, {1500, 70}, {2000, 70}, {2500, 110}};
constexpr Tone EMPTY[] = {{800, 110}, {720, 110}, {640, 110}, {560, 110}, {480, 110}, {400, 160}};
constexpr Tone WARN_TICK[] = {{2000, 120}};
constexpr Tone WARN_FAST[] = {{2400, 60}};
constexpr Tone RELAY_ON[] = {{600, 90}, {800, 90}, {1000, 90}, {1200, 90}, {1400, 90}, {1600, 90}, {1800, 160}};
constexpr Tone RELAY_OFF[] = {{1500, 60}, {1200, 60}, {900, 60}, {600, 120}};
constexpr Tone ESTOP[] = {{3000, 120}, {0, 70}, {3000, 120}, {0, 70}, {3000, 180}};
constexpr Tone MODE_CHANGE[] = {{1200, 150}, {0, 100}, {1200, 150}};
constexpr Tone CANCEL[] = {{900, 80}, {0, 50}, {600, 140}};
constexpr Tone RUNNING_TICK[] = {{1000, 30}};
constexpr Tone INTERLOCK[] = {{400, 250}, {0, 120}, {400, 250}};
constexpr Tone IDENTIFY[] = {{1000, 100}, {2000, 100}, {1000, 100}, {2000, 100}, {1000, 100}, {2000, 100},
                             {1000, 100}, {2000, 100}, {1000, 100}, {2000, 100}, {1000, 100}, {2000, 100},
                             {1000, 100}, {2000, 100}, {1000, 100}, {2000, 100}, {1000, 100}, {2000, 100},
                             {1000, 100}, {2000, 100}, {1000, 100}, {2000, 100}, {1000, 100}, {2000, 100},
                             {1000, 100}, {2000, 100}, {1000, 100}, {2000, 100}, {1000, 100}, {2000, 100}};
}  // namespace sounds
