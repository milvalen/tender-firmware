// Shift log for TENDER TND-01: what happened on the device during a shift.
//
// Hardware-independent core. The StickS3 glue appends 8-byte records to a
// file in flash and prints them as CSV lines on the serial command "L";
// tools/shift_log.py reads them after the shift. The log holds table
// numbers, stages, key actions and alerts only: no names, no amounts.
//
// CSV columns: boot,ms,event,table,stage,detail
//   boot    boot number since the log was cleared (1, 2, ...)
//   ms      milliseconds since that boot
//   event   BOOT, FIRMWARE, STEP, UNDO, NEXT, PREV, ACK, MUTE, REQUEST,
//           ALERT, BATTERY
//   table   the selected table after the event (empty for BOOT, BATTERY)
//   stage   its stage after the event: FREE SEAT ORD MAIN BILL PAY
//   detail  BOOT: reset=<reason>; FIRMWARE: commit=<8 hex digits>[+dirty]
//           (right after BOOT, with the time of the BOOT);
//           key actions: request=0|1 muted=0|1;
//           ALERT: the alert text (TAKE ORDER, BRING CHECK, ...);
//           BATTERY: the level in %
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "app.h"

namespace tender {

namespace shiftlog {

constexpr int kVersion = 1;

enum class Kind : uint8_t { Boot = 1, Action = 2, Alert = 3, Battery = 4, Firmware = 5 };

// Reset reasons, as the glue reports them.
enum class Reset : uint8_t { Other, PowerOn, Software, Panic, Watchdog, Brownout, DeepSleep, External };
const char* reset_name(Reset r);

// One record in flash. Little-endian on both the ESP32 and a desktop.
struct Record {
  uint32_t ms;
  uint8_t kind;
  uint8_t table;
  uint8_t code;   // Action, Need, Reset or battery %
  uint8_t flags;  // bits 0-3 stage, bit 4 request open, bit 5 muted
};
static_assert(sizeof(Record) == 8, "a record is 8 bytes in flash");

Record from_event(const Event& e);
Record boot(uint32_t ms, Reset reason);
Record battery(uint32_t ms, int level);
// The firmware commit: the 32 bits of the record time hold the 8 hex digits.
Record firmware(uint32_t commit, bool dirty);

// Formats records as CSV lines. Boot numbers count BOOT records, so give it
// the records in file order, oldest first.
class Writer {
 public:
  static const char* header() { return "boot,ms,event,table,stage,detail"; }
  // Writes one line with '\n' to `out`. Returns false for a record that is
  // not valid (an erased or broken part of the file): skip it.
  bool line(const Record& r, char* out, size_t len);
  int boots() const { return boot_; }

 private:
  int boot_ = 0;
  uint32_t boot_ms_ = 0;
};

// The text of an alert, from the stage and the need (no board needed).
const char* alert_text(Stage stage, Need need);

}  // namespace shiftlog
}  // namespace tender
