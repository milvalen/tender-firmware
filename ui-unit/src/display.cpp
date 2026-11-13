#include "display.h"

#include "config.h"

namespace unit {

using tender::Screen;

namespace {

// Screen layout, in pixels from the top.
constexpr int kTableY = 6;      // large table number
constexpr int kLine2Y = 34;     // stage or alert text
constexpr int kMinutesY = 60;   // minutes and progress bar
constexpr int kStripY = 78;     // row of all tables
constexpr int kStripH = 22;
constexpr int kKeysH = 14;      // key labels at the bottom
constexpr int kMargin = 6;

// Status top right, table number, then the second line (inverted as a toast).
void draw_header(M5Canvas& c, const Screen& s, uint16_t bg) {
  const int w = c.width();

  c.setTextSize(1);
  c.setTextDatum(top_right);
  c.setTextColor(kAccent, bg);
  c.drawString(s.status, w - 4, 3);

  c.setTextDatum(top_left);
  c.setTextSize(3);
  c.setTextColor(s.table_accent ? kAccent : kPaper, bg);
  c.drawString(s.table, kMargin, kTableY);

  c.setTextSize(2);
  if (s.line2_toast) {
    c.fillRect(0, kLine2Y, w, 20, kPaper);
    c.setTextColor(kInk, kPaper);
  } else {
    c.setTextColor(s.line2_accent ? kAccent : kPaper, bg);
  }
  c.drawString(s.line2, kMargin, kLine2Y + 3);
}

// Minutes in the stage and a bar that fills up to the alert threshold.
void draw_progress(M5Canvas& c, const Screen& s, uint16_t bg) {
  c.setTextSize(1);
  c.setTextColor(kGrey, bg);
  c.drawString(s.minutes, kMargin, kMinutesY);
  if (s.progress <= 0.0f) return;

  const int x = 50;
  const int w = c.width() - x - kMargin;
  const float part = s.progress > 1.0f ? 1.0f : s.progress;
  c.drawRect(x, kMinutesY, w, 7, kGrey);
  c.fillRect(x, kMinutesY, static_cast<int>(w * part), 7, s.progress >= 1.0f ? kAccent : kPaper);
}

// Every table in number order; the selected one is a filled cell.
void draw_strip(M5Canvas& c, const Screen& s, uint16_t bg) {
  if (s.strip_count == 0) return;
  const int cell = c.width() / static_cast<int>(s.strip_count);
  c.setTextSize(cell >= 28 ? 2 : 1);
  c.setTextDatum(middle_center);

  for (size_t i = 0; i < s.strip_count; ++i) {
    const Screen::Cell& t = s.strip[i];
    const int x = static_cast<int>(i) * cell;
    uint16_t fg = t.accent ? kAccent : kGrey;
    uint16_t cell_bg = bg;
    if (t.selected) {
      cell_bg = t.high ? kAccent : kPaper;
      fg = kInk;
      c.fillRect(x + 1, kStripY, cell - 2, kStripH, cell_bg);
    }
    char number[4];
    snprintf(number, sizeof number, "%u", t.number);
    c.setTextColor(fg, cell_bg);
    c.drawString(number, x + cell / 2, kStripY + kStripH / 2);
  }
  c.setTextDatum(top_left);
}

// One label per key along the bottom edge. With 3 labels, the first and the
// last go to the screen edges.
void draw_key_labels(M5Canvas& c, const Screen& s, uint16_t bg) {
  const int w = c.width();
  const int h = c.height();
  const int n = static_cast<int>(s.key_count);

  c.drawFastHLine(0, h - kKeysH, w, kGrey);
  c.setTextSize(1);
  for (int i = 0; i < n; ++i) {
    int x = w * (2 * i + 1) / (2 * n);
    c.setTextDatum(bottom_center);
    if (n == 3 && i == 0) {
      c.setTextDatum(bottom_left);
      x = 4;
    }
    if (n == 3 && i == 2) {
      c.setTextDatum(bottom_right);
      x = w - 4;
    }
    c.setTextColor(s.key_accent[i] ? kAccent : kGrey, bg);
    c.drawString(s.keys[i], x, h - 2);
  }
}

}  // namespace

void draw_board(M5Canvas& canvas, const Screen& s) {
  const uint16_t bg = s.burst_on ? kAccent : kInk;  // the whole screen flashes on an alert
  canvas.fillScreen(bg);
  canvas.setTextFont(1);
  draw_header(canvas, s, bg);
  draw_progress(canvas, s, bg);
  draw_strip(canvas, s, bg);
  draw_key_labels(canvas, s, bg);
  canvas.pushSprite(0, 0);
}

void draw_self_test(M5Canvas& canvas, const tender::SelfTest::Lines& lines) {
  canvas.fillScreen(kInk);
  canvas.setTextFont(1);
  canvas.setTextSize(2);
  canvas.setTextDatum(top_left);
  for (size_t i = 0; i < tender::SelfTest::kLines; ++i) {
    const uint16_t fg = lines.accent[i] ? kAccent : (i == 0 ? kPaper : kGrey);
    canvas.setTextColor(fg, kInk);
    canvas.drawString(lines.row[i], 0, static_cast<int>(i) * 16 + 4);
  }
  canvas.pushSprite(0, 0);
}

}  // namespace unit
