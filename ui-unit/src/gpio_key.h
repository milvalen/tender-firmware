// Debounced GPIO key with click and hold, for keys that M5Unified does not read
// (the Dual Button unit).
#pragma once

#include <Arduino.h>

#include "config.h"

namespace unit {

class GpioKey {
 public:
  GpioKey(int pin, bool active_high) : pin_(pin), active_high_(active_high) {}

  void begin() { pinMode(pin_, active_high_ ? INPUT_PULLDOWN : INPUT_PULLUP); }

  // Call once per loop. clicked() and held() are true for one call only.
  void update(uint32_t now) {
    clicked_ = false;
    held_ = false;

    const bool down = digitalRead(pin_) == (active_high_ ? HIGH : LOW);
    if (down != raw_) {
      raw_ = down;
      changed_ = now;
    }
    if (now - changed_ < kDebounceMs) return;

    if (down && !pressed_) {  // press starts
      pressed_ = true;
      since_ = now;
      hold_fired_ = false;
    }
    if (down && !hold_fired_ && now - since_ >= kHoldMs) {  // still down: a hold
      held_ = true;
      hold_fired_ = true;
    }
    if (!down && pressed_) {  // release: a click, unless it was a hold
      pressed_ = false;
      clicked_ = !hold_fired_;
    }
  }

  bool clicked() const { return clicked_; }
  bool held() const { return held_; }

 private:
  static constexpr uint32_t kDebounceMs = 20;

  int pin_;
  bool active_high_;
  bool raw_ = false;
  bool pressed_ = false;
  bool hold_fired_ = false;
  bool clicked_ = false;
  bool held_ = false;
  uint32_t changed_ = 0;
  uint32_t since_ = 0;
};

}  // namespace unit
