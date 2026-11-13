#include "view.h"

#include <stdio.h>

#include "common.h"
#include "terminal.h"

namespace sim {

using tender::KeyMode;
using tender::Screen;

namespace {

// --- ANSI styles -------------------------------------------------------------

constexpr const char* kReset = "\x1b[0m";
constexpr const char* kBold = "\x1b[1m";
constexpr const char* kFaint = "\x1b[2m";
constexpr const char* kBrightWhite = "\x1b[97m";

// 256-colour palette numbers.
constexpr int kBlack = 16;
constexpr int kOrange = 208;    // the accent colour
constexpr int kGrey = 244;
constexpr int kWhite = 255;     // toast and cursor background
constexpr int kFrame = 240;     // device frame
constexpr int kOldLine = 246;   // floor lines older than 20 s

std::string fg(int colour) { return "\x1b[38;5;" + std::to_string(colour) + "m"; }
std::string bg(int colour) { return "\x1b[48;5;" + std::to_string(colour) + "m"; }

// Text styles on the device screen. During a burst the screen is orange and
// all text is black.
struct Styles {
  std::string paper, grey, accent;
};

Styles styles_for(const Screen& s) {
  const std::string base = (s.burst_on ? bg(kOrange) : bg(kBlack)) + (s.dim ? kFaint : "");
  if (s.burst_on) return {base + fg(kBlack), base + fg(kBlack), base + fg(kBlack)};
  return {base + kBrightWhite, base + fg(kGrey), base + fg(kOrange)};
}

// --- Text layout -------------------------------------------------------------

constexpr int kScreenChars = 40;  // device screen width in characters
constexpr int kDeviceWidth = 44;  // device column with the frame and margins
constexpr int kMinFloorWidth = 40;

enum class Align { Left, Center, Right };

// Cuts or pads `s` to exactly `width` characters.
std::string pad(const std::string& s, int width, Align align) {
  const std::string t = s.substr(0, static_cast<size_t>(width));
  const int space = width - static_cast<int>(t.size());
  const int left = align == Align::Left ? 0 : align == Align::Center ? space / 2 : space;
  return std::string(static_cast<size_t>(left), ' ') + t + std::string(static_cast<size_t>(space - left), ' ');
}

std::string repeat(const char* s, int n) {
  std::string out;
  for (int i = 0; i < n; ++i) out += s;
  return out;
}

// Width of column `i` of `count` equal columns; the last one takes the rest.
int column_width(int total, size_t count, size_t i) {
  const int w = total / static_cast<int>(count);
  return i + 1 == count ? total - w * static_cast<int>(count - 1) : w;
}

// Word wrap; continuation lines are indented by `indent` spaces.
std::vector<std::string> wrap(const std::string& text, int width, int indent) {
  if (width < 12) width = 12;
  std::vector<std::string> out;
  std::string line, word;
  auto flush_word = [&]() {
    if (word.empty()) return;
    if (!line.empty() && static_cast<int>(line.size() + 1 + word.size()) > width) {
      out.push_back(line);
      line = std::string(static_cast<size_t>(indent), ' ');
    } else if (!line.empty() && line.back() != ' ') {
      line += ' ';
    }
    line += word;
    word.clear();
  };
  for (char c : text) {
    if (c == ' ') flush_word();
    else word += c;
  }
  flush_word();
  if (!line.empty()) out.push_back(line);
  return out;
}

// --- Device screen -----------------------------------------------------------

std::string framed(const std::string& content) {
  const std::string side = fg(kFrame) + "│" + kReset;
  return side + content + kReset + side;
}

std::string text_row(const std::string& style, const std::string& text, Align align) {
  return framed(style + " " + pad(text, kScreenChars - 2, align) + " ");
}

// "12 MIN ████████░░░░░░"
std::string minutes_row(const Screen& s, const Styles& st) {
  constexpr int kLabel = 8;
  const int bar = kScreenChars - 2 - kLabel;
  std::string line = st.grey + " " + pad(s.minutes, kLabel, Align::Left);
  if (s.progress > 0) {
    const float p = s.progress > 1 ? 1 : s.progress;
    const int fill = static_cast<int>(p * bar + 0.5f);
    line += s.progress >= 1 ? st.accent : st.paper;
    for (int i = 0; i < bar; ++i) line += i < fill ? "█" : "░";
  } else {
    line += std::string(static_cast<size_t>(bar), ' ');
  }
  return framed(line + st.grey + " ");
}

// Every table in number order, cursor on the selected one.
std::string strip_row(const Screen& s, const Styles& st) {
  if (s.strip_count == 0) return framed(st.grey + std::string(kScreenChars, ' '));
  const std::string cursor_text = fg(kBlack) + kBold;
  std::string line;
  for (size_t i = 0; i < s.strip_count; ++i) {
    const Screen::Cell& c = s.strip[i];
    std::string style = c.accent ? st.accent : st.grey;
    if (c.selected) style = (c.high ? bg(kOrange) : bg(kWhite)) + cursor_text;
    line += style + pad(std::to_string(c.number), column_width(kScreenChars, s.strip_count, i), Align::Center) + kReset;
  }
  return framed(line);
}

// Key labels, one column per key. In 2-key mode the device draws the first
// label at the left edge and the long hold label at the right edge; the
// simulator does the same, so the hold label gets the widest column.
std::string keys_row(const Screen& s, const Styles& st) {
  std::string line;
  if (s.key_count == 3) {
    const int widths[] = {11, 10, kScreenChars - 23};  // plus a 1-char margin on each side
    const Align aligns[] = {Align::Left, Align::Center, Align::Right};
    line = " ";
    for (size_t i = 0; i < 3; ++i)
      line += (s.key_accent[i] ? st.accent : st.grey) + pad(s.keys[i], widths[i], aligns[i]);
    return framed(line + " ");
  }
  for (size_t i = 0; i < s.key_count; ++i)
    line += (s.key_accent[i] ? st.accent : st.grey) +
            pad(s.keys[i], column_width(kScreenChars, s.key_count, i), Align::Center);
  return framed(line);
}

// --- Whole frame -------------------------------------------------------------

std::vector<std::string> device_column(const Screen& s, KeyMode mode) {
  std::vector<std::string> out;
  for (const std::string& row : device_rows(s)) out.push_back(" " + row);
  out.push_back(mode == KeyMode::FourKeys ? "  a STEP   s NEXT   d ACK   f TALK" : "  a STEP   s NEXT");
  out.push_back("  Shift + key = hold   ? = not sure");
  out.push_back("  space pause   + - speed   q end");
  return out;
}

std::vector<std::string> intro_lines(int width) {
  const char* plain[] = {
      "You are the server for tables 1 to 8.",
      "Things that happen on the floor appear here.",
      "Keep the device up to date with its keys.",
  };
  std::vector<std::string> out;
  for (const char* t : plain)
    for (const std::string& l : wrap(t, width, 0)) out.push_back(l);
  // The bold line carries 8 characters of escape codes.
  const std::string start = std::string(kBold) + "Press space to start the shift." + kReset;
  for (const std::string& l : wrap(start, width + 8, 0)) out.push_back(l);
  return out;
}

// Floor lines, oldest first. Lines of the last 20 s are bold.
std::vector<std::string> floor_log_lines(const std::vector<FloorLine>& floor, uint32_t now, int width) {
  std::vector<std::string> out;
  for (const FloorLine& f : floor) {
    const bool fresh = now - f.at < 20 * kSec;
    const std::string style = fresh ? kBold : fg(kOldLine);
    for (const std::string& l : wrap(clock_text(f.at) + "  " + f.text, width, 7)) out.push_back(style + l + kReset);
  }
  return out;
}

// Text placed at screen positions, written row by row: each row is cleared
// and drawn in one go, so the screen does not flicker.
class Frame {
 public:
  explicit Frame(int rows) : rows_(rows), cells_(static_cast<size_t>(rows) + 1) {}

  void put(int row, int col, const std::string& text) {
    if (row >= 1 && row <= rows_) cells_[static_cast<size_t>(row)].push_back(Cell{col, text});
  }

  void flush() const {
    std::string out;
    for (int r = 1; r <= rows_; ++r) {
      out += move_to(r, 1) + "\x1b[2K";
      for (const Cell& c : cells_[static_cast<size_t>(r)]) out += move_to(r, c.col) + c.text + kReset;
    }
    fputs(out.c_str(), stdout);
    fflush(stdout);
  }

 private:
  struct Cell {
    int col;
    std::string text;
  };
  static std::string move_to(int row, int col) {
    return "\x1b[" + std::to_string(row) + ";" + std::to_string(col) + "H";
  }
  int rows_;
  std::vector<std::vector<Cell>> cells_;
};

}  // namespace

std::vector<std::string> device_rows(const Screen& s) {
  const Styles st = styles_for(s);
  const std::string edge = fg(kFrame);
  const std::string bar = repeat("─", kScreenChars);
  const std::string toast = bg(kWhite) + fg(kBlack) + kBold;

  std::vector<std::string> r;
  r.push_back(edge + "┌" + bar + "┐" + kReset);
  r.push_back(text_row(st.accent, s.status, Align::Right));
  r.push_back(text_row((s.table_accent ? st.accent : st.paper) + kBold, s.table, Align::Left));
  if (s.line2_toast) r.push_back(text_row(toast, s.line2, Align::Left));
  else r.push_back(text_row((s.line2_accent ? st.accent : st.paper) + kBold, s.line2, Align::Left));
  r.push_back(minutes_row(s, st));
  r.push_back(text_row(st.grey, "", Align::Left));
  r.push_back(strip_row(s, st));
  r.push_back(framed(st.grey + bar));
  r.push_back(keys_row(s, st));
  r.push_back(edge + "└" + bar + "┘" + kReset);
  return r;
}

void draw_game(const Screen& screen, const std::vector<FloorLine>& floor, const GameState& game) {
  int cols, rows;
  terminal_size(cols, rows);

  const std::vector<std::string> device = device_column(screen, game.mode);

  // The floor log goes beside the device when the terminal is wide, else below it.
  const bool side = cols >= kDeviceWidth + 2 + kMinFloorWidth;
  const int floor_col = side ? kDeviceWidth + 3 : 1;
  const int floor_w = (side ? cols - floor_col : cols) - 2;
  const int head_row = side ? 1 : static_cast<int>(device.size()) + 2;

  char head[96];
  snprintf(head, sizeof head, "FLOOR  %s / %s  x%d%s", clock_text(game.now).c_str(),
           clock_text(shift_time(kShiftSeconds)).c_str(), game.speed, game.paused ? "  PAUSED" : "");

  const std::vector<std::string> lines =
      game.started ? floor_log_lines(floor, game.now, floor_w) : intro_lines(floor_w);
  // Show the newest lines that fit.
  int space = rows - head_row - 1;
  if (space < 3) space = 3;
  const size_t first = lines.size() > static_cast<size_t>(space) ? lines.size() - space : 0;

  Frame frame(rows);
  const int device_row = side ? 2 : 1;
  for (size_t i = 0; i < device.size(); ++i) frame.put(device_row + static_cast<int>(i), 1, device[i]);
  frame.put(head_row, floor_col, std::string(kBold) + head);
  for (size_t i = first; i < lines.size(); ++i) frame.put(head_row + 1 + static_cast<int>(i - first), floor_col, lines[i]);
  frame.flush();
}

}  // namespace sim
