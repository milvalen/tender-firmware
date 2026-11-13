// TND-01 desk test: a terminal screen simulator that runs the same table
// board and interface logic as the device (lib/tableboard), plays a scripted
// shift and scores the player.
//
//   make sim                      4-key mode (StickS3 + Dual Button unit)
//   make sim ARGS=--two-keys      2-key mode (StickS3 alone)
//   build/tnd01-sim --autoplay good|sloppy [--two-keys] [--verbose]
//
// Keys: a s d f = STEP NEXT ACK REQ (tap), Shift + key = hold.
// Pass: 0 wrong-table actions, 0 missed high alerts, no more than 1 question
// (tender-vault/10-concept/ux-principles.md, desk test).
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <vector>

#include "app.h"
#include "shift.h"

using namespace tender;

namespace {

constexpr uint32_t kSec = 1000;
constexpr uint32_t kStart = 1000;  // device time of shift second 0
constexpr uint32_t kExpectWindow = 90 * kSec;
constexpr uint32_t kAlertWindow = 60 * kSec;

uint32_t at_s(int s) { return kStart + static_cast<uint32_t>(s) * kSec; }

std::string clock_text(uint32_t now) {
  uint32_t s = (now - kStart) / kSec;
  char buf[16];
  snprintf(buf, sizeof buf, "%02u:%02u", s / 60, s % 60);
  return buf;
}

std::string label(uint8_t table) {
  char buf[8];
  table_label(table, buf, sizeof buf);
  return buf;
}

Stage next_of(Stage s) {
  switch (s) {
    case Stage::Free: return Stage::Seat;
    case Stage::Seat: return Stage::Ord;
    case Stage::Ord: return Stage::Main;
    case Stage::Main: return Stage::Bill;
    case Stage::Bill: return Stage::Free;
  }
  return Stage::Free;
}

const Table* find_table(const Board& b, uint8_t number) {
  for (size_t i = 0; i < b.size(); ++i)
    if (b.ranked(i).number == number) return &b.ranked(i);
  return nullptr;
}

// ---------------------------------------------------------------------------
// The desk: the floor script, what the device should show, and the score.

enum class Want : uint8_t { Stage, RequestOpen, RequestClosed };

struct Expect {
  uint8_t table;
  Want want;
  Stage stage;
  uint32_t since;
  bool open = true;
  bool met = false;
  bool missed = false;
  uint32_t met_at = 0;
  int presses = 0;
  std::string what;
};

struct AlertRec {
  uint8_t table;
  uint32_t since;
  bool open = true;
  bool handled = false;
  bool missed = false;
  bool false_alarm = false;
  uint32_t handled_at = 0;
};

struct WrongAction {
  uint8_t table;
  uint32_t at;
  Action action;
  bool corrected = false;
  bool extra = false;  // right table (guests are there), but no change was due
};

enum class Phase : uint8_t { Away, Seated, Ordered, Mains, Check, Done };

struct PartyState {
  const sim::Party* p;
  Phase phase = Phase::Away;
  uint32_t seated_at = 0, ordered_at = 0, mains_at = 0, check_at = 0;
  bool decoy_done = false;
  uint32_t decoy_fix_at = 0;
  size_t decoy_expect = 0;
  uint32_t asked_at[2] = {0, 0};
  size_t ask_expect[2] = {0, 0};
  bool request_done[2] = {false, false};
  bool dessert_done = false;
  bool check_brought = false;
};

class Desk {
 public:
  explicit Desk(Board& board) : board_(board) {
    for (const sim::Party& p : sim::kShift) parties.push_back(PartyState{&p});
  }

  std::vector<Expect> expects;
  std::vector<AlertRec> alerts;
  std::vector<WrongAction> wrong;
  std::vector<PartyState> parties;
  std::vector<std::pair<uint32_t, std::string>> floor;
  std::vector<std::string> log;
  int questions = 0;
  int presses = 0;
  int nav = 0;  // NEXT / PREV presses to reach a table

  PartyState* party(uint8_t table) {
    for (PartyState& p : parties)
      if (p.p->table == table && p.phase != Phase::Away) return &p;
    return nullptr;
  }

  void note(uint32_t now, const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    log.push_back("[" + clock_text(now) + "] " + buf);
  }

  void say(uint32_t now, const std::string& text) {
    floor.emplace_back(now, text);
    note(now, "FLOOR %s", text.c_str());
  }

  size_t expect(uint32_t now, uint8_t table, Want want, Stage stage, const std::string& what) {
    for (Expect& e : expects) {
      if (!e.open || e.table != table) continue;
      bool same_kind = (e.want == Want::Stage) == (want == Want::Stage);
      if (!same_kind) continue;
      close(e, now, true);  // replaced by a newer fact: too late
    }
    Expect e{table, want, stage, now, true, false, false, 0, 0, what};
    expects.push_back(e);
    return expects.size() - 1;
  }

  void supersede(size_t i, uint32_t now) {
    Expect& e = expects[i];
    if (!e.open) return;
    e.open = false;
    note(now, "EXPECT %s %s: dropped, the floor changed", label(e.table).c_str(), e.what.c_str());
  }

  void close(Expect& e, uint32_t now, bool missed) {
    e.open = false;
    e.missed = missed;
    if (missed) note(now, "MISSED %s %s", label(e.table).c_str(), e.what.c_str());
  }

  bool satisfied(const Expect& e) const {
    const Table* t = find_table(board_, e.table);
    if (!t) return false;
    switch (e.want) {
      case Want::Stage: return t->stage == e.stage;
      case Want::RequestOpen: return t->request_open;
      case Want::RequestClosed: return !t->request_open;
    }
    return false;
  }

  void update(uint32_t now) {
    for (PartyState& p : parties) advance(p, now);
    for (Expect& e : expects) {
      if (!e.open) continue;
      if (satisfied(e)) {
        e.open = false;
        e.met = true;
        e.met_at = now;
        note(now, "DONE %s %s in %u s", label(e.table).c_str(), e.what.c_str(), (now - e.since) / kSec);
      } else if (now - e.since > kExpectWindow) {
        close(e, now, true);
      }
    }
    for (AlertRec& a : alerts) {
      if (!a.open) continue;
      if (now - a.since > kAlertWindow) {
        a.open = false;
        a.missed = true;
        note(now, "MISSED ALERT %s", label(a.table).c_str());
      }
    }
  }

  void on_alert(uint32_t now) {
    AlertRec a{board_.selected().number, now, true, false, false, false, 0};
    PartyState* p = party(a.table);
    a.false_alarm = p && p->phase == Phase::Check && p->check_brought;
    alerts.push_back(a);
    const char* text = action_text(board_, board_.selected(), now);
    note(now, "ALERT %s %s%s", label(a.table).c_str(), text ? text : "", a.false_alarm ? " (check already brought)" : "");
  }

  void handle_alerts(uint8_t table, uint32_t now, bool all) {
    for (AlertRec& a : alerts) {
      if (!a.open || (!all && a.table != table)) continue;
      a.open = false;
      a.handled = true;
      a.handled_at = now;
      note(now, "HANDLED ALERT %s in %u s", label(a.table).c_str(), (now - a.since) / kSec);
    }
  }

  // Called after every key press with the table that was selected before it.
  void on_press(Key key, Press kind, Action a, uint8_t table, Stage before, uint32_t now) {
    if (a == Action::None) {
      note(now, "KEY %s%s: no function", key_name(key), kind == Press::Long ? " hold" : "");
      return;
    }
    ++presses;
    if (a == Action::Next || a == Action::Prev) {
      ++nav;
      return;
    }
    if (a == Action::Ack || a == Action::Mute) {
      note(now, "KEY %s on %s", action_name(a), label(table).c_str());
      handle_alerts(table, now, true);
      return;
    }
    bool valid = false;
    for (Expect& e : expects) {
      if (e.open && e.table == table) {
        valid = true;
        if (e.want == Want::Stage && (a == Action::Step || a == Action::Undo)) ++e.presses;
      }
    }
    for (const AlertRec& r : alerts)
      if (r.open && r.table == table) valid = true;
    PartyState* p = party(table);
    if (a == Action::Step && before == Stage::Seat && p && p->phase == Phase::Seated && p->p->order_after_s < 0)
      valid = true;  // taking the order is the server's own move
    if (a == Action::Undo) {
      for (auto it = wrong.rbegin(); it != wrong.rend(); ++it) {
        if (it->table == table && !it->corrected) {
          it->corrected = true;
          valid = true;
          break;
        }
      }
    }
    const bool extra = !valid && p && p->phase != Phase::Done;
    if (valid) handle_alerts(table, now, false);
    else wrong.push_back(WrongAction{table, now, a, false, extra});
    note(now, "KEY %s on %s%s", action_name(a), label(table).c_str(),
         valid ? "" : extra ? "  << EXTRA PRESS" : "  << WRONG TABLE");
  }

  void question(uint32_t now) {
    ++questions;
    note(now, "QUESTION (player not sure)");
  }

  static const char* key_name(Key k) {
    switch (k) {
      case Key::A: return "A";
      case Key::B: return "B";
      case Key::Red: return "RED";
      case Key::Blue: return "BLUE";
    }
    return "?";
  }

 private:
  void advance(PartyState& s, uint32_t now) {
    const sim::Party& p = *s.p;
    const std::string t = label(p.table);
    const Table* dev = find_table(board_, p.table);
    switch (s.phase) {
      case Phase::Away:
        if (now >= at_s(p.arrive_s)) {
          say(now, "Guests sit down at " + t + ".");
          expect(now, p.table, Want::Stage, Stage::Seat, "seat guests");
          s.phase = Phase::Seated;
          s.seated_at = now;
        }
        break;
      case Phase::Seated:
        if (p.order_after_s >= 0) {
          if (now >= s.seated_at + static_cast<uint32_t>(p.order_after_s) * kSec) {
            say(now, "You take the order at " + t + ".");
            expect(now, p.table, Want::Stage, Stage::Ord, "order taken");
            s.phase = Phase::Ordered;
            s.ordered_at = now;
          }
        } else if (dev && dev->stage == Stage::Ord) {
          say(now, "You take the order at " + t + ".");
          s.phase = Phase::Ordered;
          s.ordered_at = now;
        }
        break;
      case Phase::Ordered: {
        if (now < s.ordered_at + static_cast<uint32_t>(p.kitchen_s) * kSec) break;
        if (p.decoy_table && !s.decoy_done) {
          s.decoy_done = true;
          const Table* d = find_table(board_, p.decoy_table);
          if (d && d->stage == Stage::Ord) {
            say(now, "Kitchen calls: mains for " + label(p.decoy_table) + ". You serve them.");
            s.decoy_expect = expect(now, p.decoy_table, Want::Stage, Stage::Main, "mains served (kitchen mistake)");
            s.decoy_fix_at = now + 25 * kSec;
            break;
          }
        }
        if (s.decoy_fix_at) {
          if (now < s.decoy_fix_at) break;
          say(now, "Kitchen: sorry, those mains were for " + t + ", not " + label(p.decoy_table) +
                       ". You take them back and serve " + t + ".");
          supersede(s.decoy_expect, now);
          expect(now, p.decoy_table, Want::Stage, Stage::Ord, "mix-up corrected");
        } else {
          say(now, "Kitchen calls: mains for " + t + ". You serve them.");
        }
        expect(now, p.table, Want::Stage, Stage::Main, "mains served");
        s.phase = Phase::Mains;
        s.mains_at = now;
        break;
      }
      case Phase::Mains:
        for (int i = 0; i < p.request_count; ++i) {
          const sim::GuestRequest& r = p.requests[i];
          if (!s.asked_at[i]) {
            if (now >= s.mains_at + static_cast<uint32_t>(r.after_s) * kSec) {
              say(now, "A guest at " + t + " " + r.ask + ".");
              s.ask_expect[i] = expect(now, p.table, Want::RequestOpen, Stage::Free, "request opened");
              s.asked_at[i] = now;
            }
          } else if (!s.request_done[i]) {
            if (r.handled_after_s >= 0) {
              if (now >= s.asked_at[i] + static_cast<uint32_t>(r.handled_after_s) * kSec) {
                say(now, std::string("You bring ") + r.item + " to " + t + ".");
                expect(now, p.table, Want::RequestClosed, Stage::Free, "request closed");
                s.request_done[i] = true;
              }
            } else if (expects[s.ask_expect[i]].met && dev && !dev->request_open) {
              say(now, std::string("You bring ") + r.item + " to " + t + ".");
              s.request_done[i] = true;
            }
          }
        }
        if (p.dessert_after_s >= 0 && !s.dessert_done &&
            now >= s.mains_at + static_cast<uint32_t>(p.dessert_after_s) * kSec) {
          say(now, "Guests at " + t + " order dessert. You serve it.");
          s.dessert_done = true;  // nothing to record: the table stays in MAIN
        }
        if (now >= s.mains_at + static_cast<uint32_t>(p.check_after_s) * kSec) {
          say(now, "Guests at " + t + " ask for the check.");
          expect(now, p.table, Want::Stage, Stage::Bill, "check requested");
          s.phase = Phase::Check;
          s.check_at = now;
        }
        break;
      case Phase::Check:
        if (!s.check_brought && now >= s.check_at + 60 * kSec) {
          s.check_brought = true;
          if (p.pay_after_s > 180) say(now, "You bring the check to " + t + ".");
        }
        if (now >= s.check_at + static_cast<uint32_t>(p.pay_after_s) * kSec) {
          say(now, "Guests at " + t + " pay and leave.");
          expect(now, p.table, Want::Stage, Stage::Free, "table cleared");
          s.phase = Phase::Done;
        }
        break;
      case Phase::Done: break;
    }
  }

  Board& board_;
};

// ---------------------------------------------------------------------------
// Score.

struct Score {
  int events = 0, met = 0, missed = 0, multi_press = 0;
  int alerts = 0, alerts_handled = 0, alerts_missed = 0, false_alarms = 0;
  int wrong = 0, corrected = 0, extra = 0, extra_corrected = 0, questions = 0, presses = 0, nav = 0;
  double mean_reaction_s = 0;
  bool pass = false;
};

Score score(const Desk& d) {
  Score s;
  double sum = 0;
  int n = 0;
  for (const Expect& e : d.expects) {
    ++s.events;
    if (e.met) {
      ++s.met;
      sum += (e.met_at - e.since) / 1000.0;
      ++n;
      if (e.presses > 1) ++s.multi_press;
    }
    if (e.missed) ++s.missed;
  }
  for (const AlertRec& a : d.alerts) {
    ++s.alerts;
    if (a.handled) {
      ++s.alerts_handled;
      sum += (a.handled_at - a.since) / 1000.0;
      ++n;
    }
    if (a.missed) ++s.alerts_missed;
    if (a.false_alarm) ++s.false_alarms;
  }
  for (const WrongAction& w : d.wrong) {
    if (w.extra) {
      ++s.extra;
      if (w.corrected) ++s.extra_corrected;
    } else {
      ++s.wrong;
      if (w.corrected) ++s.corrected;
    }
  }
  s.questions = d.questions;
  s.presses = d.presses;
  s.nav = d.nav;
  s.mean_reaction_s = n ? sum / n : 0;
  s.pass = s.wrong == 0 && s.alerts_missed == 0 && s.questions <= 1;
  return s;
}

void print_report(FILE* f, const Score& s, KeyMode mode, const char* who) {
  fprintf(f, "TND-01 desk test, %s, %s\n", mode == KeyMode::FourKeys ? "4-key mode" : "2-key mode", who);
  fprintf(f, "  floor events     %d: %d done in time, %d missed, %d needed more than 1 press\n", s.events, s.met,
          s.missed, s.multi_press);
  fprintf(f, "  high alerts      %d: %d handled, %d missed, %d false (check already brought)\n", s.alerts,
          s.alerts_handled, s.alerts_missed, s.false_alarms);
  fprintf(f, "  wrong-table      %d (%d corrected with undo)\n", s.wrong, s.corrected);
  fprintf(f, "  extra presses    %d on a table with guests, no change due (%d corrected with undo)\n", s.extra,
          s.extra_corrected);
  fprintf(f, "  questions        %d\n", s.questions);
  fprintf(f, "  key presses      %d (%d to move between tables), mean reaction %.0f s (shift time)\n", s.presses,
          s.nav, s.mean_reaction_s);
  fprintf(f, "  result           %s  (pass: 0 wrong-table, 0 missed high alerts, <= 1 question)\n",
          s.pass ? "PASS" : "FAIL");
}

// ---------------------------------------------------------------------------
// Bots for --autoplay: they check that the script and the score work.

class Bot {
 public:
  Bot(bool good, Desk& desk, Board& board, KeyMode mode) : good_(good), desk_(desk), board_(board), mode_(mode) {}

  bool next(uint32_t now, Key& key, Press& kind) {
    if (now < next_press_) return false;
    const uint32_t delay = (good_ ? 4 : 25) * kSec;
    // Oldest open thing to do.
    uint8_t table = 0;
    uint32_t since = 0;
    int target_expect = -1;
    bool is_alert = false;
    if (good_) {
      for (const AlertRec& a : desk_.alerts)
        if (a.open && now >= a.since + delay && (!table || a.since < since)) {
          table = a.table;
          since = a.since;
          is_alert = true;
        }
    }
    for (size_t i = 0; i < desk_.expects.size(); ++i) {
      const Expect& e = desk_.expects[i];
      if (!e.open || now < e.since + delay) continue;
      if (!good_ && acted_.size() > i && acted_[i]) continue;
      if (!table || e.since < since) {
        table = e.table;
        since = e.since;
        target_expect = static_cast<int>(i);
        is_alert = false;
      }
    }
    if (!table) return false;
    next_press_ = now + 1500;
    const Table& sel = board_.selected();

    if (!good_) {  // sloppy: acts on whatever table is selected, once per event
      if (acted_.size() <= static_cast<size_t>(target_expect)) acted_.resize(target_expect + 1, false);
      acted_[target_expect] = true;
      const Expect& e = desk_.expects[target_expect];
      if (e.want == Want::Stage) set(Key::A, Press::Short, key, kind);
      else request_key(key, kind);
      return true;
    }

    if (sel.number != table) {
      // Number order: go the short way round (hold NEXT = back in 4-key mode).
      size_t target = 0;
      for (size_t i = 0; i < board_.size(); ++i)
        if (board_.at(i).number == table) target = i;
      const size_t n = board_.size(), here = board_.selected_index();
      const size_t fwd = (target + n - here) % n;
      const bool back = mode_ == KeyMode::FourKeys && n - fwd < fwd;
      set(Key::B, back ? Press::Long : Press::Short, key, kind);
      return true;
    }
    if (is_alert) {
      PartyState* p = desk_.party(table);
      if (sel.stage == Stage::Seat && p && p->phase == Phase::Seated && p->p->order_after_s < 0)
        set(Key::A, Press::Short, key, kind);
      else if (sel.request_open)
        request_key(key, kind);
      else
        ack_key(key, kind);
      return true;
    }
    const Expect& e = desk_.expects[target_expect];
    if (e.want == Want::Stage) {
      if (next_of(e.stage) == sel.stage && sel.can_undo) set(Key::A, Press::Long, key, kind);
      else set(Key::A, Press::Short, key, kind);
    } else {
      request_key(key, kind);
    }
    return true;
  }

 private:
  static void set(Key k, Press p, Key& key, Press& kind) {
    key = k;
    kind = p;
  }
  void request_key(Key& key, Press& kind) const {
    if (mode_ == KeyMode::FourKeys) set(Key::Blue, Press::Short, key, kind);
    else set(Key::B, Press::Long, key, kind);
  }
  void ack_key(Key& key, Press& kind) const {
    if (mode_ == KeyMode::FourKeys) set(Key::Red, Press::Short, key, kind);
    else set(Key::B, Press::Long, key, kind);
  }

  bool good_;
  Desk& desk_;
  Board& board_;
  KeyMode mode_;
  uint32_t next_press_ = 0;
  std::vector<bool> acted_;
};

// ---------------------------------------------------------------------------
// Terminal.

struct termios saved_tty;
bool tty_raw = false;
volatile sig_atomic_t quit_flag = 0;

void restore_tty() {
  if (!tty_raw) return;
  tcsetattr(STDIN_FILENO, TCSANOW, &saved_tty);
  fputs("\x1b[0m\x1b[?25h\x1b[?1049l", stdout);
  fflush(stdout);
  tty_raw = false;
}

void on_signal(int) { quit_flag = 1; }

void raw_tty() {
  tcgetattr(STDIN_FILENO, &saved_tty);
  struct termios t = saved_tty;
  t.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &t);
  tty_raw = true;
  atexit(restore_tty);
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  fputs("\x1b[?1049h\x1b[?25l\x1b[2J", stdout);
}

void term_size(int& cols, int& rows) {
  struct winsize w;
  cols = 100;
  rows = 30;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
    cols = w.ws_col;
    rows = w.ws_row > 0 ? w.ws_row : rows;
  }
}

// Word wrap for floor lines; continuation lines are indented under the text.
std::vector<std::string> wrap(const std::string& text, int width, int indent) {
  std::vector<std::string> out;
  if (width < 12) width = 12;
  std::string line, word;
  auto flush_word = [&]() {
    if (word.empty()) return;
    if (!line.empty() && static_cast<int>(line.size() + 1 + word.size()) > width) {
      out.push_back(line);
      line = std::string(static_cast<size_t>(indent), ' ');
    } else if (!line.empty() && line.back() != ' ') {
      line += ' ';
    }
    line += word;
    word.clear();
  };
  for (char c : text) {
    if (c == ' ') flush_word();
    else word += c;
  }
  flush_word();
  if (!line.empty()) out.push_back(line);
  return out;
}

constexpr int kInner = 40;  // device screen width in characters

std::string pad(const std::string& s, int width, int align) {  // align: 0 left, 1 center, 2 right
  std::string t = s.substr(0, static_cast<size_t>(width));
  int space = width - static_cast<int>(t.size());
  int left = align == 0 ? 0 : align == 1 ? space / 2 : space;
  return std::string(static_cast<size_t>(left), ' ') + t + std::string(static_cast<size_t>(space - left), ' ');
}

std::vector<std::string> device_rows(const Screen& s) {
  const std::string bg = s.burst_on ? "\x1b[48;5;208m" : "\x1b[48;5;16m";
  const std::string dim = s.dim ? "\x1b[2m" : "";
  const std::string paper = bg + dim + (s.burst_on ? "\x1b[38;5;16m" : "\x1b[97m");
  const std::string grey = bg + dim + (s.burst_on ? "\x1b[38;5;16m" : "\x1b[38;5;244m");
  const std::string accent = bg + dim + (s.burst_on ? "\x1b[38;5;16m" : "\x1b[38;5;208m");
  const std::string rst = "\x1b[0m";
  const std::string edge = "\x1b[38;5;240m";
  auto cell = [&](const std::string& style, const std::string& text, int align) {
    return edge + "│" + rst + style + " " + pad(text, kInner - 2, align) + " " + rst + edge + "│" + rst;
  };
  std::vector<std::string> r;
  std::string bar;
  for (int i = 0; i < kInner; ++i) bar += "─";
  r.push_back(edge + "┌" + bar + "┐" + rst);
  r.push_back(cell(accent, s.status, 2));
  r.push_back(cell(std::string(s.table_accent ? accent : paper) + "\x1b[1m", s.table, 0));
  if (s.line2_toast) r.push_back(cell("\x1b[48;5;255m\x1b[38;5;16m\x1b[1m", s.line2, 0));
  else r.push_back(cell((s.line2_accent ? accent : paper) + "\x1b[1m", s.line2, 0));
  {
    std::string line = grey + " " + pad(s.minutes, 8, 0);
    int w = kInner - 2 - 8;
    if (s.progress > 0) {
      float p = s.progress > 1 ? 1 : s.progress;
      int fill = static_cast<int>(p * w + 0.5f);
      line += (s.progress >= 1 ? accent : paper);
      for (int i = 0; i < w; ++i) line += i < fill ? "█" : "░";
    } else {
      line += std::string(static_cast<size_t>(w), ' ');
    }
    r.push_back(edge + "│" + rst + line + grey + " " + rst + edge + "│" + rst);
  }
  r.push_back(cell(grey, "", 0));
  {  // Table strip: every table in number order, cursor on the selected one.
    std::string line;
    int left = kInner;
    for (size_t i = 0; i < s.strip_count; ++i) {
      const Screen::Cell& c = s.strip[i];
      int w = kInner / static_cast<int>(s.strip_count);
      if (i + 1 == s.strip_count) w = left;
      left -= w;
      std::string style = c.accent ? accent : grey;
      if (c.selected)
        style = (c.high ? std::string("\x1b[48;5;208m") : std::string("\x1b[48;5;255m")) + "\x1b[38;5;16m\x1b[1m";
      line += style + pad(std::to_string(c.number), w, 1) + rst;
    }
    if (left > 0) line += grey + std::string(static_cast<size_t>(left), ' ');
    r.push_back(edge + "\u2502" + rst + line + rst + edge + "\u2502" + rst);
  }
  std::string sep;
  for (int i = 0; i < kInner; ++i) sep += "─";
  r.push_back(edge + "│" + rst + grey + sep + rst + edge + "│" + rst);
  {
    std::string line;
    int left = kInner;
    for (size_t i = 0; i < s.key_count; ++i) {
      int w = static_cast<int>(kInner / s.key_count);
      if (i + 1 == s.key_count) w = left;
      left -= w;
      line += (s.key_accent[i] ? accent : grey) + pad(s.keys[i], w, 1);
    }
    r.push_back(edge + "│" + rst + line + rst + edge + "│" + rst);
  }
  r.push_back(edge + "└" + bar + "┘" + rst);
  return r;
}

void render(const Screen& s, const Desk& d, uint32_t now, int speed, bool paused, bool started, KeyMode mode) {
  int cols, rows;
  term_size(cols, rows);

  std::vector<std::string> left;
  for (const std::string& row : device_rows(s)) left.push_back(" " + row);
  left.push_back(mode == KeyMode::FourKeys ? "  a STEP   s NEXT   d ACK   f REQ" : "  a STEP   s NEXT");
  left.push_back("  Shift + key = hold   ? = not sure");
  left.push_back("  space pause   + - speed   q end");

  // Floor column beside the device when the terminal is wide, else below it.
  const int device_w = 44;
  const bool side = cols >= device_w + 2 + 40;
  const int floor_col = side ? device_w + 3 : 1;
  const int floor_w = (side ? cols - floor_col : cols) - 2;

  char head[96];
  snprintf(head, sizeof head, "FLOOR  %s / %s  x%d%s", clock_text(now).c_str(),
           clock_text(at_s(sim::kShiftSeconds)).c_str(), speed, paused ? "  PAUSED" : "");

  std::vector<std::string> floor;  // wrapped, oldest first
  if (!started) {
    const char* intro[] = {"You are the server for tables 1 to 8.",
                           "Things that happen on the floor appear here.",
                           "Keep the device up to date with its keys.",
                           "\x1b[1mPress space to start the shift.\x1b[0m"};
    for (const char* t : intro)
      for (const std::string& l : wrap(t, floor_w + (t[0] == '\x1b' ? 8 : 0), 0)) floor.push_back(l);
  } else {
    for (const auto& ev : d.floor) {
      const bool fresh = now - ev.first < 20 * kSec;
      const std::string style = fresh ? "\x1b[1m" : "\x1b[38;5;246m";
      for (const std::string& l : wrap(clock_text(ev.first) + "  " + ev.second, floor_w, 7))
        floor.push_back(style + l + "\x1b[0m");
    }
  }

  // Lines available for the floor log.
  const int head_row = side ? 1 : static_cast<int>(left.size()) + 2;
  int space = rows - head_row - 1;
  if (space < 3) space = 3;
  const size_t first = floor.size() > static_cast<size_t>(space) ? floor.size() - space : 0;

  // Build each screen row, clear it and write it in one go: no flicker.
  std::vector<std::vector<std::pair<int, std::string>>> frame(static_cast<size_t>(rows) + 1);
  auto put = [&](int row, int col, const std::string& text) {
    if (row >= 1 && row <= rows) frame[static_cast<size_t>(row)].emplace_back(col, text);
  };
  for (size_t i = 0; i < left.size(); ++i) put(static_cast<int>(i) + (side ? 2 : 1), 1, left[i]);
  put(head_row, floor_col, std::string("\x1b[1m") + head);
  for (size_t i = first; i < floor.size(); ++i)
    put(head_row + 1 + static_cast<int>(i - first), floor_col, floor[i]);
  std::string out;
  for (int r = 1; r <= rows; ++r) {
    out += "\x1b[" + std::to_string(r) + ";1H\x1b[2K";
    for (const auto& seg : frame[static_cast<size_t>(r)])
      out += "\x1b[" + std::to_string(r) + ";" + std::to_string(seg.first) + "H" + seg.second + "\x1b[0m";
  }
  fputs(out.c_str(), stdout);
  fflush(stdout);
}

// ---------------------------------------------------------------------------

struct Options {
  KeyMode mode = KeyMode::FourKeys;
  const char* autoplay = nullptr;
  bool expect_fail = false;
  bool verbose = false;
  int speed = 4;
  std::string log_path;
};

void usage() {
  fputs(
      "usage: tnd01-sim [--two-keys] [--speed N] [--log FILE]\n"
      "       tnd01-sim --autoplay good|sloppy [--two-keys] [--verbose] [--expect-fail]\n",
      stderr);
}

bool parse(int argc, char** argv, Options& o) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--two-keys") o.mode = KeyMode::TwoKeys;
    else if (a == "--autoplay" && i + 1 < argc) o.autoplay = argv[++i];
    else if (a == "--expect-fail") o.expect_fail = true;
    else if (a == "--verbose") o.verbose = true;
    else if (a == "--speed" && i + 1 < argc) o.speed = atoi(argv[++i]);
    else if (a == "--log" && i + 1 < argc) o.log_path = argv[++i];
    else return false;
  }
  if (o.autoplay && strcmp(o.autoplay, "good") != 0 && strcmp(o.autoplay, "sloppy") != 0) return false;
  if (o.speed < 1 || o.speed > 16) o.speed = 4;
  return true;
}

void press(App& app, Board& board, Desk& desk, Key k, Press p, uint32_t now) {
  const uint8_t table = board.selected().number;
  const Stage before = board.selected().stage;
  Action a = app.press(k, p, now);
  desk.on_press(k, p, a, table, before, now);
}

int autoplay(const Options& o) {
  Board board(sim::kTables, sizeof sim::kTables);
  App app(board, o.mode);
  Desk desk(board);
  Bot bot(strcmp(o.autoplay, "good") == 0, desk, board, o.mode);
  const uint32_t end = at_s(sim::kShiftSeconds);
  for (uint32_t now = kStart; now <= end; now += 100) {
    Key k;
    Press p;
    if (bot.next(now, k, p)) press(app, board, desk, k, p, now);
    if (app.tick(now)) desk.on_alert(now);
    desk.update(now);
  }
  if (o.verbose)
    for (const std::string& l : desk.log) puts(l.c_str());
  Score s = score(desk);
  print_report(stdout, s, o.mode, o.autoplay[0] == 'g' ? "autoplay: good player" : "autoplay: sloppy player");
  bool ok = o.expect_fail ? !s.pass : s.pass;
  if (!ok) fprintf(stderr, "autoplay %s: unexpected result\n", o.autoplay);
  return ok ? 0 : 1;
}

int interactive(const Options& o) {
  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
    fputs("tnd01-sim: needs a terminal (or use --autoplay)\n", stderr);
    return 2;
  }
  Board board(sim::kTables, sizeof sim::kTables);
  App app(board, o.mode);
  Desk desk(board);
  int speed = o.speed;
  bool paused = true, started = false;
  uint32_t now = kStart;
  double frac = 0;
  const uint32_t end = at_s(sim::kShiftSeconds);
  raw_tty();
  auto last = std::chrono::steady_clock::now();
  while (!quit_flag && now < end) {
    char buf[32];
    ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
    for (ssize_t i = 0; i < n; ++i) {
      const char c = buf[i];
      if (c == 'q' || c == 'Q') quit_flag = 1;
      else if (c == ' ') {
        paused = !paused;
        started = true;
      } else if (c == '+' || c == '=') speed = speed >= 16 ? 16 : speed * 2;
      else if (c == '-' || c == '_') speed = speed <= 1 ? 1 : speed / 2;
      else if (paused) continue;
      else if (c == '?') desk.question(now);
      else {
        const char* keys = "asdf";
        const char* hit = strchr(keys, c | 0x20);
        if (hit && *hit && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
          press(app, board, desk, static_cast<Key>(hit - keys), (c & 0x20) ? Press::Short : Press::Long, now);
      }
    }
    auto t = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double, std::milli>(t - last).count();
    last = t;
    if (!paused) {
      frac += dt * speed;
      uint32_t whole = static_cast<uint32_t>(frac);
      frac -= whole;
      now += whole;
    }
    if (app.tick(now)) {
      desk.on_alert(now);
      fputs("\a", stdout);
    }
    desk.update(now);
    Screen s;
    app.screen(now, s);
    render(s, desk, now, speed, paused, started, o.mode);
    usleep(40000);
  }
  restore_tty();

  Score s = score(desk);
  std::string path = o.log_path;
  if (path.empty()) {
    char stamp[32];
    time_t t = time(nullptr);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&t));
    path = std::string("build/desk-test-") + stamp + ".log";
  }
  print_report(stdout, s, o.mode, now >= end ? "full shift" : ("stopped at " + clock_text(now)).c_str());
  if (FILE* f = fopen(path.c_str(), "w")) {
    print_report(f, s, o.mode, now >= end ? "full shift" : ("stopped at " + clock_text(now)).c_str());
    fputs("\n", f);
    for (const std::string& l : desk.log) fprintf(f, "%s\n", l.c_str());
    fclose(f);
    printf("  log              %s\n", path.c_str());
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Options o;
  if (!parse(argc, argv, o)) {
    usage();
    return 2;
  }
  return o.autoplay ? autoplay(o) : interactive(o);
}
