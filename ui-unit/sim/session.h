// One desk-test session: the device logic (board and app), the floor and the
// referee, wired together. Both the interactive game and the autoplay bots
// drive a Session.
#pragma once

#include <stdint.h>

#include "app.h"
#include "event_log.h"
#include "floor.h"
#include "referee.h"
#include "shift.h"

namespace sim {

class Session {
 public:
  explicit Session(tender::KeyMode mode)
      : board_(kTables, sizeof kTables), app_(board_, mode), referee_(board_, log_), floor_(board_, referee_, log_) {}

  // The player pressed a device key.
  void press(tender::Key key, tender::Press kind, uint32_t now) {
    const tender::Table& selected = board_.selected();
    const uint8_t table = selected.number;
    const bool own_move =
        selected.stage == tender::Stage::Seat && floor_.order_is_yours(table);  // read before the press
    const tender::Action action = app_.press(key, kind, now);
    PressContext context;
    context.guests_at_table = floor_.guests_at(table);
    context.own_move = own_move && action == tender::Action::Step;
    referee_.key_press(key, kind, action, table, context, now);
  }

  // Advances the device, the floor and the referee. Returns true when the
  // device starts a high alert.
  bool tick(uint32_t now) {
    const bool alert = app_.tick(now);
    if (alert) referee_.alert(now, floor_.check_brought(board_.selected().number));
    floor_.advance(now);
    referee_.check(now);
    return alert;
  }

  void question(uint32_t now) { referee_.question(now); }

  const tender::Board& board() const { return board_; }
  const tender::App& app() const { return app_; }
  const Referee& referee() const { return referee_; }
  const Floor& floor() const { return floor_; }
  const EventLog& log() const { return log_; }

 private:
  tender::Board board_;
  tender::App app_;
  EventLog log_;
  Referee referee_;
  Floor floor_;
};

}  // namespace sim
