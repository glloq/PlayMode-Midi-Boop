#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

#include <Arduino.h>
#include <atomic>
#include "config.h"
#include "types.h"
#include "pca_driver.h"

// Forward declaration — the kill switch flushes the scheduler queues.
class Scheduler;

// ============================================================================
// PlayMode — Resource Manager (unified Safety + Power)
// ============================================================================
//
// AUDIT FIX (architecture): a SINGLE owner of the electrical/safety budget,
// running on Core 1. It replaces the previous SafetyManager + PowerManager,
// which each estimated current independently with conflicting thresholds
// (5000 mA vs 6000 mA). There is now ONE current estimate, ONE set of
// thresholds, and ONE lifecycle:
//
//     admit()   → decide + (implicitly) reserve   (before execution)
//     observe() → allocate / release on the REAL activation transition
//     update()  → reconcile from actual actuator states (authoritative)
//
// Because the scheduler runs admit → execute → observe for each event before
// moving to the next, admit() always sees the previous event's allocation, so
// a burst of same-timestamp NOTE_ONs is bounded without a separate reservation
// counter. update() runs every SAFETY_CHECK_INTERVAL_MS and corrects any drift.
//
// Budgets are keyed by physical bus_id (bus 0 / bus 1), not by an assumed
// "bus 0 = servos, bus 1 = solenoids" mapping.
//

class ResourceManager {
public:
    ResourceManager(PCADriver& pca);

    // Initialise with the persisted budget + safety limits (single effective
    // set of thresholds derived from both).
    void begin(const PowerBudget& budget, const SafetyLimits& limits);

    // Register the scheduler so the kill switch can flush its queues.
    void setScheduler(Scheduler* scheduler);

    // ------------------------------------------------------------------------
    // Lifecycle (called by the scheduler task, Core 1)
    // ------------------------------------------------------------------------

    // Admission gate for a due event. Returns true if it may execute. Applies
    // the frequency / duty / polyphony / current-budget checks for NOTE_ON.
    bool admit(const ActuatorConfig& actuator, const SchedulerEvent& event,
               uint8_t instrument_index);

    // Called right after the engine executed the event, with the actuator's
    // active state before/after. Allocates on a false→true transition, releases
    // on true→false. The event carries the instrument, velocity and behaviour
    // override needed for an accurate estimate.
    void observe(const ActuatorConfig& actuator, bool was_active, bool is_active,
                 const SchedulerEvent& event);

    // Periodic reconciliation from the real actuator states (rate-limited
    // internally). Also runs watchdogs, degradation hysteresis and the
    // over-current latch.
    void update(ActuatorConfig* actuators[], uint8_t count);

    // ------------------------------------------------------------------------
    // Kill switch / cross-task requests
    // ------------------------------------------------------------------------
    void activateKillSwitch();    // Core 1 only
    void deactivateKillSwitch();  // Core 1 only
    bool isKillSwitchActive() const;

    void requestKillSwitch();     // from web (Core 0) — raises an atomic flag
    void requestRearm();
    void requestRescan();
    void requestAcknowledge();    // clear a latched fault (does NOT re-arm)
    void requestBusFrequency(uint8_t bus_id, uint16_t hz);
    void processPendingRequests(ActuatorConfig* actuators[], uint8_t count);

    // AUDIT FIX (P0.2): a safety-critical hardware write failed (PCA missing /
    // I²C error). Latch a fault and cut everything. Callable from Core 1.
    void latchHardwareFault(const char* reason);
    bool isFaultLatched() const { return _global_state.fault_latched; }

    // ------------------------------------------------------------------------
    // Accessors (web / status)
    // ------------------------------------------------------------------------
    const SafetyState& getGlobalState() const;
    const PowerStats&  getStats() const;
    const PowerBudget& getBudget() const;
    uint32_t getEstimatedCurrentMA() const;
    uint8_t  getActiveActuatorCount() const;
    bool     isDegradationActive() const;
    uint32_t getTotalRejected() const;
    const ActuatorSafetyState& getActuatorSafetyState(uint8_t actuator_id) const;

    // ------------------------------------------------------------------------
    // Runtime configuration (current cap and polyphony are UNIFIED — the
    // power-form and safety-form setters write the same effective field).
    // ------------------------------------------------------------------------
    void setGlobalMaxMA(uint32_t ma);          // == setMaxTotalCurrent
    void setMaxTotalCurrent(uint32_t ma);
    void setBusMaxMA(uint8_t bus_id, uint32_t ma);
    void setGlobalMaxPolyphony(uint8_t max);   // == setMaxPolyphony
    void setMaxPolyphony(uint8_t max);
    void setInstrumentMaxPolyphony(uint8_t instrument_index, uint8_t max);
    void setMaxDutyCycle(uint8_t percent);
    void setMaxFrequency(uint16_t hz);
    void setWatchdogTimeout(uint16_t ms);
    void setSmartRejection(bool on);

    uint8_t  getMaxDutyCycle() const     { return _max_duty_cycle; }
    uint16_t getMaxFrequency() const     { return _max_freq_hz; }
    uint16_t getWatchdogTimeout() const  { return _watchdog_ms; }
    uint8_t  getMaxPolyphony() const     { return _budget.global_max_polyphony; }
    uint32_t getMaxTotalCurrent() const  { return _budget.global_max_ma; }

private:
    PCADriver& _pca;
    Scheduler* _scheduler;

    // Unified budget + limits (effective values).
    PowerBudget _budget;         // current cap, per-bus caps, polyphony
    uint8_t  _max_duty_cycle;
    uint16_t _max_freq_hz;
    uint16_t _watchdog_ms;

    // Live aggregates.
    SafetyState _global_state;                     // current/active/kill/degrade
    PowerStats  _stats;                            // web-facing stats mirror
    ActuatorSafetyState _actuator_safety[MAX_ACTUATORS];

    // Per-actuator allocation tracking (for exact release in observe()).
    uint16_t _alloc_ma[MAX_ACTUATORS];
    uint8_t  _alloc_bus[MAX_ACTUATORS];
    uint8_t  _alloc_inst[MAX_ACTUATORS];           // 0xFF = none
    bool     _tracked[MAX_ACTUATORS];

    uint32_t _bus_ma[2];                           // running per-bus current

    uint32_t _last_check_us;

    // AUDIT FIX (P0.3): INDEPENDENT request flags (not a single "last order
    // wins" variable). A re-arm can no longer overwrite a pending kill; Core 1
    // processes them in strict priority order (kill > rescan > ack > rearm).
    std::atomic<bool>     _kill_requested;
    std::atomic<bool>     _rearm_requested;
    std::atomic<bool>     _rescan_requested;
    std::atomic<bool>     _ack_requested;
    std::atomic<uint16_t> _req_freq_bus0;
    std::atomic<uint16_t> _req_freq_bus1;

    // Cached actuator view for kill-time reset.
    ActuatorConfig** _cached_actuators;
    uint8_t          _cached_actuator_count;

    static const ActuatorSafetyState _default_safety_state;

    // Internal helpers.
    void doRescan();
    void applyBusFrequency(uint8_t bus_id, uint16_t hz);
    bool checkFrequency(uint8_t actuator_id);
    bool checkDutyCycle(uint8_t actuator_id, const ActuatorConfig& actuator);
    void checkWatchdog(uint8_t actuator_id, ActuatorConfig& actuator);
    void resetWindow(uint8_t actuator_id);
    void resetActuatorStates();
    void syncDerivedStats();

    // ONE current model: estimate an actuator's draw for the EFFECTIVE
    // behaviour (per-note override or the actuator default). When
    // `assume_active` is true (admission), estimate from type + velocity;
    // otherwise from the live state (hold vs full for solenoids).
    uint16_t estimateCurrent(const ActuatorConfig& actuator, uint8_t behavior,
                             uint8_t velocity, bool assume_active) const;
};

#endif // RESOURCE_MANAGER_H
