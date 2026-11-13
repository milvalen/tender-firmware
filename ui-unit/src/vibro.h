// Vibration motor on one LEDC PWM channel. The table board and the self-test
// share it; the self-test can move it to another pin.
#pragma once

#include <stdint.h>

namespace unit {

class Vibro {
 public:
  // Connects the PWM channel to pin, motor off. -1 disconnects it.
  void attach(int pin);
  // Sets the PWM duty. Does nothing if no pin is attached.
  void set(uint8_t duty);
  int pin() const { return pin_; }

 private:
  int pin_ = -1;
  uint8_t duty_ = 0;
};

}  // namespace unit
