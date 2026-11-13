// Terminal drawing for the interactive game: the device screen on the left,
// the floor log beside it (wide terminal) or below it (narrow terminal).
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "app.h"
#include "floor.h"

namespace sim {

struct GameState {
  uint32_t now;
  int speed;
  bool paused;
  bool started;  // false: show the intro
  tender::KeyMode mode;
  bool vibrate;  // the vibration motor runs this frame (also in MUTED)
};

// The device screen as terminal rows, with a frame around it.
std::vector<std::string> device_rows(const tender::Screen& s);

// Draws one whole frame of the game.
void draw_game(const tender::Screen& screen, const std::vector<FloorLine>& floor, const GameState& game);

}  // namespace sim
