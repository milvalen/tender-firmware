// Native tests for the board self-test core. Run: make -C ui-unit test
#include <stdio.h>
#include <string.h>

#include "selftest.h"

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

static char reply[1024];

static void run(SelfTest& t, const char* line, uint32_t now = 0) { t.command(line, now, reply, sizeof reply); }

static void test_vibro_pins() {
  CHECK(SelfTest::vibro_pin_allowed(1));
  CHECK(SelfTest::vibro_pin_allowed(3));
  CHECK(SelfTest::vibro_pin_allowed(5));
  CHECK(SelfTest::vibro_pin_allowed(8));
  CHECK(SelfTest::vibro_pin_allowed(9));
  CHECK(SelfTest::vibro_pin_allowed(10));
  CHECK(!SelfTest::vibro_pin_allowed(0));
  CHECK(!SelfTest::vibro_pin_allowed(4));   // Boot on the Hat2 pinout
  CHECK(!SelfTest::vibro_pin_allowed(11));  // key A
  CHECK(!SelfTest::vibro_pin_allowed(43));  // UART0
  CHECK(!SelfTest::vibro_pin_allowed(-1));

  SelfTest t(9, 10, true);
  t.begin(0, false, false, "POWERON");
  run(t, "vibro 4");
  CHECK(t.vibro_pin() == -1);
  run(t, "vibro 9");  // the Dual Button uses it in this test
  CHECK(t.vibro_pin() == -1);
  CHECK(strstr(reply, "Dual Button") != nullptr);
  const uint32_t v = t.setup_version();
  run(t, "VIBRO 3");
  CHECK(t.vibro_pin() == 3);
  CHECK(t.setup_version() == v + 1);
  CHECK(t.vibro() == Vibro::Off);
  run(t, "vibro off");
  CHECK(t.vibro_pin() == -1);
}

static void test_duty_cap_and_pulse() {
  SelfTest t(9, 10, true);
  t.begin(0, false, false, "POWERON");
  run(t, "on");
  CHECK(strstr(reply, "vibro <pin> first") != nullptr);
  CHECK(t.vibro_duty(0) == 0);
  run(t, "vibro 2");
  CHECK(t.duty() == SelfTest::kDefaultDuty);
  run(t, "duty 200");  // above the cap
  CHECK(t.duty() == SelfTest::kDefaultDuty);
  run(t, "duty 129");
  CHECK(t.duty() == SelfTest::kDefaultDuty);
  run(t, "duty x");
  CHECK(t.duty() == SelfTest::kDefaultDuty);
  run(t, "duty 128");
  CHECK(t.duty() == 128);
  run(t, "pulse", 1000);
  CHECK(t.vibro() == Vibro::Pulse);
  CHECK(t.vibro_duty(1000) == 128);
  CHECK(t.vibro_duty(1249) == 128);
  CHECK(t.vibro_duty(1250) == 0);
  CHECK(t.vibro_duty(1500) == 128);
  CHECK(t.vibro_duty(2999) == 0);  // the last blink slot is off
  CHECK(t.vibro_duty(3000) == 0);  // the pattern is over
  run(t, "on", 4000);
  CHECK(t.vibro_duty(60000) == 128);
  run(t, "off");
  CHECK(t.vibro_duty(60000) == 0);
  run(t, "on");
  run(t, "exit");
  CHECK(t.exit_requested());
  CHECK(t.vibro_duty(0) == 0);  // the motor stops before the restart
}

static void test_dual_button_level() {
  SelfTest t(9, 10, true);
  t.begin(0, true, true, "POWERON");  // pull-up: both pins HIGH when not pressed
  t.dual_levels(true, true);
  CHECK(t.red().presses == 0);
  CHECK(t.red().active == -1);
  char ev[128];
  CHECK(!t.take_event(ev, sizeof ev));
  t.dual_levels(false, true);  // red pressed
  CHECK(t.red().presses == 1);
  CHECK(t.red().active == 0);
  CHECK(t.take_event(ev, sizeof ev));
  CHECK(strstr(ev, "RED G9") != nullptr);
  CHECK(strstr(ev, "kDualActiveHigh = false") != nullptr);
  CHECK(!t.take_event(ev, sizeof ev));  // once
  t.dual_levels(false, true);           // still held: no new press
  t.dual_levels(true, true);            // released
  CHECK(t.red().presses == 1);
  t.dual_levels(true, false);  // blue pressed
  CHECK(t.blue().presses == 1);
  run(t, "keys");
  CHECK(strstr(reply, "set kDualActiveHigh = false") != nullptr);

  // A new pull: the next levels are "not pressed", counts start again.
  const uint32_t v = t.setup_version();
  run(t, "pull down");
  CHECK(t.pull() == Pull::Down);
  CHECK(t.setup_version() == v + 1);
  t.dual_levels(false, false);
  CHECK(t.red().presses == 0);
  CHECK(t.red().idle_level == false);
  t.dual_levels(true, false);
  CHECK(t.red().active == 1);
  run(t, "pull sideways");
  CHECK(t.pull() == Pull::Down);
}

static void test_keys_rotation_and_screen() {
  SelfTest t(9, 10, false);
  CHECK(t.pull() == Pull::Up);  // the guess for active-low keys
  t.begin(0, true, true, "BROWNOUT");
  t.keys(true, false);
  t.keys(true, false);
  t.keys(false, true);
  CHECK(t.a_presses() == 1);
  CHECK(t.b_presses() == 1);
  run(t, "rot 3");
  CHECK(t.rotation() == 3);
  run(t, "rot 2");
  CHECK(t.rotation() == 3);

  PowerInfo p;
  p.level = 20;
  p.battery_mv = 3610;
  p.vbus_mv = 0;
  t.set_power(p);
  SelfTest::Lines l;
  t.lines(0, l);
  CHECK(strcmp(l.row[0], "SELF TEST BROWNOUT") == 0);
  CHECK(l.accent[0]);  // a brownout restart is marked
  CHECK(strcmp(l.row[1], "A 1  B 1") == 0);
  CHECK(strcmp(l.row[2], "RED G9 HI 0 KEY=?") == 0);
  CHECK(strcmp(l.row[4], "VIB NO PIN") == 0);
  CHECK(strcmp(l.row[5], "BAT 20% 3610MV") == 0);
  CHECK(l.accent[5]);  // low battery
  CHECK(strcmp(l.row[6], "USB OFF I N/A") == 0);
  CHECK(strcmp(l.row[7], "< HAT2 END") == 0);
  for (size_t i = 0; i < SelfTest::kLines; ++i) CHECK(strlen(l.row[i]) <= 20);  // text size 2 on 240 px

  SelfTest u(9, 10, true);
  u.begin(0, false, false, "POWERON");
  u.lines(0, l);
  CHECK(!l.accent[0]);
}

static void test_power_log() {
  SelfTest t(9, 10, true);
  t.begin(5000, false, false, "POWERON");
  CHECK(!t.log_due(5000));  // off by default
  run(t, "log", 5000);
  CHECK(t.log_due(5000));
  CHECK(!t.log_due(14999));
  CHECK(t.log_due(15000));
  PowerInfo p;
  p.level = 87;
  p.battery_mv = 4050;
  p.vbus_mv = 5010;
  p.charging = true;
  p.current_ma = -230;
  t.set_power(p);
  run(t, "vibro 3");
  run(t, "power", 61000);
  CHECK(strcmp(reply, "t=61s bat=87% 4050mV usb=5010mV charging=yes I=-230mA vibro=G3 35% OFF\n") == 0);
  run(t, "log");
  CHECK(!t.log_due(100000));
  CHECK(!t.take_clear_log());
  run(t, "clearlog");
  CHECK(t.take_clear_log());
  CHECK(!t.take_clear_log());  // once
  run(t, "hello");
  CHECK(strstr(reply, "unknown command") != nullptr);
  run(t, "help");
  CHECK(strstr(reply, "vibro <pin>") != nullptr);
  CHECK(strlen(reply) < sizeof reply - 1);  // the help text fits
  run(t, "");
  CHECK(reply[0] == '\0');
}

int main() {
  test_vibro_pins();
  test_duty_cap_and_pulse();
  test_dual_button_level();
  test_keys_rotation_and_screen();
  test_power_log();
  printf("selftest: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
