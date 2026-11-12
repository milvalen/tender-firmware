// TENDER TND-01 interface unit, stage 0: table board and timers on M5Stack StickS3.
//
// UX rules (tender-vault/10-concept/ux-principles.md):
//   - one function per key; long press only for rare, reversible actions;
//     the current meaning of each key is printed at the bottom of the screen
//   - the main line says what to do ("BRING CHECK"), not the stage code
//   - a high alert is one short burst, then a steady marker; it comes back
//     after 3 min if not handled; low alerts (dessert) are only marked
//   - every press shows a 1 s confirmation of what changed
//   - the list order changes only after an action, an alert or while idle
//
// Keys:
//   4-key mode (M5Stack Dual Button unit on the Grove port, kHasDualButton):
//     A (front) STEP, hold UNDO | B (side) NEXT, hold PREV
//     red ACK, hold MUTE        | blue REQUEST
//   2-key mode (StickS3 alone):
//     A STEP, hold UNDO | B NEXT, hold ACK while an alert is shown, else REQUEST
//
// The StickS3 has no vibration motor. Set kVibroPin to drive one [?].
// A USB serial console gives the full control set for desk testing.
#include <M5Unified.h>

#include "tableboard.h"

using tender::Board;
using tender::Priority;
using tender::Stage;
using tender::Table;

namespace {

// Table numbers on the floor. Edit for the real restaurant.
constexpr uint8_t kTables[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

// Hardware options.
constexpr bool kHasDualButton = false;  // set true when the Dual Button unit is plugged in
constexpr int kRedPin = 9;              // Grove PORT.A yellow [?]
constexpr int kBluePin = 10;            // Grove PORT.A white [?]
constexpr bool kDualActiveHigh = true;  // [?] check on the real unit
constexpr int kVibroPin = -1;           // GPIO of a vibration motor driver, -1 = none
constexpr bool kBeepOnAlert = false;    // the floor stays silent by default

// Timing.
constexpr uint32_t kHoldMs = 600;
constexpr uint32_t kBurstMs = 2000;
constexpr uint32_t kBurstBlinkMs = 250;
constexpr uint32_t kToastMs = 1200;
constexpr uint32_t kRedrawMs = 500;
constexpr uint32_t kDimAfterMs = 20000;
constexpr uint32_t kIdleResortMs = 10000;
constexpr uint8_t kBright = 128;
constexpr uint8_t kDim = 16;

// Ink Industrial palette on a colour LCD: black, white, grey, one accent.
constexpr uint16_t kInk = TFT_BLACK;
constexpr uint16_t kPaper = TFT_WHITE;
constexpr uint16_t kGrey = 0x8410;
constexpr uint16_t kAccent = 0xEAA4;  // signal orange

const char kHelp[] =
    "TND-01 serial console\n"
    "  j / k  next / previous table    1  STEP    u  UNDO\n"
    "  2      ACK                      m  MUTE 10 min\n"
    "  3      REQUEST (open / close)   ?  state\n";

// Debounced GPIO key with click and hold, for keys outside M5Unified.
class GpioKey {
 public:
  GpioKey(int pin, bool active_high) : pin_(pin), active_high_(active_high) {}
  void begin() { pinMode(pin_, active_high_ ? INPUT_PULLDOWN : INPUT_PULLUP); }
  void update(uint32_t now) {
    clicked_ = held_ = false;
    bool down = digitalRead(pin_) == (active_high_ ? HIGH : LOW);
    if (down != raw_) { raw_ = down; changed_ = now; }
    if (now - changed_ < 20) return;  // debounce
    if (down && !pressed_) { pressed_ = true; since_ = now; hold_fired_ = false; }
    if (down && pressed_ && !hold_fired_ && now - since_ >= kHoldMs) { held_ = true; hold_fired_ = true; }
    if (!down && pressed_) { pressed_ = false; if (!hold_fired_) clicked_ = true; }
  }
  bool clicked() const { return clicked_; }
  bool held() const { return held_; }

 private:
  int pin_;
  bool active_high_;
  bool raw_ = false, pressed_ = false, hold_fired_ = false, clicked_ = false, held_ = false;
  uint32_t changed_ = 0, since_ = 0;
};

Board board(kTables, sizeof(kTables));
M5Canvas canvas(&M5.Display);
GpioKey red_key(kRedPin, kDualActiveHigh);
GpioKey blue_key(kBluePin, kDualActiveHigh);

uint32_t last_input = 0;
uint32_t last_redraw = 0;
uint32_t last_idle_resort = 0;
uint32_t burst_until = 0;
uint32_t toast_until = 0;
char toast[28] = "";
bool dirty = true;

void show_toast(const char* text, uint32_t now) {
  snprintf(toast, sizeof toast, "%s", text);
  toast_until = now + kToastMs;
}

void confirm_table(const char* what, uint32_t now) {
  char label[8], line[28];
  tender::table_label(board.selected().number, label, sizeof label);
  snprintf(line, sizeof line, "%s %s", label, what);
  show_toast(line, now);
}

// Actions. Each one confirms what changed (UX rule P4).
void do_step(uint32_t now) {
  board.step(now);
  confirm_table(tender::stage_status(board.selected().stage), now);
}
void do_undo(uint32_t now) {
  if (board.undo(now)) confirm_table("UNDONE", now);
  else show_toast("NOTHING TO UNDO", now);
}
void do_request(uint32_t now) {
  board.request(now);
  confirm_table(board.selected().request_open ? "REQUEST OPEN" : "REQUEST DONE", now);
}
void do_ack(uint32_t now) {
  board.ack(now);
  burst_until = 0;
  show_toast("OK, AGAIN IN 3 MIN", now);
}
void do_mute(uint32_t now) {
  board.mute(now);
  burst_until = 0;
  show_toast("MUTED 10 MIN", now);
}
void do_next(int delta, uint32_t now) {
  (void)now;
  board.scroll(delta);
}

bool alert_on_screen(uint32_t now) {
  return board.priority(board.selected(), now) == Priority::High;
}

void format_minutes(const Table& t, uint32_t now, char* buf, size_t len) {
  if (t.stage == Stage::Free && !t.request_open) {
    snprintf(buf, len, "FREE");
    return;
  }
  uint32_t m = board.elapsed_min(t, now);
  snprintf(buf, len, "%lu MIN", static_cast<unsigned long>(m > 99 ? 99 : m));
}

void draw_keys(uint32_t now, uint16_t bg) {
  const int w = canvas.width(), h = canvas.height();
  canvas.drawFastHLine(0, h - 14, w, kGrey);
  canvas.setTextSize(1);
  canvas.setTextColor(kGrey, bg);
  if (kHasDualButton) {
    const char* labels[] = {"A STEP", "B NEXT", "RED ACK", "BLUE REQ"};
    for (int i = 0; i < 4; ++i) {
      canvas.setTextDatum(bottom_center);
      canvas.drawString(labels[i], w * (2 * i + 1) / 8, h - 2);
    }
  } else {
    canvas.setTextDatum(bottom_left);
    canvas.drawString("A STEP", 4, h - 2);
    canvas.setTextDatum(bottom_center);
    canvas.drawString("B NEXT", w / 2, h - 2);
    canvas.setTextDatum(bottom_right);
    canvas.setTextColor(alert_on_screen(now) ? kAccent : kGrey, bg);
    canvas.drawString(alert_on_screen(now) ? "HOLD B: ACK" : "HOLD B: REQ", w - 4, h - 2);
  }
}

void draw(uint32_t now) {
  const int w = canvas.width();
  const bool burst = now < burst_until && ((burst_until - now) / kBurstBlinkMs) % 2 == 0;
  const uint16_t bg = burst ? kAccent : kInk;
  canvas.fillScreen(bg);
  canvas.setTextFont(1);

  // Status line: only what needs attention (UX rule P8).
  canvas.setTextSize(1);
  canvas.setTextDatum(top_right);
  int level = M5.Power.getBatteryLevel();
  int x = w - 4;
  if (level >= 0 && level < 25) {
    canvas.setTextColor(kAccent, bg);
    canvas.drawString("LOW BATTERY", x, 3);
    x -= 80;
  }
  if (board.muted(now)) {
    canvas.setTextColor(kAccent, bg);
    canvas.drawString("MUTED", x, 3);
  }

  // Selected table and what to do.
  const Table& sel = board.selected();
  const char* action = tender::action_text(board, sel, now);
  const Priority prio = board.priority(sel, now);
  char label[8], mins[12], line[28];
  tender::table_label(sel.number, label, sizeof label);
  canvas.setTextDatum(top_left);
  canvas.setTextSize(3);
  canvas.setTextColor(prio == Priority::High ? kAccent : kPaper, bg);
  canvas.drawString(label, 6, 6);

  canvas.setTextSize(2);
  if (now < toast_until) {
    canvas.fillRect(0, 34, w, 20, kPaper);
    canvas.setTextColor(kInk, kPaper);
    canvas.drawString(toast, 6, 37);
  } else if (action) {
    canvas.setTextColor(prio == Priority::None ? kPaper : kAccent, bg);
    canvas.drawString(action, 6, 37);
  } else {
    canvas.setTextColor(kPaper, bg);
    canvas.drawString(tender::stage_status(sel.stage), 6, 37);
  }

  // Secondary: minutes and progress to the threshold.
  format_minutes(sel, now, mins, sizeof mins);
  canvas.setTextSize(1);
  canvas.setTextColor(kGrey, bg);
  canvas.drawString(mins, 6, 60);
  float p = board.progress(sel, now);
  if (p > 0.0f) {
    int bx = 50, bw = w - bx - 6;
    int fill = static_cast<int>(bw * (p > 1.0f ? 1.0f : p));
    canvas.drawRect(bx, 60, bw, 7, kGrey);
    canvas.fillRect(bx, 60, fill, 7, p >= 1.0f ? kAccent : kPaper);
  }

  // Next 2 tables in the list snapshot.
  size_t me = board.selected_rank();
  int y = 76;
  canvas.setTextSize(2);
  for (size_t i = 0, shown = 0; i < board.size() && shown < 2; ++i) {
    if (i == me) continue;
    const Table& t = board.ranked(i);
    const char* a = tender::action_text(board, t, now);
    tender::table_label(t.number, label, sizeof label);
    format_minutes(t, now, mins, sizeof mins);
    snprintf(line, sizeof line, "%s %s", label, a ? a : mins);
    canvas.setTextColor(board.priority(t, now) == Priority::None ? kGrey : kAccent, bg);
    canvas.drawString(line, 6, y);
    y += 20;
    ++shown;
  }

  draw_keys(now, bg);
  canvas.pushSprite(0, 0);
}

void print_state(uint32_t now) {
  char label[8], mins[12];
  for (size_t i = 0; i < board.size(); ++i) {
    const Table& t = board.ranked(i);
    const char* a = tender::action_text(board, t, now);
    tender::table_label(t.number, label, sizeof label);
    format_minutes(t, now, mins, sizeof mins);
    Serial.printf("%c %s %-4s %-7s %s\n", &t == &board.selected() ? '>' : ' ', label,
                  tender::stage_name(t.stage), mins, a ? a : "");
  }
}

bool handle_serial(uint32_t now) {
  bool any = false;
  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    switch (c) {
      case 'j': do_next(1, now); break;
      case 'k': do_next(-1, now); break;
      case '1': do_step(now); break;
      case 'u': do_undo(now); break;
      case '2': do_ack(now); break;
      case 'm': do_mute(now); break;
      case '3': do_request(now); break;
      case '?': print_state(now); continue;
      case 'h': Serial.print(kHelp); continue;
      default: continue;
    }
    any = true;
  }
  return any;
}

bool handle_keys(uint32_t now) {
  bool any = true;
  if (M5.BtnA.wasHold()) do_undo(now);
  else if (M5.BtnA.wasClicked()) do_step(now);
  else if (M5.BtnB.wasHold()) {
    if (kHasDualButton) do_next(-1, now);
    else if (alert_on_screen(now)) do_ack(now);
    else do_request(now);
  } else if (M5.BtnB.wasClicked()) do_next(1, now);
  else any = false;

  if (kHasDualButton) {
    red_key.update(now);
    blue_key.update(now);
    if (red_key.held()) { do_mute(now); any = true; }
    else if (red_key.clicked()) { do_ack(now); any = true; }
    if (blue_key.clicked()) { do_request(now); any = true; }
  }
  return any;
}

void signal_alert(uint32_t now) {
  burst_until = now + kBurstMs;
  if (kBeepOnAlert) M5.Speaker.tone(2000, 80);
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  if (kHasDualButton) {
    M5.Power.setExtOutput(true);  // Grove 5 V is off by default in M5Unified
    red_key.begin();
    blue_key.begin();
  }
  if (kVibroPin >= 0) pinMode(kVibroPin, OUTPUT);
  M5.Display.setRotation(1);  // landscape; use 3 if the screen is upside down in the enclosure [?]
  M5.Display.setBrightness(kBright);
  M5.BtnA.setHoldThresh(kHoldMs);
  M5.BtnB.setHoldThresh(kHoldMs);
  canvas.setColorDepth(16);
  canvas.createSprite(M5.Display.width(), M5.Display.height());
  if (kBeepOnAlert) M5.Speaker.setVolume(64);
  Serial.print(kHelp);
  last_input = millis();
}

void loop() {
  M5.update();
  const uint32_t now = millis();
  const bool was_dim = now - last_input >= kDimAfterMs;

  if (handle_keys(now) | handle_serial(now)) {
    if (was_dim) board.resort(now);
    last_input = now;
    dirty = true;
  }

  if (board.poll_alert(now)) {
    signal_alert(now);
    last_input = now;  // wake the screen
    dirty = true;
  }

  const bool idle = now - last_input >= kDimAfterMs;
  if (idle && now - last_idle_resort >= kIdleResortMs) {
    board.resort(now);  // nobody is reading: safe to reorder
    last_idle_resort = now;
  }

  if (kVibroPin >= 0) digitalWrite(kVibroPin, now < burst_until && ((burst_until - now) / kBurstBlinkMs) % 2 == 0);
  M5.Display.setBrightness(idle ? kDim : kBright);

  if (dirty || now < burst_until || now < toast_until + kRedrawMs || now - last_redraw >= kRedrawMs) {
    draw(now);
    last_redraw = now;
    dirty = false;
  }
  delay(10);
}
