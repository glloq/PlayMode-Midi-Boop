#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "types.h"

// ============================================================================
// PlayMode — Power Manager (Phase 5)
// ============================================================================
//
// Manages the energy budget per bus (servo/solenoid) and per instrument.
// Called by the EventNormalizer before any actuator activation.
//
// Operation:
//   - canActivate()       : checks if activation respects the budgets
//   - notifyActivation()  : accounts for an activated actuator
//   - notifyDeactivation(): decrements the counters on NOTE_OFF
//   - update()            : periodic statistics update
//
// The SafetyManager limits (watchdog, duty cycle, frequency) remain
// separate: the PowerManager acts upstream, at the polyphony/energy level.
//

class PowerManager {
public:
    PowerManager();

    // Initialize with the configured budget
    // Called once in setup()
    void begin(const PowerBudget& budget);

    // -------------------------------------------------------------------------
    // Activation decision (called by EventNormalizer before scheduler push)
    // -------------------------------------------------------------------------

    // Checks if the actuator can be activated without exceeding the budgets.
    // instrument_index : instrument index in the config (MAX_INSTRUMENTS = unknown)
    // velocity         : MIDI velocity (0-127), used to estimate power consumption
    // Returns true if activation is allowed.
    bool canActivate(const ActuatorConfig& actuator, uint8_t instrument_index,
                     uint8_t velocity);

    // Records the effective activation of an actuator (after successful scheduler push)
    void notifyActivation(const ActuatorConfig& actuator, uint8_t instrument_index,
                          uint8_t velocity);

    // Records the deactivation of an actuator (NOTE_OFF received)
    void notifyDeactivation(const ActuatorConfig& actuator, uint8_t instrument_index);

    // -------------------------------------------------------------------------
    // Periodic update (called from loop() Core 0)
    // -------------------------------------------------------------------------

    // Recalculates power consumption statistics from actuator states.
    // actuators[] : array of pointers to registered actuator configs
    // count       : number of actuators
    void update(ActuatorConfig* actuators[], uint8_t count);

    // -------------------------------------------------------------------------
    // Statistics accessors (for monitoring and future Web UI)
    // -------------------------------------------------------------------------

    const PowerStats& getStats() const;

    // Remaining budget in mA (global)
    uint32_t getBudgetRemainingMA() const;

    // Percentage of global budget used (0-100)
    uint8_t getBudgetUsedPercent() const;

    // Indicates if graceful degradation is active (>= POWER_DEGRADATION_THRESHOLD_PCT %)
    bool isDegradationActive() const;

    // -------------------------------------------------------------------------
    // Runtime configuration (modifiable from Web UI Phase 6)
    // -------------------------------------------------------------------------

    void setGlobalMaxMA(uint32_t ma);
    void setServoBusMaxMA(uint32_t ma);
    void setSolenoidBusMaxMA(uint32_t ma);
    void setGlobalMaxPolyphony(uint8_t max);
    void setInstrumentMaxPolyphony(uint8_t instrument_index, uint8_t max);

    // Returns the current budget (read-only)
    const PowerBudget& getBudget() const;

private:
    PowerBudget _budget;
    PowerStats  _stats;

    // Per-actuator tracking: allocated current (0 = inactive)
    uint16_t _actuator_allocated_ma[MAX_ACTUATORS];
    bool     _actuator_tracked[MAX_ACTUATORS];

    // Timestamp of last full update
    uint32_t _last_update_us;

    // -------------------------------------------------------------------------
    // Internal methods
    // -------------------------------------------------------------------------

    // Estimates an actuator's power consumption based on its type and velocity
    uint16_t estimateCurrent(const ActuatorConfig& actuator, uint8_t velocity) const;

    // Recalculates budget_used_percent and degradation_active
    void updateDerivedStats();
};

#endif // POWER_MANAGER_H
