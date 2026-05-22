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

    // Initialize the safety manager
    void begin();

    // --- Pre-event check (called by the scheduler before dispatch) ---

    // Check if an event is allowed for a given actuator
    // Returns true if the event can be executed
    bool checkEvent(const ActuatorConfig& actuator, const SchedulerEvent& event);

    // --- Periodic monitoring (called in the scheduler loop) ---

    // Update the safety state (called each scheduler tick)
    void update(ActuatorConfig* actuators[], uint8_t count);

    // --- Kill switch ---

    // Activate the global kill switch (disable all outputs)
    void activateKillSwitch();

    // Deactivate the kill switch (re-enable outputs)
    void deactivateKillSwitch();

    // Check if the kill switch is active
    bool isKillSwitchActive() const;

    // --- State accessors ---

    // Return the safety state of an actuator
    const ActuatorSafetyState& getActuatorSafetyState(uint8_t actuator_id) const;

    // Return the global safety state
    const SafetyState& getGlobalState() const;

    // Return the total estimated current (mA)
    uint32_t getEstimatedCurrentMA() const;

    // Return the number of active actuators
    uint8_t getActiveActuatorCount() const;

    // Check if graceful degradation is active
    bool isDegradationActive() const;

    // --- Configuration runtime ---

    void setMaxDutyCycle(uint8_t percent);
    void setMaxFrequency(uint16_t hz);
    void setWatchdogTimeout(uint16_t ms);
    void setMaxPolyphony(uint8_t max);
    void setMaxTotalCurrent(uint16_t ma);

private:
    PCADriver& _pca;

    // Per-actuator safety state
    ActuatorSafetyState _actuator_safety[MAX_ACTUATORS];
    SafetyState _global_state;

    // Configuration (modifiable at runtime)
    uint8_t _max_duty_cycle;         // % (default SAFETY_MAX_DUTY_CYCLE)
    uint16_t _max_freq_hz;           // Hz (default SAFETY_MAX_FREQ_HZ)
    uint16_t _watchdog_ms;           // ms (default SAFETY_WATCHDOG_MS)
    uint8_t _max_polyphony;          // (default SAFETY_MAX_POLYPHONY)
    uint16_t _max_total_current_ma;  // mA (default SAFETY_MAX_TOTAL_CURRENT_MA)

    uint32_t _last_check_us;         // Last periodic check
    uint32_t _kill_switch_activated_us; // esp_timer when kill switch was last activated (auto-recovery)
    bool     _kill_switch_auto;       // true if last activation was automatic (overcurrent)

    // Default state (for invalid IDs)
    static const ActuatorSafetyState _default_safety_state;

    // --- Internal methods ---

    // Check the trigger frequency
    bool checkFrequency(uint8_t actuator_id);

    // Check the duty cycle of an actuator
    bool checkDutyCycle(uint8_t actuator_id, const ActuatorConfig& actuator);

    // Check the watchdog for an actuator
    void checkWatchdog(uint8_t actuator_id, ActuatorConfig& actuator);

    // Estimate the current consumed by an actuator
    uint16_t estimateActuatorCurrent(const ActuatorConfig& actuator);

    // Calculate total current and apply limits
    void updateCurrentEstimate(ActuatorConfig* actuators[], uint8_t count);

    // Reset the counting window of an actuator
    void resetWindow(uint8_t actuator_id);
};

#endif // SAFETY_MANAGER_H
