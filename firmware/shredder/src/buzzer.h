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
// A passive buzzer is barely audible below ~1.5 kHz. All sounds stay in 1.6-3.1 kHz (fw 0.2.5, restored in
// 0.2.8) and keep their shape (rising, falling, "error" lower than "ok"). fw 0.2.7 tried the pitch test's
// loudest single tone (4000-4500 Hz on the installed 5 V module), but at the machine the 2-3 kHz set sounded
// louder: judge by ear with the real sounds, not only by the pitch test. (IDENTIFY keeps its 1/2 kHz warble.)
namespace sounds {
constexpr Tone BOOT[] = {{2093, 110}, {2637, 110}, {3136, 200}};  // C-E-G, two octaves up
constexpr Tone CLICK[] = {{2500, 70}};
// LOADED: two quick rising chirps (high). EMPTY: one long falling tone (lower). Deliberately opposite.
constexpr Tone LOADED[] = {{2000, 70}, {2500, 70}, {3000, 90}, {0, 70}, {2000, 70}, {2500, 70}, {3000, 110}};
constexpr Tone EMPTY[] = {{2600, 110}, {2400, 110}, {2200, 110}, {2000, 110}, {1800, 110}, {1600, 180}};
constexpr Tone WARN_TICK[] = {{2000, 120}};
constexpr Tone WARN_FAST[] = {{2400, 60}};
constexpr Tone RELAY_ON[] = {{1600, 80}, {1850, 80}, {2100, 80}, {2350, 80}, {2600, 80}, {2850, 80}, {3100, 160}};
constexpr Tone RELAY_OFF[] = {{2800, 60}, {2400, 60}, {2000, 60}, {1600, 130}};
constexpr Tone ESTOP[] = {{3000, 120}, {0, 70}, {3000, 120}, {0, 70}, {3000, 180}};
constexpr Tone MODE_CHANGE[] = {{2000, 150}, {0, 100}, {2000, 150}};
constexpr Tone CANCEL[] = {{2400, 80}, {0, 50}, {1800, 160}};
constexpr Tone RUNNING_TICK[] = {{2000, 30}};                     // played QUIET on purpose
constexpr Tone INTERLOCK[] = {{1600, 250}, {0, 120}, {1600, 250}};  // "not now": lower + longer than MODE_CHANGE
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
