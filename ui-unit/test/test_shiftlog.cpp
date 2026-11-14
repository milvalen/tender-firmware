// Native tests for the shift log core and the App event queue.
// Run: make -C ui-unit test
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "shiftlog.h"

using namespace tender;
namespace sl = tender::shiftlog;

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

static const uint8_t kTables[] = {1, 2, 3, 4};
constexpr uint32_t M = kMinute;

static void test_events_from_app() {
  Board b(kTables, 4);
  App app(b, KeyMode::FourKeys);
  Event e;
  CHECK(!app.pop_event(e));
  app.press(Key::B, Press::Short, 1000);  // NEXT to TBL-02
  app.press(Key::A, Press::Short, 2000);  // STEP: seated
  CHECK(app.pop_event(e));
  CHECK(e.type == Event::Type::Action);
  CHECK(e.action == Action::Next);
  CHECK(e.table == 2);
  CHECK(e.ms == 1000);
  CHECK(app.pop_event(e));
  CHECK(e.action == Action::Step);
  CHECK(e.stage == Stage::Seat);
  CHECK(!app.pop_event(e));
  CHECK(app.tick(2000 + 3 * M));  // TAKE ORDER
  CHECK(app.pop_event(e));
  CHECK(e.type == Event::Type::Alert);
  CHECK(e.need == Need::Stage);
  CHECK(e.table == 2);
  CHECK(e.stage == Stage::Seat);
  // A press that does nothing gives no event.
  CHECK(app.press(Key::Blue, Press::Short, 2000 + 3 * M) == Action::Request);  // opens a request
  CHECK(app.pop_event(e));
  CHECK(e.request_open);
  CHECK(app.press(Key::Blue, Press::Short, 2000 + 3 * M + 1) == Action::None);  // TALK only opens
  CHECK(!app.pop_event(e));
}

static void test_queue_keeps_the_last_events() {
  Board b(kTables, 4);
  App app(b, KeyMode::FourKeys);
  for (uint32_t i = 0; i < App::kEventQueue + 3; ++i) app.press(Key::B, Press::Short, 100 * (i + 1));
  Event e;
  size_t n = 0;
  uint32_t first = 0;
  while (app.pop_event(e)) {
    if (n == 0) first = e.ms;
    ++n;
  }
  CHECK(n == App::kEventQueue);
  CHECK(first == 400);  // the 3 oldest are dropped
}

static void test_csv_lines() {
  char line[96];
  sl::Writer w;
  CHECK(strcmp(sl::Writer::header(), "boot,ms,event,table,stage,detail") == 0);
  CHECK(w.line(sl::boot(120, sl::Reset::PowerOn), line, sizeof line));
  CHECK(strcmp(line, "1,120,BOOT,,,reset=POWERON\n") == 0);
  CHECK(w.line(sl::firmware(0x0a2b3c4dUL, false), line, sizeof line));
  CHECK(strcmp(line, "1,120,FIRMWARE,,,commit=0a2b3c4d\n") == 0);  // the time of the BOOT
  CHECK(w.line(sl::firmware(0, true), line, sizeof line));
  CHECK(strcmp(line, "1,120,FIRMWARE,,,commit=unknown\n") == 0);
  CHECK(w.line(sl::firmware(0xec29f0baUL, true), line, sizeof line));
  CHECK(strcmp(line, "1,120,FIRMWARE,,,commit=ec29f0ba+dirty\n") == 0);
  CHECK(w.line(sl::battery(500, 87), line, sizeof line));
  CHECK(strcmp(line, "1,500,BATTERY,,,87\n") == 0);

  Event e;
  e.type = Event::Type::Action;
  e.ms = 61000;
  e.action = Action::Step;
  e.table = 7;
  e.stage = Stage::Pay;
  e.request_open = true;
  CHECK(w.line(sl::from_event(e), line, sizeof line));
  CHECK(strcmp(line, "1,61000,STEP,7,PAY,request=1 muted=0\n") == 0);

  e.type = Event::Type::Alert;
  e.action = Action::None;
  e.need = Need::Stage;
  e.stage = Stage::Bill;
  e.ms = 180000;
  CHECK(w.line(sl::from_event(e), line, sizeof line));
  CHECK(strcmp(line, "1,180000,ALERT,7,BILL,BRING CHECK\n") == 0);
  e.need = Need::Request;
  CHECK(w.line(sl::from_event(e), line, sizeof line));
  CHECK(strcmp(line, "1,180000,ALERT,7,BILL,GUEST REQUEST\n") == 0);

  e.type = Event::Type::Action;
  e.action = Action::Mute;
  e.stage = Stage::Main;
  e.request_open = false;
  e.muted = true;
  CHECK(w.line(sl::from_event(e), line, sizeof line));
  CHECK(strcmp(line, "1,180000,MUTE,7,MAIN,request=0 muted=1\n") == 0);

  CHECK(w.line(sl::boot(90, sl::Reset::Brownout), line, sizeof line));
  CHECK(strcmp(line, "2,90,BOOT,,,reset=BROWNOUT\n") == 0);
  CHECK(w.boots() == 2);
  CHECK(w.line(sl::battery(100, -1), line, sizeof line));
  CHECK(strcmp(line, "2,100,BATTERY,,,?\n") == 0);
}

static void test_broken_records_are_skipped() {
  char line[96];
  sl::Writer w;
  sl::Record erased;
  memset(&erased, 0xff, sizeof erased);  // erased flash
  CHECK(!w.line(erased, line, sizeof line));
  sl::Record zero{};
  CHECK(!w.line(zero, line, sizeof line));
  sl::Record bad_action{1000, static_cast<uint8_t>(sl::Kind::Action), 3, 0, 0};  // Action::None
  CHECK(!w.line(bad_action, line, sizeof line));
  sl::Record bad_stage{1000, static_cast<uint8_t>(sl::Kind::Action), 3, 1, 0x0f};
  CHECK(!w.line(bad_stage, line, sizeof line));
  sl::Record fw_before_boot = sl::firmware(0x12345678UL, false);
  CHECK(!w.line(fw_before_boot, line, sizeof line));  // no BOOT yet
  CHECK(w.boots() == 0);
}

int main() {
  test_events_from_app();
  test_queue_keeps_the_last_events();
  test_csv_lines();
  test_broken_records_are_skipped();
  printf("shiftlog: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
