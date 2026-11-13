// TND-01 desk test: a terminal screen simulator that runs the same table
// board and interface logic as the device (lib/tableboard), plays a scripted
// shift and scores the player.
//
//   make sim                      4-key mode (StickS3 + Dual Button unit)
//   make sim ARGS=--two-keys      2-key mode (StickS3 alone)
//   build/tnd01-sim --autoplay good|sloppy [--two-keys] [--verbose]
//
// Keys: a s d f = STEP NEXT ACK TALK (tap), Shift + key = hold.
//
// Parts:
//   shift.h     the shift script: parties and their timelines
//   floor.*     plays the script and tells the player what happens
//   referee.*   what the device should show, alerts and mistakes
//   score.*     the score and the report
//   session.h   device logic, floor and referee wired together
//   bot.*       autoplay players
//   view.*      terminal drawing
//   terminal.*  raw terminal input
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <chrono>
#include <string>

#include "bot.h"
#include "common.h"
#include "score.h"
#include "session.h"
#include "terminal.h"
#include "view.h"

using namespace sim;
using tender::Key;
using tender::KeyMode;
using tender::Press;

namespace {

constexpr int kMinSpeed = 1;
constexpr int kMaxSpeed = 16;
constexpr uint32_t kAutoplayStepMs = 100;
constexpr useconds_t kFrameUs = 40000;

struct Options {
  KeyMode mode = KeyMode::FourKeys;
  const char* autoplay = nullptr;  // "good" or "sloppy"; nullptr = interactive
  bool expect_fail = false;
  bool verbose = false;
  int speed = 4;
  std::string log_path;
};

void usage() {
  fputs(
      "usage: tnd01-sim [--two-keys] [--speed N] [--log FILE]\n"
      "       tnd01-sim --autoplay good|sloppy [--two-keys] [--verbose] [--expect-fail]\n",
      stderr);
}

bool parse(int argc, char** argv, Options& o) {
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const bool has_value = i + 1 < argc;
    if (a == "--two-keys") o.mode = KeyMode::TwoKeys;
    else if (a == "--autoplay" && has_value) o.autoplay = argv[++i];
    else if (a == "--expect-fail") o.expect_fail = true;
    else if (a == "--verbose") o.verbose = true;
    else if (a == "--speed" && has_value) o.speed = atoi(argv[++i]);
    else if (a == "--log" && has_value) o.log_path = argv[++i];
    else return false;
  }
  if (o.autoplay && strcmp(o.autoplay, "good") != 0 && strcmp(o.autoplay, "sloppy") != 0) return false;
  if (o.speed < kMinSpeed || o.speed > kMaxSpeed) o.speed = 4;
  return true;
}

// --- Autoplay ----------------------------------------------------------------

int autoplay(const Options& o) {
  const bool good = strcmp(o.autoplay, "good") == 0;
  Session session(o.mode);
  Bot bot(good ? Bot::Style::Good : Bot::Style::Sloppy, session, o.mode);
  const uint32_t end = shift_time(kShiftSeconds);
  for (uint32_t now = kShiftStart; now <= end; now += kAutoplayStepMs) {
    if (std::optional<KeyPress> p = bot.next(now)) session.press(p->key, p->kind, now);
    session.tick(now);
  }
  if (o.verbose)
    for (const std::string& l : session.log().lines()) puts(l.c_str());

  const Score s = score(session.referee());
  print_report(stdout, s, o.mode, good ? "autoplay: good player" : "autoplay: sloppy player");
  const bool ok = o.expect_fail ? !s.pass : s.pass;
  if (!ok) fprintf(stderr, "autoplay %s: unexpected result\n", o.autoplay);
  return ok ? 0 : 1;
}

// --- Interactive game ----------------------------------------------------------

// Pause, speed and quit keys work at any time. Device keys and '?' work only
// while the shift runs: lowercase a s d f = tap, uppercase = hold.
void handle_key(char c, Session& session, GameState& game) {
  if (c == 'q' || c == 'Q') {
    request_quit();
  } else if (c == ' ') {
    game.paused = !game.paused;
    game.started = true;
  } else if (c == '+' || c == '=') {
    game.speed = game.speed >= kMaxSpeed ? kMaxSpeed : game.speed * 2;
  } else if (c == '-' || c == '_') {
    game.speed = game.speed <= kMinSpeed ? kMinSpeed : game.speed / 2;
  } else if (game.paused) {
    return;
  } else if (c == '?') {
    session.question(game.now);
  } else {
    static const char kDeviceKeys[] = "asdf";  // index = Key
    const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    const char* hit = letter ? strchr(kDeviceKeys, c | 0x20) : nullptr;  // c | 0x20 = lowercase
    if (!hit) return;
    const bool upper = c >= 'A' && c <= 'Z';
    session.press(static_cast<Key>(hit - kDeviceKeys), upper ? Press::Long : Press::Short, game.now);
  }
}

// Shift time runs at `speed` times wall time.
class ShiftClock {
 public:
  explicit ShiftClock(uint32_t start) : now_(start), last_(std::chrono::steady_clock::now()) {}

  uint32_t advance(bool paused, int speed) {
    const auto t = std::chrono::steady_clock::now();
    const double wall_ms = std::chrono::duration<double, std::milli>(t - last_).count();
    last_ = t;
    if (!paused) {
      carry_ms_ += wall_ms * speed;
      const uint32_t whole = static_cast<uint32_t>(carry_ms_);
      carry_ms_ -= whole;
      now_ += whole;
    }
    return now_;
  }

 private:
  uint32_t now_;
  double carry_ms_ = 0;
  std::chrono::steady_clock::time_point last_;
};

std::string default_log_path() {
  char stamp[32];
  const time_t t = time(nullptr);
  strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&t));
  return std::string("build/desk-test-") + stamp + ".log";
}

void write_results(const Session& session, const Options& o, uint32_t now) {
  const Score s = score(session.referee());
  const bool full = now >= shift_time(kShiftSeconds);
  const std::string who = full ? "full shift" : "stopped at " + clock_text(now);
  print_report(stdout, s, o.mode, who.c_str());

  const std::string path = o.log_path.empty() ? default_log_path() : o.log_path;
  FILE* f = fopen(path.c_str(), "w");
  if (!f) return;
  print_report(f, s, o.mode, who.c_str());
  fputs("\n", f);
  for (const std::string& l : session.log().lines()) fprintf(f, "%s\n", l.c_str());
  fclose(f);
  printf("  log              %s\n", path.c_str());
}

int interactive(const Options& o) {
  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
    fputs("tnd01-sim: needs a terminal (or use --autoplay)\n", stderr);
    return 2;
  }
  Session session(o.mode);
  GameState game{kShiftStart, o.speed, /*paused=*/true, /*started=*/false, o.mode, /*vibrate=*/false};
  ShiftClock clock(kShiftStart);
  const uint32_t end = shift_time(kShiftSeconds);
  {
    RawTerminal terminal;
    while (!quit_requested() && game.now < end) {
      char keys[32];
      const size_t n = terminal.read_keys(keys, sizeof keys);
      for (size_t i = 0; i < n; ++i) handle_key(keys[i], session, game);
      game.now = clock.advance(game.paused, game.speed);
      if (session.tick(game.now)) fputs("\a", stdout);  // bell on a high alert
      game.vibrate = session.app().vibrate(game.now);
      tender::Screen screen;
      session.app().screen(game.now, screen);
      draw_game(screen, session.floor().lines(), game);
      usleep(kFrameUs);
    }
  }
  write_results(session, o, game.now);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Options o;
  if (!parse(argc, argv, o)) {
    usage();
    return 2;
  }
  return o.autoplay ? autoplay(o) : interactive(o);
}
