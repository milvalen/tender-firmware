// The referee of the desk test. It records what the device should show
// (expectations), the high alerts of the device and the player's mistakes.
// The score is made from these records.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "app.h"
#include "event_log.h"

namespace sim {

// What the device must show to meet an expectation.
enum class Want : uint8_t { Stage, RequestOpen, RequestClosed };

// Something happened on the floor; the player must show it on the device.
struct Expectation {
  uint8_t table = 0;
  Want want = Want::Stage;
  tender::Stage stage = tender::Stage::Free;  // for Want::Stage
  std::string what;                           // "seat guests"
  uint32_t since = 0;
  bool open = true;
  bool met = false;
  bool missed = false;
  uint32_t met_at = 0;
  int stage_presses = 0;  // STEP and UNDO presses on the table while open
};

// A high alert that the device started.
struct AlertRecord {
  uint8_t table = 0;
  uint32_t since = 0;
  bool request = false;      // a guest request; else a stage alert
  bool false_alarm = false;  // the check was already brought
  bool open = true;
  bool handled = false;
  bool missed = false;
  uint32_t handled_at = 0;
};

// A STEP, UNDO or REQUEST that nothing on the floor asked for.
struct Mistake {
  uint8_t table = 0;
  uint32_t at = 0;
  tender::Action action = tender::Action::None;
  bool extra = false;  // right table (guests are there), but no change was due
  bool corrected = false;
};

// What the floor knows about a key press, from the referee's point of view.
struct PressContext {
  bool guests_at_table = false;  // the party is still at the table
  bool own_move = false;         // the server's own step: taking an order nobody told about
};

class Referee {
 public:
  using Id = size_t;

  Referee(const tender::Board& board, EventLog& log) : board_(board), log_(log) {}

  // Opens an expectation. An open expectation of the same kind on the same
  // table is replaced, and counts as missed.
  Id expect(uint32_t now, uint8_t table, Want want, tender::Stage stage, const std::string& what);
  // Closes an expectation without a result: the floor changed.
  void drop(Id id, uint32_t now);
  bool met(Id id) const { return expectations_[id].met; }

  // Marks expectations met or missed, and alerts missed. Run every tick.
  void check(uint32_t now);
  // The device started a high alert on the selected table.
  void alert(uint32_t now, bool check_already_brought);
  // The player pressed a key; `table` was selected before the press.
  void key_press(tender::Key key, tender::Press kind, tender::Action action, uint8_t table,
                 const PressContext& context, uint32_t now);
  // The player was not sure what to do.
  void question(uint32_t now);

  const std::vector<Expectation>& expectations() const { return expectations_; }
  const std::vector<AlertRecord>& alerts() const { return alerts_; }
  const std::vector<Mistake>& mistakes() const { return mistakes_; }
  int questions() const { return questions_; }
  int presses() const { return presses_; }
  int nav_presses() const { return nav_presses_; }

 private:
  bool shown(const Expectation& e) const;
  void miss(Expectation& e, uint32_t now);
  // An action on a table is valid if the floor asked for it.
  bool valid(tender::Action action, uint8_t table, const PressContext& context);
  bool undo_mistake(uint8_t table);
  void handle_alerts(uint32_t now, uint8_t table, tender::Action action);
  void handle_all_alerts(uint32_t now);
  void handle(AlertRecord& a, uint32_t now);

  const tender::Board& board_;
  EventLog& log_;
  std::vector<Expectation> expectations_;
  std::vector<AlertRecord> alerts_;
  std::vector<Mistake> mistakes_;
  int questions_ = 0;
  int presses_ = 0;
  int nav_presses_ = 0;  // NEXT and PREV presses to reach a table
};

}  // namespace sim
