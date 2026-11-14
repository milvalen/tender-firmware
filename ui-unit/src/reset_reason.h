// Why the chip started: the ESP-IDF reset reason as a shift log code.
#pragma once

#include "shiftlog.h"

namespace unit {

tender::shiftlog::Reset reset_code();
inline const char* reset_name() { return tender::shiftlog::reset_name(reset_code()); }

}  // namespace unit
