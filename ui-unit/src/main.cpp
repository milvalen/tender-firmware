// TENDER TND-01 interface unit, stage 0: table board and timers on M5Stack StickS3.
//
// All interface logic (key mapping, alerts, confirmations, screen content)
// lives in lib/tableboard/src/app.h and is shared with the desktop simulator
// in sim/. This file only reads keys, draws the Screen and drives outputs.
//
// Keys (enclosure rev 0.2 engraves the function next to each key):
//   4-key mode (M5Stack Dual Button unit on the Grove port, kHasDualButton):
//     STEP = key A (front), hold UNDO | NEXT = key B (top), hold PREV
//     ACK = red key, hold MUTE        | TALK = blue key
//   2-key mode (StickS3 alone):
//     STEP, hold UNDO | NEXT; hold NEXT closes an open request, else ACK
//     while an alert is shown, else opens a request
//
// A USB serial console uses the same keys as the simulator:
//   a / s / d / f = A / B / red / blue, uppercase = long press, ? = state.
//
// The StickS3 has no vibration motor. Set kVibroPin to drive one [?]: the
// motor runs on PWM with a limited duty, because at full power it can draw
// more than the 5 V output gives on battery.
#include <M5Unified.h>

#include "app.h"
#include "tableboard.h"

using tender::Action;
using tender::App;
using tender::Board;
using tender::Key;
using tender::KeyMode;
using tender::Press;
using tender::Screen;

namespace {

// Table numbers on the floor. Edit for the real restaurant.
constexpr uint8_t kTables[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

// Hardware options.
constexpr bool kHasDualButton = false;  // set true when the Dual Button unit is plugged in
constexpr int kRedPin = 9;              // Grove PORT.A yellow [?]
constexpr int kBluePin = 10;            // Grove PORT.A white [?]
constexpr bool kDualActiveHigh = true;  // [?] check on the real unit
constexpr int kVibroPin = -1;           // GPIO of a vibration motor driver, -1 = none
// Unit Vibrator (U059): the maker gives 424 mA at 5 V for 10 kHz PWM, 50 % duty.
// The StickS3 gives 0.38 A on Grove 5 V. Start low; raise only after a
// current measurement on battery [?].
constexpr uint32_t kVibroFreqHz = 10000;
constexpr uint8_t kVibroBits = 8;
constexpr uint32_t kVibroDuty = 90;     // of 255, about 35 % [?]
constexpr uint8_t kVibroChannel = 2;    // LEDC; the backlight and power code use 6 and 7
constexpr bool kBeepOnAlert = false;    // the floor stays silent by default

constexpr uint32_t kHoldMs = 600;
constexpr uint32_t kRedrawMs = 500;
constexpr uint8_t kBright = 128;
constexpr uint8_t kDim = 16;

// Ink Industrial palette on a colour LCD: black, white, grey, one accent.
constexpr uint16_t kInk = TFT_BLACK;
constexpr uint16_t kPaper = TFT_WHITE;
constexpr uint16_t kGrey = 0x8410;
constexpr uint16_t kAccent = 0xEAA4;  // signal orange

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
App app(board, kHasDualButton ? KeyMode::FourKeys : KeyMode::TwoKeys);
M5Canvas canvas(&M5.Display);
GpioKey red_key(kRedPin, kDualActiveHigh);
GpioKey blue_key(kBluePin, kDualActiveHigh);
uint32_t last_redraw = 0;
bool dirty = true;

void draw(const Screen& s) {
  const int w = canvas.width(), h = canvas.height();
  const uint16_t bg = s.burst_on ? kAccent : kInk;
  canvas.fillScreen(bg);
  canvas.setTextFont(1);

  canvas.setTextSize(1);
  canvas.setTextDatum(top_right);
  canvas.setTextColor(kAccent, bg);
  canvas.drawString(s.status, w - 4, 3);

  canvas.setTextDatum(top_left);
  canvas.setTextSize(3);
  canvas.setTextColor(s.table_accent ? kAccent : kPaper, bg);
  canvas.drawString(s.table, 6, 6);

  canvas.setTextSize(2);
  if (s.line2_toast) {
    canvas.fillRect(0, 34, w, 20, kPaper);
    canvas.setTextColor(kInk, kPaper);
  } else {
    canvas.setTextColor(s.line2_accent ? kAccent : kPaper, bg);
  }
  canvas.drawString(s.line2, 6, 37);

  canvas.setTextSize(1);
  canvas.setTextColor(kGrey, bg);
  canvas.drawString(s.minutes, 6, 60);
  if (s.progress > 0.0f) {
    int bx = 50, bw = w - bx - 6;
    int fill = static_cast<int>(bw * (s.progress > 1.0f ? 1.0f : s.progress));
    canvas.drawRect(bx, 60, bw, 7, kGrey);
    canvas.fillRect(bx, 60, fill, 7, s.progress >= 1.0f ? kAccent : kPaper);
  }

  // Table strip: every table in number order, cursor on the selected one.
  if (s.strip_count > 0) {
    const int cell = w / static_cast<int>(s.strip_count);
    const int y = 78, ch = 22;
    canvas.setTextSize(cell >= 28 ? 2 : 1);
    canvas.setTextDatum(middle_center);
    for (size_t i = 0; i < s.strip_count; ++i) {
      const Screen::Cell& c = s.strip[i];
      const int x = static_cast<int>(i) * cell;
      char num[4];
      snprintf(num, sizeof num, "%u", c.number);
      uint16_t fg = c.accent ? kAccent : kGrey;
      if (c.selected) {
        canvas.fillRect(x + 1, y, cell - 2, ch, c.high ? kAccent : kPaper);
        fg = kInk;
      }
      canvas.setTextColor(fg, c.selected ? (c.high ? kAccent : kPaper) : bg);
      canvas.drawString(num, x + cell / 2, y + ch / 2);
    }
    canvas.setTextDatum(top_left);
  }

  canvas.drawFastHLine(0, h - 14, w, kGrey);
  canvas.setTextSize(1);
  for (size_t i = 0; i < s.key_count; ++i) {
    int x = w * (2 * static_cast<int>(i) + 1) / (2 * static_cast<int>(s.key_count));
    canvas.setTextDatum(bottom_center);
    if (s.key_count == 3 && i == 0) { canvas.setTextDatum(bottom_left); x = 4; }
    if (s.key_count == 3 && i == 2) { canvas.setTextDatum(bottom_right); x = w - 4; }  // long hold label
    canvas.setTextColor(s.key_accent[i] ? kAccent : kGrey, bg);
    canvas.drawString(s.keys[i], x, h - 2);
  }
  canvas.pushSprite(0, 0);
}

void print_state(uint32_t now) {
  char label[8];
  for (size_t i = 0; i < board.size(); ++i) {
    const tender::Table& t = board.ranked(i);
    const char* a = tender::action_text(board, t, now);
    tender::table_label(t.number, label, sizeof label);
    Serial.printf("%c %s %-4s %3lu min %s\n", &t == &board.selected() ? '>' : ' ', label,
                  tender::stage_name(t.stage), static_cast<unsigned long>(board.elapsed_min(t, now)),
                  a ? a : "");
  }
}

bool feed(Key k, Press p, uint32_t now) {
  Action a = app.press(k, p, now);
  if (a != Action::None) Serial.printf("%lu %s\n", static_cast<unsigned long>(now), tender::action_name(a));
  return a != Action::None;
}

bool handle_serial(uint32_t now) {
  bool any = false;
  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    const char* keys = "asdf";
    const char* hit = strchr(keys, c | 0x20);
    if (c == '?') print_state(now);
    else if (hit && *hit) any |= feed(static_cast<Key>(hit - keys), (c & 0x20) ? Press::Short : Press::Long, now);
  }
  return any;
}

bool handle_keys(uint32_t now) {
  bool any = false;
  if (M5.BtnA.wasHold()) any |= feed(Key::A, Press::Long, now);
  else if (M5.BtnA.wasClicked()) any |= feed(Key::A, Press::Short, now);
  if (M5.BtnB.wasHold()) any |= feed(Key::B, Press::Long, now);
  else if (M5.BtnB.wasClicked()) any |= feed(Key::B, Press::Short, now);
  if (kHasDualButton) {
    red_key.update(now);
    blue_key.update(now);
    if (red_key.held()) any |= feed(Key::Red, Press::Long, now);
    else if (red_key.clicked()) any |= feed(Key::Red, Press::Short, now);
    if (blue_key.held()) any |= feed(Key::Blue, Press::Long, now);
    else if (blue_key.clicked()) any |= feed(Key::Blue, Press::Short, now);
  }
  return any;
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  if (kHasDualButton || kVibroPin >= 0) {
    M5.Power.setExtOutput(true);  // Grove 5 V is off by default in M5Unified; Hat2 5 V too [?]
  }
  if (kHasDualButton) {
    red_key.begin();
    blue_key.begin();
  }
  if (kVibroPin >= 0) {
    static_assert(kVibroDuty < (1u << kVibroBits), "kVibroDuty is above the PWM range");
    ledcSetup(kVibroChannel, kVibroFreqHz, kVibroBits);
    ledcAttachPin(kVibroPin, kVibroChannel);
    ledcWrite(kVibroChannel, 0);
  }
  M5.Display.setRotation(1);  // landscape; use 3 if the screen is upside down in the enclosure [?]
  M5.Display.setBrightness(kBright);
  M5.BtnA.setHoldThresh(kHoldMs);
  M5.BtnB.setHoldThresh(kHoldMs);
  canvas.setColorDepth(16);
  canvas.createSprite(M5.Display.width(), M5.Display.height());
  if (kBeepOnAlert) M5.Speaker.setVolume(64);
  Serial.println("TND-01: a/s/d/f = A/B/red/blue, uppercase = hold, ? = state");
}

void loop() {
  M5.update();
  const uint32_t now = millis();

  if (handle_keys(now) | handle_serial(now)) dirty = true;
  if (app.tick(now)) {
    dirty = true;
    if (kBeepOnAlert) M5.Speaker.tone(2000, 80);
  }

  app.set_battery(M5.Power.getBatteryLevel());
  if (kVibroPin >= 0) {
    static bool vibro_on = false;
    const bool on = app.vibrate(now);
    if (on != vibro_on) ledcWrite(kVibroChannel, on ? kVibroDuty : 0);
    vibro_on = on;
  }
  M5.Display.setBrightness(app.idle(now) ? kDim : kBright);

  if (dirty || app.busy(now) || now - last_redraw >= kRedrawMs) {
    Screen s;
    app.screen(now, s);
    draw(s);
    last_redraw = now;
    dirty = false;
  }
  delay(10);
}
