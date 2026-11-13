#include "tableboard.h"

#include <stdio.h>

namespace tender {

uint32_t Config::threshold(Stage s) const {
  switch (s) {
    case Stage::Seat: return seat_ms;
    case Stage::Ord: return ord_ms;
    case Stage::Main: return main_ms;
    case Stage::Bill: return bill_ms;
    case Stage::Free: break;
  }
  return kNoThreshold;
}

const char* stage_name(Stage s) {
  switch (s) {
    case Stage::Free: return "FREE";
    case Stage::Seat: return "SEAT";
    case Stage::Ord: return "ORD";
    case Stage::Main: return "MAIN";
    case Stage::Bill: return "BILL";
  }
  return "?";
}

const char* stage_status(Stage s) {
  switch (s) {
    case Stage::Free: return "TABLE FREE";
    case Stage::Seat: return "GUESTS SEATED";
    case Stage::Ord: return "ORDER TAKEN";
    case Stage::Main: return "MAINS SERVED";
    case Stage::Bill: return "CHECK REQUESTED";
  }
  return "?";
}

const char* action_text(const Board& b, const Table& t, uint32_t now) {
  switch (b.need(t, now)) {
    case Need::Request: return "GUEST REQUEST";
    case Need::Stage:
      switch (t.stage) {
        case Stage::Seat: return "TAKE ORDER";
        case Stage::Main: return "OFFER DESSERT";
        case Stage::Bill: return "BRING CHECK";
        default: return "CHECK TABLE";
      }
    case Need::None: break;
  }
  return nullptr;
}

void table_label(uint8_t number, char* buf, size_t len) {
  snprintf(buf, len, "TBL-%02u", static_cast<unsigned>(number));
}

Stage next_stage(Stage s) {
  switch (s) {
    case Stage::Free: return Stage::Seat;
    case Stage::Seat: return Stage::Ord;
    case Stage::Ord: return Stage::Main;
    case Stage::Main: return Stage::Bill;
    case Stage::Bill: return Stage::Free;
  }
  return Stage::Free;
}

const char* step_label(Stage current) {
  switch (next_stage(current)) {
    case Stage::Seat: return "SEAT";
    case Stage::Ord: return "ORDER";
    case Stage::Main: return "SERVED";
    case Stage::Bill: return "CHECK";
    case Stage::Free: return "CLEAR";
  }
  return "STEP";
}

Board::Board(const uint8_t* numbers, size_t count, Config cfg) : cfg_(cfg) {
  count_ = count < kMaxTables ? count : kMaxTables;
  for (size_t i = 0; i < count_; ++i) {
    // Insertion sort by number: the strip shows tables in number order.
    uint8_t n = numbers[i];
    size_t j = i;
    while (j > 0 && tables_[j - 1].number > n) {
      tables_[j].number = tables_[j - 1].number;
      --j;
    }
    tables_[j].number = n;
  }
  for (size_t i = 0; i < count_; ++i) order_[i] = static_cast<uint8_t>(i);
  resort(0);
}

void Board::step(uint32_t now) {
  Table& t = tables_[selected_];
  t.prev_stage = t.stage;
  t.prev_since = t.stage_since;
  t.can_undo = true;
  t.stage = next_stage(t.stage);
  t.stage_since = now;
  t.next_alert = 0;
  t.hint_done = false;
  if (t.stage == Stage::Free) t.request_open = false;
  resort(now);
}

bool Board::undo(uint32_t now) {
  Table& t = tables_[selected_];
  if (!t.can_undo) return false;
  t.stage = t.prev_stage;
  t.stage_since = t.prev_since;
  t.can_undo = false;
  resort(now);
  return true;
}

void Board::request(uint32_t now) {
  Table& t = tables_[selected_];
  t.request_open = !t.request_open;
  if (t.request_open) {
    t.request_since = now;
    t.next_alert = 0;
  }
  resort(now);
}

void Board::ack(uint32_t now) {
  Table& sel = tables_[selected_];
  if (priority(sel, now) == Priority::Low) sel.hint_done = true;
  for (size_t i = 0; i < count_; ++i) {
    Table& t = tables_[i];
    if (priority(t, now) == Priority::High) t.next_alert = now + cfg_.ack_snooze_ms;
  }
}

void Board::mute(uint32_t now) { muted_until_ = now + cfg_.mute_ms; }

Need Board::need(const Table& t, uint32_t now) const {
  if (t.request_open && now - t.request_since >= cfg_.request_ms) return Need::Request;
  uint32_t th = cfg_.threshold(t.stage);
  if (t.stage == Stage::Main && t.hint_done) return Need::None;  // dessert offered
  if (th != kNoThreshold && now - t.stage_since >= th) return Need::Stage;
  return Need::None;
}

Priority Board::priority(const Table& t, uint32_t now) const {
  switch (need(t, now)) {
    case Need::Request: return Priority::High;
    case Need::Stage:
      if (t.stage != Stage::Main) return Priority::High;
      return t.hint_done ? Priority::None : Priority::Low;
    case Need::None: break;
  }
  return Priority::None;
}

bool Board::any_high(uint32_t now) const {
  for (size_t i = 0; i < count_; ++i)
    if (priority(tables_[i], now) == Priority::High) return true;
  return false;
}

static bool time_reached(uint32_t now, uint32_t at) {
  // at == 0 means "any time"; otherwise compare with wrap-around safety.
  return at == 0 || static_cast<int32_t>(now - at) >= 0;
}

bool Board::poll_alert(uint32_t now) {
  if (muted(now)) return false;
  int best = -1;
  int64_t best_u = 0;
  for (size_t i = 0; i < count_; ++i) {
    const Table& t = tables_[i];
    if (priority(t, now) != Priority::High || !time_reached(now, t.next_alert)) continue;
    int64_t u = urgency(t, now);
    if (best < 0 || u < best_u) {
      best = static_cast<int>(i);
      best_u = u;
    }
  }
  if (best < 0) return false;
  tables_[best].next_alert = now + cfg_.realert_ms;
  if (tables_[best].next_alert == 0) tables_[best].next_alert = 1;
  selected_ = static_cast<size_t>(best);
  resort(now);
  return true;
}

float Board::progress(const Table& t, uint32_t now) const {
  uint32_t th = cfg_.threshold(t.stage);
  if (th == kNoThreshold) return 0.0f;
  return static_cast<float>(now - t.stage_since) / static_cast<float>(th);
}

uint32_t Board::elapsed_min(const Table& t, uint32_t now) const {
  if (t.stage == Stage::Free) return 0;
  return (now - t.stage_since) / kMinute;
}

int64_t Board::urgency(const Table& t, uint32_t now) const {
  // Bands, most urgent first: high need (most overdue first), low need,
  // running with a threshold (least time left first), running without a
  // threshold (longest running first), free tables (by number).
  constexpr int64_t kBand = 1LL << 40;
  if (t.stage == Stage::Free && !t.request_open) return 5 * kBand + t.number;
  int64_t best = 4 * kBand - (now - t.stage_since);
  uint32_t th = cfg_.threshold(t.stage);
  if (th != kNoThreshold) {
    int64_t left = static_cast<int64_t>(th) - static_cast<int64_t>(now - t.stage_since);
    if (left > 0) best = 2 * kBand + left;
    else best = (t.stage == Stage::Main ? kBand : 0) + left;
  }
  if (t.request_open) {
    int64_t left = static_cast<int64_t>(cfg_.request_ms) - static_cast<int64_t>(now - t.request_since);
    int64_t req = left <= 0 ? left : 2 * kBand + left;
    if (req < best) best = req;
  }
  return best;
}

void Board::resort(uint32_t now) {
  for (size_t i = 0; i < count_; ++i) order_[i] = static_cast<uint8_t>(i);
  // Insertion sort: stable, tiny n.
  for (size_t i = 1; i < count_; ++i) {
    uint8_t v = order_[i];
    int64_t u = urgency(tables_[v], now);
    size_t j = i;
    while (j > 0 && urgency(tables_[order_[j - 1]], now) > u) {
      order_[j] = order_[j - 1];
      --j;
    }
    order_[j] = v;
  }
}

size_t Board::selected_rank() const {
  for (size_t i = 0; i < count_; ++i)
    if (order_[i] == selected_) return i;
  return 0;
}

void Board::scroll(int delta) {
  if (count_ == 0) return;
  int n = static_cast<int>(count_);
  int next = (static_cast<int>(selected_) + delta) % n;
  if (next < 0) next += n;
  selected_ = static_cast<size_t>(next);
}

}  // namespace tender
