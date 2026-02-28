#ifndef TEST_MANAGER_H
#define TEST_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "scheduler.h"
#include "config_manager.h"

// ============================================================================
// PlayMode — Test Manager Industriel (Phase 8)
// ============================================================================
//
// Pilote des séquences de test sur les actionneurs sans MIDI externe.
// Trois modes de test :
//
//   Sweep  : parcourt tous les actionneurs actifs un par un
//   Burst  : rafale rapide sur un seul actionneur
//   Stress : déclenche tous les actionneurs simultanément (test polyphonie)
//
// Les événements sont injectés directement dans le Scheduler, bypass MIDI.
// Le Safety Manager et le Power Manager restent actifs.
//
// Journal circulaire des 64 derniers événements pour audit.
//

enum TestMode : uint8_t {
    TEST_IDLE    = 0,   // En attente
    TEST_SWEEP   = 1,   // Scan séquentiel de tous les actionneurs
    TEST_BURST   = 2,   // Rafale sur un actionneur
    TEST_STRESS  = 3,   // Tous actionneurs simultanément
};

// Entrée de journal d'événements test
struct TestLogEntry {
    uint32_t timestamp_ms;      // millis()
    uint8_t  actuator_id;       // ID actionneur
    uint8_t  velocity;          // Vélocité envoyée
    TestMode mode;              // Mode de test
    bool     scheduled;         // true = événement envoyé au scheduler
};

class TestManager {
public:
    TestManager(Scheduler& scheduler, ConfigManager& config);

    // --- Démarrage des tests ---

    // Sweep séquentiel : test chaque actionneur actif à intervalle régulier
    // loop=true → recommencer indéfiniment jusqu'à stop()
    bool startSweep(uint8_t velocity      = TEST_DEFAULT_VELOCITY,
                    uint16_t interval_ms  = TEST_DEFAULT_INTERVAL_MS,
                    uint16_t hold_ms      = TEST_DEFAULT_HOLD_MS,
                    bool loop             = false);

    // Burst : N frappes rapides sur un actionneur
    bool startBurst(uint8_t actuator_id,
                    uint8_t  count        = TEST_BURST_DEFAULT_COUNT,
                    uint8_t  velocity     = TEST_DEFAULT_VELOCITY,
                    uint16_t interval_ms  = TEST_BURST_DEFAULT_INTVL_MS);

    // Stress : déclenche tous les actionneurs actifs simultanément
    bool startStress(uint8_t velocity = TEST_DEFAULT_VELOCITY,
                     uint16_t hold_ms = TEST_DEFAULT_HOLD_MS);

    // Arrêt immédiat
    void stop();

    // --- Boucle principale — appeler chaque loop() ---
    void update();

    // --- Accesseurs d'état ---
    TestMode getMode()               const;
    bool     isRunning()             const;
    uint8_t  getProgress()           const;  // 0-100 %
    uint8_t  getCurrentActuatorId()  const;
    uint32_t getEventsSent()         const;
    uint32_t getTestsRun()           const;  // Nombre de sweeps/bursts complétés

    // --- Journal circulaire ---
    uint8_t              getLogCount()             const;
    const TestLogEntry&  getLogEntry(uint8_t idx)  const;  // 0 = plus récent
    void                 clearLog();

private:
    Scheduler&     _scheduler;
    ConfigManager& _config;

    TestMode _mode;
    bool     _loop;

    // Paramètres courants
    uint8_t  _velocity;
    uint16_t _interval_ms;
    uint16_t _hold_ms;

    // Sweep state
    uint8_t  _sweep_act_idx;     // Index courant dans tableau actionneurs
    uint32_t _sweep_last_ms;     // millis() du dernier trigger sweep

    // Burst state
    uint8_t  _burst_act_id;
    uint8_t  _burst_count;
    uint8_t  _burst_remaining;
    uint16_t _burst_interval_ms;
    uint32_t _burst_last_ms;

    // Compteurs
    uint32_t _events_sent;
    uint32_t _tests_run;

    // Journal circulaire (TEST_LOG_SIZE entrées)
    TestLogEntry _log[TEST_LOG_SIZE];
    uint8_t      _log_head;     // Prochaine position d'écriture
    uint8_t      _log_count;    // Entrées valides (0..TEST_LOG_SIZE)

    // --- Méthodes internes ---
    bool    triggerActuator(uint8_t act_id);   // Retourne false si scheduler plein
    void    scheduleNoteOff(uint8_t act_id, uint16_t delay_ms);
    void    logEvent(uint8_t act_id, uint8_t velocity, bool scheduled);
    uint8_t getEnabledActuatorCount() const;
    bool    getActuatorIdByIndex(uint8_t idx, uint8_t& id_out) const;
};

#endif // TEST_MANAGER_H
