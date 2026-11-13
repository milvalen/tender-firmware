#include "floor.h"

#include "common.h"

namespace sim {

using tender::Stage;

namespace {
constexpr int kCheckBroughtAfterS = 60;   // the server brings the check this long after the request
constexpr int kTellCheckIfPayAfterS = 180;  // the floor says so only before a slow payment
constexpr int kKitchenFixAfterS = 25;     // the kitchen corrects its mix-up after this time
}  // namespace

Floor::Floor(const tender::Board& board, Referee& referee, EventLog& log)
    : board_(board), referee_(referee), log_(log) {
  for (const Party& p : kShift) {
    PartyProgress s;
    s.plan = &p;
    parties_.push_back(s);
  }
}

void Floor::advance(uint32_t now) {
  for (PartyProgress& s : parties_) {
    switch (s.phase) {
      case Phase::Away: away(s, now); break;
      case Phase::Seated: seated(s, now); break;
      case Phase::Ordered: ordered(s, now); break;
      case Phase::Mains: mains(s, now); break;
      case Phase::Check: check(s, now); break;
      case Phase::Done: break;
    }
  }
}

bool Floor::order_is_yours(uint8_t table) const {
  const PartyProgress* s = party(table);
  return s && s->phase == Phase::Seated && s->plan->order_after_s == kRemind;
}

bool Floor::check_brought(uint8_t table) const {
  const PartyProgress* s = party(table);
  return s && s->phase == Phase::Check && s->check_brought;
}

bool Floor::guests_at(uint8_t table) const {
  const PartyProgress* s = party(table);
  return s && s->phase != Phase::Done;
}

const Floor::PartyProgress* Floor::party(uint8_t table) const {
  for (const PartyProgress& s : parties_)
    if (s.plan->table == table && s.phase != Phase::Away) return &s;
  return nullptr;
}

// --- Phases ------------------------------------------------------------------

void Floor::away(PartyProgress& s, uint32_t now) {
  const Party& p = *s.plan;
  if (now < shift_time(p.arrive_s)) return;
  say(now, "Guests sit down at " + table_name(p.table) + ".");
  referee_.expect(now, p.table, Want::Stage, Stage::Seat, "seat guests");
  s.phase = Phase::Seated;
  s.seated_at = now;
}

void Floor::seated(PartyProgress& s, uint32_t now) {
  const Party& p = *s.plan;
  if (p.order_after_s == kRemind) {
    // Nobody tells the server: the order is taken when the device shows it.
    const tender::Table* t = find_table(board_, p.table);
    if (t && t->stage == Stage::Ord) take_order(s, now);
    return;
  }
  if (now < after(s.seated_at, p.order_after_s)) return;
  take_order(s, now);
  referee_.expect(now, p.table, Want::Stage, Stage::Ord, "order taken");
}

void Floor::ordered(PartyProgress& s, uint32_t now) {
  const Party& p = *s.plan;
  const std::string t = table_name(p.table);
  if (now < after(s.ordered_at, p.kitchen_s)) return;
  if (p.decoy_table && !s.decoy_called) {
    s.decoy_called = true;
    if (call_wrong_table(s, now)) return;
  }
  if (s.decoy_fix_at) {
    if (now < s.decoy_fix_at) return;
    say(now, "Kitchen: sorry, those mains were for " + t + ", not " + table_name(p.decoy_table) +
                 ". You take them back and serve " + t + ".");
    referee_.drop(s.decoy_mains, now);
    referee_.expect(now, p.decoy_table, Want::Stage, Stage::Ord, "mix-up corrected");
  } else {
    say(now, "Kitchen calls: mains for " + t + ". You serve them.");
  }
  referee_.expect(now, p.table, Want::Stage, Stage::Main, "mains served");
  s.phase = Phase::Mains;
  s.mains_at = now;
}

void Floor::mains(PartyProgress& s, uint32_t now) {
  const Party& p = *s.plan;
  const std::string t = table_name(p.table);
  for (int i = 0; i < p.request_count; ++i) guest_request(s, i, now);
  if (p.dessert_after_s != kNever && !s.dessert_ordered && now >= after(s.mains_at, p.dessert_after_s)) {
    say(now, "Guests at " + t + " order dessert.");
    s.dessert_ordered = true;  // nothing to record: the table stays in MAIN
  }
  if (now < after(s.mains_at, p.check_after_s)) return;
  say(now, "Guests at " + t + " ask for the check.");
  referee_.expect(now, p.table, Want::Stage, Stage::Bill, "check requested");
  s.phase = Phase::Check;
  s.check_at = now;
}

void Floor::check(PartyProgress& s, uint32_t now) {
  const Party& p = *s.plan;
  const std::string t = table_name(p.table);
  if (!s.check_brought && now >= after(s.check_at, kCheckBroughtAfterS)) {
    s.check_brought = true;
    if (p.pay_after_s > kTellCheckIfPayAfterS) say(now, "You bring the check to " + t + ".");
  }
  if (now < after(s.check_at, p.pay_after_s)) return;
  say(now, "Guests at " + t + " pay and leave.");
  referee_.expect(now, p.table, Want::Stage, Stage::Free, "table cleared");
  s.phase = Phase::Done;
}

// --- Steps -------------------------------------------------------------------

void Floor::take_order(PartyProgress& s, uint32_t now) {
  say(now, "You take the order at " + table_name(s.plan->table) + ".");
  s.phase = Phase::Ordered;
  s.ordered_at = now;
}

// The kitchen calls the mains for the wrong table. This only happens if that
// table waits for its mains too. Returns true if the call was made.
bool Floor::call_wrong_table(PartyProgress& s, uint32_t now) {
  const uint8_t wrong = s.plan->decoy_table;
  const tender::Table* t = find_table(board_, wrong);
  if (!t || t->stage != Stage::Ord) return false;
  say(now, "Kitchen calls: mains for " + table_name(wrong) + ". You serve them.");
  s.decoy_mains = referee_.expect(now, wrong, Want::Stage, Stage::Main, "mains served (kitchen mistake)");
  s.decoy_fix_at = after(now, kKitchenFixAfterS);
  return true;
}

void Floor::guest_request(PartyProgress& s, int i, uint32_t now) {
  const Party& p = *s.plan;
  const GuestRequest& r = p.requests[i];
  RequestProgress& q = s.requests[i];
  const std::string t = table_name(p.table);
  if (!q.asked) {
    if (now < after(s.mains_at, r.after_mains_s)) return;
    say(now, "A guest at " + t + " " + r.ask + ".");
    q.opened = referee_.expect(now, p.table, Want::RequestOpen, Stage::Free, "request opened");
    q.asked = true;
    q.asked_at = now;
    return;
  }
  if (q.done) return;
  if (r.done_after_s == kRemind) {
    // Nobody tells the server: the request is done when the device closes it.
    const tender::Table* dev = find_table(board_, p.table);
    if (!referee_.met(q.opened) || !dev || dev->request_open) return;
    say(now, std::string("You bring ") + r.item + " to " + t + ".");
    q.done = true;
    return;
  }
  if (now < after(q.asked_at, r.done_after_s)) return;
  say(now, std::string("You bring ") + r.item + " to " + t + ".");
  referee_.expect(now, p.table, Want::RequestClosed, Stage::Free, "request closed");
  q.done = true;
}

void Floor::say(uint32_t now, const std::string& text) {
  lines_.push_back(FloorLine{now, text});
  log_.add(now, "FLOOR %s", text.c_str());
}

}  // namespace sim
