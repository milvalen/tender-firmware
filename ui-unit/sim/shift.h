// Shift script for the TND-01 desk test.
//
// Each party has its own timeline. Times are shift seconds. The simulator
// tells the player what happens on the floor ("Guests sit down at TBL-04");
// the player must keep the device up to date. Some steps are not told: the
// device must remind the player (order_after_s = -1, handled_after_s = -1).
#pragma once

#include <stdint.h>

namespace sim {

struct GuestRequest {
  int after_s;          // after the mains are served
  const char* ask;      // "asks for more bread"
  const char* item;     // "the bread"
  int handled_after_s;  // the floor says it is done after this time; -1 = the device must remind
};

struct Party {
  uint8_t table;
  int arrive_s;
  int order_after_s;    // after seating; -1 = no prompt, the device must remind
  int kitchen_s;        // from order to mains
  uint8_t decoy_table;  // kitchen first calls this table by mistake; 0 = none
  GuestRequest requests[2];
  int request_count;
  int dessert_after_s;  // after mains; -1 = no dessert
  int check_after_s;    // after mains
  int pay_after_s;      // after the check request
};

constexpr uint8_t kTables[] = {1, 2, 3, 4, 5, 6, 7, 8};
constexpr int kShiftSeconds = 32 * 60;

// 7 parties on 8 tables, 32 min. Covers: 3 order reminders, 3 guest
// requests (1 reminded by the device), 1 kitchen mix-up that needs UNDO,
// checks with and without dessert, 1 slow payment.
constexpr Party kShift[] = {
    {4, 20, 100, 420, 0, {{60, "asks for more bread", "the bread", 70}, {0, nullptr, nullptr, 0}}, 1, -1, 600, 120},
    {7, 60, -1, 480, 0, {{300, "asks for the wine list", "the wine list", -1}, {0, nullptr, nullptr, 0}}, 1, 600, 900, 150},
    {2, 150, 80, 360, 0, {{0, nullptr, nullptr, 0}, {0, nullptr, nullptr, 0}}, 0, -1, 540, 270},
    {5, 400, -1, 420, 0, {{0, nullptr, nullptr, 0}, {0, nullptr, nullptr, 0}}, 0, -1, 600, 100},
    {1, 500, 60, 360, 5, {{200, "asks for a clean fork", "a clean fork", 40}, {0, nullptr, nullptr, 0}}, 1, -1, 540, 90},
    {8, 900, 120, 400, 0, {{0, nullptr, nullptr, 0}, {0, nullptr, nullptr, 0}}, 0, -1, 400, 60},
    {3, 1200, -1, 360, 0, {{0, nullptr, nullptr, 0}, {0, nullptr, nullptr, 0}}, 0, -1, 900, 60},
};

}  // namespace sim
