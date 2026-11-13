#include "terminal.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace sim {

namespace {

struct termios saved_tty;
bool tty_raw = false;
volatile sig_atomic_t quit_flag = 0;

void on_signal(int) { quit_flag = 1; }

// Escape sequences.
constexpr const char* kAltScreenOn = "\x1b[?1049h";
constexpr const char* kAltScreenOff = "\x1b[?1049l";
constexpr const char* kCursorHide = "\x1b[?25l";
constexpr const char* kCursorShow = "\x1b[?25h";
constexpr const char* kClear = "\x1b[2J";
constexpr const char* kReset = "\x1b[0m";

}  // namespace

RawTerminal::RawTerminal() {
  tcgetattr(STDIN_FILENO, &saved_tty);
  struct termios t = saved_tty;
  t.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
  t.c_cc[VMIN] = 0;  // read() returns at once
  t.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &t);
  tty_raw = true;
  atexit(restore);
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  fputs(kAltScreenOn, stdout);
  fputs(kCursorHide, stdout);
  fputs(kClear, stdout);
}

RawTerminal::~RawTerminal() { restore(); }

size_t RawTerminal::read_keys(char* buf, size_t size) {
  const ssize_t n = read(STDIN_FILENO, buf, size);
  return n > 0 ? static_cast<size_t>(n) : 0;
}

void RawTerminal::restore() {
  if (!tty_raw) return;
  tcsetattr(STDIN_FILENO, TCSANOW, &saved_tty);
  fputs(kReset, stdout);
  fputs(kCursorShow, stdout);
  fputs(kAltScreenOff, stdout);
  fflush(stdout);
  tty_raw = false;
}

bool quit_requested() { return quit_flag != 0; }
void request_quit() { quit_flag = 1; }

void terminal_size(int& cols, int& rows) {
  cols = 100;
  rows = 30;
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
    cols = w.ws_col;
    if (w.ws_row > 0) rows = w.ws_row;
  }
}

}  // namespace sim
