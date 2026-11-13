// Raw terminal for the interactive game: no echo, no line buffer, an
// alternate screen and a hidden cursor. The terminal is restored on exit,
// on Ctrl+C and when the object goes out of scope.
#pragma once

#include <stddef.h>

namespace sim {

class RawTerminal {
 public:
  RawTerminal();
  ~RawTerminal();
  RawTerminal(const RawTerminal&) = delete;
  RawTerminal& operator=(const RawTerminal&) = delete;

  // Reads the keys typed since the last call, without waiting.
  size_t read_keys(char* buf, size_t size);
  // Restores the terminal now; safe to call more than once.
  static void restore();
};

// True after Ctrl+C or a stop request.
bool quit_requested();
void request_quit();

// Terminal size; 100 x 30 if unknown.
void terminal_size(int& cols, int& rows);

}  // namespace sim
