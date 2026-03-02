#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

// ============================================================================
// PlayMode — Real-Time Log Manager (Phase 9)
// ============================================================================
//
// Thread-safe circular log for Core 0 and Core 1.
// Stores the last LOG_BUFFER_SIZE system events with timestamps.
// Accessible via GET /api/logs (REST) and displayed in real-time in the UI.
//
// Usage:
//   logger.log(LOG_INFO, CAT_MIDI, "Note %d -> act %d", note, act_id);
//

enum LogLevel : uint8_t {
    LOG_DEBUG    = 0,   // Detailed diagnostics
    LOG_INFO     = 1,   // Normal event
    LOG_WARN     = 2,   // Non-critical abnormal situation
    LOG_ERROR    = 3,   // Recoverable error
    LOG_CRITICAL = 4    // Critical error (kill switch, overcurrent...)
};

enum LogCategory : uint8_t {
    CAT_SYSTEM  = 0,   // Boot, WiFi, config, save
    CAT_MIDI    = 1,   // MIDI messages received/routed/rejected
    CAT_SCHED   = 2,   // Scheduler (queue overflow, etc.)
    CAT_SAFETY  = 3,   // Kill switch, degradation, watchdog
    CAT_POWER   = 4,   // Current budget, polyphony
    CAT_CAL     = 5,   // Acoustic calibration
    CAT_TEST    = 6    // Industrial Test Manager
};

struct LogEntry {
    uint32_t    timestamp_ms;
    LogLevel    level;
    LogCategory category;
    char        message[LOG_MAX_MSG_LEN];
};

class LogManager {
public:
    LogManager();

    // Initializes the FreeRTOS mutex.
    // Call from setup() before starting the scheduler.
    void begin();

    // Records a message (printf-style).
    // Thread-safe: can be called from Core 0 and Core 1.
    // Also prints to Serial.
    void log(LogLevel level, LogCategory cat, const char* fmt, ...);

    // Returns the number of valid entries (0 ... LOG_BUFFER_SIZE)
    uint8_t getCount() const;

    // Returns the entry at index idx (0 = most recent)
    const LogEntry& getEntry(uint8_t idx) const;

    // Thread-safe copy of the entire buffer into out_buf (size LOG_BUFFER_SIZE).
    // count_out receives the number of valid entries copied (order: idx 0 = most recent).
    // Use from web handlers to avoid races with log().
    void getAllEntries(LogEntry* out_buf, uint8_t& count_out) const;

    // Clears the log
    void clear();

private:
    LogEntry          _buf[LOG_BUFFER_SIZE];
    volatile uint8_t  _head;    // Next write position
    volatile uint8_t  _count;   // Valid entries (0 .. LOG_BUFFER_SIZE)
    SemaphoreHandle_t _mutex;

    static const LogEntry _empty;
};

// Global singleton — instantiated in log_manager.cpp
extern LogManager logger;

#endif // LOG_MANAGER_H
