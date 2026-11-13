// Shift script for the TND-01 desk test.
//
// Each party has its own timeline in shift seconds. The simulator tells the
// player what happens on the floor ("Guests sit down at TBL-04"), and the
// player keeps the device up to date. Some steps are never told: the device
// must remind the player (kRemind).
#pragma once

#include <stdint.h>

namespace sim {

constexpr int kRemind = -1;  // the floor says nothing: the device must remind
constexpr int kNever = -1;   // the step does not happen

struct GuestRequest {
  int after_mains_s = 0;       // asked this long after the mains are served
  const char* ask = nullptr;   // "asks for more bread"
  const char* item = nullptr;  // "the bread"
  int done_after_s = kRemind;  // the floor says it is done after this time
};

// One party, written as a chain of steps:
//   party_at(4).arrives(20).order_told(100).mains_after(420).check_after(600)
struct Party {
  static constexpr int kMaxRequests = 2;

  uint8_t table = 0;
  int arrive_s = 0;
  int order_after_s = kRemind;  // after seating
  int kitchen_s = 0;            // from the order to the mains
  uint8_t decoy_table = 0;      // the kitchen first calls this table by mistake; 0 = none
  GuestRequest requests[kMaxRequests] = {};
  int request_count = 0;
  int dessert_after_s = kNever;  // after the mains
  int check_after_s = 0;         // after the mains
  int pay_after_s = 0;           // after the check request

  constexpr Party arrives(int s) const { Party p = *this; p.arrive_s = s; return p; }
  constexpr Party order_told(int after_s) const { Party p = *this; p.order_after_s = after_s; return p; }
  constexpr Party order_not_told() const { Party p = *this; p.order_after_s = kRemind; return p; }
  constexpr Party mains_after(int s) const { Party p = *this; p.kitchen_s = s; return p; }
  constexpr Party kitchen_calls_first(uint8_t wrong_table) const {
    Party p = *this;
    p.decoy_table = wrong_table;
    return p;
  }
  constexpr Party request(int after_mains_s, const char* ask, const char* item, int done_after_s) const {
    Party p = *this;
    p.requests[p.request_count++] = GuestRequest{after_mains_s, ask, item, done_after_s};
    return p;
  }
  constexpr Party dessert_after(int s) const { Party p = *this; p.dessert_after_s = s; return p; }
  constexpr Party check_after(int s) const { Party p = *this; p.check_after_s = s; return p; }
  constexpr Party pays_after(int s) const { Party p = *this; p.pay_after_s = s; return p; }
};

constexpr Party party_at(uint8_t table) {
  Party p;
  p.table = table;
  return p;
}

constexpr uint8_t kTables[] = {1, 2, 3, 4, 5, 6, 7, 8};
constexpr int kShiftSeconds = 32 * 60;

// 7 parties on 8 tables, 32 min. Covers: 3 order reminders, 3 guest
// requests (1 reminded by the device), 1 kitchen mix-up that needs UNDO,
// checks with and without dessert, 1 slow payment.
constexpr Party kShift[] = {
    party_at(4).arrives(20).order_told(100).mains_after(420)
        .request(60, "asks for more bread", "the bread", 70)
        .check_after(600).pays_after(120),
    party_at(7).arrives(60).order_not_told().mains_after(480)
        .request(300, "asks for the wine list", "the wine list", kRemind)
        .dessert_after(600).check_after(900).pays_after(150),
    party_at(2).arrives(150).order_told(80).mains_after(360)
        .check_after(540).pays_after(270),
    party_at(5).arrives(400).order_not_told().mains_after(420)
        .check_after(600).pays_after(100),
    party_at(1).arrives(500).order_told(60).mains_after(360).kitchen_calls_first(5)
        .request(200, "asks for a clean fork", "a clean fork", 40)
        .check_after(540).pays_after(90),
    party_at(8).arrives(900).order_told(120).mains_after(400)
        .check_after(400).pays_after(60),
    party_at(3).arrives(1200).order_not_told().mains_after(360)
        .check_after(900).pays_after(60),
};

}  // namespace sim
