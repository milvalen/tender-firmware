#include "vibro.h"

#include <Arduino.h>

#include "config.h"

namespace unit {

void Vibro::attach(int pin) {
  if (pin == pin_) return;
  if (pin_ >= 0) {
    ledcWrite(kVibroChannel, 0);
    ledcDetachPin(pin_);
    pinMode(pin_, INPUT);
  }
  pin_ = pin;
  duty_ = 0;
  if (pin_ >= 0) {
    ledcSetup(kVibroChannel, kVibroFreqHz, kVibroBits);
    ledcAttachPin(pin_, kVibroChannel);
    ledcWrite(kVibroChannel, 0);
  }
}

void Vibro::set(uint8_t duty) {
  if (pin_ < 0 || duty == duty_) return;
  ledcWrite(kVibroChannel, duty);
  duty_ = duty;
}

}  // namespace unit
