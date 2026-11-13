// Firmware version for TENDER TND-01: the git commit of the build.
//
// scripts/version.py defines these for the PlatformIO build. Desktop builds
// (tests, simulator) get "unknown" unless the Makefile passes them.
#pragma once

#include <stdint.h>

#ifndef TND_COMMIT
#define TND_COMMIT "unknown"
#endif
#ifndef TND_COMMIT_U32
#define TND_COMMIT_U32 0UL
#endif
#ifndef TND_DIRTY
#define TND_DIRTY 0
#endif
#ifndef TND_BUILD_DATE
#define TND_BUILD_DATE "unknown"
#endif

namespace tender {

constexpr const char* kFirmwareCommit = TND_COMMIT;
constexpr uint32_t kFirmwareCommitU32 = static_cast<uint32_t>(TND_COMMIT_U32);
constexpr bool kFirmwareDirty = TND_DIRTY != 0;
// "+dirty" after the commit when the build had uncommitted changes, else "".
constexpr const char* kFirmwareDirtyMark = kFirmwareDirty ? "+dirty" : "";
constexpr const char* kFirmwareDate = TND_BUILD_DATE;

}  // namespace tender
