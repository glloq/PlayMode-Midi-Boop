#ifndef CALIBRATOR_H
#define CALIBRATOR_H

#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"
#include "types.h"
#include "scheduler.h"
#include "config_manager.h"

// ============================================================================
// PlayMode — Calibrateur Acoustique (Phase 7)
// ============================================================================
//
// Mesure la latence mécanique réelle de chaque actionneur en utilisant un
// microphone numérique I²S (INMP441 ou compatible).
//
// Procédure pour chaque actionneur :
//   1. Mesure du bruit ambiant → seuil de détection adaptatif
//   2. Flush du buffer DMA I²S + déclenchement actionneur
//   3. Lecture streaming des échantillons I²S post-trigger
//   4. Détection du premier onset (|sample| > seuil)
//   5. Latence = samples_avant_onset / sample_rate
//   6. Moyenne sur CAL_RETRIES mesures valides
//

enum CalibrationState : uint8_t {
    CAL_IDLE       = 0,   // En attente
    CAL_AMBIENT    = 1,   // Mesure bruit ambiant (baseline)
    CAL_TRIGGERING = 2,   // Flush DMA + déclenchement
    CAL_RECORDING  = 3,   // Enregistrement + détection onset
    CAL_PAUSING    = 4,   // Pause inter-essais / inter-actionneurs
    CAL_COMPLETE   = 5,   // Calibration terminée avec succès
    CAL_ERROR      = 6    // Erreur (timeout, pas de son, init échouée)
};

struct CalibrationResult {
    uint8_t  actuator_id;
    uint16_t measured_latency_ms;  // Latence moyenne mesurée (ms)
    uint8_t  samples_taken;        // Nombre de mesures valides
    bool     success;              // Calibration réussie (≥1 mesure valide)
    uint32_t timestamp_ms;         // millis() au moment de la complétion
};

class Calibrator {
public:
    Calibrator(Scheduler& scheduler, ConfigManager& config);

    // --- Cycle de vie ---
    bool begin();  // Initialise le driver I²S (appeler une fois dans setup)
    void stop();   // Arrêt immédiat, retour à IDLE

    // --- Démarrage ---
    bool startCalibration(uint8_t actuator_id);  // 1 actionneur spécifique
    bool startCalibrationAll();                   // Tous les actionneurs actifs

    // --- Boucle principale — appeler chaque loop() ---
    void update();

    // --- Lecture d'état ---
    CalibrationState getState()             const;
    bool             isRunning()            const;
    uint8_t          getProgress()          const;  // 0-100 %
    uint8_t          getCurrentActuatorId() const;
    bool             isMicReady()           const { return _i2s_ready; }

    // --- Résultats ---
    const CalibrationResult* getResult(uint8_t actuator_id) const;
    uint8_t                  getResultCount()               const;

    // Écrit les latences mesurées dans les ActuatorConfig
    uint8_t applyResults();

private:
    Scheduler&     _scheduler;
    ConfigManager& _config;

    // État machine
    CalibrationState _state;
    bool             _i2s_ready;

    // Actionneur en cours
    uint8_t  _cur_act_id;
    uint8_t  _cur_try;           // 0 .. CAL_RETRIES-1

    // Timing
    uint32_t _state_enter_us;    // esp_timer_get_time() à l'entrée dans l'état
    uint32_t _trigger_time_us;   // Timestamp du NOTE_ON (esp_timer)

    // Seuil de détection (calculé en phase AMBIENT)
    int32_t  _onset_threshold;

    // Comptage de samples depuis le flush/trigger
    uint32_t _samples_after_trigger;

    // Accumulation pour la phase AMBIENT (mean absolute value)
    uint32_t _ambient_abs_sum;
    uint32_t _ambient_sample_count;

    // Accumulation des mesures de latence
    uint32_t _latency_sum_us;
    uint8_t  _latency_count;

    // File d'attente pour le mode startCalibrationAll()
    uint8_t _queue[MAX_ACTUATORS];
    uint8_t _queue_count;
    uint8_t _queue_idx;

    // Résultats
    CalibrationResult _results[MAX_ACTUATORS];
    uint8_t           _result_count;

    // Buffer de lecture I²S (DMA → RAM)
    int32_t  _i2s_buf[CAL_READ_CHUNK];
    uint32_t _chunk_samples;  // Samples valides dans _i2s_buf après readChunk()

    // --- Méthodes internes ---
    bool    initI2S();
    void    deinitI2S();
    bool    probeMic();                // Lit des samples pour vérifier qu'un micro est réellement branché
    void    flushI2S();
    bool    readChunk();               // Lit jusqu'à CAL_READ_CHUNK samples, renvoie vrai si ≥1 sample lu
    int32_t detectOnset() const;       // Retourne index dans _i2s_buf ou -1
    void    triggerActuator();
    void    finishCurrent();           // Finalise les mesures et passe au suivant
    bool    advanceToNext();           // Retourne true si un actionneur suivant est dispo
    void    enterState(CalibrationState s);

    CalibrationResult*       findResult(uint8_t actuator_id);
    const CalibrationResult* findResult(uint8_t actuator_id) const;
};

#endif // CALIBRATOR_H
