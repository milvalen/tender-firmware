// TENDER TND-01 interface unit, stage 0: table board and timers on M5Stack StickS3.
//
// Stage 0 board limits:
//   - 2 buttons instead of wheel + 3 keys: actions are mapped to single,
//     double and long presses (see kHelp below and tender-firmware/README.md).
//   - no vibration motor: the signal is a screen flash. Sound is off by
//     default because the device must stay silent on the floor.
// A USB serial console gives the full control set for desk testing.
#include <M5Unified.h>

#include "tableboard.h"

using tender::Board;
using tender::Need;
using tender::Stage;
using tender::Table;

namespace {

// Table numbers on the floor. Edit for the real restaurant.
constexpr uint8_t kTables[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

constexpr bool kBeepOnSignal = false;
constexpr uint32_t kRedrawMs = 500;
constexpr uint32_t kFlashPeriodMs = 1500;
constexpr uint32_t kFlashOnMs = 300;
constexpr uint32_t kDimAfterMs = 20000;
constexpr uint8_t kBright = 128;
constexpr uint8_t kDim = 16;

// Ink Industrial palette on a colour LCD: black, white, one accent.
constexpr uint16_t kInk = TFT_BLACK;
constexpr uint16_t kPaper = TFT_WHITE;
constexpr uint16_t kGrey = 0x8410;
constexpr uint16_t kAccent = 0xEAA4;  // signal orange

const char kHelp[] =
    "TND-01 serial console\n"
    "  j / k  wheel down / up      1  STEP     2  ACK     3  REQUEST\n"
    "  u      undo last STEP       m  MUTE 10 min         ?  state\n"
    "Buttons (stage 0): A single=STEP double=REQUEST hold=UNDO;\n"
    "                   B single=next table double=ACK hold=MUTE\n";

Board board(kTables, sizeof(kTables));
M5Canvas canvas(&M5.Display);
uint32_t last_input = 0;
uint32_t last_redraw = 0;
bool dirty = true;

void touch(uint32_t now) {
  last_input = now;
  dirty = true;
}

void format_elapsed(const Table& t, uint32_t now, char* buf, size_t len) {
  if (t.stage == Stage::Free) {
    snprintf(buf, len, "T+--");
    return;
  }
  uint32_t m = board.elapsed_min(t, now);
  snprintf(buf, len, "T+%02lu", static_cast<unsigned long>(m > 99 ? 99 : m));
}

const char* load_label() {
  int level = M5.Power.getBatteryLevel();
  if (level < 0) return "LOAD ?";
  if (level > 87) return "LOAD 4/4";
  if (level > 62) return "LOAD 3/4";
  if (level > 37) return "LOAD 2/4";
  if (level > 12) return "LOAD 1/4";
  return "LOAD 0/4";
}

void draw(uint32_t now) {
  const int w = canvas.width();
  const bool flash = board.signalling(now) && (now % kFlashPeriodMs) < kFlashOnMs;
  const uint16_t bg = flash ? kAccent : kInk;
  canvas.fillScreen(bg);
  canvas.setTextFont(1);

  // Header.
  canvas.setTextSize(1);
  canvas.setTextColor(kGrey, bg);
  canvas.setTextDatum(top_left);
  canvas.drawString("UNIT TND-01", 4, 3);
  canvas.setTextDatum(top_right);
  canvas.drawString(load_label(), w - 4, 3);
  if (board.muted(now)) {
    canvas.setTextDatum(top_center);
    canvas.setTextColor(kAccent, bg);
    canvas.drawString("MUTE", w / 2, 3);
  }
  canvas.drawFastHLine(4, 14, w - 8, kGrey);

  // Selected table.
  const Table& sel = board.selected();
  const bool due = board.overdue(sel, now);
  char label[8], elapsed[8], line[24];
  tender::table_label(sel.number, label, sizeof label);
  format_elapsed(sel, now, elapsed, sizeof elapsed);
  snprintf(line, sizeof line, "%s %s", label, elapsed);
  canvas.setTextDatum(top_left);
  canvas.setTextSize(3);
  canvas.setTextColor(due ? kAccent : kPaper, bg);
  canvas.drawString(line, 6, 22);

  // Stage and request.
  canvas.setTextSize(2);
  canvas.setTextColor(kPaper, bg);
  canvas.drawString(tender::stage_name(sel.stage), 6, 52);
  if (sel.request_open) {
    uint32_t req_min = (now - sel.request_since) / tender::kMinute;
    snprintf(line, sizeof line, "REQ %lu", static_cast<unsigned long>(req_min));
    canvas.setTextColor(board.need(sel, now) == Need::Request ? kAccent : kPaper, bg);
    canvas.drawString(line, 90, 52);
  }

  // Progress to the threshold of the current stage.
  float p = board.progress(sel, now);
  if (p > 0.0f) {
    int bar = static_cast<int>((w - 12) * (p > 1.0f ? 1.0f : p));
    canvas.drawRect(6, 72, w - 12, 6, kGrey);
    canvas.fillRect(6, 72, bar, 6, p >= 1.0f ? kAccent : kPaper);
  }

  // Next tables by urgency.
  canvas.setTextSize(2);
  size_t me = board.selected_rank(now);
  int y = 86;
  for (size_t i = 0, shown = 0; i < board.size() && shown < 2; ++i) {
    if (i == me) continue;
    const Table& t = board.ranked(i, now);
    tender::table_label(t.number, label, sizeof label);
    format_elapsed(t, now, elapsed, sizeof elapsed);
    snprintf(line, sizeof line, "%s %-4s %s", label, tender::stage_name(t.stage), elapsed);
    canvas.setTextColor(board.overdue(t, now) ? kAccent : kGrey, bg);
    canvas.drawString(line, 6, y);
    y += 22;
    ++shown;
  }

  canvas.pushSprite(0, 0);
}

void print_state(uint32_t now) {
  char label[8], elapsed[8];
  for (size_t i = 0; i < board.size(); ++i) {
    const Table& t = board.ranked(i, now);
    tender::table_label(t.number, label, sizeof label);
    format_elapsed(t, now, elapsed, sizeof elapsed);
    Serial.printf("%c %s %-4s %s%s%s\n", &t == &board.selected() ? '>' : ' ', label,
                  tender::stage_name(t.stage), elapsed, t.request_open ? " REQ" : "",
                  board.overdue(t, now) ? " DUE" : "");
  }
}

void handle_serial(uint32_t now) {
  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    switch (c) {
      case 'j': board.scroll(1, now); break;
      case 'k': board.scroll(-1, now); break;
      case '1': board.step(now); break;
      case '2': board.ack(now); break;
      case '3': board.request(now); break;
      case 'u': board.undo(now); break;
      case 'm': board.mute(now); break;
      case '?': print_state(now); continue;
      case 'h': Serial.print(kHelp); continue;
      default: continue;
    }
    touch(now);
  }
}

void handle_buttons(uint32_t now) {
  if (M5.BtnA.wasSingleClicked()) { board.step(now); touch(now); }
  if (M5.BtnA.wasDoubleClicked()) { board.request(now); touch(now); }
  if (M5.BtnA.wasHold()) { board.undo(now); touch(now); }
  if (M5.BtnB.wasSingleClicked()) { board.scroll(1, now); touch(now); }
  if (M5.BtnB.wasDoubleClicked()) { board.ack(now); touch(now); }
  if (M5.BtnB.wasHold()) { board.mute(now); touch(now); }
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Display.setRotation(1);  // landscape; flip to 3 if the screen is upside down in the enclosure [?]
  M5.Display.setBrightness(kBright);
  canvas.setColorDepth(16);
  canvas.createSprite(M5.Display.width(), M5.Display.height());
  if (kBeepOnSignal) M5.Speaker.setVolume(64);
  Serial.print(kHelp);
  touch(millis());
}

void loop() {
  M5.update();
  const uint32_t now = millis();
  handle_buttons(now);
  handle_serial(now);

  const bool signal = board.signalling(now);
  const bool awake = signal || now - last_input < kDimAfterMs;
  M5.Display.setBrightness(awake ? kBright : kDim);

  static bool was_signalling = false;
  if (signal && !was_signalling && kBeepOnSignal) M5.Speaker.tone(2000, 80);
  was_signalling = signal;

  if (dirty || signal || now - last_redraw >= kRedrawMs) {
    draw(now);
    last_redraw = now;
    dirty = false;
  }
  delay(10);
}
