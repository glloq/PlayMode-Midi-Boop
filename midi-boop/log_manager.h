#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

// ============================================================================
// PlayMode Midi B∞p — Log Manager Temps Réel (Phase 9)
// ============================================================================
//
// Journal circulaire thread-safe pour Core 0 et Core 1.
// Stocke les LOG_BUFFER_SIZE derniers événements système avec horodatage.
// Accessible via GET /api/logs (REST) et affiché en temps réel dans l'UI.
//
// Usage :
//   logger.log(LOG_INFO, CAT_MIDI, "Note %d → act %d", note, act_id);
//

enum LogLevel : uint8_t {
    LOG_DEBUG    = 0,   // Diagnostic détaillé
    LOG_INFO     = 1,   // Événement normal
    LOG_WARN     = 2,   // Situation anormale non critique
    LOG_ERROR    = 3,   // Erreur récupérable
    LOG_CRITICAL = 4    // Erreur critique (kill switch, surintensité…)
};

enum LogCategory : uint8_t {
    CAT_SYSTEM  = 0,   // Boot, WiFi, config, sauvegarde
    CAT_MIDI    = 1,   // Messages MIDI reçus/routés/rejetés
    CAT_SCHED   = 2,   // Scheduler (débordement queue, etc.)
    CAT_SAFETY  = 3,   // Kill switch, dégradation, watchdog
    CAT_POWER   = 4,   // Budget courant, polyphonie
    CAT_CAL     = 5,   // Calibration acoustique
    CAT_TEST    = 6    // Test Manager industriel
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

    // Initialise le mutex FreeRTOS.
    // À appeler depuis setup() avant le démarrage du scheduler.
    void begin();

    // Enregistre un message (printf-style).
    // Thread-safe : peut être appelé depuis Core 0 et Core 1.
    // Imprime aussi sur Serial.
    void log(LogLevel level, LogCategory cat, const char* fmt, ...);

    // Retourne le nombre d'entrées valides (0 … LOG_BUFFER_SIZE)
    uint8_t getCount() const;

    // Retourne l'entrée à l'indice idx (0 = la plus récente)
    const LogEntry& getEntry(uint8_t idx) const;

    // Efface le journal
    void clear();

private:
    LogEntry          _buf[LOG_BUFFER_SIZE];
    volatile uint8_t  _head;    // Prochaine position d'écriture
    volatile uint8_t  _count;   // Entrées valides (0 .. LOG_BUFFER_SIZE)
    SemaphoreHandle_t _mutex;

    static const LogEntry _empty;
};

// Singleton global — instancié dans log_manager.cpp
extern LogManager logger;

#endif // LOG_MANAGER_H
