// Bots for --autoplay. They check that the shift script and the score work:
// the good player must pass, the sloppy player must fail.
#pragma once

#include <stdint.h>

#include <optional>
#include <vector>

#include "app.h"
#include "session.h"

namespace sim {

struct KeyPress {
  tender::Key key;
  tender::Press kind;
};

class Bot {
 public:
  enum class Style : uint8_t {
    Good,    // reacts in 4 s: goes to the right table, handles alerts
    Sloppy,  // reacts in 25 s, acts once on whatever table is selected, ignores alerts
  };

  Bot(Style style, const Session& session, tender::KeyMode mode) : style_(style), session_(session), mode_(mode) {}

  // The key press for this moment, if any.
  std::optional<KeyPress> next(uint32_t now);

 private:
  // The oldest open thing to do.
  struct Task {
    uint8_t table = 0;  // 0 = nothing to do
    uint32_t since = 0;
    int expectation = -1;  // index in the referee's expectations; -1 = an alert
  };

  Task oldest_task(uint32_t now) const;
  KeyPress sloppy_press(const Task& task);
  KeyPress good_press(const Task& task) const;
  KeyPress go_to(uint8_t table) const;
  KeyPress request_key() const;
  KeyPress ack_key() const;

  Style style_;
  const Session& session_;
  tender::KeyMode mode_;
  uint32_t next_press_at_ = 0;
  std::vector<bool> acted_;  // sloppy: the expectations it already acted on
};

}  // namespace sim
