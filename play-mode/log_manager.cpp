#include "log_manager.h"
#include <stdarg.h>
#include <string.h>

// ============================================================================
// PlayMode — Log Manager Temps Réel (implémentation)
// ============================================================================

// Singleton global
LogManager logger;

// Entrée vide pour index invalide
const LogEntry LogManager::_empty = {};

// ============================================================================
// Construction
// ============================================================================

LogManager::LogManager()
    : _head(0)
    , _count(0)
    , _mutex(nullptr)
{
    memset(_buf, 0, sizeof(_buf));
}

void LogManager::begin() {
    _mutex = xSemaphoreCreateMutex();
}

// ============================================================================
// Enregistrement
// ============================================================================

void LogManager::log(LogLevel level, LogCategory cat, const char* fmt, ...) {
    // Prépare l'entrée
    LogEntry entry;
    entry.timestamp_ms = millis();
    entry.level        = level;
    entry.category     = cat;

    va_list args;
    va_start(args, fmt);
    vsnprintf(entry.message, LOG_MAX_MSG_LEN, fmt, args);
    va_end(args);
    entry.message[LOG_MAX_MSG_LEN - 1] = '\0';

    // Miroir sur Serial avec préfixe niveau
    const char* lvl_tag = "?";
    switch (level) {
        case LOG_DEBUG:    lvl_tag = "DBG"; break;
        case LOG_INFO:     lvl_tag = "INF"; break;
        case LOG_WARN:     lvl_tag = "WRN"; break;
        case LOG_ERROR:    lvl_tag = "ERR"; break;
        case LOG_CRITICAL: lvl_tag = "CRT"; break;
    }
    Serial.printf("[LOG/%s] %s\n", lvl_tag, entry.message);

    // Écriture thread-safe dans le buffer circulaire
    // AUDIT FIX : timeout augmenté à 10ms (au lieu de 5ms) pour réduire les pertes.
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _buf[_head] = entry;
        _head = (_head + 1) % LOG_BUFFER_SIZE;
        if (_count < LOG_BUFFER_SIZE) _count++;
        xSemaphoreGive(_mutex);
    }
    // Si le mutex n'est pas disponible, l'entrée est quand même émise sur Serial
    // (miroir ci-dessus) — la perte de buffer est acceptable pour les cas extrêmes.
}

// ============================================================================
// Accesseurs
// ============================================================================

uint8_t LogManager::getCount() const {
    return _count;
}

const LogEntry& LogManager::getEntry(uint8_t idx) const {
    if (idx >= _count) return _empty;
    // idx 0 = entrée la plus récente (_head - 1)
    int16_t pos = (int16_t)_head - 1 - (int16_t)idx;
    if (pos < 0) pos += LOG_BUFFER_SIZE;
    return _buf[(uint8_t)(pos % LOG_BUFFER_SIZE)];
}

void LogManager::getAllEntries(LogEntry* out_buf, uint8_t& count_out) const {
    count_out = 0;
    if (!out_buf || !_mutex) return;

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        uint8_t n = _count;
        // Copie triée : index 0 = plus récente
        for (uint8_t i = 0; i < n; i++) {
            int16_t pos = (int16_t)_head - 1 - (int16_t)i;
            if (pos < 0) pos += LOG_BUFFER_SIZE;
            out_buf[i] = _buf[(uint8_t)(pos % LOG_BUFFER_SIZE)];
        }
        count_out = n;
        xSemaphoreGive(_mutex);
    }
}

void LogManager::clear() {
    if (!_mutex) return;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memset(_buf, 0, sizeof(_buf));
        _head  = 0;
        _count = 0;
        xSemaphoreGive(_mutex);
    }
}
