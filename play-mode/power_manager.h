#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "types.h"

// ============================================================================
// PlayMode — Power Manager (Phase 5)
// ============================================================================
//
// Gère le budget énergétique par bus (servo/solénoïde) et par instrument.
// Appelé par l'EventNormalizer avant toute activation d'actionneur.
//
// Fonctionnement :
//   - canActivate()       : vérifie si l'activation respecte les budgets
//   - notifyActivation()  : comptabilise un actionneur activé
//   - notifyDeactivation(): décrémente les compteurs lors d'un NOTE_OFF
//   - update()            : mise à jour périodique des statistiques
//
// Les limites du SafetyManager (watchdog, duty cycle, fréquence) restent
// distinctes : le PowerManager agit en amont, au niveau polyphonie/énergie.
//

class PowerManager {
public:
    PowerManager();

    // Initialise avec le budget configuré
    // Appelé une fois dans setup()
    void begin(const PowerBudget& budget);

    // -------------------------------------------------------------------------
    // Décision d'activation (appelé par EventNormalizer avant push scheduler)
    // -------------------------------------------------------------------------

    // Vérifie si l'actionneur peut être activé sans dépasser les budgets.
    // instrument_index : index de l'instrument dans la config (MAX_INSTRUMENTS = inconnu)
    // velocity         : vélocité MIDI (0-127), utilisée pour estimer la consommation
    // Retourne true si l'activation est autorisée.
    bool canActivate(const ActuatorConfig& actuator, uint8_t instrument_index,
                     uint8_t velocity);

    // Enregistre l'activation effective d'un actionneur (après push scheduler réussi)
    void notifyActivation(const ActuatorConfig& actuator, uint8_t instrument_index,
                          uint8_t velocity);

    // Enregistre la désactivation d'un actionneur (NOTE_OFF reçu)
    void notifyDeactivation(const ActuatorConfig& actuator, uint8_t instrument_index);

    // -------------------------------------------------------------------------
    // Mise à jour périodique (appelé depuis loop() Core 0)
    // -------------------------------------------------------------------------

    // Recalcule les statistiques de consommation depuis l'état des actionneurs.
    // actuators[] : tableau de pointeurs vers les configs actionneurs enregistrés
    // count       : nombre d'actionneurs
    void update(ActuatorConfig* actuators[], uint8_t count);

    // -------------------------------------------------------------------------
    // Accesseurs statistiques (pour monitoring et futur Web UI)
    // -------------------------------------------------------------------------

    const PowerStats& getStats() const;

    // Budget restant en mA (global)
    uint32_t getBudgetRemainingMA() const;

    // Pourcentage du budget global utilisé (0–100)
    uint8_t getBudgetUsedPercent() const;

    // Indique si la dégradation gracieuse est active (>= POWER_DEGRADATION_THRESHOLD_PCT %)
    bool isDegradationActive() const;

    // -------------------------------------------------------------------------
    // Configuration runtime (modifiable depuis Web UI Phase 6)
    // -------------------------------------------------------------------------

    void setGlobalMaxMA(uint32_t ma);
    void setServoBusMaxMA(uint32_t ma);
    void setSolenoidBusMaxMA(uint32_t ma);
    void setGlobalMaxPolyphony(uint8_t max);
    void setInstrumentMaxPolyphony(uint8_t instrument_index, uint8_t max);

    // Retourne le budget courant (lecture seule)
    const PowerBudget& getBudget() const;

private:
    PowerBudget _budget;
    PowerStats  _stats;

    // Suivi par actionneur : courant alloué (0 = inactif)
    uint16_t _actuator_allocated_ma[MAX_ACTUATORS];
    bool     _actuator_tracked[MAX_ACTUATORS];

    // Timestamp de dernière mise à jour complète
    uint32_t _last_update_us;

    // -------------------------------------------------------------------------
    // Méthodes internes
    // -------------------------------------------------------------------------

    // Estime la consommation d'un actionneur selon son type et la vélocité
    uint16_t estimateCurrent(const ActuatorConfig& actuator, uint8_t velocity) const;

    // Recalcule servo_bus_ma et solenoid_bus_ma depuis les allocations actives
    void recalculateBusTotals();

    // Recalcule budget_used_percent et degradation_active
    void updateDerivedStats();
};

#endif // POWER_MANAGER_H
