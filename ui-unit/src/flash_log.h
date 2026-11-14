// Shift log storage in flash (LittleFS). The record format and the CSV text
// come from lib/tableboard/src/shiftlog.h; this file only stores and reads.
#pragma once

#include <Print.h>
#include <stdint.h>

#include "shiftlog.h"

namespace unit {

class FlashLog {
 public:
  // Mounts the file system; formats the partition on the first boot.
  bool begin();
  bool ok() const { return ok_; }

  void append(const tender::shiftlog::Record& r);
  // Prints the log as CSV, oldest first. tools/shift_log.py reads this.
  void dump(uint32_t now, Print& out) const;
  void clear();

 private:
  bool ok_ = false;
};

}  // namespace unit
