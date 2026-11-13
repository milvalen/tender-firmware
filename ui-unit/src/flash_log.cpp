#include "flash_log.h"

#include <LittleFS.h>

#include "config.h"
#include "version.h"

namespace unit {

namespace sl = tender::shiftlog;

bool FlashLog::begin() {
  ok_ = LittleFS.begin(true);
  return ok_;
}

void FlashLog::append(const sl::Record& r) {
  if (!ok_) return;
  File f = LittleFS.open(kLogPath, FILE_APPEND);
  if (f && f.size() >= kLogMaxBytes) {  // full: keep it as the old file
    f.close();
    LittleFS.remove(kLogOldPath);
    LittleFS.rename(kLogPath, kLogOldPath);
    f = LittleFS.open(kLogPath, FILE_APPEND);
  }
  if (!f) return;
  f.write(reinterpret_cast<const uint8_t*>(&r), sizeof r);
  f.close();
}

void FlashLog::dump(uint32_t now, Print& out) const {
  out.printf("# TND-01 shift log v%d now_ms=%lu fw=%s%s\n", sl::kVersion, static_cast<unsigned long>(now),
             tender::kFirmwareCommit, tender::kFirmwareDirtyMark);
  out.println(sl::Writer::header());

  sl::Writer writer;
  unsigned long records = 0;
  unsigned long skipped = 0;
  char line[96];
  const char* paths[] = {kLogOldPath, kLogPath};
  for (const char* path : paths) {
    if (!ok_ || !LittleFS.exists(path)) continue;
    File f = LittleFS.open(path, FILE_READ);
    sl::Record r;
    while (f && f.read(reinterpret_cast<uint8_t*>(&r), sizeof r) == sizeof r) {
      if (writer.line(r, line, sizeof line)) {
        out.print(line);
        ++records;
      } else {
        ++skipped;
      }
    }
    if (f) f.close();
  }

  out.printf("# end records=%lu skipped=%lu boots=%d log=%s\n", records, skipped, writer.boots(),
             ok_ ? "ok" : "unavailable");
}

void FlashLog::clear() {
  if (!ok_) return;
  LittleFS.remove(kLogPath);
  LittleFS.remove(kLogOldPath);
}

}  // namespace unit
