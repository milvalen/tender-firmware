// Table board and timers for TENDER TND-01.
//
// Hardware-independent core: no Arduino, no heap after setup. Time is passed
// in as milliseconds since boot, so the logic is testable on a desktop.
//
// Behaviour follows tender-vault/20-device/controls.md:
//   STEP     moves a table to the next stage and resets its timer
//   UNDO     reverts the last STEP of the selected table
//   ACK      stops the signal and snoozes it for 3 min
//   MUTE     mutes all signals for 10 min
//   REQUEST  opens a guest request with a 2 min timer; a second press closes it
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tender {

enum class Stage : uint8_t { Free, Seat, Ord, Main, Dsrt, Bill };

constexpr uint32_t kMinute = 60UL * 1000UL;
constexpr uint32_t kNoThreshold = 0;

struct Config {
  // Approved by Artemy 2026-11-13 (tender-vault decision log).
  uint32_t seat_ms = 3 * kMinute;
  uint32_t ord_ms = kNoThreshold;
  uint32_t main_ms = 15 * kMinute;
  uint32_t dsrt_ms = kNoThreshold;
  uint32_t bill_ms = 3 * kMinute;
  uint32_t request_ms = 2 * kMinute;
  uint32_t ack_snooze_ms = 3 * kMinute;
  uint32_t mute_ms = 10 * kMinute;

  uint32_t threshold(Stage s) const;
};

struct Table {
  uint8_t number = 0;
  Stage stage = Stage::Free;
  uint32_t stage_since = 0;
  bool request_open = false;
  uint32_t request_since = 0;
  uint32_t snooze_until = 0;
  // One level of undo for STEP.
  bool can_undo = false;
  Stage prev_stage = Stage::Free;
  uint32_t prev_since = 0;
};

// What a table needs right now.
enum class Need : uint8_t { None, Stage, Request };

class Board {
 public:
  static constexpr size_t kMaxTables = 24;

  Board(const uint8_t* numbers, size_t count, Config cfg = Config());

  // Actions on the selected table.
  void step(uint32_t now);
  void undo(uint32_t now);
  void request(uint32_t now);
  // Actions on signals.
  void ack(uint32_t now);
  void mute(uint32_t now);
  // Wheel: move the selection through the urgency-sorted list.
  void scroll(int delta, uint32_t now);

  // Queries.
  size_t size() const { return count_; }
  const Table& selected() const { return tables_[selected_]; }
  // i-th table in urgency order (0 = most urgent).
  const Table& ranked(size_t i, uint32_t now) const;
  size_t selected_rank(uint32_t now) const;

  Need need(const Table& t, uint32_t now) const;
  bool overdue(const Table& t, uint32_t now) const { return need(t, now) != Need::None; }
  // True while any overdue table is not snoozed and signals are not muted.
  bool signalling(uint32_t now) const;
  bool muted(uint32_t now) const { return now < muted_until_; }
  // Fraction of the current threshold used, 0..1+ (0 when there is no threshold).
  float progress(const Table& t, uint32_t now) const;
  uint32_t elapsed_min(const Table& t, uint32_t now) const;

  const Config& config() const { return cfg_; }

 private:
  // Lower score = more urgent.
  int64_t urgency(const Table& t, uint32_t now) const;
  void rank(uint32_t now, uint8_t* order) const;

  Table tables_[kMaxTables];
  size_t count_ = 0;
  size_t selected_ = 0;
  uint32_t muted_until_ = 0;
  Config cfg_;
};

const char* stage_name(Stage s);

// "TBL-07" style label. buf must hold at least 8 bytes.
void table_label(uint8_t number, char* buf, size_t len);

}  // namespace tender
