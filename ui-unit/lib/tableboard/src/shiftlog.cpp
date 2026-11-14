#include "shiftlog.h"

#include <stdio.h>

namespace tender {
namespace shiftlog {

namespace {

constexpr uint8_t kStageMask = 0x0f;
constexpr uint8_t kRequestBit = 0x10;
constexpr uint8_t kMutedBit = 0x20;
constexpr uint8_t kMaxStage = static_cast<uint8_t>(Stage::Pay);
constexpr uint8_t kMaxAction = static_cast<uint8_t>(Action::Request);
constexpr uint8_t kMaxNeed = static_cast<uint8_t>(Need::Request);
constexpr uint8_t kMaxReset = static_cast<uint8_t>(Reset::External);

uint8_t flags_of(Stage stage, bool request_open, bool muted) {
  return static_cast<uint8_t>((static_cast<uint8_t>(stage) & kStageMask) | (request_open ? kRequestBit : 0) |
                              (muted ? kMutedBit : 0));
}

}  // namespace

const char* reset_name(Reset r) {
  switch (r) {
    case Reset::Other: return "OTHER";
    case Reset::PowerOn: return "POWERON";
    case Reset::Software: return "SOFTWARE";
    case Reset::Panic: return "PANIC";
    case Reset::Watchdog: return "WATCHDOG";
    case Reset::Brownout: return "BROWNOUT";
    case Reset::DeepSleep: return "DEEPSLEEP";
    case Reset::External: return "EXTERNAL";
  }
  return "?";
}

const char* alert_text(Stage stage, Need need) {
  if (need == Need::Request) return "GUEST REQUEST";
  switch (stage) {
    case Stage::Seat: return "TAKE ORDER";
    case Stage::Ord: return "CHECK MAINS";
    case Stage::Main: return "OFFER DESSERT";
    case Stage::Bill: return "BRING CHECK";
    default: return "CHECK TABLE";
  }
}

Record from_event(const Event& e) {
  Record r{};
  r.ms = e.ms;
  r.table = e.table;
  r.flags = flags_of(e.stage, e.request_open, e.muted);
  if (e.type == Event::Type::Alert) {
    r.kind = static_cast<uint8_t>(Kind::Alert);
    r.code = static_cast<uint8_t>(e.need);
  } else {
    r.kind = static_cast<uint8_t>(Kind::Action);
    r.code = static_cast<uint8_t>(e.action);
  }
  return r;
}

Record boot(uint32_t ms, Reset reason) {
  Record r{};
  r.ms = ms;
  r.kind = static_cast<uint8_t>(Kind::Boot);
  r.code = static_cast<uint8_t>(reason);
  return r;
}

Record battery(uint32_t ms, int level) {
  Record r{};
  r.ms = ms;
  r.kind = static_cast<uint8_t>(Kind::Battery);
  r.code = static_cast<uint8_t>(level < 0 ? 255 : level > 100 ? 100 : level);
  return r;
}

Record firmware(uint32_t commit, bool dirty) {
  Record r{};
  r.ms = commit;
  r.kind = static_cast<uint8_t>(Kind::Firmware);
  r.code = dirty ? 1 : 0;
  return r;
}

bool Writer::line(const Record& r, char* out, size_t len) {
  const uint8_t stage_code = r.flags & kStageMask;
  const Stage stage = static_cast<Stage>(stage_code);
  const bool request = (r.flags & kRequestBit) != 0;
  const bool muted = (r.flags & kMutedBit) != 0;
  switch (static_cast<Kind>(r.kind)) {
    case Kind::Boot:
      if (r.code > kMaxReset) return false;
      ++boot_;
      boot_ms_ = r.ms;
      snprintf(out, len, "%d,%lu,BOOT,,,reset=%s\n", boot_, static_cast<unsigned long>(r.ms),
               reset_name(static_cast<Reset>(r.code)));
      return true;
    case Kind::Action:
      if (r.code == 0 || r.code > kMaxAction || stage_code > kMaxStage) return false;
      snprintf(out, len, "%d,%lu,%s,%u,%s,request=%d muted=%d\n", boot_, static_cast<unsigned long>(r.ms),
               action_name(static_cast<Action>(r.code)), r.table, stage_name(stage), request ? 1 : 0,
               muted ? 1 : 0);
      return true;
    case Kind::Alert:
      if (r.code == 0 || r.code > kMaxNeed || stage_code > kMaxStage) return false;
      snprintf(out, len, "%d,%lu,ALERT,%u,%s,%s\n", boot_, static_cast<unsigned long>(r.ms), r.table,
               stage_name(stage), alert_text(stage, static_cast<Need>(r.code)));
      return true;
    case Kind::Firmware:
      if (r.code > 1 || boot_ == 0) return false;
      if (r.ms == 0)
        snprintf(out, len, "%d,%lu,FIRMWARE,,,commit=unknown\n", boot_, static_cast<unsigned long>(boot_ms_));
      else
        snprintf(out, len, "%d,%lu,FIRMWARE,,,commit=%08lx%s\n", boot_, static_cast<unsigned long>(boot_ms_),
                 static_cast<unsigned long>(r.ms), r.code ? "+dirty" : "");
      return true;
    case Kind::Battery:
      if (r.code == 255) snprintf(out, len, "%d,%lu,BATTERY,,,?\n", boot_, static_cast<unsigned long>(r.ms));
      else snprintf(out, len, "%d,%lu,BATTERY,,,%u\n", boot_, static_cast<unsigned long>(r.ms), r.code);
      return true;
  }
  return false;
}

}  // namespace shiftlog
}  // namespace tender
