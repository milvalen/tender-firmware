// Table board and timers for TENDER TND-01.
//
// Hardware-independent core: no Arduino, no heap. Time is passed in as
// milliseconds since boot, so the logic is testable on a desktop.
//
// Behaviour follows tender-vault/20-device/controls.md and the UX audit in
// tender-vault/10-concept/ux-principles.md:
//   STEP     moves the selected table to the next stage, resets its timer
//   UNDO     reverts the last STEP of the selected table
//   ACK      stops the current alert; high alerts come back after 3 min;
//            on the selected table it also clears the dessert hint
//   MUTE     mutes all alerts for 10 min
//   REQUEST  opens a guest request with a 2 min timer; a second press closes it
//
// Alerts have 2 priorities. High (guest waits: order not taken, check
// requested, open request) raises an alert and moves the selection to the
// table. Low (offer dessert) is only marked on the list.
//
// Tables are kept in number order. NEXT and PREV move the selection in this
// order, so each table has a fixed place on the screen strip (desk test D5).
// The urgency order is a snapshot for alerts and logs: it changes only on
// resort().
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tender {

// No dessert stage: the cycle has one STEP for each change the server makes
// at every table (desk test D6). Dessert is only a low hint during MAIN.
enum class Stage : uint8_t { Free, Seat, Ord, Main, Bill };
enum class Need : uint8_t { None, Stage, Request };
enum class Priority : uint8_t { None, Low, High };

constexpr uint32_t kMinute = 60UL * 1000UL;
constexpr uint32_t kNoThreshold = 0;

struct Config {
  // Thresholds approved by Artemy 2026-11-13 (tender-vault decision log).
  uint32_t seat_ms = 3 * kMinute;
  uint32_t ord_ms = kNoThreshold;
  uint32_t main_ms = 15 * kMinute;
  uint32_t bill_ms = 3 * kMinute;
  uint32_t request_ms = 2 * kMinute;
  // A high alert that is not handled comes back after this time.
  uint32_t realert_ms = 3 * kMinute;
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
  // Earliest time of the next high alert for this table (0 = any time).
  uint32_t next_alert = 0;
  // The server offered dessert (ACK on the hint): no hint until the next STEP.
  bool hint_done = false;
  // One level of undo for STEP.
  bool can_undo = false;
  Stage prev_stage = Stage::Free;
  uint32_t prev_since = 0;
};

class Board {
 public:
  static constexpr size_t kMaxTables = 24;

  Board(const uint8_t* numbers, size_t count, Config cfg = Config());

  // Actions on the selected table. Each one re-sorts the list.
  void step(uint32_t now);
  bool undo(uint32_t now);  // false if there is nothing to undo
  void request(uint32_t now);
  // Actions on alerts.
  void ack(uint32_t now);
  void mute(uint32_t now);
  // NEXT / PREV: move the selection in table-number order, with wrap-around.
  void scroll(int delta);
  // Rebuild the list snapshot by urgency. Call on wake and after alerts.
  void resort(uint32_t now);

  // Returns true when a new high alert starts now. The alerting table becomes
  // the selected table and the list is re-sorted. Call once per loop.
  bool poll_alert(uint32_t now);

  // Queries.
  size_t size() const { return count_; }
  const Table& selected() const { return tables_[selected_]; }
  // Table i in number order, and the index of the selected table.
  const Table& at(size_t i) const { return tables_[i < count_ ? i : count_ - 1]; }
  size_t selected_index() const { return selected_; }
  const Table& ranked(size_t i) const { return tables_[order_[i < count_ ? i : count_ - 1]]; }
  size_t selected_rank() const;

  Need need(const Table& t, uint32_t now) const;
  Priority priority(const Table& t, uint32_t now) const;
  bool any_high(uint32_t now) const;
  bool muted(uint32_t now) const { return now < muted_until_; }
  // Fraction of the current threshold used, 0..1+ (0 when there is no threshold).
  float progress(const Table& t, uint32_t now) const;
  uint32_t elapsed_min(const Table& t, uint32_t now) const;

  const Config& config() const { return cfg_; }

 private:
  int64_t urgency(const Table& t, uint32_t now) const;

  Table tables_[kMaxTables];
  uint8_t order_[kMaxTables];
  size_t count_ = 0;
  size_t selected_ = 0;
  uint32_t muted_until_ = 0;
  Config cfg_;
};

// The stage that STEP moves to.
Stage next_stage(Stage s);
// What the next STEP records, for the STEP key label: "ORDER", "CHECK".
const char* step_label(Stage current);
// Short stage code for logs: "SEAT", "MAIN".
const char* stage_name(Stage s);
// What just happened, for the confirmation after STEP: "ORDER TAKEN".
const char* stage_status(Stage s);
// What the server must do now, or nullptr: "BRING CHECK".
const char* action_text(const Board& b, const Table& t, uint32_t now);
// "TBL-07" style label. buf must hold at least 8 bytes.
void table_label(uint8_t number, char* buf, size_t len);

}  // namespace tender
