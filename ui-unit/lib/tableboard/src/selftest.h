// Board self-test for TENDER TND-01, for the day the stage 0 boards arrive.
//
// Hardware-independent core, like tableboard.h: the StickS3 glue in
// src/main.cpp feeds key states, pin levels and power readings, runs serial
// command lines and applies the outputs (vibration motor PWM, input pulls,
// display rotation). The checks follow part E of the measurement sheet and
// the vibrator desk test in tender-vault/40-hardware/.
//
//   E1  Dual Button keys: the screen shows the level of each Grove pin, counts
//       presses and tells which level a press gives (kDualActiveHigh).
//   E2  Display rotation: the screen marks the Hat2 end; "rot 1|3" turns it.
//   E3  Vibrator: "vibro <pin>", "duty <n>", "pulse", "on", "off". The duty
//       is capped (kMaxDuty) because the motor can draw more than the 5 V
//       output gives on battery.
//   Power: battery level and voltage, USB voltage, charging, and the reset
//   reason (a brownout when the motor starts shows here after the restart).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tender {

struct PowerInfo {
  int level = -1;       // battery %, -1 = unknown
  int battery_mv = -1;  // -1 = unknown
  int vbus_mv = -1;     // USB voltage, -1 = unknown
  bool charging = false;
  int32_t current_ma = 0;  // + charge, - discharge; 0 = the board cannot measure it
};

enum class Pull : uint8_t { Up, Down };
enum class Vibro : uint8_t { Off, On, Pulse };

class SelfTest {
 public:
  static constexpr uint8_t kMaxDuty = 128;  // 50 %: the motor maker's test point
  static constexpr uint8_t kDefaultDuty = 90;
  static constexpr uint32_t kPulseMs = 2000;  // the same pattern as an alert (app.h Timing)
  static constexpr uint32_t kPulseBlinkMs = 250;
  static constexpr uint32_t kLogMs = 10000;
  static constexpr size_t kLines = 8;
  static constexpr size_t kLineChars = 24;

  // GPIOs that may drive the motor: Hat2 G1-G3 and G5-G8, Grove G9 and G10.
  // Not G4 (marked Boot on the Hat2 pinout) and not G43/G44 (UART0).
  static bool vibro_pin_allowed(int pin);

  SelfTest(int red_pin, int blue_pin, bool active_high_guess);

  // Call when the self-test starts. `reset_reason` is a short word ("POWERON",
  // "BROWNOUT", ...). The current pin levels are taken as "not pressed".
  void begin(uint32_t now, bool red_level, bool blue_level, const char* reset_reason);

  // Inputs, once per loop.
  void keys(bool a_down, bool b_down);
  void dual_levels(bool red_level, bool blue_level);
  void set_power(const PowerInfo& p) { power_ = p; }

  // One serial command line, without the line end. The reply goes to `out`
  // (several lines, each ending with '\n').
  void command(const char* line, uint32_t now, char* out, size_t len);

  // Outputs.
  int vibro_pin() const { return vibro_pin_; }  // -1 = none
  uint8_t vibro_duty(uint32_t now) const;       // PWM duty this frame
  Pull pull() const { return pull_; }
  uint8_t rotation() const { return rotation_; }
  bool exit_requested() const { return exit_; }
  // True once after the command "clearlog": the glue deletes the shift log.
  bool take_clear_log() {
    const bool c = clear_log_;
    clear_log_ = false;
    return c;
  }
  // Changes each time the pins or the pull must be set up again.
  uint32_t setup_version() const { return setup_version_; }
  // A power log line every kLogMs while "log" is on (for the battery time).
  bool log_due(uint32_t now);
  // A message for the serial console after a Dual Button press, once.
  bool take_event(char* out, size_t len);
  void power_line(uint32_t now, char* out, size_t len) const;

  // Screen text: kLines rows of up to kLineChars characters.
  struct Lines {
    char row[kLines][kLineChars + 1];
    bool accent[kLines];
  };
  void lines(uint32_t now, Lines& out) const;

  // For tests and the screen.
  struct DualKey {
    int pin = -1;
    bool idle_level = false;
    bool level = false;
    int presses = 0;
    int active = -1;  // level of a press: -1 unknown, 0 LOW, 1 HIGH
  };
  const DualKey& red() const { return red_; }
  const DualKey& blue() const { return blue_; }
  int a_presses() const { return a_presses_; }
  int b_presses() const { return b_presses_; }
  uint8_t duty() const { return duty_; }
  Vibro vibro() const { return vibro_; }

 private:
  void update_key(DualKey& k, bool level);
  void reset_dual(bool red_level, bool blue_level);

  DualKey red_, blue_;
  bool a_down_ = false, b_down_ = false;
  int a_presses_ = 0, b_presses_ = 0;
  PowerInfo power_;
  char reset_[12] = "";
  int vibro_pin_ = -1;
  uint8_t duty_ = kDefaultDuty;
  Vibro vibro_ = Vibro::Off;
  uint32_t pulse_start_ = 0;
  Pull pull_;
  uint8_t rotation_ = 1;
  bool exit_ = false;
  bool clear_log_ = false;
  bool repull_ = false;  // take the next pin levels as "not pressed"
  uint32_t setup_version_ = 0;
  bool log_ = false;
  uint32_t next_log_ = 0;
  char event_[80] = "";
};

}  // namespace tender
