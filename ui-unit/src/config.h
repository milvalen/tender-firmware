// Settings of the TND-01 interface unit on the M5Stack StickS3.
// Values marked [?] are not yet checked on the real hardware.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace unit {

// Table numbers on the floor. Edit for the real restaurant.
constexpr uint8_t kTables[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

// --- Keys --------------------------------------------------------------------

constexpr bool kHasDualButton = false;  // true when the Dual Button unit is on the Grove port
constexpr int kRedPin = 9;              // Grove PORT.A yellow [?]
constexpr int kBluePin = 10;            // Grove PORT.A white [?]
constexpr bool kDualActiveHigh = true;  // [?]
constexpr uint32_t kHoldMs = 600;       // a press this long is a hold

// --- Vibration motor -----------------------------------------------------------
// The StickS3 has no motor. Unit Vibrator (U059): the maker gives 424 mA at
// 5 V for 10 kHz PWM, 50 % duty, but the StickS3 gives only 0.38 A on Grove
// 5 V. Start low; raise only after a current measurement on battery [?].

constexpr int kVibroPin = -1;  // GPIO of the motor driver, -1 = no motor
constexpr uint32_t kVibroFreqHz = 10000;
constexpr uint8_t kVibroBits = 8;
constexpr uint32_t kVibroDuty = 90;   // of 255, about 35 % [?]
constexpr uint8_t kVibroChannel = 2;  // LEDC; the backlight and power code use 6 and 7
static_assert(kVibroDuty < (1u << kVibroBits), "kVibroDuty is above the PWM range");

constexpr bool kBeepOnAlert = false;  // the floor stays silent by default

// --- Screen ------------------------------------------------------------------

constexpr uint8_t kRotation = 1;  // landscape; 3 if the screen is upside down [?]
constexpr uint8_t kBright = 128;
constexpr uint8_t kDim = 16;
constexpr uint32_t kRedrawMs = 500;

// Palette: black, white, grey and one accent.
constexpr uint16_t kInk = 0x0000;
constexpr uint16_t kPaper = 0xFFFF;
constexpr uint16_t kGrey = 0x8410;
constexpr uint16_t kAccent = 0xEAA4;  // signal orange

// --- Shift log -----------------------------------------------------------------
// Kept in the "spiffs" flash partition (1.5 MB). Each record is 8 bytes. When
// the file reaches kLogMaxBytes it becomes the old file and a new one starts,
// so the last 2 files (up to 65,536 records) are kept.

constexpr const char* kLogPath = "/shift.log";
constexpr const char* kLogOldPath = "/shift.old";
constexpr size_t kLogMaxBytes = 256 * 1024;
constexpr uint32_t kBatteryLogMs = 10 * 60 * 1000;

}  // namespace unit
