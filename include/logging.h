// Leveled, tagged, timestamped logging over Serial — replaces this
// project's previous ad hoc `Serial.printf("[tag] message\n", ...)` calls
// scattered across every module with one consistent format:
// `<timestamp> [<level>][<tag>] message`.
//
// Timestamp is uptime ("+12345ms") until NTP sync completes (see
// syncTimeOverNtp() in main.cpp), then switches to a real UTC wall-clock
// time — logSetTimeSynced() flips that over, called once after the sync
// attempt succeeds or gives up.
//
// Deliberately still just Serial underneath, not a new sink or a
// heap-buffered formatter: logPrefix() and the caller's own
// Serial.printf(fmt, ...) are two separate calls (see logPrefix()'s own
// comment) rather than composing one combined string here, so this
// doesn't add its own heap allocation on top of whatever Serial::printf()
// already does internally for longer messages — this session spent a lot
// of effort getting heap/DMA contention under control elsewhere
// (docs/hardware.md, docs/firmware-architecture.md) and a new
// cross-cutting utility used everywhere is exactly the kind of thing that
// could quietly undo that.
#pragma once

#include <Arduino.h>

enum class LogLevel : uint8_t { kError, kWarn, kInfo, kDebug };

// Called once, after main.cpp determines wall-clock time is available —
// either the RTC-backed system clock already looked valid at boot (an
// earlier session's NTP sync survived the reset), or a fresh NTP sync
// completed. Safe to call from any task.
void logSetTimeSynced(bool synced);

// True once logSetTimeSynced(true) has been called — main.cpp checks this
// before bothering with a network-dependent NTP sync at all.
bool logTimeIsSynced();

// Minimum level actually printed — a call below this level does no
// Serial I/O at all, not just hidden formatting. Defaults to kDebug (show
// everything), matching this project's original "print everything"
// behavior before this module existed; raise it (e.g. to kInfo) to quiet
// the per-item/arena-usage debug lines once the firmware needs to run
// hands-off rather than be watched over Serial.
void logSetMinLevel(LogLevel level);
bool logLevelEnabled(LogLevel level);

// Prints `<timestamp> [<level>][<tag>] ` with no trailing newline or
// space-separated message — callers finish the line themselves via
// Serial.printf(fmt, ...), same as every pre-existing Serial.printf()
// call in this codebase already does (each ends its own format string
// with "\n"). Not intended to be called directly — use the LOG_* macros
// below, which pair this with the caller's own Serial.printf() and the
// level-gating check above.
void logPrefix(LogLevel level, const char *tag);

#define LOG_E(tag, fmt, ...)                             \
  do {                                                   \
    if (logLevelEnabled(LogLevel::kError)) {              \
      logPrefix(LogLevel::kError, tag);                   \
      Serial.printf(fmt, ##__VA_ARGS__);                  \
    }                                                      \
  } while (0)

#define LOG_W(tag, fmt, ...)                             \
  do {                                                   \
    if (logLevelEnabled(LogLevel::kWarn)) {               \
      logPrefix(LogLevel::kWarn, tag);                    \
      Serial.printf(fmt, ##__VA_ARGS__);                  \
    }                                                      \
  } while (0)

#define LOG_I(tag, fmt, ...)                             \
  do {                                                   \
    if (logLevelEnabled(LogLevel::kInfo)) {               \
      logPrefix(LogLevel::kInfo, tag);                    \
      Serial.printf(fmt, ##__VA_ARGS__);                  \
    }                                                      \
  } while (0)

#define LOG_D(tag, fmt, ...)                             \
  do {                                                   \
    if (logLevelEnabled(LogLevel::kDebug)) {              \
      logPrefix(LogLevel::kDebug, tag);                   \
      Serial.printf(fmt, ##__VA_ARGS__);                  \
    }                                                      \
  } while (0)
