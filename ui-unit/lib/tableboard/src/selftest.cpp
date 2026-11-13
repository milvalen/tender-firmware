#include "selftest.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace tender {

namespace {

bool reached(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

const char* level_text(bool level) { return level ? "HI" : "LO"; }

const char* active_text(int active) { return active < 0 ? "?" : active ? "HI" : "LO"; }

int duty_percent(uint8_t duty) { return (duty * 100 + 127) / 255; }

const char* vibro_text(Vibro v) {
  switch (v) {
    case Vibro::Off: return "OFF";
    case Vibro::On: return "ON";
    case Vibro::Pulse: return "PULSE";
  }
  return "";
}

// Appends printf output to a buffer that already holds a string.
void add(char* out, size_t len, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
void add(char* out, size_t len, const char* fmt, ...) {
  const size_t n = strlen(out);
  if (n + 1 >= len) return;
  va_list args;
  va_start(args, fmt);
  vsnprintf(out + n, len - n, fmt, args);
  va_end(args);
}

// Splits "word rest" into a lower-case word and the rest.
const char* split(const char* line, char* word, size_t len) {
  while (*line == ' ') ++line;
  size_t i = 0;
  while (*line && *line != ' ') {
    char c = *line++;
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (i + 1 < len) word[i++] = c;
  }
  word[i] = '\0';
  while (*line == ' ') ++line;
  return line;
}

bool parse_int(const char* s, int& v) {
  if (*s == '\0') return false;
  char* end = nullptr;
  long x = strtol(s, &end, 10);
  while (end && *end == ' ') ++end;
  if (!end || *end != '\0') return false;
  v = static_cast<int>(x);
  return true;
}

}  // namespace

bool SelfTest::vibro_pin_allowed(int pin) {
  return (pin >= 1 && pin <= 3) || (pin >= 5 && pin <= 10);
}

SelfTest::SelfTest(int red_pin, int blue_pin, bool active_high_guess)
    : pull_(active_high_guess ? Pull::Down : Pull::Up) {
  red_.pin = red_pin;
  blue_.pin = blue_pin;
}

void SelfTest::begin(uint32_t now, bool red_level, bool blue_level, const char* reset_reason) {
  snprintf(reset_, sizeof reset_, "%s", reset_reason ? reset_reason : "");
  reset_dual(red_level, blue_level);
  a_presses_ = b_presses_ = 0;
  vibro_ = Vibro::Off;
  exit_ = false;
  next_log_ = now;
}

void SelfTest::reset_dual(bool red_level, bool blue_level) {
  DualKey* both[] = {&red_, &blue_};
  for (DualKey* k : both) {
    const bool level = k == &red_ ? red_level : blue_level;
    k->idle_level = k->level = level;
    k->presses = 0;
    k->active = -1;
  }
}

void SelfTest::keys(bool a_down, bool b_down) {
  if (a_down && !a_down_) ++a_presses_;
  if (b_down && !b_down_) ++b_presses_;
  a_down_ = a_down;
  b_down_ = b_down;
}

void SelfTest::update_key(DualKey& k, bool level) {
  if (level != k.level && level != k.idle_level) {
    ++k.presses;
    k.active = level ? 1 : 0;
    snprintf(event_, sizeof event_, "%s G%d: a press reads %s, so kDualActiveHigh = %s\n",
             &k == &red_ ? "RED" : "BLUE", k.pin, level ? "HIGH" : "LOW", level ? "true" : "false");
  }
  k.level = level;
}

void SelfTest::dual_levels(bool red_level, bool blue_level) {
  if (repull_) {  // the pull just changed: these levels are "not pressed"
    reset_dual(red_level, blue_level);
    repull_ = false;
    return;
  }
  update_key(red_, red_level);
  update_key(blue_, blue_level);
}

bool SelfTest::take_event(char* out, size_t len) {
  if (event_[0] == '\0') return false;
  snprintf(out, len, "%s", event_);
  event_[0] = '\0';
  return true;
}

uint8_t SelfTest::vibro_duty(uint32_t now) const {
  if (vibro_pin_ < 0) return 0;
  switch (vibro_) {
    case Vibro::Off: return 0;
    case Vibro::On: return duty_;
    case Vibro::Pulse: {
      const uint32_t t = now - pulse_start_;
      if (t >= kPulseMs) return 0;
      return (t / kPulseBlinkMs) % 2 == 0 ? duty_ : 0;
    }
  }
  return 0;
}

bool SelfTest::log_due(uint32_t now) {
  if (!log_ || !reached(now, next_log_)) return false;
  next_log_ = now + kLogMs;
  return true;
}

void SelfTest::power_line(uint32_t now, char* out, size_t len) const {
  out[0] = '\0';
  add(out, len, "t=%lus", static_cast<unsigned long>(now / 1000));
  if (power_.level >= 0) add(out, len, " bat=%d%%", power_.level);
  else add(out, len, " bat=?");
  if (power_.battery_mv > 0) add(out, len, " %dmV", power_.battery_mv);
  if (power_.vbus_mv >= 0) add(out, len, " usb=%dmV", power_.vbus_mv);
  add(out, len, " charging=%s", power_.charging ? "yes" : "no");
  if (power_.current_ma != 0) add(out, len, " I=%ldmA", static_cast<long>(power_.current_ma));
  else add(out, len, " I=n/a");
  if (vibro_pin_ >= 0)
    add(out, len, " vibro=G%d %d%% %s", vibro_pin_, duty_percent(duty_), vibro_text(vibro_));
  add(out, len, "\n");
}

void SelfTest::command(const char* line, uint32_t now, char* out, size_t len) {
  out[0] = '\0';
  char word[12];
  const char* rest = split(line, word, sizeof word);
  int n = 0;

  if (word[0] == '\0') return;
  if (strcmp(word, "help") == 0) {
    add(out, len,
        "help            this list\n"
        "keys            key counts and the level of a press\n"
        "pull up|down    input pull on the Dual Button pins (now %s)\n"
        "rot 1|3         display rotation (now %u)\n"
        "vibro <pin>|off motor GPIO: 1-3, 5-8 (Hat2), 9, 10 (Grove)\n"
        "duty <0-%u>    motor PWM duty of 255 (now %u, %d%%)\n"
        "pulse           one alert pattern, 2 s\n"
        "on | off        motor on or off\n"
        "power           battery and USB now\n"
        "log             power line every 10 s on or off\n"
        "clearlog        delete the shift log in flash\n"
        "exit            restart into the table board\n",
        pull_ == Pull::Up ? "up" : "down", rotation_, kMaxDuty, duty_, duty_percent(duty_));
  } else if (strcmp(word, "keys") == 0) {
    add(out, len, "A %d  B %d\n", a_presses_, b_presses_);
    const DualKey* both[] = {&red_, &blue_};
    for (const DualKey* k : both) {
      add(out, len, "%s G%d: now %s, %d presses, a press reads %s\n", k == &red_ ? "RED" : "BLUE",
          k->pin, k->level ? "HIGH" : "LOW", k->presses,
          k->active < 0 ? "? (press the key)" : k->active ? "HIGH" : "LOW");
    }
    if (red_.active >= 0 && red_.active == blue_.active)
      add(out, len, "set kDualActiveHigh = %s\n", red_.active ? "true" : "false");
  } else if (strcmp(word, "pull") == 0) {
    if (strcmp(rest, "up") == 0 || strcmp(rest, "down") == 0) {
      pull_ = rest[0] == 'u' ? Pull::Up : Pull::Down;
      repull_ = true;
      ++setup_version_;
      add(out, len, "pull %s; key counts reset\n", rest);
    } else {
      add(out, len, "use: pull up | pull down\n");
    }
  } else if (strcmp(word, "rot") == 0) {
    if (parse_int(rest, n) && (n == 1 || n == 3)) {
      rotation_ = static_cast<uint8_t>(n);
      ++setup_version_;
      add(out, len, "rotation %d\n", n);
    } else {
      add(out, len, "use: rot 1 | rot 3\n");
    }
  } else if (strcmp(word, "vibro") == 0) {
    if (strcmp(rest, "off") == 0) {
      vibro_pin_ = -1;
      vibro_ = Vibro::Off;
      ++setup_version_;
      add(out, len, "motor pin released\n");
    } else if (parse_int(rest, n) && vibro_pin_allowed(n) && n != red_.pin && n != blue_.pin) {
      vibro_pin_ = n;
      vibro_ = Vibro::Off;
      ++setup_version_;
      add(out, len, "motor on G%d, duty %u (%d%%), off; type pulse or on\n", n, duty_,
          duty_percent(duty_));
    } else if (parse_int(rest, n) && (n == red_.pin || n == blue_.pin)) {
      add(out, len, "G%d is a Dual Button pin in this test\n", n);
    } else {
      add(out, len, "use: vibro <pin>, pin 1-3, 5-8 or 9-10; vibro off\n");
    }
  } else if (strcmp(word, "duty") == 0) {
    if (parse_int(rest, n) && n >= 0 && n <= kMaxDuty) {
      duty_ = static_cast<uint8_t>(n);
      add(out, len, "duty %d of 255 (%d%%)\n", n, duty_percent(duty_));
    } else {
      add(out, len, "use: duty <0-%u>; above %u the motor can draw more than the 5 V output gives\n",
          kMaxDuty, kMaxDuty);
    }
  } else if (strcmp(word, "pulse") == 0 || strcmp(word, "on") == 0) {
    if (vibro_pin_ < 0) {
      add(out, len, "no motor pin; type vibro <pin> first\n");
    } else {
      vibro_ = word[0] == 'p' ? Vibro::Pulse : Vibro::On;
      pulse_start_ = now;
      add(out, len, "motor %s on G%d, %d%%\n", word[0] == 'p' ? "pulse" : "on", vibro_pin_,
          duty_percent(duty_));
    }
  } else if (strcmp(word, "off") == 0) {
    vibro_ = Vibro::Off;
    add(out, len, "motor off\n");
  } else if (strcmp(word, "power") == 0) {
    power_line(now, out, len);
  } else if (strcmp(word, "log") == 0) {
    log_ = !log_;
    next_log_ = now;
    add(out, len, "power log %s\n", log_ ? "on, every 10 s" : "off");
  } else if (strcmp(word, "clearlog") == 0) {
    clear_log_ = true;
    add(out, len, "shift log cleared\n");
  } else if (strcmp(word, "exit") == 0) {
    vibro_ = Vibro::Off;
    exit_ = true;
    add(out, len, "restart\n");
  } else {
    add(out, len, "unknown command \"%s\"; type help\n", word);
  }
}

void SelfTest::lines(uint32_t now, Lines& out) const {
  memset(&out, 0, sizeof out);
  auto row = [&out](size_t i, bool accent) -> char* {
    out.accent[i] = accent;
    return out.row[i];
  };
  const size_t w = sizeof out.row[0];
  const bool bad_reset = strcmp(reset_, "POWERON") != 0 && strcmp(reset_, "USB") != 0 &&
                         strcmp(reset_, "SOFTWARE") != 0 && reset_[0] != '\0';
  snprintf(row(0, bad_reset), w, "SELF TEST %s", reset_);
  snprintf(row(1, false), w, "A %d  B %d", a_presses_, b_presses_);
  for (int i = 0; i < 2; ++i) {
    const DualKey& k = i == 0 ? red_ : blue_;
    snprintf(row(static_cast<size_t>(2 + i), k.level != k.idle_level), w, "%s G%d %s %d KEY=%s",
             i == 0 ? "RED" : "BLU", k.pin, level_text(k.level), k.presses, active_text(k.active));
  }
  if (vibro_pin_ < 0) snprintf(row(4, false), w, "%s", "VIB NO PIN");
  else
    snprintf(row(4, vibro_duty(now) > 0), w, "VIB G%d %d%% %s", vibro_pin_, duty_percent(duty_),
             vibro_text(vibro_));
  if (power_.level >= 0)
    snprintf(row(5, power_.level < 25), w, "BAT %d%% %dMV%s", power_.level, power_.battery_mv,
             power_.charging ? " CHG" : "");
  else snprintf(row(5, false), w, "%s", "BAT ?");
  char usb[12];
  if (power_.vbus_mv < 0) snprintf(usb, sizeof usb, "USB ?");
  else if (power_.vbus_mv < 4000) snprintf(usb, sizeof usb, "USB OFF");
  else snprintf(usb, sizeof usb, "USB %dMV", power_.vbus_mv);
  if (power_.current_ma != 0) snprintf(row(6, false), w, "%s I %ldMA", usb, static_cast<long>(power_.current_ma));
  else snprintf(row(6, false), w, "%s I N/A", usb);
  snprintf(row(7, false), w, "%s", "< HAT2 END");
}

}  // namespace tender
