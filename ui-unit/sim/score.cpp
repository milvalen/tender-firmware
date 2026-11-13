#include "score.h"

namespace sim {

Score score(const Referee& referee) {
  Score s;
  double reaction_sum_s = 0;
  int reactions = 0;
  auto react = [&](uint32_t since, uint32_t at) {
    reaction_sum_s += (at - since) / 1000.0;
    ++reactions;
  };

  for (const Expectation& e : referee.expectations()) {
    ++s.events;
    if (e.met) {
      ++s.met;
      react(e.since, e.met_at);
      if (e.stage_presses > 1) ++s.multi_press;
    }
    if (e.missed) ++s.missed;
  }
  for (const AlertRecord& a : referee.alerts()) {
    ++s.alerts;
    if (a.handled) {
      ++s.alerts_handled;
      react(a.since, a.handled_at);
    }
    if (a.missed) ++s.alerts_missed;
    if (a.false_alarm) ++s.false_alarms;
  }
  for (const Mistake& m : referee.mistakes()) {
    if (m.extra) {
      ++s.extra;
      if (m.corrected) ++s.extra_corrected;
    } else {
      ++s.wrong;
      if (m.corrected) ++s.corrected;
    }
  }
  s.questions = referee.questions();
  s.presses = referee.presses();
  s.nav = referee.nav_presses();
  s.mean_reaction_s = reactions ? reaction_sum_s / reactions : 0;
  s.pass = s.wrong == 0 && s.alerts_missed == 0 && s.questions <= 1;
  return s;
}

void print_report(FILE* f, const Score& s, tender::KeyMode mode, const char* who) {
  const char* mode_name = mode == tender::KeyMode::FourKeys ? "4-key mode" : "2-key mode";
  fprintf(f, "TND-01 desk test, %s, %s\n", mode_name, who);
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

}  // namespace sim
