#include "app.h"

#include <stdio.h>
#include <string.h>

namespace tender {

const char* action_name(Action a) {
  switch (a) {
    case Action::None: return "NONE";
    case Action::Step: return "STEP";
    case Action::Undo: return "UNDO";
    case Action::Next: return "NEXT";
    case Action::Prev: return "PREV";
    case Action::Ack: return "ACK";
    case Action::Mute: return "MUTE";
    case Action::Request: return "REQUEST";
  }
  return "?";
}

App::App(Board& board, KeyMode mode, Timing timing)
    : board_(board), mode_(mode), timing_(timing) {}

static bool reached(uint32_t now, uint32_t until) { return static_cast<int32_t>(now - until) < 0; }

bool App::blink_on(uint32_t now) const {
  if (!reached(now, burst_until_)) return false;
  return ((burst_until_ - now) / timing_.burst_blink_ms) % 2 == 0;
}

Action App::map(Key key, Press kind, uint32_t now) const {
  const bool alert_here = board_.priority(board_.selected(), now) == Priority::High;
  switch (key) {
    case Key::A: return kind == Press::Short ? Action::Step : Action::Undo;
    case Key::B:
      if (kind == Press::Short) return Action::Next;
      if (mode_ == KeyMode::FourKeys) return Action::Prev;
      // An open request is closed first: otherwise an overdue request could
      // only be acknowledged and would alert again and again.
      if (board_.selected().request_open) return Action::Request;
      return alert_here ? Action::Ack : Action::Request;
    case Key::Red:
      if (mode_ != KeyMode::FourKeys) return Action::None;
      return kind == Press::Short ? Action::Ack : Action::Mute;
    case Key::Blue:
      if (mode_ != KeyMode::FourKeys) return Action::None;
      return Action::Request;
  }
  return Action::None;
}

void App::toast(const char* text, uint32_t now) {
  snprintf(toast_, sizeof toast_, "%s", text);
  toast_until_ = now + timing_.toast_ms;
}

void App::toast_table(const char* what, uint32_t now) {
  char label[8], line[28];
  table_label(board_.selected().number, label, sizeof label);
  snprintf(line, sizeof line, "%s %s", label, what);
  toast(line, now);
}

void App::run(Action a, uint32_t now) {
  switch (a) {
    case Action::Step:
      board_.step(now);
      toast_table(stage_status(board_.selected().stage), now);
      break;
    case Action::Undo:
      if (board_.undo(now)) toast_table("UNDONE", now);
      else toast("NOTHING TO UNDO", now);
      break;
    case Action::Next: board_.scroll(1); break;
    case Action::Prev: board_.scroll(-1); break;
    case Action::Ack:
      board_.ack(now);
      burst_until_ = now;
      toast("OK, AGAIN IN 3 MIN", now);
      break;
    case Action::Mute:
      board_.mute(now);
      burst_until_ = now;
      toast("MUTED 10 MIN", now);
      break;
    case Action::Request:
      board_.request(now);
      toast_table(board_.selected().request_open ? "REQUEST OPEN" : "REQUEST DONE", now);
      break;
    case Action::None: break;
  }
}

Action App::press(Key key, Press kind, uint32_t now) {
  Action a = map(key, kind, now);
  if (a == Action::None) return a;
  if (idle(now)) board_.resort(now);  // the server starts reading: order it now
  run(a, now);
  last_input_ = now;
  return a;
}

bool App::tick(uint32_t now) {
  bool alert = board_.poll_alert(now);
  if (alert) {
    burst_until_ = now + timing_.burst_ms;
    last_input_ = now;  // wake the screen
  }
  if (idle(now) && now - last_idle_resort_ >= timing_.idle_resort_ms) {
    board_.resort(now);  // nobody is reading: safe to reorder
    last_idle_resort_ = now;
  }
  return alert;
}

bool App::vibrate(uint32_t now) const { return blink_on(now); }

bool App::busy(uint32_t now) const {
  return reached(now, burst_until_) || reached(now, toast_until_ + 500);
}

static void minutes_text(const Board& b, const Table& t, uint32_t now, char* buf, size_t len) {
  if (t.stage == Stage::Free && !t.request_open) {
    snprintf(buf, len, "FREE");
    return;
  }
  uint32_t m = b.elapsed_min(t, now);
  snprintf(buf, len, "%lu MIN", static_cast<unsigned long>(m > 99 ? 99 : m));
}

void App::screen(uint32_t now, Screen& s) const {
  memset(&s, 0, sizeof s);
  s.burst_on = blink_on(now);
  s.dim = idle(now);

  // Status: only what needs attention.
  if (battery_ >= 0 && battery_ < 25) snprintf(s.status, sizeof s.status, "LOW BATTERY");
  if (board_.muted(now)) {
    size_t n = strlen(s.status);
    snprintf(s.status + n, sizeof s.status - n, "%sMUTED", n ? "  " : "");
  }

  const Table& sel = board_.selected();
  const Priority prio = board_.priority(sel, now);
  table_label(sel.number, s.table, sizeof s.table);
  s.table_accent = prio == Priority::High;

  const char* action = action_text(board_, sel, now);
  if (reached(now, toast_until_)) {
    snprintf(s.line2, sizeof s.line2, "%s", toast_);
    s.line2_toast = true;
  } else if (action) {
    snprintf(s.line2, sizeof s.line2, "%s", action);
    s.line2_accent = prio != Priority::None;
  } else {
    snprintf(s.line2, sizeof s.line2, "%s", stage_status(sel.stage));
  }

  minutes_text(board_, sel, now, s.minutes, sizeof s.minutes);
  s.progress = board_.progress(sel, now);

  const size_t me = board_.selected_rank();
  for (size_t i = 0; i < board_.size() && s.row_count < 2; ++i) {
    if (i == me) continue;
    const Table& t = board_.ranked(i);
    const char* a = action_text(board_, t, now);
    char label[8], mins[12];
    table_label(t.number, label, sizeof label);
    minutes_text(board_, t, now, mins, sizeof mins);
    Screen::Row& r = s.rows[s.row_count++];
    snprintf(r.text, sizeof r.text, "%s %s", label, a ? a : mins);
    r.accent = board_.priority(t, now) != Priority::None;
  }

  if (mode_ == KeyMode::FourKeys) {
    const char* labels[] = {"A STEP", "B NEXT", "RED ACK", "BLUE REQ"};
    for (int i = 0; i < 4; ++i) snprintf(s.keys[i], sizeof s.keys[i], "%s", labels[i]);
    s.key_count = 4;
  } else {
    snprintf(s.keys[0], sizeof s.keys[0], "A STEP");
    snprintf(s.keys[1], sizeof s.keys[1], "B NEXT");
    const char* hold = sel.request_open ? "HOLD B: DONE" : prio == Priority::High ? "HOLD B: ACK" : "HOLD B: REQ";
    snprintf(s.keys[2], sizeof s.keys[2], "%s", hold);
    s.key_accent[2] = prio == Priority::High;
    s.key_count = 3;
  }
}

}  // namespace tender
