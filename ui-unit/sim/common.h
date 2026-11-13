// Small helpers shared by the simulator parts: shift time and table names.
#pragma once

#include <stdint.h>
#include <stdio.h>

#include <string>

#include "tableboard.h"

namespace sim {

constexpr uint32_t kSec = 1000;
constexpr uint32_t kShiftStart = 1000;  // device time of shift second 0

// Device time of a shift second.
inline uint32_t shift_time(int s) { return kShiftStart + static_cast<uint32_t>(s) * kSec; }

// Device time `s` seconds after `t`.
inline uint32_t after(uint32_t t, int s) { return t + static_cast<uint32_t>(s) * kSec; }

// "mm:ss" of the shift clock.
inline std::string clock_text(uint32_t now) {
  const uint32_t s = (now - kShiftStart) / kSec;
  char buf[16];
  snprintf(buf, sizeof buf, "%02u:%02u", s / 60, s % 60);
  return buf;
}

// "TBL-04".
inline std::string table_name(uint8_t table) {
  char buf[8];
  tender::table_label(table, buf, sizeof buf);
  return buf;
}

inline const tender::Table* find_table(const tender::Board& board, uint8_t number) {
  for (size_t i = 0; i < board.size(); ++i)
    if (board.at(i).number == number) return &board.at(i);
  return nullptr;
}

// The stage that STEP moves a table to.
inline tender::Stage stage_after(tender::Stage s) {
  using tender::Stage;
  switch (s) {
    case Stage::Free: return Stage::Seat;
    case Stage::Seat: return Stage::Ord;
    case Stage::Ord: return Stage::Main;
    case Stage::Main: return Stage::Bill;
    case Stage::Bill: return Stage::Pay;
    case Stage::Pay: return Stage::Free;
  }
  return Stage::Free;
}

}  // namespace sim
