// TENDER TND-01 interface unit, stage 0: table board and timers on M5Stack StickS3.
//
// The interface logic (keys, alerts, screen content) is in lib/tableboard and
// is shared with the desktop simulator in sim/. This file only connects it to
// the hardware: it reads keys, sends the screen to the LCD and drives outputs.
//
//   config.h          pins, timings, colours, log size
//   display.cpp       screen drawing
//   gpio_key.h        Dual Button keys
//   vibro.cpp         vibration motor PWM
//   flash_log.cpp     shift log in flash
//   self_test_mode.cpp  board self-test ("!" on the console)
//
// Keys (enclosure rev 0.2 engraves the function next to each key):
//   4-key mode (Dual Button unit on the Grove port, kHasDualButton):
//     STEP = key A (front), hold UNDO | NEXT = key B (top), hold PREV
//     ACK = red key, hold MUTE        | TALK = blue key
//   2-key mode (StickS3 alone):
//     STEP, hold UNDO | NEXT; hold NEXT closes an open request, else ACK
//     while an alert is shown, else opens a request
//
// USB serial console, same keys as the simulator:
//   a / s / d / f = A / B / red / blue, uppercase = hold,
//   ? = state, ! = self-test, L = shift log as CSV (read with tools/shift_log.py).
#include <M5Unified.h>

#include "app.h"
#include "config.h"
#include "display.h"
#include "flash_log.h"
#include "gpio_key.h"
#include "reset_reason.h"
#include "self_test_mode.h"
#include "shiftlog.h"
#include "tableboard.h"
#include "version.h"
#include "vibro.h"

using namespace unit;
using tender::Action;
using tender::Key;
using tender::Press;

namespace {

tender::Board board(kTables, sizeof(kTables));
tender::App app(board, kHasDualButton ? tender::KeyMode::FourKeys : tender::KeyMode::TwoKeys);

M5Canvas canvas(&M5.Display);
GpioKey red_key(kRedPin, kDualActiveHigh);
GpioKey blue_key(kBluePin, kDualActiveHigh);
Vibro vibro;
FlashLog shift_log;
SelfTestMode self_test(canvas, vibro, shift_log);

bool redraw_needed = true;
uint32_t last_redraw = 0;
uint32_t last_battery_log = 0;

// --- Input ---------------------------------------------------------------------

// Gives one key press to the core. Returns true if it changed something.
bool feed(Key k, Press p, uint32_t now) {
  const Action a = app.press(k, p, now);
  if (a == Action::None) return false;
  Serial.printf("%lu %s\n", static_cast<unsigned long>(now), tender::action_name(a));
  return true;
}

bool read_keys(uint32_t now) {
  bool changed = false;
  if (M5.BtnA.wasHold()) changed |= feed(Key::A, Press::Long, now);
  else if (M5.BtnA.wasClicked()) changed |= feed(Key::A, Press::Short, now);
  if (M5.BtnB.wasHold()) changed |= feed(Key::B, Press::Long, now);
  else if (M5.BtnB.wasClicked()) changed |= feed(Key::B, Press::Short, now);

  if (kHasDualButton) {
    red_key.update(now);
    blue_key.update(now);
    if (red_key.held()) changed |= feed(Key::Red, Press::Long, now);
    else if (red_key.clicked()) changed |= feed(Key::Red, Press::Short, now);
    if (blue_key.held()) changed |= feed(Key::Blue, Press::Long, now);
    else if (blue_key.clicked()) changed |= feed(Key::Blue, Press::Short, now);
  }
  return changed;
}

// Prints all tables, most urgent first; ">" marks the selected one.
void print_state(uint32_t now) {
  char label[8];
  for (size_t i = 0; i < board.size(); ++i) {
    const tender::Table& t = board.ranked(i);
    const char* action = tender::action_text(board, t, now);
    tender::table_label(t.number, label, sizeof label);
    Serial.printf("%c %s %-4s %3lu min %s\n", &t == &board.selected() ? '>' : ' ', label,
                  tender::stage_name(t.stage), static_cast<unsigned long>(board.elapsed_min(t, now)),
                  action ? action : "");
  }
}

bool read_console(uint32_t now) {
  static const char kKeys[] = "asdf";  // A, B, red, blue
  bool changed = false;
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    const char* key = strchr(kKeys, c | 0x20);  // lowercase
    const bool upper = (c & 0x20) == 0;
    if (c == '?') print_state(now);
    else if (c == '!') self_test.request();
    else if (c == 'L') shift_log.dump(now, Serial);
    else if (key && *key) changed |= feed(static_cast<Key>(key - kKeys), upper ? Press::Long : Press::Short, now);
  }
  return changed;
}

// --- Output --------------------------------------------------------------------

// Writes core events to the shift log, and the battery level every 10 minutes.
void log_events(uint32_t now) {
  tender::Event e;
  while (app.pop_event(e)) shift_log.append(tender::shiftlog::from_event(e));
  if (now - last_battery_log >= kBatteryLogMs) {
    shift_log.append(tender::shiftlog::battery(now, M5.Power.getBatteryLevel()));
    last_battery_log = now;
  }
}

void drive_outputs(uint32_t now) {
  vibro.set(app.vibrate(now) ? kVibroDuty : 0);
  const bool off = app.screen_off(now);
  M5.Display.setBrightness(off ? 0 : app.idle(now) ? kDim : kBright);
}

void redraw(uint32_t now) {
  if (!redraw_needed && !app.busy(now) && now - last_redraw < kRedrawMs) return;
  tender::Screen s;
  app.screen(now, s);
  draw_board(canvas, s);
  last_redraw = now;
  redraw_needed = false;
}

// --- Start -----------------------------------------------------------------------

void start_hardware() {
  if (kHasDualButton || kVibroPin >= 0) {
    M5.Power.setExtOutput(true);  // Grove 5 V is off by default in M5Unified; Hat2 5 V too [?]
  }
  if (kHasDualButton) {
    red_key.begin();
    blue_key.begin();
  }
  vibro.attach(kVibroPin);
  M5.BtnA.setHoldThresh(kHoldMs);
  M5.BtnB.setHoldThresh(kHoldMs);
  if (kBeepOnAlert) M5.Speaker.setVolume(64);

  M5.Display.setRotation(kRotation);
  M5.Display.setBrightness(kBright);
  canvas.setColorDepth(16);
  canvas.createSprite(M5.Display.width(), M5.Display.height());
}

void start_shift_log() {
  shift_log.begin();
  shift_log.append(tender::shiftlog::boot(millis(), reset_code()));
  shift_log.append(tender::shiftlog::firmware(tender::kFirmwareCommitU32, tender::kFirmwareDirty));
}

}  // namespace

void setup() {
  M5.begin(M5.config());
  Serial.begin(115200);
  start_hardware();
  start_shift_log();
  Serial.printf(
      "TND-01 firmware %s%s (%s): a/s/d/f = A/B/red/blue, uppercase = hold, ? = state, ! = self-test, "
      "L = shift log; reset: %s; log %s\n",
      tender::kFirmwareCommit, tender::kFirmwareDirtyMark, tender::kFirmwareDate, reset_name(),
      shift_log.ok() ? "ok" : "unavailable");
}

void loop() {
  M5.update();
  const uint32_t now = millis();

  if (self_test.active()) {
    self_test.loop(now);
    delay(5);
    return;
  }

  if (read_keys(now) | read_console(now)) redraw_needed = true;
  if (app.tick(now)) {  // a new alert
    redraw_needed = true;
    if (kBeepOnAlert && !app.board().muted()) M5.Speaker.tone(2000, 80);
  }
  log_events(now);
  app.set_battery(M5.Power.getBatteryLevel());
  drive_outputs(now);
  redraw(now);
  delay(10);
}
