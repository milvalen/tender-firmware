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

static bool select_table(Board& b, uint8_t number) {
  for (size_t i = 0; i < b.size(); ++i) {
    if (b.selected().number == number) return true;
    b.scroll(1);
  }
  return b.selected().number == number;
}

static void test_step_cycle() {
  Board b(kTables, 4);
  CHECK(select_table(b, 7));
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

static void test_thresholds_and_priorities() {
  Board b(kTables, 4);
  CHECK(select_table(b, 3));
  b.step(0);  // SEAT: high after 3 min
  CHECK(b.priority(b.selected(), 3 * M - 1) == Priority::None);
  CHECK(b.priority(b.selected(), 3 * M) == Priority::High);
  CHECK(strcmp(action_text(b, b.selected(), 3 * M), "TAKE ORDER") == 0);
  b.step(3 * M);  // ORD: no threshold
  CHECK(b.priority(b.selected(), 60 * M) == Priority::None);
  CHECK(action_text(b, b.selected(), 60 * M) == nullptr);
  b.step(10 * M);  // MAIN: low after 15 min
  CHECK(b.priority(b.selected(), 25 * M - 1) == Priority::None);
  CHECK(b.priority(b.selected(), 25 * M) == Priority::Low);
  CHECK(strcmp(action_text(b, b.selected(), 25 * M), "OFFER DESSERT") == 0);
  b.step(30 * M);  // DSRT
  b.step(40 * M);  // BILL: high after 3 min
  CHECK(b.priority(b.selected(), 43 * M) == Priority::High);
  CHECK(strcmp(action_text(b, b.selected(), 43 * M), "BRING CHECK") == 0);
  CHECK(b.elapsed_min(b.selected(), 52 * M + 30000) == 12);
}

static void test_undo() {
  Board b(kTables, 4);
  b.step(5 * M);
  b.step(7 * M);
  CHECK(b.selected().stage == Stage::Ord);
  CHECK(b.undo(8 * M));
  CHECK(b.selected().stage == Stage::Seat);
  CHECK(b.selected().stage_since == 5 * M);
  CHECK(!b.undo(9 * M));  // only one level
  CHECK(b.selected().stage == Stage::Seat);
}

static void test_request_toggle() {
  Board b(kTables, 4);
  CHECK(select_table(b, 12));
  b.request(10 * M);
  CHECK(b.selected().request_open);
  CHECK(b.selected().stage == Stage::Free);  // a request does not change the stage
  CHECK(b.priority(b.selected(), 12 * M - 1) == Priority::None);
  CHECK(b.priority(b.selected(), 12 * M) == Priority::High);
  CHECK(strcmp(action_text(b, b.selected(), 12 * M), "GUEST REQUEST") == 0);
  b.request(12 * M);  // second press closes
  CHECK(!b.selected().request_open);
  CHECK(b.priority(b.selected(), 20 * M) == Priority::None);
}

static void test_alert_once_then_realert() {
  Board b(kTables, 4);
  CHECK(select_table(b, 1));
  b.step(0);  // TBL-01 SEAT, high at 3 min
  CHECK(!b.poll_alert(3 * M - 1));
  CHECK(b.poll_alert(3 * M));       // one alert...
  CHECK(!b.poll_alert(3 * M + 10));  // ...not an endless one
  CHECK(!b.poll_alert(6 * M - 1));
  CHECK(b.poll_alert(6 * M));  // comes back after 3 min if not handled
}

static void test_low_priority_never_alerts() {
  Board b(kTables, 4);
  b.step(0);
  b.step(0);
  b.step(0);  // MAIN
  CHECK(b.priority(b.selected(), 20 * M) == Priority::Low);
  CHECK(!b.poll_alert(20 * M));
  CHECK(!b.any_high(20 * M));
}

static void test_alert_selects_table() {
  Board b(kTables, 4);
  CHECK(select_table(b, 7));
  b.step(0);  // TBL-07 SEAT
  CHECK(select_table(b, 3));  // server looks at another table
  CHECK(b.poll_alert(3 * M));
  CHECK(b.selected().number == 7);  // STEP now acts on the alerting table
  CHECK(b.ranked(0).number == 7);
}

static void test_ack_and_mute() {
  Board b(kTables, 4);
  b.step(0);  // SEAT
  CHECK(b.poll_alert(3 * M));
  b.ack(4 * M);
  CHECK(!b.poll_alert(7 * M - 1));
  CHECK(b.poll_alert(7 * M));
  b.mute(7 * M);
  CHECK(!b.poll_alert(17 * M - 1));
  CHECK(b.poll_alert(17 * M));
  b.step(17 * M);  // order taken: no more alerts for this table
  CHECK(!b.any_high(17 * M));
  CHECK(!b.poll_alert(30 * M));
}

static void test_stable_order() {
  Board b(kTables, 4);
  // TBL-01 MAIN at 0 (low due at 15), TBL-03 SEAT at 10 min (high due at 13).
  CHECK(select_table(b, 1));
  b.step(0); b.step(0); b.step(0);
  CHECK(select_table(b, 3));
  b.step(10 * M);
  CHECK(b.ranked(0).number == 3);  // 3 min left beats 5 min left
  // Without a resort the order does not move, even when time passes.
  CHECK(b.ranked(0).number == 3);
  b.resort(16 * M);  // TBL-03 high due, TBL-01 low due
  CHECK(b.ranked(0).number == 3);
  CHECK(b.ranked(1).number == 1);
  // Free tables last, by number.
  CHECK(b.ranked(2).number == 7);
  CHECK(b.ranked(3).number == 12);
}

static void test_high_outranks_low() {
  Board b(kTables, 4);
  CHECK(select_table(b, 1));
  b.step(0); b.step(0); b.step(0);  // MAIN at 0: low due at 15, 25 min overdue at 40
  CHECK(select_table(b, 12));
  b.step(36 * M);  // SEAT: high due at 39, 1 min overdue at 40
  b.resort(40 * M);
  CHECK(b.ranked(0).number == 12);
  CHECK(b.ranked(1).number == 1);
}

static void test_scroll_wraps() {
  Board b(kTables, 4);
  uint8_t seen[4] = {0};
  for (int i = 0; i < 4; ++i) {
    seen[i] = b.selected().number;
    b.scroll(1);
  }
  CHECK(b.selected().number == seen[0]);
  b.scroll(-1);
  CHECK(b.selected().number == seen[3]);
}

static void test_labels() {
  char buf[8];
  table_label(7, buf, sizeof buf);
  CHECK(strcmp(buf, "TBL-07") == 0);
  CHECK(strcmp(stage_status(Stage::Ord), "ORDER TAKEN") == 0);
  CHECK(strcmp(stage_name(Stage::Bill), "BILL") == 0);
}

static void test_wraparound_millis() {
  // millis() wraps after about 49.7 days; unsigned arithmetic must keep working.
  Board b(kTables, 4);
  uint32_t start = 0xFFFFFFFFu - M;  // 1 min before wrap
  b.step(start);
  CHECK(b.priority(b.selected(), start + 2 * M) == Priority::None);
  CHECK(b.priority(b.selected(), start + 3 * M) == Priority::High);
  CHECK(b.poll_alert(start + 3 * M));
  CHECK(!b.poll_alert(start + 4 * M));
  CHECK(b.poll_alert(start + 6 * M));
}

int main() {
  test_step_cycle();
  test_thresholds_and_priorities();
  test_undo();
  test_request_toggle();
  test_alert_once_then_realert();
  test_low_priority_never_alerts();
  test_alert_selects_table();
  test_ack_and_mute();
  test_stable_order();
  test_high_outranks_low();
  test_scroll_wraps();
  test_labels();
  test_wraparound_millis();
  printf("%d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
