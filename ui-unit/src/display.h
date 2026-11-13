// Draws the screens on the StickS3 LCD (240 x 135 in landscape). What to show
// comes from the core (app.h, selftest.h); this file only places it.
#pragma once

#include <M5Unified.h>

#include "app.h"
#include "selftest.h"

namespace unit {

void draw_board(M5Canvas& canvas, const tender::Screen& s);
void draw_self_test(M5Canvas& canvas, const tender::SelfTest::Lines& lines);

}  // namespace unit
