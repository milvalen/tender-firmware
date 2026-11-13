// Time-stamped log of a desk test: floor events, key presses and results.
#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include <string>
#include <vector>

#include "common.h"

namespace sim {

class EventLog {
 public:
  // Adds "[mm:ss] text".
  __attribute__((format(printf, 3, 4))) void add(uint32_t now, const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    lines_.push_back("[" + clock_text(now) + "] " + buf);
  }

  const std::vector<std::string>& lines() const { return lines_; }

 private:
  std::vector<std::string> lines_;
};

}  // namespace sim
