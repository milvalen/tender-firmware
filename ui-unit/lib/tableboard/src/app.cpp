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
  if (!reached(now, alert_until_)) return false;
  return ((alert_until_ - now) / timing_.burst_blink_ms) % 2 == 0;
}

bool App::screen_off(uint32_t now) const { return board_.muted() && idle(now); }

Action App::map(Key key, Press kind, uint32_t now) const {
  // Any hint on the selected table (a waiting guest or the dessert hint) can be acknowledged.
  const bool alert_here = board_.priority(board_.selected(), now) != Priority::None;
  const bool open = board_.selected().request_open;
  switch (key) {
    case Key::A:
      if (kind == Press::Long) return Action::Undo;
      // The main key means "done": with an open request it closes the
      // request and leaves the stage as it is (desk test D15).
      return open ? Action::Request : Action::Step;
    case Key::B:
      if (kind == Press::Short) return Action::Next;
      if (mode_ == KeyMode::FourKeys) return Action::Prev;
      if (alert_here) return Action::Ack;
      return open ? Action::None : Action::Request;
    case Key::Red:
      if (mode_ != KeyMode::FourKeys) return Action::None;
      return kind == Press::Short ? Action::Ack : Action::Mute;
    case Key::Blue:
      // TALK only opens a request; STEP closes it.
      if (mode_ != KeyMode::FourKeys || open) return Action::None;
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
      undo_until_ = now + timing_.undo_hint_ms;
      undo_table_ = board_.selected().number;
      break;
    case Action::Undo:
      undo_until_ = now;
      if (board_.undo(now)) toast_table("UNDONE", now);
      else toast("NOTHING TO UNDO", now);
      break;
    case Action::Next: board_.scroll(1); break;
    case Action::Prev: board_.scroll(-1); break;
    case Action::Ack: {
      const bool dessert = board_.priority(board_.selected(), now) == Priority::Low;
      board_.ack(now);
      alert_until_ = now;
      if (dessert) toast_table("DESSERT OFFERED", now);
      else toast("OK, AGAIN IN 3 MIN", now);
      break;
    }
    case Action::Mute:
      board_.toggle_mute();
      alert_until_ = now;
      toast(board_.muted() ? "MUTED ON" : "MUTED OFF", now);
      break;
    case Action::Request:
      board_.request(now);
      toast_table(board_.selected().request_open ? "REQUEST OPEN" : "REQUEST DONE", now);
      break;
    case Action::None: break;
  }
  if (a != Action::None) push_event(Event::Type::Action, a, Need::None, now);
}

void App::push_event(Event::Type type, Action a, Need need, uint32_t now) {
  Event& e = events_[(event_head_ + event_count_) % kEventQueue];
  if (event_count_ == kEventQueue) event_head_ = (event_head_ + 1) % kEventQueue;  // drop the oldest
  else ++event_count_;
  const Table& t = board_.selected();
  e.type = type;
  e.ms = now;
  e.action = a;
  e.need = need;
  e.table = t.number;
  e.stage = t.stage;
  e.request_open = t.request_open;
  e.muted = board_.muted();
}

bool App::pop_event(Event& out) {
  if (event_count_ == 0) return false;
  out = events_[event_head_];
  event_head_ = (event_head_ + 1) % kEventQueue;
  --event_count_;
  return true;
}

Action App::press(Key key, Press kind, uint32_t now) {
  // With the screen off (MUTED, idle), the first press only turns it on:
  // the server cannot see which table is selected.
  if (screen_off(now)) {
    board_.resort(now);
    last_input_ = now;
    return Action::None;
  }
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
    alert_until_ = now + timing_.burst_ms;
    // MUTED: vibration only, the screen stays as it is (off when idle).
    if (!board_.muted()) last_input_ = now;  // wake the screen
    push_event(Event::Type::Alert, Action::None, board_.need(board_.selected(), now), now);
  }
  if (idle(now) && now - last_idle_resort_ >= timing_.idle_resort_ms) {
    board_.resort(now);  // nobody is reading: safe to reorder
    last_idle_resort_ = now;
  }
  return alert;
}

bool App::vibrate(uint32_t now) const { return blink_on(now); }

bool App::busy(uint32_t now) const {
  return reached(now, alert_until_) || reached(now, toast_until_ + 500);
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
  s.burst_on = blink_on(now) && !board_.muted();  // MUTED: no screen burst
  s.dim = idle(now);
  s.off = screen_off(now);

  // Status: only what needs attention.
  if (battery_ >= 0 && battery_ < 25) snprintf(s.status, sizeof s.status, "LOW BATTERY");
  if (board_.muted()) {
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

  for (size_t i = 0; i < board_.size(); ++i) {
    const Table& t = board_.at(i);
    const Priority p = board_.priority(t, now);
    Screen::Cell& c = s.strip[s.strip_count++];
    c.number = t.number;
    c.selected = i == board_.selected_index();
    c.accent = p != Priority::None;
    c.high = p == Priority::High;
  }

  // The STEP key names what it does next: DONE for an open request (D15),
  // else the next stage (D6). For 5 s after a STEP the status line shows how
  // to take it back (D10, D16).
  const bool undo_hint = reached(now, undo_until_) && sel.number == undo_table_ && sel.can_undo;
  if (undo_hint) {
    size_t n = strlen(s.status);
    snprintf(s.status + n, sizeof s.status - n, "%sHOLD STEP = UNDO", n ? "  " : "");
  }
  const char* step = sel.request_open ? "DONE" : step_label(sel.stage);
  // The label of the key that ends the alert is orange (desk test D9):
  // STEP for a guest request or a stage alert, ACK for the dessert hint.
  const Need need = board_.need(sel, now);
  const bool req_due = need == Need::Request;
  const bool step_due = need == Need::Stage && prio == Priority::High;
  const bool ack_due = prio == Priority::Low;
  if (mode_ == KeyMode::FourKeys) {
    // Function names, the same words as engraved next to the keys
    // (design review rev 0.1, R2).
    const char* labels[] = {step, "NEXT", "ACK", "TALK"};
    for (int i = 0; i < 4; ++i) snprintf(s.keys[i], sizeof s.keys[i], "%s", labels[i]);
    s.key_accent[0] = step_due || req_due;
    s.key_accent[2] = ack_due;
    s.key_count = 4;
  } else {
    snprintf(s.keys[0], sizeof s.keys[0], "%s", step);
    snprintf(s.keys[1], sizeof s.keys[1], "NEXT");
    const char* hold = prio != Priority::None ? "HOLD NEXT: ACK"
                       : sel.request_open     ? ""
                                              : "HOLD NEXT: TALK";
    snprintf(s.keys[2], sizeof s.keys[2], "%s", hold);
    s.key_accent[0] = step_due || req_due;
    s.key_accent[2] = ack_due;
    s.key_count = 3;
  }
}

}  // namespace tender
