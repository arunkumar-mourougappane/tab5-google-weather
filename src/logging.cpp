#include "logging.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>

namespace {

bool g_timeSynced = false;
LogLevel g_minLevel = LogLevel::kDebug;

// Guards the whole prefix+message pair a LOG_* call makes (see
// logLock()/logUnlock()) — without this, uiTaskFn (core 0) and netTaskFn
// (core 1) logging at nearly the same instant (confirmed on hardware,
// right at boot: "uiTaskFn start"/"netTaskFn start" land together) can
// interleave their two separate Serial calls, garbling both lines. A
// function-local static is safe to lazily create here specifically
// because logging is never used before setup() starts running — and
// setup() itself already executes inside a FreeRTOS task (Arduino's
// loopTask), so the scheduler (and thus xSemaphoreCreateMutex()) is
// already up by the time any LOG_* call can happen.
SemaphoreHandle_t logMutex() {
  static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
  return mutex;
}

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

void logLock() {
  xSemaphoreTake(logMutex(), portMAX_DELAY);
}

void logUnlock() {
  xSemaphoreGive(logMutex());
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
