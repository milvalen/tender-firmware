// Native tests for the table board core. Run: make -C ui-unit test
#include <stdio.h>
#include <string.h>

#include "tableboard.h"

using namespace tender;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    ++checks;                                                          \
    if (!(cond)) {                                                     \
      ++failures;                                                      \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
              #cond);                                                  \
    }                                                                  \
  } while (0)

static const uint32_t M = kMinute;
static const uint8_t kTables[] = {1, 3, 7, 12};

static void select_table(Board& b, uint8_t number, uint32_t now) {
  for (size_t i = 0; i < b.size(); ++i) {
    if (b.selected().number == number) return;
    b.scroll(1, now);
  }
}

static void test_step_cycle() {
  Board b(kTables, 4);
  select_table(b, 7, 0);
  CHECK(b.selected().number == 7);
  const Stage expected[] = {Stage::Seat, Stage::Ord, Stage::Main, Stage::Dsrt, Stage::Bill, Stage::Free};
  uint32_t now = 1000;
  for (Stage s : expected) {
    b.step(now);
    CHECK(b.selected().number == 7);  // selection follows the table, not the rank
    CHECK(b.selected().stage == s);
    CHECK(b.selected().stage_since == now);
    now += 1000;
  }
}

static void test_thresholds() {
  Board b(kTables, 4);
  select_table(b, 3, 0);
  b.step(0);  // SEAT at t=0
  CHECK(!b.overdue(b.selected(), 3 * M - 1));
  CHECK(b.need(b.selected(), 3 * M) == Need::Stage);
  b.step(3 * M);  // ORD: no threshold
  CHECK(!b.overdue(b.selected(), 60 * M));
  b.step(10 * M);  // MAIN: 15 min
  CHECK(!b.overdue(b.selected(), 25 * M - 1));
  CHECK(b.overdue(b.selected(), 25 * M));
  CHECK(b.elapsed_min(b.selected(), 22 * M + 30000) == 12);
}

static void test_undo() {
  Board b(kTables, 4);
  b.step(5 * M);
  b.step(7 * M);
  CHECK(b.selected().stage == Stage::Ord);
  b.undo(8 * M);
  CHECK(b.selected().stage == Stage::Seat);
  CHECK(b.selected().stage_since == 5 * M);
  b.undo(9 * M);  // only one level
  CHECK(b.selected().stage == Stage::Seat);
}

static void test_request_toggle_and_due() {
  Board b(kTables, 4);
  select_table(b, 12, 0);
  b.request(10 * M);
  CHECK(b.selected().request_open);
  CHECK(b.selected().stage == Stage::Free);  // a request does not change the stage
  CHECK(!b.overdue(b.selected(), 12 * M - 1));
  CHECK(b.need(b.selected(), 12 * M) == Need::Request);
  b.request(12 * M);  // second press closes
  CHECK(!b.selected().request_open);
  CHECK(!b.overdue(b.selected(), 20 * M));
}

static void test_signal_ack_and_mute() {
  Board b(kTables, 4);
  b.step(0);  // SEAT, due at 3 min
  CHECK(!b.signalling(3 * M - 1));
  CHECK(b.signalling(3 * M));
  b.ack(4 * M);  // snooze 3 min
  CHECK(!b.signalling(7 * M - 1));
  CHECK(b.signalling(7 * M));
  b.mute(7 * M);  // 10 min
  CHECK(!b.signalling(17 * M - 1));
  CHECK(b.signalling(17 * M));
  b.step(17 * M);  // serving the table clears the signal
  CHECK(!b.signalling(17 * M));
}

static void test_ranking() {
  Board b(kTables, 4);
  // TBL-01 SEAT at 0 (due 3 min), TBL-03 MAIN at 0 (due 15 min),
  // TBL-07 ORD (no threshold), TBL-12 free.
  select_table(b, 1, 0);
  b.step(0);
  select_table(b, 3, 0);
  b.step(0); b.step(0); b.step(0);
  select_table(b, 7, 0);
  b.step(0); b.step(0);
  uint32_t now = 1 * M;
  CHECK(b.ranked(0, now).number == 1);   // 2 min left
  CHECK(b.ranked(1, now).number == 3);   // 14 min left
  CHECK(b.ranked(2, now).number == 7);   // running, no threshold
  CHECK(b.ranked(3, now).number == 12);  // free
  now = 20 * M;  // both overdue: TBL-01 by 17 min, TBL-03 by 5 min
  CHECK(b.ranked(0, now).number == 1);
  CHECK(b.ranked(1, now).number == 3);
  // An overdue request outranks a table that is not overdue yet.
  select_table(b, 12, now);
  b.request(now);
  CHECK(b.ranked(3, now + 1).number == 7);
  CHECK(b.ranked(2, now + 2 * M).number == 12);
}

static void test_scroll_wraps() {
  Board b(kTables, 4);
  uint8_t seen[4] = {0};
  for (int i = 0; i < 4; ++i) {
    seen[i] = b.selected().number;
    b.scroll(1, 0);
  }
  CHECK(b.selected().number == seen[0]);
  b.scroll(-1, 0);
  CHECK(b.selected().number == seen[3]);
}

static void test_labels() {
  char buf[8];
  table_label(7, buf, sizeof buf);
  CHECK(strcmp(buf, "TBL-07") == 0);
  table_label(12, buf, sizeof buf);
  CHECK(strcmp(buf, "TBL-12") == 0);
  CHECK(strcmp(stage_name(Stage::Bill), "BILL") == 0);
}

static void test_wraparound_millis() {
  // millis() wraps after about 49.7 days; unsigned arithmetic must keep working.
  Board b(kTables, 4);
  uint32_t start = 0xFFFFFFFFu - M;  // 1 min before wrap
  b.step(start);
  CHECK(!b.overdue(b.selected(), start + 2 * M));
  CHECK(b.overdue(b.selected(), start + 3 * M));
}

int main() {
  test_step_cycle();
  test_thresholds();
  test_undo();
  test_request_toggle_and_due();
  test_signal_ack_and_mute();
  test_ranking();
  test_scroll_wraps();
  test_labels();
  test_wraparound_millis();
  printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
