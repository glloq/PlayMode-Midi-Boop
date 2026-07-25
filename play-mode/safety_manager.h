#ifndef SAFETY_MANAGER_H
#define SAFETY_MANAGER_H

#include <Arduino.h>
#include <atomic>
#include "config.h"
#include "types.h"
#include "pca_driver.h"

// Forward declaration — the kill switch flushes the scheduler queues.
class Scheduler;

// ============================================================================
// PlayMode — Safety Manager + Power Manager
// ============================================================================

class SafetyManager {
public:
    SafetyManager(PCADriver& pca);

    // Initialize the safety manager
    void begin();

    // AUDIT FIX (P0.6): register the scheduler so the kill switch can flush
    // every pending event as part of the emergency-stop sequence.
    void setScheduler(Scheduler* scheduler);

    // --- Pre-event check (called by the scheduler before dispatch) ---

    // Check if an event is allowed for a given actuator
    // Returns true if the event can be executed
    bool checkEvent(const ActuatorConfig& actuator, const SchedulerEvent& event);

    // --- Periodic monitoring (called in the scheduler loop) ---

    // Update the safety state (called each scheduler tick)
    void update(ActuatorConfig* actuators[], uint8_t count);

    // --- Kill switch ---

    // Activate the global kill switch (disable all outputs).
    // MUST run on the scheduler task (Core 1) — the sole PCA/queue owner.
    void activateKillSwitch();

    // Deactivate the kill switch (re-enable outputs). Core 1 only.
    void deactivateKillSwitch();

    // Check if the kill switch is active
    bool isKillSwitchActive() const;

    // --- AUDIT FIX (P0.1/P0.2/P0.3): cross-task requests ---
    // Called from the web task (Core 0). They only raise atomic state; the
    // actual hardware/queue work happens on Core 1 in processPendingRequests().
    void requestKillSwitch();
    void requestRearm();
    void requestRescan();
    // Request a bus PWM frequency change (applied on Core 1: OE off, set freq,
    // OE restored only if armed). hz is clamped by PCADriver::setFrequency.
    void requestBusFrequency(uint8_t bus_id, uint16_t hz);

    // Executed by the scheduler task (Core 1) every tick, inside the actuator
    // lock. Processes any pending state transition / frequency change.
    void processPendingRequests(ActuatorConfig* actuators[], uint8_t count);

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

    // AUDIT FIX (UI-P1): runtime getters so the API returns the values actually
    // in effect, not the compile-time constants.
    uint8_t  getMaxDutyCycle() const     { return _max_duty_cycle; }
    uint16_t getMaxFrequency() const     { return _max_freq_hz; }
    uint16_t getWatchdogTimeout() const  { return _watchdog_ms; }
    uint8_t  getMaxPolyphony() const     { return _max_polyphony; }
    uint16_t getMaxTotalCurrent() const  { return _max_total_current_ma; }

private:
    PCADriver& _pca;
    Scheduler* _scheduler;  // Optional — used to flush queues on kill switch

    // AUDIT FIX: a SINGLE requested state (not independent booleans) removes the
    // ambiguous ordering where a re-arm and a rescan could both apply in one
    // pass. Last request wins; processed on Core 1.
    enum RequestedSafetyState : uint8_t {
        REQ_NONE = 0,
        REQ_KILL,
        REQ_RESCAN,
        REQ_REARM
    };
    std::atomic<uint8_t>  _requested_state;   // RequestedSafetyState
    // AUDIT FIX (P0.3): pending per-bus PWM frequency (0 = none). Applied on
    // Core 1 so I²C prescaler writes never race actuator PWM writes.
    std::atomic<uint16_t> _req_freq_bus0;
    std::atomic<uint16_t> _req_freq_bus1;

    // Perform the I²C rescan safely on Core 1 (kill first, rebuild drivers,
    // leave outputs disabled until a manual re-arm).
    void doRescan();
    // Apply a pending bus frequency change on Core 1.
    void applyBusFrequency(uint8_t bus_id, uint16_t hz);

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

    // AUDIT FIX: cached view of the actuator array last passed to update(),
    // so activateKillSwitch() — which may be called from the web task — can
    // reset every actuator runtime state when the outputs are physically cut.
    ActuatorConfig** _cached_actuators;
    uint8_t          _cached_actuator_count;

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

    // AUDIT FIX: mark every actuator runtime state inactive — used when the
    // kill switch cuts the outputs (they are physically at rest).
    void resetActuatorStates();
};

#endif // SAFETY_MANAGER_H
