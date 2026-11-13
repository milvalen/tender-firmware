#include "bot.h"

#include "common.h"

namespace sim {

using tender::Key;
using tender::KeyMode;
using tender::Press;
using tender::Stage;

namespace {
constexpr uint32_t kGoodDelay = 4 * kSec;
constexpr uint32_t kSloppyDelay = 25 * kSec;
constexpr uint32_t kPressGap = 1500;  // between two presses
}  // namespace

std::optional<KeyPress> Bot::next(uint32_t now) {
  if (now < next_press_at_) return std::nullopt;
  const Task task = oldest_task(now);
  if (!task.table) return std::nullopt;
  next_press_at_ = now + kPressGap;
  return style_ == Style::Good ? good_press(task) : sloppy_press(task);
}

// Alerts come first when they are as old as an expectation. The sloppy bot
// does not see alerts and does not act twice on one expectation.
Bot::Task Bot::oldest_task(uint32_t now) const {
  const uint32_t delay = style_ == Style::Good ? kGoodDelay : kSloppyDelay;
  const Referee& referee = session_.referee();
  Task task;
  if (style_ == Style::Good) {
    for (const AlertRecord& a : referee.alerts()) {
      if (!a.open || now < a.since + delay) continue;
      if (!task.table || a.since < task.since) {
        task.table = a.table;
        task.since = a.since;
      }
    }
  }
  const std::vector<Expectation>& all = referee.expectations();
  for (size_t i = 0; i < all.size(); ++i) {
    const Expectation& e = all[i];
    if (!e.open || now < e.since + delay) continue;
    if (style_ == Style::Sloppy && i < acted_.size() && acted_[i]) continue;
    if (!task.table || e.since < task.since) {
      task.table = e.table;
      task.since = e.since;
      task.expectation = static_cast<int>(i);
    }
  }
  return task;
}

KeyPress Bot::sloppy_press(const Task& task) {
  const size_t i = static_cast<size_t>(task.expectation);
  if (acted_.size() <= i) acted_.resize(i + 1, false);
  acted_[i] = true;
  if (session_.referee().expectations()[i].want == Want::Stage) return {Key::A, Press::Short};
  return request_key();
}

KeyPress Bot::good_press(const Task& task) const {
  const tender::Table& selected = session_.board().selected();
  if (selected.number != task.table) return go_to(task.table);

  if (task.expectation < 0) {  // an alert
    if (selected.stage == Stage::Seat && session_.floor().order_is_yours(task.table))
      return {Key::A, Press::Short};  // take the order
    if (selected.request_open) return {Key::A, Press::Short};  // DONE closes the request
    return ack_key();
  }

  const Expectation& e = session_.referee().expectations()[static_cast<size_t>(task.expectation)];
  switch (e.want) {
    case Want::Stage:
      // One step too far (the kitchen mix-up): hold STEP to undo.
      if (stage_after(e.stage) == selected.stage && selected.can_undo) return {Key::A, Press::Long};
      return {Key::A, Press::Short};
    case Want::RequestClosed: return {Key::A, Press::Short};  // DONE
    case Want::RequestOpen: return request_key();
  }
  return {Key::A, Press::Short};
}

// Tables are in number order on a ring: go the short way round. Only the
// 4-key mode can go back (hold NEXT = PREV).
KeyPress Bot::go_to(uint8_t table) const {
  const tender::Board& board = session_.board();
  size_t target = 0;
  for (size_t i = 0; i < board.size(); ++i)
    if (board.at(i).number == table) target = i;
  const size_t n = board.size();
  const size_t forward = (target + n - board.selected_index()) % n;
  const bool back = mode_ == KeyMode::FourKeys && n - forward < forward;
  return {Key::B, back ? Press::Long : Press::Short};
}

KeyPress Bot::request_key() const {
  if (mode_ == KeyMode::FourKeys) return {Key::Blue, Press::Short};
  return {Key::B, Press::Long};
}

KeyPress Bot::ack_key() const {
  if (mode_ == KeyMode::FourKeys) return {Key::Red, Press::Short};
  return {Key::B, Press::Long};
}

}  // namespace sim
