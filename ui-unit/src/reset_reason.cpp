#include "reset_reason.h"

#include <esp_system.h>

namespace unit {

using tender::shiftlog::Reset;

Reset reset_code() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return Reset::PowerOn;
    case ESP_RST_SW: return Reset::Software;
    case ESP_RST_PANIC: return Reset::Panic;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return Reset::Watchdog;
    case ESP_RST_BROWNOUT: return Reset::Brownout;
    case ESP_RST_DEEPSLEEP: return Reset::DeepSleep;
    case ESP_RST_EXT: return Reset::External;
    default: return Reset::Other;
  }
}

}  // namespace unit
