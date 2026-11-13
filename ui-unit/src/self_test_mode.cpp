#include "self_test_mode.h"

#include "config.h"
#include "display.h"
#include "reset_reason.h"
#include "version.h"

namespace unit {

namespace {
constexpr uint32_t kPowerReadMs = 500;
constexpr uint32_t kDrawMs = 100;

bool high(int pin) { return digitalRead(pin) == HIGH; }
}  // namespace

SelfTestMode::SelfTestMode(M5Canvas& canvas, Vibro& vibro, FlashLog& log)
    : test_(kRedPin, kBluePin, kDualActiveHigh), canvas_(canvas), vibro_(vibro), log_(log) {}

void SelfTestMode::loop(uint32_t now) {
  if (!started_) start(now);
  started_ = true;

  read_console(now);
  if (test_.take_clear_log()) log_.clear();
  if (test_.exit_requested()) {
    vibro_.set(0);
    Serial.flush();
    ESP.restart();
  }
  if (test_.setup_version() != setup_version_) apply_setup();

  read_inputs();
  read_power(now);
  vibro_.set(test_.vibro_duty(now));

  if (now - last_draw_ >= kDrawMs) {
    tender::SelfTest::Lines lines;
    test_.lines(now, lines);
    draw_self_test(canvas_, lines);
    last_draw_ = now;
  }
}

void SelfTestMode::start(uint32_t now) {
  vibro_.attach(-1);            // the core attaches the motor pin it tests
  M5.Power.setExtOutput(true);  // 5 V for the Dual Button unit and the motor
  M5.Display.setBrightness(kBright);
  apply_setup();
  delay(5);  // let the pulls settle before the "not pressed" levels are read
  test_.begin(now, high(kRedPin), high(kBluePin), reset_name());

  if (kVibroPin >= 0) {  // start with the motor pin of this build
    snprintf(line_, sizeof line_, "vibro %d", kVibroPin);
    test_.command(line_, now, out_, sizeof out_);
  }
  Serial.printf("TND-01 self-test, firmware %s%s. Type help and press Enter.\n", tender::kFirmwareCommit,
                tender::kFirmwareDirtyMark);
}

void SelfTestMode::apply_setup() {
  const uint8_t mode = test_.pull() == tender::Pull::Up ? INPUT_PULLUP : INPUT_PULLDOWN;
  pinMode(kRedPin, mode);
  pinMode(kBluePin, mode);
  vibro_.attach(test_.vibro_pin());
  M5.Display.setRotation(test_.rotation());
  setup_version_ = test_.setup_version();
}

// Collects typed characters; Enter sends the line to the core.
void SelfTestMode::read_console(uint32_t now) {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      line_[line_len_] = '\0';
      line_len_ = 0;
      test_.command(line_, now, out_, sizeof out_);
      print(out_);
    } else if (line_len_ + 1 < sizeof line_) {
      line_[line_len_++] = c;
    }
  }
}

void SelfTestMode::read_inputs() {
  test_.keys(M5.BtnA.isPressed(), M5.BtnB.isPressed());
  test_.dual_levels(high(kRedPin), high(kBluePin));
  if (test_.take_event(out_, sizeof out_)) print(out_);
}

void SelfTestMode::read_power(uint32_t now) {
  if (now - last_power_ >= kPowerReadMs) {
    tender::PowerInfo p;
    p.level = M5.Power.getBatteryLevel();
    p.battery_mv = M5.Power.getBatteryVoltage();
    p.vbus_mv = M5.Power.getVBUSVoltage();
    p.charging = M5.Power.isCharging();
    p.current_ma = M5.Power.getBatteryCurrent();
    test_.set_power(p);
    last_power_ = now;
  }
  if (test_.log_due(now)) {
    test_.power_line(now, out_, sizeof out_);
    print(out_);
  }
}

void SelfTestMode::print(const char* text) {
  if (text[0]) Serial.print(text);
}

}  // namespace unit
