// Interface logic of the TND-01 interface unit, shared by the device and the
// desktop simulator: key mapping, alert bursts, confirmations, dimming and
// the content of every screen line. Hardware code only draws a Screen and
// feeds key presses in.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "tableboard.h"

namespace tender {

enum class KeyMode : uint8_t { TwoKeys, FourKeys };
// Physical keys: A (front), B (side), red and blue (Dual Button unit).
enum class Key : uint8_t { A, B, Red, Blue };
enum class Press : uint8_t { Short, Long };
// Logical actions, for logs and the simulator score.
enum class Action : uint8_t { None, Step, Undo, Next, Prev, Ack, Mute, Request };

const char* action_name(Action a);

struct Timing {
  uint32_t burst_ms = 2000;
  uint32_t burst_blink_ms = 250;
  uint32_t toast_ms = 1200;
  uint32_t dim_after_ms = 20000;
  uint32_t idle_resort_ms = 10000;
};

struct Screen {
  // One cell of the table strip: all tables in number order, always in the
  // same place, with a cursor on the selected one (desk test D5).
  struct Cell {
    uint8_t number;
    bool selected;
    bool accent;  // the table needs an action (low or high)
    bool high;    // the guest waits
  };
  char status[28];      // top right: "MUTED", "LOW BATTERY", or empty
  char table[8];        // "TBL-07"
  bool table_accent;    // high alert on the selected table
  char line2[28];       // action, toast or last status
  bool line2_accent;
  bool line2_toast;     // confirmation: drawn inverted
  char minutes[12];     // "12 MIN" or "FREE"
  float progress;       // 0 = no bar
  Cell strip[Board::kMaxTables];
  size_t strip_count;
  char keys[4][16];     // bottom labels by function, in the order STEP, NEXT, ACK, REQ
  bool key_accent[4];
  size_t key_count;
  bool burst_on;        // whole screen in accent colour this frame
  bool dim;             // backlight low
};

class App {
 public:
  App(Board& board, KeyMode mode, Timing timing = Timing());

  // Feed a key press. Returns the logical action that ran.
  Action press(Key key, Press kind, uint32_t now);
  // Run once per loop. Returns true when a new high alert starts.
  bool tick(uint32_t now);
  // Battery level 0..100, or -1 if unknown.
  void set_battery(int level) { battery_ = level; }

  void screen(uint32_t now, Screen& out) const;
  bool vibrate(uint32_t now) const;  // vibration motor on this frame
  bool busy(uint32_t now) const;     // needs fast redraws (burst or toast)
  bool idle(uint32_t now) const { return now - last_input_ >= timing_.dim_after_ms; }

  Board& board() { return board_; }
  KeyMode mode() const { return mode_; }

 private:
  Action map(Key key, Press kind, uint32_t now) const;
  void run(Action a, uint32_t now);
  void toast(const char* text, uint32_t now);
  void toast_table(const char* what, uint32_t now);
  bool blink_on(uint32_t now) const;

  Board& board_;
  KeyMode mode_;
  Timing timing_;
  uint32_t last_input_ = 0;
  uint32_t last_idle_resort_ = 0;
  uint32_t burst_until_ = 0;
  uint32_t toast_until_ = 0;
  char toast_[28] = "";
  int battery_ = -1;
};

}  // namespace tender
