#include "logging.h"

#include <time.h>

namespace {

bool g_timeSynced = false;
LogLevel g_minLevel = LogLevel::kDebug;

const char *levelTag(LogLevel level) {
  switch (level) {
    case LogLevel::kError:
      return "E";
    case LogLevel::kWarn:
      return "W";
    case LogLevel::kInfo:
      return "I";
    case LogLevel::kDebug:
      return "D";
  }
  return "?";
}

}  // namespace

void logSetTimeSynced(bool synced) {
  g_timeSynced = synced;
}

bool logTimeIsSynced() {
  return g_timeSynced;
}

void logSetMinLevel(LogLevel level) {
  g_minLevel = level;
}

bool logLevelEnabled(LogLevel level) {
  // Enum order is kError < kWarn < kInfo < kDebug (most to least severe),
  // so a call is enabled if it's at least as severe as the configured
  // minimum — e.g. with g_minLevel == kInfo, kError/kWarn/kInfo are
  // enabled but kDebug is not.
  return static_cast<uint8_t>(level) <= static_cast<uint8_t>(g_minLevel);
}

void logPrefix(LogLevel level, const char *tag) {
  if (g_timeSynced) {
    time_t now = time(nullptr);
    struct tm tmInfo;
    gmtime_r(&now, &tmInfo);
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmInfo);
    Serial.printf("%s UTC [%s][%s] ", buf, levelTag(level), tag);
  } else {
    Serial.printf("+%lums [%s][%s] ", millis(), levelTag(level), tag);
  }
}
