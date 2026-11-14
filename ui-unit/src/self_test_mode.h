// Board self-test mode, started with "!" on the serial console. The core
// (lib/tableboard/src/selftest.h) decides what to test and what to show; this
// class only connects it to the pins, the power chip, the serial port and the
// screen. "exit" in the self-test restarts the unit.
#pragma once

#include <M5Unified.h>

#include "flash_log.h"
#include "selftest.h"
#include "vibro.h"

namespace unit {

class SelfTestMode {
 public:
  SelfTestMode(M5Canvas& canvas, Vibro& vibro, FlashLog& log);

  void request() { requested_ = true; }
  bool active() const { return requested_; }
  // Call from loop() while active(). The first call starts the self-test.
  void loop(uint32_t now);

 private:
  void start(uint32_t now);
  void apply_setup();            // pulls, motor pin, rotation from the core
  void read_console(uint32_t now);
  void read_inputs();
  void read_power(uint32_t now);
  void print(const char* text);

  tender::SelfTest test_;
  M5Canvas& canvas_;
  Vibro& vibro_;
  FlashLog& log_;

  bool requested_ = false;
  bool started_ = false;
  uint32_t setup_version_ = 0;
  uint32_t last_power_ = 0;
  uint32_t last_draw_ = 0;
  char line_[48] = {};  // console line being typed
  size_t line_len_ = 0;
  char out_[1024] = {};  // core output text
};

}  // namespace unit
