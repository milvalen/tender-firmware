#include "referee.h"

#include "common.h"

namespace sim {

using tender::Action;
using tender::Key;
using tender::Press;
using tender::Stage;

namespace {

constexpr uint32_t kExpectationWindow = 90 * kSec;
constexpr uint32_t kAlertWindow = 60 * kSec;

const char* key_name(Key k) {
  switch (k) {
    case Key::A: return "A";
    case Key::B: return "B";
    case Key::Red: return "RED";
    case Key::Blue: return "BLUE";
  }
  return "?";
}

bool is_stage_want(Want w) { return w == Want::Stage; }

// REQUEST ends a request alert, STEP ends a stage alert.
bool ends(Action action, const AlertRecord& a) {
  return (action == Action::Request && a.request) || (action == Action::Step && !a.request);
}

uint32_t seconds_since(uint32_t since, uint32_t now) { return (now - since) / kSec; }

}  // namespace

Referee::Id Referee::expect(uint32_t now, uint8_t table, Want want, Stage stage, const std::string& what) {
  for (Expectation& e : expectations_) {
    if (e.open && e.table == table && is_stage_want(e.want) == is_stage_want(want)) miss(e, now);
  }
  Expectation e;
  e.table = table;
  e.want = want;
  e.stage = stage;
  e.what = what;
  e.since = now;
  expectations_.push_back(e);
  return expectations_.size() - 1;
}

void Referee::drop(Id id, uint32_t now) {
  Expectation& e = expectations_[id];
  if (!e.open) return;
  e.open = false;
  log_.add(now, "EXPECT %s %s: dropped, the floor changed", table_name(e.table).c_str(), e.what.c_str());
}

void Referee::check(uint32_t now) {
  for (Expectation& e : expectations_) {
    if (!e.open) continue;
    if (shown(e)) {
      e.open = false;
      e.met = true;
      e.met_at = now;
      log_.add(now, "DONE %s %s in %u s", table_name(e.table).c_str(), e.what.c_str(), seconds_since(e.since, now));
    } else if (now - e.since > kExpectationWindow) {
      miss(e, now);
    }
  }
  for (AlertRecord& a : alerts_) {
    if (a.open && now - a.since > kAlertWindow) {
      a.open = false;
      a.missed = true;
      log_.add(now, "MISSED ALERT %s", table_name(a.table).c_str());
    }
  }
}

void Referee::alert(uint32_t now, bool check_already_brought) {
  const tender::Table& t = board_.selected();
  AlertRecord a;
  a.table = t.number;
  a.since = now;
  a.request = board_.need(t, now) == tender::Need::Request;
  a.false_alarm = check_already_brought;
  alerts_.push_back(a);
  const char* text = tender::action_text(board_, t, now);
  log_.add(now, "ALERT %s %s%s", table_name(a.table).c_str(), text ? text : "",
           a.false_alarm ? " (check already brought)" : "");
}

void Referee::key_press(Key key, Press kind, Action action, uint8_t table, const PressContext& context,
                        uint32_t now) {
  if (action == Action::None) {
    log_.add(now, "KEY %s%s: no function", key_name(key), kind == Press::Long ? " hold" : "");
    return;
  }
  ++presses_;
  if (action == Action::Next || action == Action::Prev) {
    ++nav_presses_;
    return;
  }
  if (action == Action::Ack || action == Action::Mute) {
    log_.add(now, "KEY %s on %s", tender::action_name(action), table_name(table).c_str());
    handle_all_alerts(now);
    return;
  }

  const bool ok = valid(action, table, context);
  const bool extra = !ok && context.guests_at_table;
  if (ok) {
    handle_alerts(now, table, action);
  } else {
    Mistake m;
    m.table = table;
    m.at = now;
    m.action = action;
    m.extra = extra;
    mistakes_.push_back(m);
  }
  const char* verdict = ok ? "" : extra ? "  << EXTRA PRESS" : "  << WRONG TABLE";
  log_.add(now, "KEY %s on %s%s", tender::action_name(action), table_name(table).c_str(), verdict);
}

void Referee::question(uint32_t now) {
  ++questions_;
  log_.add(now, "QUESTION (player not sure)");
}

bool Referee::shown(const Expectation& e) const {
  const tender::Table* t = find_table(board_, e.table);
  if (!t) return false;
  switch (e.want) {
    case Want::Stage: return t->stage == e.stage;
    case Want::RequestOpen: return t->request_open;
    case Want::RequestClosed: return !t->request_open;
  }
  return false;
}

void Referee::miss(Expectation& e, uint32_t now) {
  e.open = false;
  e.missed = true;
  log_.add(now, "MISSED %s %s", table_name(e.table).c_str(), e.what.c_str());
}

// Every check runs: each one also records something (presses, corrections).
bool Referee::valid(Action action, uint8_t table, const PressContext& context) {
  bool ok = false;
  const bool stage_key = action == Action::Step || action == Action::Undo;
  for (Expectation& e : expectations_) {
    if (!e.open || e.table != table) continue;
    ok = true;  // something is due on this table
    if (e.want == Want::Stage && stage_key) ++e.stage_presses;
  }
  for (const AlertRecord& a : alerts_)
    if (a.open && a.table == table && ends(action, a)) ok = true;
  if (context.own_move) ok = true;
  if (action == Action::Undo && undo_mistake(table)) ok = true;
  return ok;
}

// UNDO corrects the last uncorrected mistake on the table.
bool Referee::undo_mistake(uint8_t table) {
  for (auto it = mistakes_.rbegin(); it != mistakes_.rend(); ++it) {
    if (it->table == table && !it->corrected) {
      it->corrected = true;
      return true;
    }
  }
  return false;
}

void Referee::handle_alerts(uint32_t now, uint8_t table, Action action) {
  for (AlertRecord& a : alerts_)
    if (a.open && a.table == table && ends(action, a)) handle(a, now);
}

void Referee::handle_all_alerts(uint32_t now) {
  for (AlertRecord& a : alerts_)
    if (a.open) handle(a, now);
}

void Referee::handle(AlertRecord& a, uint32_t now) {
  a.open = false;
  a.handled = true;
  a.handled_at = now;
  log_.add(now, "HANDLED ALERT %s in %u s", table_name(a.table).c_str(), seconds_since(a.since, now));
}

}  // namespace sim
