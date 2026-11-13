// Native tests for the shared interface logic. Run: make -C ui-unit test
#include <stdio.h>
#include <string.h>

#include "app.h"

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

static void test_two_key_mapping() {
  Board b(kTables, 4);
  App app(b, KeyMode::TwoKeys);
  uint32_t now = 1000;
  CHECK(app.press(Key::A, Press::Short, now) == Action::Step);
  CHECK(app.press(Key::A, Press::Long, now) == Action::Undo);
  CHECK(app.press(Key::B, Press::Short, now) == Action::Next);
  // No alert on the selected table: hold B opens a request.
  CHECK(app.press(Key::B, Press::Long, now) == Action::Request);
  CHECK(b.selected().request_open);
  Screen s;
  app.screen(now, s);
  CHECK(strcmp(s.keys[2], "HOLD NEXT: DONE") == 0);
  // The request is overdue after 2 min: hold B still closes it, so an
  // overdue request cannot get stuck behind ACK.
  now += 2 * M;
  app.tick(now);
  CHECK(b.priority(b.selected(), now) == Priority::High);
  CHECK(app.press(Key::B, Press::Long, now) == Action::Request);
  CHECK(!b.selected().request_open);
  // A stage alert without a request: hold B acknowledges it.
  app.press(Key::A, Press::Short, now);  // seat guests
  now += 3 * M;
  app.tick(now);
  CHECK(b.priority(b.selected(), now) == Priority::High);
  CHECK(app.press(Key::B, Press::Long, now) == Action::Ack);
  CHECK(app.press(Key::Red, Press::Short, now) == Action::None);
  CHECK(app.press(Key::Blue, Press::Short, now) == Action::None);
}

static void test_four_key_mapping() {
  Board b(kTables, 4);
  App app(b, KeyMode::FourKeys);
  uint32_t now = 1000;
  CHECK(app.press(Key::B, Press::Long, now) == Action::Prev);
  CHECK(app.press(Key::Red, Press::Short, now) == Action::Ack);
  CHECK(app.press(Key::Red, Press::Long, now) == Action::Mute);
  CHECK(b.muted(now));
  CHECK(app.press(Key::Blue, Press::Short, now) == Action::Request);
  Screen s;
  app.screen(now, s);
  CHECK(s.key_count == 4);
  CHECK(strcmp(s.keys[2], "ACK") == 0);
  CHECK(strstr(s.status, "MUTED") != nullptr);
}

static void test_toast_after_step() {
  Board b(kTables, 4);
  App app(b, KeyMode::TwoKeys);
  app.press(Key::A, Press::Short, 1000);
  Screen s;
  app.screen(1500, s);
  CHECK(s.line2_toast);
  char want[28];
  char label[8];
  table_label(b.selected().number, label, sizeof label);
  snprintf(want, sizeof want, "%s %s", label, stage_status(Stage::Seat));
  CHECK(strcmp(s.line2, want) == 0);
  app.screen(1000 + 1300, s);  // toast is over: action text again
  CHECK(!s.line2_toast);
  CHECK(strcmp(s.line2, "TAKE ORDER") == 0 || strcmp(s.line2, stage_status(Stage::Seat)) == 0);
}

static void test_alert_burst_then_steady() {
  Board b(kTables, 4);
  App app(b, KeyMode::TwoKeys);
  app.press(Key::A, Press::Short, 1000);  // seated at 1000
  const uint8_t seated = b.selected().number;
  app.press(Key::B, Press::Short, 2000);  // look at another table
  CHECK(b.selected().number != seated);
  const uint32_t due = 1000 + 3 * M;
  CHECK(app.tick(due));
  CHECK(b.selected().number == seated);  // the alert selects its table
  Screen s;
  app.screen(due, s);
  CHECK(s.burst_on);
  CHECK(s.table_accent);
  CHECK(strcmp(s.keys[2], "HOLD NEXT: ACK") == 0);
  CHECK(strcmp(s.keys[0], "ORDER") == 0);
  CHECK(s.key_accent[0]);   // STEP ends a "take order" alert
  CHECK(!s.key_accent[2]);  // ACK only snoozes it
  CHECK(app.vibrate(due));
  app.screen(due + 2500, s);  // burst is over, marker stays
  CHECK(!s.burst_on);
  CHECK(s.table_accent);
  CHECK(!app.tick(due + 2500));  // one burst only
}

static void test_ack_stops_burst() {
  Board b(kTables, 4);
  App app(b, KeyMode::TwoKeys);
  app.press(Key::A, Press::Short, 1000);
  const uint32_t due = 1000 + 3 * M;
  CHECK(app.tick(due));
  CHECK(app.press(Key::B, Press::Long, due + 300) == Action::Ack);
  Screen s;
  app.screen(due + 400, s);
  CHECK(!s.burst_on);
  CHECK(strcmp(s.line2, "OK, AGAIN IN 3 MIN") == 0);
}

static void test_dim_when_idle() {
  Board b(kTables, 4);
  App app(b, KeyMode::TwoKeys);
  app.press(Key::B, Press::Short, 1000);
  CHECK(!app.idle(1000 + 19000));
  CHECK(app.idle(1000 + 20000));
  Screen s;
  app.screen(1000 + 20000, s);
  CHECK(s.dim);
}

static void test_low_battery() {
  Board b(kTables, 4);
  App app(b, KeyMode::TwoKeys);
  Screen s;
  app.set_battery(80);
  app.screen(1000, s);
  CHECK(s.status[0] == '\0');
  app.set_battery(20);
  app.screen(1000, s);
  CHECK(strcmp(s.status, "LOW BATTERY") == 0);
}

static void test_table_strip() {
  Board b(kTables, 4);
  App app(b, KeyMode::FourKeys);
  app.press(Key::B, Press::Short, 1000);  // NEXT: TBL-03
  app.press(Key::A, Press::Short, 1000);  // seat guests at TBL-03
  Screen s;
  app.screen(1000, s);
  CHECK(s.strip_count == 4);
  CHECK(s.strip[0].number == 1 && s.strip[3].number == 12);  // number order
  CHECK(s.strip[1].selected && !s.strip[0].selected);
  CHECK(!s.strip[1].accent);
  app.screen(1000 + 3 * M, s);  // order not taken: high
  CHECK(s.strip[1].accent && s.strip[1].high);
  app.press(Key::B, Press::Long, 1000 + 3 * M);  // PREV: TBL-01
  app.screen(1000 + 3 * M, s);
  CHECK(s.strip[0].selected);
  CHECK(s.strip[1].number == 3);  // places do not move
}

static void test_step_label_and_dessert_hint() {
  Board b(kTables, 4);
  App app(b, KeyMode::TwoKeys);
  Screen s;
  app.screen(1000, s);
  CHECK(strcmp(s.keys[0], "SEAT") == 0);
  app.press(Key::A, Press::Short, 1000);
  app.press(Key::A, Press::Short, 1000);
  app.screen(7000, s);  // after the undo hint
  CHECK(strcmp(s.keys[0], "SERVED") == 0);
  app.press(Key::A, Press::Short, 1000);  // mains served
  app.screen(7000, s);
  CHECK(strcmp(s.keys[0], "CHECK") == 0);  // no dessert stage in the way
  const uint32_t hint = 1000 + 15 * M;
  app.screen(hint, s);
  CHECK(strcmp(s.keys[2], "HOLD NEXT: ACK") == 0);
  CHECK(app.press(Key::B, Press::Long, hint) == Action::Ack);
  app.screen(hint + 100, s);
  CHECK(strstr(s.line2, "DESSERT OFFERED") != nullptr);
  app.screen(hint + 5000, s);
  CHECK(!s.strip[0].accent);  // hint gone
}

static void test_alert_key_and_undo_hint() {
  Board b(kTables, 4);
  App app(b, KeyMode::FourKeys);
  Screen s;
  app.press(Key::Blue, Press::Short, 1000);  // open a request on TBL-01
  const uint32_t due = 1000 + 2 * M;
  app.tick(due);
  app.screen(due, s);
  CHECK(s.key_accent[3]);   // REQ ends a request alert
  CHECK(!s.key_accent[0]);
  CHECK(!s.key_accent[2]);
  // After a STEP the STEP key shows how to take it back, for 5 s.
  app.press(Key::A, Press::Short, due);
  app.screen(due + 1000, s);
  CHECK(strcmp(s.keys[0], "HOLD: UNDO") == 0);
  app.screen(due + 5000, s);
  CHECK(strcmp(s.keys[0], "ORDER") == 0);
  // Not on another table.
  app.press(Key::A, Press::Short, due + 6000);
  app.press(Key::B, Press::Short, due + 6000);
  app.screen(due + 6500, s);
  CHECK(strcmp(s.keys[0], "SEAT") == 0);
  // Undo ends the hint.
  app.press(Key::B, Press::Long, due + 7000);  // back to TBL-01
  app.press(Key::A, Press::Long, due + 7000);  // ORD back to SEAT
  app.screen(due + 7500, s);
  CHECK(strcmp(s.keys[0], "ORDER") == 0);
}

int main() {
  test_two_key_mapping();
  test_four_key_mapping();
  test_toast_after_step();
  test_alert_burst_then_steady();
  test_ack_stops_burst();
  test_dim_when_idle();
  test_low_battery();
  test_table_strip();
  test_step_label_and_dessert_hint();
  test_alert_key_and_undo_hint();
  printf("app: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
