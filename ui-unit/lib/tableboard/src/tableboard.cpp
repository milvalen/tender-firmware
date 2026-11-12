#include "tableboard.h"

#include <stdio.h>

namespace tender {

uint32_t Config::threshold(Stage s) const {
  switch (s) {
    case Stage::Seat: return seat_ms;
    case Stage::Ord: return ord_ms;
    case Stage::Main: return main_ms;
    case Stage::Dsrt: return dsrt_ms;
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
    case Stage::Dsrt: return "DSRT";
    case Stage::Bill: return "BILL";
  }
  return "?";
}

void table_label(uint8_t number, char* buf, size_t len) {
  snprintf(buf, len, "TBL-%02u", static_cast<unsigned>(number));
}

static Stage next_stage(Stage s) {
  switch (s) {
    case Stage::Free: return Stage::Seat;
    case Stage::Seat: return Stage::Ord;
    case Stage::Ord: return Stage::Main;
    case Stage::Main: return Stage::Dsrt;
    case Stage::Dsrt: return Stage::Bill;
    case Stage::Bill: return Stage::Free;
  }
  return Stage::Free;
}

Board::Board(const uint8_t* numbers, size_t count, Config cfg) : cfg_(cfg) {
  count_ = count < kMaxTables ? count : kMaxTables;
  for (size_t i = 0; i < count_; ++i) tables_[i].number = numbers[i];
}

void Board::step(uint32_t now) {
  Table& t = tables_[selected_];
  t.prev_stage = t.stage;
  t.prev_since = t.stage_since;
  t.can_undo = true;
  t.stage = next_stage(t.stage);
  t.stage_since = now;
  t.snooze_until = 0;
  if (t.stage == Stage::Free) t.request_open = false;
}

void Board::undo(uint32_t /*now*/) {
  Table& t = tables_[selected_];
  if (!t.can_undo) return;
  t.stage = t.prev_stage;
  t.stage_since = t.prev_since;
  t.can_undo = false;
}

void Board::request(uint32_t now) {
  Table& t = tables_[selected_];
  if (t.request_open) {
    t.request_open = false;
  } else {
    t.request_open = true;
    t.request_since = now;
  }
}

void Board::ack(uint32_t now) {
  for (size_t i = 0; i < count_; ++i) {
    if (overdue(tables_[i], now)) tables_[i].snooze_until = now + cfg_.ack_snooze_ms;
  }
}

void Board::mute(uint32_t now) { muted_until_ = now + cfg_.mute_ms; }

Need Board::need(const Table& t, uint32_t now) const {
  if (t.request_open && now - t.request_since >= cfg_.request_ms) return Need::Request;
  uint32_t th = cfg_.threshold(t.stage);
  if (th != kNoThreshold && now - t.stage_since >= th) return Need::Stage;
  return Need::None;
}

bool Board::signalling(uint32_t now) const {
  if (muted(now)) return false;
  for (size_t i = 0; i < count_; ++i) {
    const Table& t = tables_[i];
    if (overdue(t, now) && now >= t.snooze_until) return true;
  }
  return false;
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
  // Bands: overdue (most overdue first), then running with a threshold
  // (least time left first), then running without a threshold (longest
  // running first), then free tables (by number).
  constexpr int64_t kBand = 1LL << 40;
  if (t.stage == Stage::Free && !t.request_open) return 3 * kBand + t.number;
  int64_t best = 2 * kBand - (now - t.stage_since);
  uint32_t th = cfg_.threshold(t.stage);
  if (th != kNoThreshold) {
    int64_t left = static_cast<int64_t>(th) - static_cast<int64_t>(now - t.stage_since);
    best = left <= 0 ? left : kBand + left;
  }
  if (t.request_open) {
    int64_t left = static_cast<int64_t>(cfg_.request_ms) - static_cast<int64_t>(now - t.request_since);
    int64_t req = left <= 0 ? left : kBand + left;
    if (req < best) best = req;
  }
  return best;
}

void Board::rank(uint32_t now, uint8_t* order) const {
  for (size_t i = 0; i < count_; ++i) order[i] = static_cast<uint8_t>(i);
  // Insertion sort: stable, tiny n.
  for (size_t i = 1; i < count_; ++i) {
    uint8_t v = order[i];
    int64_t u = urgency(tables_[v], now);
    size_t j = i;
    while (j > 0 && urgency(tables_[order[j - 1]], now) > u) {
      order[j] = order[j - 1];
      --j;
    }
    order[j] = v;
  }
}

const Table& Board::ranked(size_t i, uint32_t now) const {
  uint8_t order[kMaxTables];
  rank(now, order);
  return tables_[order[i < count_ ? i : count_ - 1]];
}

size_t Board::selected_rank(uint32_t now) const {
  uint8_t order[kMaxTables];
  rank(now, order);
  for (size_t i = 0; i < count_; ++i)
    if (order[i] == selected_) return i;
  return 0;
}

void Board::scroll(int delta, uint32_t now) {
  if (count_ == 0) return;
  uint8_t order[kMaxTables];
  rank(now, order);
  size_t pos = selected_rank(now);
  int n = static_cast<int>(count_);
  int next = (static_cast<int>(pos) + delta) % n;
  if (next < 0) next += n;
  selected_ = order[next];
}

}  // namespace tender
