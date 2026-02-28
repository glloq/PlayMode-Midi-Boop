#ifndef SAFETY_MANAGER_H
#define SAFETY_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "pca_driver.h"

// ============================================================================
// PlayMode — Safety Manager + Power Manager
// ============================================================================

class SafetyManager {
public:
    SafetyManager(PCADriver& pca);

    // Initialise le safety manager
    void begin();

    // --- Vérification pré-événement (appelé par le scheduler avant dispatch) ---

    // Vérifie si un événement est autorisé pour un actionneur donné
    // Retourne true si l'événement peut être exécuté
    bool checkEvent(const ActuatorConfig& actuator, const SchedulerEvent& event);

    // --- Monitoring périodique (appelé dans la boucle scheduler) ---

    // Met à jour l'état de sécurité (appelé chaque tick scheduler)
    void update(ActuatorConfig* actuators[], uint8_t count);

    // --- Kill switch ---

    // Active le kill switch global (désactive toutes les sorties)
    void activateKillSwitch();

    // Désactive le kill switch (réactive les sorties)
    void deactivateKillSwitch();

    // Vérifie si le kill switch est actif
    bool isKillSwitchActive() const;

    // --- Accesseurs état ---

    // Retourne l'état de sécurité d'un actionneur
    const ActuatorSafetyState& getActuatorSafetyState(uint8_t actuator_id) const;

    // Retourne l'état global de sécurité
    const SafetyState& getGlobalState() const;

    // Retourne le courant total estimé (mA)
    uint32_t getEstimatedCurrentMA() const;

    // Retourne le nombre d'actionneurs actifs
    uint8_t getActiveActuatorCount() const;

    // Vérifie si la dégradation gracieuse est active
    bool isDegradationActive() const;

    // --- Configuration runtime ---

    void setMaxDutyCycle(uint8_t percent);
    void setMaxFrequency(uint16_t hz);
    void setWatchdogTimeout(uint16_t ms);
    void setMaxPolyphony(uint8_t max);
    void setMaxTotalCurrent(uint16_t ma);

private:
    PCADriver& _pca;

    // État de sécurité par actionneur
    ActuatorSafetyState _actuator_safety[MAX_ACTUATORS];
    SafetyState _global_state;

    // Configuration (modifiable à runtime)
    uint8_t _max_duty_cycle;         // % (défaut SAFETY_MAX_DUTY_CYCLE)
    uint16_t _max_freq_hz;           // Hz (défaut SAFETY_MAX_FREQ_HZ)
    uint16_t _watchdog_ms;           // ms (défaut SAFETY_WATCHDOG_MS)
    uint8_t _max_polyphony;          // (défaut SAFETY_MAX_POLYPHONY)
    uint16_t _max_total_current_ma;  // mA (défaut SAFETY_MAX_TOTAL_CURRENT_MA)

    uint32_t _last_check_us;         // Dernier check périodique

    // État par défaut (pour IDs invalides)
    static const ActuatorSafetyState _default_safety_state;

    // --- Méthodes internes ---

    // Vérifie la fréquence de déclenchement
    bool checkFrequency(uint8_t actuator_id);

    // Vérifie le duty cycle d'un actionneur
    bool checkDutyCycle(uint8_t actuator_id, const ActuatorConfig& actuator);

    // Vérifie le watchdog pour un actionneur
    void checkWatchdog(uint8_t actuator_id, ActuatorConfig& actuator);

    // Estime le courant consommé par un actionneur
    uint16_t estimateActuatorCurrent(const ActuatorConfig& actuator);

    // Calcule le courant total et applique les limites
    void updateCurrentEstimate(ActuatorConfig* actuators[], uint8_t count);

    // Réinitialise la fenêtre de comptage d'un actionneur
    void resetWindow(uint8_t actuator_id);
};

#endif // SAFETY_MANAGER_H
