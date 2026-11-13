// Desk-test score and report.
//
// Pass: 0 wrong-table actions, 0 missed high alerts, no more than 1 question
// (tender-vault/10-concept/ux-principles.md, desk test).
#pragma once

#include <stdio.h>

#include "app.h"
#include "referee.h"

namespace sim {

struct Score {
  // Floor events (expectations).
  int events = 0, met = 0, missed = 0, multi_press = 0;
  // High alerts.
  int alerts = 0, alerts_handled = 0, alerts_missed = 0, false_alarms = 0;
  // Mistakes.
  int wrong = 0, corrected = 0, extra = 0, extra_corrected = 0;
  int questions = 0, presses = 0, nav = 0;
  double mean_reaction_s = 0;
  bool pass = false;
};

Score score(const Referee& referee);

// `who`: "full shift", "autoplay: good player" and so on.
void print_report(FILE* f, const Score& s, tender::KeyMode mode, const char* who);

}  // namespace sim
