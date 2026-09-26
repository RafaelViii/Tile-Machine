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
  /** Silence now and drop everything queued. */
  void stop() {
    count_ = 0;
    active_ = false;
    output(0, Level::NORMAL);
  }
  /** Pitch playing right now (0 = silent or a gap). */
  uint16_t currentHz() const { return active_ ? cur_.tones[step_].hz : 0; }

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
// A passive buzzer is only loud near its resonance. The installed module (5 V) measured loudest at 4000-4500 Hz
// with PITCH_TEST (2026-09-26); below ~1.5 kHz it is barely audible. fw 0.2.7 centres every sound on ~4.25 kHz
// (3.4-4.9 kHz) and keeps each one's shape (rising, falling, "error" lower than "ok"). Warnings sit on the
// loudest pitch. A different buzzer: run the pitch test again and re-centre. (IDENTIFY keeps its 1/2 kHz warble.)
namespace sounds {
constexpr Tone BOOT[] = {{3600, 110}, {4000, 110}, {4500, 200}};
constexpr Tone CLICK[] = {{4250, 70}};
// LOADED: two quick rising chirps (high). EMPTY: one long falling tone (lower). Deliberately opposite.
constexpr Tone LOADED[] = {{3600, 70}, {4100, 70}, {4600, 90}, {0, 70}, {3600, 70}, {4100, 70}, {4600, 110}};
constexpr Tone EMPTY[] = {{4800, 110}, {4600, 110}, {4400, 110}, {4200, 110}, {4000, 110}, {3700, 180}};
constexpr Tone WARN_TICK[] = {{4250, 120}};
constexpr Tone WARN_FAST[] = {{4500, 60}};
constexpr Tone RELAY_ON[] = {{3400, 80}, {3650, 80}, {3900, 80}, {4150, 80}, {4400, 80}, {4650, 80}, {4900, 160}};
constexpr Tone RELAY_OFF[] = {{4800, 60}, {4400, 60}, {4000, 60}, {3600, 130}};
constexpr Tone ESTOP[] = {{4300, 120}, {0, 70}, {4300, 120}, {0, 70}, {4300, 180}};
constexpr Tone MODE_CHANGE[] = {{4000, 150}, {0, 100}, {4000, 150}};
constexpr Tone CANCEL[] = {{4500, 80}, {0, 50}, {3700, 160}};
constexpr Tone RUNNING_TICK[] = {{4250, 30}};                     // played QUIET on purpose
constexpr Tone INTERLOCK[] = {{3600, 250}, {0, 120}, {3600, 250}};  // "not now": lower + longer than MODE_CHANGE
// Pitch test (IDENTIFY with arg 1, idle only): 1.5-5 kHz in 250 Hz steps, the OLED shows each pitch, so the
// buzzer's loudest pitch (its resonance) can be read off and every sound tuned to it.
constexpr Tone PITCH_TEST[] = {{1500, 700}, {0, 300}, {1750, 700}, {0, 300}, {2000, 700}, {0, 300}, {2250, 700},
                               {0, 300},    {2500, 700}, {0, 300}, {2750, 700}, {0, 300}, {3000, 700}, {0, 300},
                               {3250, 700}, {0, 300}, {3500, 700}, {0, 300}, {3750, 700}, {0, 300}, {4000, 700},
                               {0, 300},    {4250, 700}, {0, 300}, {4500, 700}, {0, 300}, {4750, 700}, {0, 300},
                               {5000, 700}};
constexpr Tone IDENTIFY[] = {{1000, 100}, {2000, 100}, {1000, 100}, {2000, 100}, {1000, 100}, {2000, 100},
                             {1000, 100}, {2000, 100}, {1000, 100}, {2000, 100}, {1000, 100}, {2000, 100},
                             {1000, 100}, {2000, 100}, {1000, 100}, {2000, 100}, {1000, 100}, {2000, 100},
                             {1000, 100}, {2000, 100}, {1000, 100}, {2000, 100}, {1000, 100}, {2000, 100},
                             {1000, 100}, {2000, 100}, {1000, 100}, {2000, 100}, {1000, 100}, {2000, 100}};
}  // namespace sounds
