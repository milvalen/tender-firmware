// The restaurant floor: plays the shift script (shift.h), tells the player
// what happens and asks the referee to expect the matching device change.
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "event_log.h"
#include "referee.h"
#include "shift.h"

namespace sim {

// A line that the floor told the player.
struct FloorLine {
  uint32_t at;
  std::string text;
};

class Floor {
 public:
  Floor(const tender::Board& board, Referee& referee, EventLog& log);

  // Moves every party along its timeline. Run every tick.
  void advance(uint32_t now);

  // The party is at the table and nobody told the server to take the order:
  // a STEP from SEAT is the server's own move.
  bool order_is_yours(uint8_t table) const;
  bool check_brought(uint8_t table) const;
  bool guests_at(uint8_t table) const;

  const std::vector<FloorLine>& lines() const { return lines_; }

 private:
  enum class Phase : uint8_t { Away, Seated, Ordered, Mains, Check, Done };

  struct RequestProgress {
    bool asked = false;
    uint32_t asked_at = 0;
    Referee::Id opened = 0;  // expectation "request opened"
    bool done = false;
  };

  struct PartyProgress {
    const Party* plan = nullptr;
    Phase phase = Phase::Away;
    uint32_t seated_at = 0, ordered_at = 0, mains_at = 0, check_at = 0;
    bool decoy_called = false;      // the kitchen made its mistake call
    uint32_t decoy_fix_at = 0;      // when the kitchen corrects it; 0 = no mistake
    Referee::Id decoy_mains = 0;    // expectation "mains served" on the wrong table
    RequestProgress requests[Party::kMaxRequests];
    bool dessert_ordered = false;
    bool check_brought = false;
  };

  const PartyProgress* party(uint8_t table) const;

  void away(PartyProgress& s, uint32_t now);
  void seated(PartyProgress& s, uint32_t now);
  void ordered(PartyProgress& s, uint32_t now);
  void mains(PartyProgress& s, uint32_t now);
  void check(PartyProgress& s, uint32_t now);

  void take_order(PartyProgress& s, uint32_t now);
  bool call_wrong_table(PartyProgress& s, uint32_t now);
  void guest_request(PartyProgress& s, int i, uint32_t now);

  void say(uint32_t now, const std::string& text);

  const tender::Board& board_;
  Referee& referee_;
  EventLog& log_;
  std::vector<PartyProgress> parties_;
  std::vector<FloorLine> lines_;
};

}  // namespace sim
